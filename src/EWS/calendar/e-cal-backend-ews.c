/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2017 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <camel/camel.h>

#include <libedata-cal/libedata-cal.h>
#include <libecal/libecal.h>

#include "e-ews-common-utils.h"
#include "common/e-source-ews-folder.h"
#include "common/e-ews-calendar-utils.h"
#include "common/e-ews-connection-utils.h"
#include "common/e-ews-camel-common.h"

#include "e-cal-backend-ews.h"
#include "e-cal-backend-ews-utils.h"
#include "e-cal-backend-ews-m365.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)
#define ECC_ERROR_EX(_code, _msg) e_cal_client_error_create (_code, _msg)

/* Private part of the CalBackendEws structure */
struct _ECalBackendEwsPrivate {
	GRecMutex cnc_lock;
	EEwsConnection *cnc;
	gchar *folder_id;

	guint subscription_key;

	/* The subscription ID is not tight to the actual connection, it survives
	   disconnects, thus remember it and pass it back to the new connection,
	   thus it can eventually unsubscribe from it. */
	gchar *last_subscription_id;

	gboolean is_freebusy_calendar;

	gchar *attachments_dir;

	EThreeState is_user_calendar;
};

#define ECB_EWS_SYNC_TAG_STAMP_KEY "ews-sync-tag-stamp"

#define X_EWS_ORIGINAL_COMP "X-EWS-ORIGINAL-COMP"

#define EWS_MAX_FETCH_COUNT 100

#define GET_ITEMS_SYNC_PROPERTIES \
	"item:Attachments" \
	" item:Body" \
	" item:Categories" \
	" item:HasAttachments" \
	" item:MimeContent" \
	" calendar:UID" \
	" calendar:Start" \
	" calendar:End" \
	" calendar:Resources" \
	" calendar:ModifiedOccurrences" \
	" calendar:IsMeeting" \
	" calendar:IsResponseRequested" \
	" calendar:MyResponseType" \
	" calendar:RequiredAttendees" \
	" calendar:OptionalAttendees"

#define GET_ITEMS_SYNC_PROPERTIES_2007 \
	GET_ITEMS_SYNC_PROPERTIES \
	" calendar:TimeZone"

#define GET_ITEMS_SYNC_PROPERTIES_2010 \
	GET_ITEMS_SYNC_PROPERTIES \
	" calendar:StartTimeZone" \
	" calendar:EndTimeZone"

#define e_data_cal_error_if_fail(expr, _code)					\
	G_STMT_START {								\
		if (G_LIKELY (expr)) {						\
		} else {							\
			g_log (G_LOG_DOMAIN,					\
				G_LOG_LEVEL_CRITICAL,				\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			g_set_error (&error, E_DATA_CAL_ERROR, (_code),		\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			goto exit;						\
		}								\
	} G_STMT_END

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendEws, e_cal_backend_ews, E_TYPE_CAL_META_BACKEND)

static CamelEwsSettings *
ecb_ews_get_collection_settings (ECalBackendEws *cbews)
{
	ESource *source;
	ESource *collection;
	ESourceCamel *extension;
	ESourceRegistry *registry;
	CamelSettings *settings;
	const gchar *extension_name;

	source = e_backend_get_source (E_BACKEND (cbews));
	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbews));

	extension_name = e_source_camel_get_extension_name ("ews");
	e_source_camel_generate_subtype ("ews", CAMEL_TYPE_EWS_SETTINGS);

	/* The collection settings live in our parent data source. */
	collection = e_source_registry_find_extension (
		registry, source, extension_name);
	g_return_val_if_fail (collection != NULL, NULL);

	extension = e_source_get_extension (collection, extension_name);
	settings = e_source_camel_get_settings (extension);

	g_object_unref (collection);

	return CAMEL_EWS_SETTINGS (settings);
}

static gboolean
ecb_ews_get_sync_tag_stamp_changed (ECalBackendEws *cbews)
{
	CamelEwsSettings *settings;
	ECalCache *cal_cache;
	guint sync_tag_stamp;

	settings = ecb_ews_get_collection_settings (cbews);
	g_return_val_if_fail (settings != NULL, FALSE);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbews));
	if (!cal_cache)
		return FALSE;

	sync_tag_stamp = e_cache_get_key_int (E_CACHE (cal_cache), ECB_EWS_SYNC_TAG_STAMP_KEY, NULL);
	if (sync_tag_stamp == (guint) -1)
		sync_tag_stamp = 0;

	g_clear_object (&cal_cache);

	return sync_tag_stamp != camel_ews_settings_get_sync_tag_stamp (settings);
}

static void
ecb_ews_update_sync_tag_stamp (ECalBackendEws *cbews)
{
	CamelEwsSettings *settings;
	ECalCache *cal_cache;
	guint sync_tag_stamp;

	settings = ecb_ews_get_collection_settings (cbews);
	g_return_if_fail (settings != NULL);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbews));
	if (!cal_cache)
		return;

	sync_tag_stamp = e_cache_get_key_int (E_CACHE (cal_cache), ECB_EWS_SYNC_TAG_STAMP_KEY, NULL);

	if (sync_tag_stamp != camel_ews_settings_get_sync_tag_stamp (settings))
		e_cache_set_key_int (E_CACHE (cal_cache), ECB_EWS_SYNC_TAG_STAMP_KEY, camel_ews_settings_get_sync_tag_stamp (settings), NULL);

	g_clear_object (&cal_cache);
}

static void
ecb_ews_convert_error_to_edc_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror ||
	    (*perror)->domain == E_CLIENT_ERROR ||
	    (*perror)->domain == E_CAL_CLIENT_ERROR)
		return;

	if ((*perror)->domain == EWS_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EC_ERROR_EX (E_CLIENT_ERROR_AUTHENTICATION_FAILED, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_FOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_EVENTNOTFOUND:
		case EWS_CONNECTION_ERROR_ITEMNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_UNAVAILABLE:
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND, (*perror)->message);
			break;
		}

		if (!error)
			error = EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, (*perror)->message);
	}

	if (error) {
		g_error_free (*perror);
		*perror = error;
	}
}

static void
ecb_ews_maybe_disconnect_sync (ECalBackendEws *cbews,
			       GError **in_perror,
			       GCancellable *cancellable)
{
	g_return_if_fail (E_IS_CAL_BACKEND_EWS (cbews));

	if (in_perror && g_error_matches (*in_perror, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		e_cal_meta_backend_disconnect_sync (E_CAL_META_BACKEND (cbews), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (cbews), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static void
ecb_ews_server_notification_cb (ECalBackendEws *cbews,
				GSList *events,
				EEwsConnection *cnc)
{
	GSList *link;
	gboolean update_folder = FALSE;

	g_return_if_fail (cbews != NULL);
	g_return_if_fail (cbews->priv != NULL);

	for (link = events; link && !update_folder; link = g_slist_next (link)) {
		EEwsNotificationEvent *event = link->data;

		switch (event->type) {
			case E_EWS_NOTIFICATION_EVENT_CREATED:
			case E_EWS_NOTIFICATION_EVENT_DELETED:
			case E_EWS_NOTIFICATION_EVENT_MODIFIED:
				g_rec_mutex_lock (&cbews->priv->cnc_lock);
				if (g_strcmp0 (event->folder_id, cbews->priv->folder_id) == 0)
					update_folder = TRUE;
				g_rec_mutex_unlock (&cbews->priv->cnc_lock);
				break;
			case E_EWS_NOTIFICATION_EVENT_MOVED:
			case E_EWS_NOTIFICATION_EVENT_COPIED:
				g_rec_mutex_lock (&cbews->priv->cnc_lock);
				if (g_strcmp0 (event->folder_id, cbews->priv->folder_id) == 0 ||
				    g_strcmp0 (event->old_folder_id, cbews->priv->folder_id) == 0)
					update_folder = TRUE;
				g_rec_mutex_unlock (&cbews->priv->cnc_lock);
				break;
			default:
				return;
		}
	}

	if (update_folder)
		e_cal_meta_backend_schedule_refresh (E_CAL_META_BACKEND (cbews));
}

static void
ecb_ews_unset_connection (ECalBackendEws *cbews,
			  gboolean is_disconnect)
{
	g_return_if_fail (E_IS_CAL_BACKEND_EWS (cbews));

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	if (cbews->priv->cnc) {
		if (is_disconnect)
			e_ews_connection_set_disconnected_flag (cbews->priv->cnc, TRUE);

		g_signal_handlers_disconnect_by_func (cbews->priv->cnc, ecb_ews_server_notification_cb, cbews);

		if (cbews->priv->subscription_key != 0) {
			e_ews_connection_disable_notifications_sync (
				cbews->priv->cnc,
				cbews->priv->subscription_key);
			cbews->priv->subscription_key = 0;
		}
	}

	g_clear_object (&cbews->priv->cnc);

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);
}

static ICalTimezone *
ecb_ews_get_timezone (ETimezoneCache *timezone_cache,
		      const gchar *msdn_tzid,
		      const gchar *tzid,
		      const gchar *evo_ews_tzid)
{
	ICalTimezone *zone = NULL;
	const gchar *evo_ews_msdn_tzid;

	if (tzid) {
		zone = e_timezone_cache_get_timezone (timezone_cache, tzid);
		if (zone == NULL)
			zone = i_cal_timezone_get_builtin_timezone (tzid);
	}

	if (g_strcmp0 (tzid, evo_ews_tzid) == 0)
		return zone;

	if (evo_ews_tzid != NULL) {
		evo_ews_msdn_tzid = e_cal_backend_ews_tz_util_get_msdn_equivalent (evo_ews_tzid);

		if (g_strcmp0 (msdn_tzid, evo_ews_msdn_tzid) == 0) {
			zone = e_timezone_cache_get_timezone (timezone_cache, evo_ews_tzid);
			if (zone == NULL)
				zone = i_cal_timezone_get_builtin_timezone (evo_ews_tzid);
		}
	}

	return zone;
}

static ICalParameter *
ecb_ews_responsetype_to_partstat (const gchar *responsetype)
{
	ICalParameter *param = NULL;

	if (!responsetype)
		return i_cal_parameter_new_partstat (I_CAL_PARTSTAT_NONE);

	if (g_ascii_strcasecmp (responsetype, "Organizer") == 0)
		param = i_cal_parameter_new_partstat (I_CAL_PARTSTAT_ACCEPTED);
	else if (g_ascii_strcasecmp (responsetype, "Tentative") == 0)
		param = i_cal_parameter_new_partstat (I_CAL_PARTSTAT_TENTATIVE);
	else if (g_ascii_strcasecmp (responsetype, "Accept") == 0)
		param = i_cal_parameter_new_partstat (I_CAL_PARTSTAT_ACCEPTED);
	else if (g_ascii_strcasecmp (responsetype, "Decline") == 0)
		param = i_cal_parameter_new_partstat (I_CAL_PARTSTAT_DECLINED);
	else if (g_ascii_strcasecmp (responsetype, "NoResponseReceived") == 0)
		param = i_cal_parameter_new_partstat (I_CAL_PARTSTAT_NEEDSACTION);
	else if (g_ascii_strcasecmp (responsetype, "Unknown") == 0)
		param = i_cal_parameter_new_partstat (I_CAL_PARTSTAT_NEEDSACTION);

	if (!param)
		param = i_cal_parameter_new_partstat (I_CAL_PARTSTAT_NONE);

	return param;
}

static void
ecb_ews_maybe_update_datetime (ETimezoneCache *timezone_cache,
			       ICalComponent *vcomp,
			       ICalComponent *icomp,
			       ICalPropertyKind prop_kind,
			       ICalTime * (* get_func) (ICalProperty *prop),
			       void (* set_func) (ICalProperty *prop,
						  ICalTime *v),
			       time_t utc_value)
{
	ICalProperty *prop;
	ICalTime *dt, *val;

	g_return_if_fail (I_CAL_IS_COMPONENT (icomp));
	g_return_if_fail (get_func != NULL);
	g_return_if_fail (set_func != NULL);

	if (utc_value == (time_t) -1)
		return;

	prop = i_cal_component_get_first_property (icomp, prop_kind);
	if (!prop)
		return;

	dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, vcomp, icomp, prop_kind, get_func);

	if (i_cal_time_is_date (dt)) {
		g_clear_object (&prop);
		g_clear_object (&dt);
		return;
	}

	val = i_cal_time_new_from_timet_with_zone (utc_value, i_cal_time_is_date (dt), i_cal_timezone_get_utc_timezone ());
	i_cal_time_convert_to_zone_inplace (val, i_cal_time_get_timezone (dt));
	set_func (prop, val);

	g_clear_object (&prop);
	g_clear_object (&val);
	g_clear_object (&dt);
}

static ECalComponent *
ecb_ews_item_to_component_sync (ECalBackendEws *cbews,
				EEwsItem *item,
				GCancellable *cancellable,
				GError **error)
{
	ECalComponent *res_component = NULL;
	ETimezoneCache *timezone_cache;
	ICalComponentKind kind;
	EEwsItemType item_type;
	ICalComponent *icomp, *vcomp;
	ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();
	CamelEwsSettings *ews_settings;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), NULL);
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	timezone_cache = E_TIMEZONE_CACHE (cbews);

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbews));
	ews_settings = ecb_ews_get_collection_settings (cbews);

	item_type = e_ews_item_get_item_type (item);
	if (item_type == E_EWS_ITEM_TYPE_TASK || item_type == E_EWS_ITEM_TYPE_MEMO) {
		ICalProperty *prop;
		ICalTime *itt;
		ICalPropertyStatus status = I_CAL_STATUS_NONE;
		ICalProperty_Class class = I_CAL_CLASS_NONE;
		const gchar *ews_task_status, *sensitivity;
		EwsImportance item_importance;
		gint priority = 5;
		gboolean has_this_date = FALSE;

		vcomp = i_cal_component_new (I_CAL_VCALENDAR_COMPONENT);
		/*subject*/
		icomp = i_cal_component_new (item_type == E_EWS_ITEM_TYPE_TASK ? I_CAL_VTODO_COMPONENT : I_CAL_VJOURNAL_COMPONENT);
		prop = i_cal_property_new_summary (e_ews_item_get_subject (item));
		i_cal_component_take_property (icomp, prop);

		/*date time created*/
		itt = i_cal_time_new_from_timet_with_zone (e_ews_item_get_date_created (item), 0, utc_zone);
		prop = i_cal_property_new_created (itt);
		i_cal_component_take_property (icomp, prop);
		g_clear_object (&itt);

		/*sensitivity*/
		sensitivity = e_ews_item_get_sensitivity (item);
		if (g_strcmp0 (sensitivity, "Normal") == 0)
			class = I_CAL_CLASS_PUBLIC;
		else if (g_strcmp0 (sensitivity, "Private") == 0)
			class = I_CAL_CLASS_PRIVATE;
		else if ((g_strcmp0 (sensitivity, "Confidential") == 0) ||
			 (g_strcmp0 (sensitivity, "Personal") == 0))
			class = I_CAL_CLASS_CONFIDENTIAL;
		prop = i_cal_property_new_class (class);
		i_cal_component_take_property (icomp, prop);

		/*description*/
		prop = i_cal_property_new_description (e_ews_item_get_body (item));
		i_cal_component_take_property (icomp, prop);

		/*task assaingments*/
		if (e_ews_item_get_delegator (item) != NULL) {
			const gchar *task_owner = e_ews_item_get_delegator (item);
			GSList *mailboxes = NULL, *l;
			gboolean includes_last_item;
			gchar *mailtoname, *user_email;
			ICalParameter *param;

			/*The task owner according to Exchange is current user, even that the task was assigned by
			 *someone else. I'm making the current user attendee and task delegator will be a task organizer */

			user_email = camel_ews_settings_dup_email (ews_settings);
			mailtoname = g_strdup_printf ("mailto:%s", user_email);
			prop = i_cal_property_new_attendee (mailtoname);
			g_free (mailtoname);
			g_free (user_email);

			param = i_cal_parameter_new_cn (e_ews_item_get_owner (item));
			i_cal_property_take_parameter (prop, param);
			i_cal_component_take_property (icomp, prop);

			/* get delegator mail box*/
			e_ews_connection_resolve_names_sync (
				cbews->priv->cnc, EWS_PRIORITY_MEDIUM, task_owner,
				EWS_SEARCH_AD, NULL, FALSE, &includes_last_item, &mailboxes, NULL,
				cancellable, error);

			for (l = mailboxes; l != NULL; l = g_slist_next (l)) {
				EwsMailbox *mb = l->data;

				mailtoname = g_strdup_printf ("mailto:%s", mb->email);
				prop = i_cal_property_new_organizer (mailtoname);
				param = i_cal_parameter_new_cn (mb->name);
				i_cal_property_take_parameter (prop, param);
				i_cal_component_take_property (icomp, prop);

				g_free (mailtoname);
				e_ews_mailbox_free (mb);
			}
			g_slist_free (mailboxes);
		}

		if (item_type == E_EWS_ITEM_TYPE_TASK) {
			ICalTimezone *user_timezone = e_ews_common_utils_get_configured_icaltimezone ();
			const gchar *percent_complete;

			/*start date*/
			has_this_date = FALSE;
			e_ews_item_task_has_start_date (item, &has_this_date);
			if (has_this_date) {
				itt = i_cal_time_new_from_timet_with_zone (e_ews_item_get_start_date (item), 0, user_timezone);
				i_cal_time_set_is_date (itt, TRUE);
				prop = i_cal_property_new_dtstart (itt);
				i_cal_component_take_property (icomp, prop);
				g_clear_object (&itt);
			}

			/*status*/
			ews_task_status = e_ews_item_get_status (item);
			if (g_strcmp0 (ews_task_status, "NotStarted") != 0) {
				if (g_strcmp0 (ews_task_status, "Completed") == 0)
					status = I_CAL_STATUS_COMPLETED;
				else if (g_strcmp0 (ews_task_status, "InProgress") == 0)
					status = I_CAL_STATUS_INPROCESS;
				else if (g_strcmp0 (ews_task_status, "WaitingOnOthers") == 0)
					status = I_CAL_STATUS_NEEDSACTION;
				else if (g_strcmp0 (ews_task_status, "Deferred") == 0)
					status = I_CAL_STATUS_CANCELLED;
				prop = i_cal_property_new_status (status);
				i_cal_component_take_property (icomp, prop);
			}

			/*precent complete*/
			percent_complete = e_ews_item_get_percent_complete (item);
			prop  = i_cal_property_new_percentcomplete (atoi (percent_complete ? percent_complete : "0"));
			i_cal_component_take_property (icomp, prop);

			/*due date*/
			e_ews_item_task_has_due_date (item, &has_this_date);
			if (has_this_date) {
				itt = i_cal_time_new_from_timet_with_zone (e_ews_item_get_due_date (item), 0, user_timezone);
				i_cal_time_set_is_date (itt, TRUE);
				prop = i_cal_property_new_due (itt);
				i_cal_component_take_property (icomp, prop);
				g_clear_object (&itt);
			}

			/*complete date*/
			has_this_date = FALSE;
			e_ews_item_task_has_complete_date (item, &has_this_date);
			if (has_this_date) {
				itt = i_cal_time_new_from_timet_with_zone (e_ews_item_get_complete_date (item), 0, utc_zone);
				prop = i_cal_property_new_completed (itt);
				i_cal_component_take_property (icomp, prop);
				g_clear_object (&itt);
			}

			/*priority*/
			item_importance = e_ews_item_get_importance (item);
			if (item_importance == EWS_ITEM_HIGH)
				priority = 3;
			else if (item_importance == EWS_ITEM_LOW)
				priority = 7;
			prop = i_cal_property_new_priority (priority);
			i_cal_component_take_property (icomp, prop);

			/* recurrence */
			e_ews_cal_utils_recurrence_to_rrule (item, icomp);

			/* reminders */
			/* The Exchange server stores start of the Task reminder and Start of the Task
			   itself in separate properties, which doesn't work for evolution at the moment. */
			/* if (e_ews_item_get_reminder_is_set (item)) {
				time_t reminder_due_by = e_ews_item_get_reminder_due_by (item);
				gint minutes_before_start = e_ews_item_get_reminder_minutes_before_start (item);

				if (minutes_before_start >= 0 && reminder_due_by > (time_t) 0) {
					ECalComponentAlarmTrigger *trigger;
					ECalComponentAlarm *alarm;
					ICalTime *dtstart, *due_by;
					ICalDuration *duration;
					ICalComponent *alarm_icomp;

					dtstart = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, NULL, icomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);
					due_by = i_cal_time_new_from_timet_with_zone (reminder_due_by, 0, utc_zone);

					if (!dtstart || i_cal_time_is_null_time (dtstart)) {
						g_clear_object (&dtstart);

						dtstart = i_cal_time_clone (due_by);
						i_cal_time_set_is_date (dtstart, TRUE);

						i_cal_component_set_dtstart (icomp, dtstart);
					}

					i_cal_time_set_is_date (dtstart, FALSE);
					i_cal_time_set_time (dtstart, 0, 0, 0);
					i_cal_time_set_timezone (dtstart, utc_zone);

					minutes_before_start = minutes_before_start + (
						(i_cal_time_as_timet_with_zone (dtstart, utc_zone) -
						 i_cal_time_as_timet_with_zone (due_by, utc_zone)) / 60);

					duration = i_cal_duration_null_duration ();
					i_cal_duration_set_is_neg (duration, minutes_before_start < 0 ? 0 : 1); / * negative 'before start' means 'after start' * /
					i_cal_duration_set_minutes (duration, minutes_before_start < 0 ? -minutes_before_start : minutes_before_start);

					trigger = e_cal_component_alarm_trigger_new_relative (E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START, duration);
					g_clear_object (&duration);

					alarm = e_cal_component_alarm_new ();

					e_cal_component_alarm_take_trigger (alarm, trigger);
					e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);

					alarm_icomp = e_cal_component_alarm_get_as_component (alarm);
					if (alarm_icomp)
						i_cal_component_take_component (icomp, alarm_icomp);

					e_cal_component_alarm_free (alarm);
					g_clear_object (&dtstart);
					g_clear_object (&due_by);
				}
			} */
		}

		i_cal_component_take_component (vcomp, icomp);
	} else {
		ICalTime *dt;
		const gchar *mime_content;
		const gchar *tzid;
		gboolean timezone_set = FALSE;

		mime_content = e_ews_item_get_mime_content (item);
		vcomp = mime_content && *mime_content ? i_cal_parser_parse_string (mime_content) : NULL;

		if (!vcomp && mime_content && *mime_content) {
			const gchar *begin_vcalendar, *end_vcalendar;

			/* Workaround Exchange 2016 error, which returns invalid iCalendar object (without 'END:VCALENDAR'),
			   when the event has at least one detached instance. */
			begin_vcalendar = camel_strstrcase (mime_content, "BEGIN:VCALENDAR");
			end_vcalendar = camel_strstrcase (mime_content, "END:VCALENDAR");

			/* If it exists, then it should be alone on a separate line */
			if (!(begin_vcalendar && (begin_vcalendar == mime_content || begin_vcalendar[-1] == '\n') &&
			    (begin_vcalendar[15 /* strlen ("BEGIN:VCALENDAR") */] == '\r' || begin_vcalendar[15] == '\n')))
				begin_vcalendar = NULL;

			/* If it exists, then it should be alone on a separate line and not at the very beginning of the mime_content */
			if (!(end_vcalendar && end_vcalendar > mime_content && end_vcalendar[-1] == '\n' &&
			    (end_vcalendar[13 /* strlen ("END:VCALENDAR") */] == '\r' || end_vcalendar[13] == '\n' || end_vcalendar[13] == '\0')))
				end_vcalendar = NULL;

			if (begin_vcalendar && !end_vcalendar) {
				gchar *str;

				str = g_strconcat (mime_content, "\r\n", "END:VCALENDAR", "\r\n", NULL);
				vcomp = i_cal_parser_parse_string (str);
				g_free (str);
			}
		}

		if (!vcomp) {
			if (mime_content)
				g_warning ("%s: Failed to parse mime content:---%s---", G_STRFUNC, mime_content);
			return NULL;
		}

		icomp = i_cal_component_get_first_component (vcomp, kind);

		ecb_ews_maybe_update_datetime (timezone_cache, vcomp, icomp,
			I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart, i_cal_property_set_dtstart,
			e_ews_item_get_start (item));

		ecb_ews_maybe_update_datetime (timezone_cache, vcomp, icomp,
			I_CAL_DTEND_PROPERTY, i_cal_property_get_dtend, i_cal_property_set_dtend,
			e_ews_item_get_end (item));

		tzid = e_ews_item_get_tzid (item);
		if (tzid == NULL) {
			/*
			 * When we are working with Exchange server 2010 or newer, we have to handle a few
			 * things more than we do working old servers. These things are:
			 * - MSDN timezone names:
			 *   Used setting StartTimeZone and EndTimeZone. MSDN timezone names are not
			 *   the same used in libical, so we need to have a table of equivalence to
			 *   convert from one to another and avoid show the MSDN timezone name to the
			 *   user and save it in the ETimezoneCache.
			 * - EvoEWSStartTimeZone/EvoEWSEndTimeZone
			 *   Used to keep track if the timezone shown to the user is the same one set
			 *   by him/her. As we have a table of equivalence, sometimes the user sets a
			 *   timezone but without EvoEWSStartTiemZone property, another timezone name,
			 *   in the same offset, can be shown. And we want to avoid this.
			 * - DTEND property:
			 *   As we have to work with DTEND setting an event when using EWS server 2010 or
			 *   newer, we have to care about set it properly here, instead of use the same
			 *   as is used in DTSTART.
			 */
			ICalTimezone *start_zone, *end_zone;
			const gchar *start_tzid, *end_tzid;
			const gchar *ical_start_tzid, *ical_end_tzid;
			const gchar *evo_ews_start_tzid, *evo_ews_end_tzid;

			start_tzid = e_ews_item_get_start_tzid (item);
			end_tzid = e_ews_item_get_end_tzid (item);

			/* Workaround Teams bug - see https://gitlab.gnome.org/GNOME/evolution-ews/-/issues/151 */
			if (!start_tzid || !*start_tzid)
				start_tzid = end_tzid;

			ical_start_tzid = e_cal_backend_ews_tz_util_get_ical_equivalent (start_tzid);
			ical_end_tzid = e_cal_backend_ews_tz_util_get_ical_equivalent (end_tzid);

			evo_ews_start_tzid = e_ews_item_get_iana_start_time_zone (item);
			evo_ews_end_tzid = e_ews_item_get_iana_end_time_zone (item);

			/*
			 * We have a few timezones that don't have an equivalent MSDN timezone.
			 * For those, we will get ical_start_tzid being NULL and then we need to use
			 * start_tzid, which one has the libical's expected name.
			 */
			start_zone = ecb_ews_get_timezone (
				timezone_cache,
				start_tzid,
				ical_start_tzid != NULL ? ical_start_tzid : start_tzid,
				evo_ews_start_tzid);
			end_zone = ecb_ews_get_timezone (
				timezone_cache,
				end_tzid,
				ical_end_tzid != NULL ? ical_end_tzid : end_tzid,
				evo_ews_end_tzid);

			if (start_zone != NULL) {
				dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, vcomp, icomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);

				if (!i_cal_time_is_date (dt)) {
					i_cal_time_convert_to_zone_inplace (dt, start_zone);
					i_cal_component_set_dtstart (icomp, dt);
				}

				g_clear_object (&dt);

				timezone_set = TRUE;
				e_timezone_cache_add_timezone (timezone_cache, start_zone);

				if (end_zone != NULL) {
					dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, vcomp, icomp, I_CAL_DTEND_PROPERTY, i_cal_property_get_dtend);

					if (!i_cal_time_is_date (dt)) {
						i_cal_time_convert_to_zone_inplace (dt, end_zone);
						i_cal_component_set_dtend (icomp, dt);
					}

					g_clear_object (&dt);

					e_timezone_cache_add_timezone (timezone_cache, end_zone);
				}
			}

			if (!timezone_set)
				tzid = start_tzid;
		}

		if (!timezone_set && tzid) {
			/*
			 * When we are working with Exchange server older than 2010, we don't set different
			 * DTSTART and DTEND properties in VTIMEZONE. The reason of that is we don't use
			 * those properties settings/changing a meeting timezone.
			 * So, for older servers, here, we only set the DTSTART and DTEND properties with
			 * the same values.
			 */
			ICalTimezone *zone;
			gchar *new_tzid = NULL;

			if (!i_cal_timezone_get_builtin_timezone (tzid) &&
			    i_cal_component_get_uid (icomp)) {
				ICalComponent *vtimezone, *clone;

				/* Add the timezone */
				vtimezone = i_cal_component_get_first_component (vcomp, I_CAL_VTIMEZONE_COMPONENT);
				if (vtimezone != NULL) {
					ICalProperty *prop;

					new_tzid = g_strconcat ("/evolution/ews/tzid/", i_cal_component_get_uid (icomp), NULL);

					zone = i_cal_timezone_new ();
					clone = i_cal_component_clone (vtimezone);
					g_object_unref (vtimezone);
					vtimezone = clone;

					prop = i_cal_component_get_first_property (vtimezone, I_CAL_TZID_PROPERTY);
					if (prop) {
						i_cal_property_set_tzid (prop, new_tzid);
						g_object_unref (prop);

						prop = i_cal_component_get_first_property (vtimezone, I_CAL_LOCATION_PROPERTY);
						if (!prop) {
							/* Use the original tzid as the timezone Location, to not expose
							   evolution-ews TZID. */
							prop = i_cal_property_new_location (tzid);
							i_cal_component_take_property (vtimezone, prop);
						} else {
							g_object_unref (prop);
						}
					} else {
						g_free (new_tzid);
						new_tzid = NULL;
					}
					i_cal_timezone_set_component (zone, vtimezone);
					e_timezone_cache_add_timezone (timezone_cache, zone);
					g_object_unref (zone);
					g_object_unref (vtimezone);
				}
			}

			zone = e_timezone_cache_get_timezone (timezone_cache, new_tzid ? new_tzid : tzid);

			if (!zone && new_tzid)
				zone = e_timezone_cache_get_timezone (timezone_cache, tzid);

			if (!zone)
				zone = i_cal_timezone_get_builtin_timezone (tzid);

			if (zone != NULL) {
				dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, vcomp, icomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);

				if (!i_cal_time_is_date (dt)) {
					i_cal_time_convert_to_zone_inplace (dt, zone);
					i_cal_component_set_dtstart (icomp, dt);
				}

				g_object_unref (dt);

				dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, vcomp, icomp, I_CAL_DTEND_PROPERTY, i_cal_property_get_dtend);

				if (!i_cal_time_is_date (dt)) {
					i_cal_time_convert_to_zone_inplace (dt, zone);
					i_cal_component_set_dtend (icomp, dt);
				}

				g_object_unref (dt);

				dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, vcomp, icomp, I_CAL_RECURRENCEID_PROPERTY, i_cal_property_get_recurrenceid);

				if (dt && !i_cal_time_is_date (dt)) {
					i_cal_time_convert_to_zone_inplace (dt, zone);
					i_cal_component_set_recurrenceid (icomp, dt);
				}

				g_clear_object (&dt);
			}

			g_free (new_tzid);
		}

		g_clear_object (&icomp);
	}

	/* Vevent or Vtodo */
	icomp = i_cal_component_get_first_component (vcomp, kind);
	if (icomp) {
		ICalProperty *prop, *freebusy;
		ICalTime *itt;
		gchar *xvalue;
		const EwsId *item_id;
		const gchar *uid = e_ews_item_get_uid (item);

		item_id = e_ews_item_get_id (item);

		if (e_ews_item_get_is_meeting (item) ||
		    e_ews_item_get_attendees (item)) {
			const GSList *link;
			gboolean is_response_requested = e_ews_item_get_is_response_requested (item);
			gchar *user_email;

			/* Remove any existing attendees first (as Office365.com (and possibly Exchange 2016) includes them) */
			e_cal_util_component_remove_property_by_kind (icomp, I_CAL_ATTENDEE_PROPERTY, TRUE);

			user_email = camel_ews_settings_dup_email (ews_settings);

			/* Attendees */
			for (link = e_ews_item_get_attendees (item); link; link = g_slist_next (link)) {
				ICalParameter *param, *cutype;
				gchar *mailtoname;
				const gchar *email = NULL;
				EwsAttendee *attendee = (EwsAttendee *) link->data;

				if (!attendee->mailbox)
					continue;

				if (g_strcmp0 (attendee->mailbox->routing_type, "EX") == 0)
					email = e_ews_item_util_strip_ex_address (attendee->mailbox->email);

				mailtoname = g_strdup_printf ("mailto:%s", email ? email : attendee->mailbox->email);
				prop = i_cal_property_new_attendee (mailtoname);
				g_free (mailtoname);

				param = i_cal_parameter_new_cn (attendee->mailbox->name);
				i_cal_property_take_parameter (prop, param);

				if (g_ascii_strcasecmp (attendee->attendeetype, "Required") == 0) {
					param = i_cal_parameter_new_role (I_CAL_ROLE_REQPARTICIPANT);
					cutype = i_cal_parameter_new_cutype (I_CAL_CUTYPE_INDIVIDUAL);
				} else if (g_ascii_strcasecmp (attendee->attendeetype, "Resource") == 0) {
					param = i_cal_parameter_new_role (I_CAL_ROLE_NONPARTICIPANT);
					cutype = i_cal_parameter_new_cutype (I_CAL_CUTYPE_RESOURCE);
				} else {
					param = i_cal_parameter_new_role (I_CAL_ROLE_OPTPARTICIPANT);
					cutype = i_cal_parameter_new_cutype (I_CAL_CUTYPE_INDIVIDUAL);
				}
				i_cal_property_take_parameter (prop, cutype);
				i_cal_property_take_parameter (prop, param);

				if (is_response_requested) {
					param = i_cal_parameter_new_rsvp (I_CAL_RSVP_TRUE);
					i_cal_property_take_parameter (prop, param);
				}

				if (user_email && (email || attendee->mailbox->email) && e_ews_item_get_my_response_type (item) &&
				    g_ascii_strcasecmp (email ? email : attendee->mailbox->email, user_email) == 0) {
					param = ecb_ews_responsetype_to_partstat (e_ews_item_get_my_response_type (item));
				} else {
					param = ecb_ews_responsetype_to_partstat (attendee->responsetype);
				}
				i_cal_property_take_parameter (prop, param);

				i_cal_component_take_property (icomp, prop);
			}

			g_free (user_email);
		}

		/* Free/Busy */
		xvalue = e_cal_util_component_dup_x_property (icomp, "X-MICROSOFT-CDO-BUSYSTATUS");
		freebusy = i_cal_component_get_first_property (icomp, I_CAL_TRANSP_PROPERTY);
		if (!freebusy && (e_ews_item_get_item_type (item) != E_EWS_ITEM_TYPE_TASK)) {
			/* Busy by default */
			freebusy = i_cal_property_new_transp (g_strcmp0 (xvalue, "BUSY") == 0 ? I_CAL_TRANSP_OPAQUE : I_CAL_TRANSP_TRANSPARENT);
			i_cal_component_add_property (icomp, freebusy);
		} else if (freebusy) {
			if (g_strcmp0 (xvalue, "BUSY") == 0) {
				i_cal_property_set_transp (freebusy, I_CAL_TRANSP_OPAQUE);
			} else {
				i_cal_property_set_transp (freebusy, I_CAL_TRANSP_TRANSPARENT);
			}
		}
		g_clear_object (&freebusy);
		g_free (xvalue);

		/* AllDayEvent */
		xvalue = e_cal_util_component_dup_x_property (icomp, "X-MICROSOFT-CDO-ALLDAYEVENT");
		if (g_strcmp0 (xvalue, "TRUE") == 0) {
			itt = i_cal_component_get_dtstart (icomp);
			i_cal_time_set_is_date (itt, TRUE);
			i_cal_component_set_dtstart (icomp, itt);
			g_object_unref (itt);

			itt = i_cal_component_get_dtend (icomp);
			i_cal_time_set_is_date (itt, TRUE);
			i_cal_component_set_dtend (icomp, itt);
			g_object_unref (itt);
		}
		g_free (xvalue);

		/* Present the online meeting URL as the 'URL' property, thus it can be found in the UI */
		xvalue = e_cal_util_component_dup_x_property (icomp, "X-MICROSOFT-SKYPETEAMSMEETINGURL");
		if (xvalue && *xvalue) {
			prop = i_cal_component_get_first_property (icomp, I_CAL_URL_PROPERTY);
			if (prop) {
				/* Do not overwrite existing property. */
				g_object_unref (prop);
			} else {
				prop = i_cal_property_new_url (xvalue);
				i_cal_component_take_property (icomp, prop);
			}
		}
		g_free (xvalue);

		if (e_cal_util_component_has_property (icomp, I_CAL_RECURRENCEID_PROPERTY)) {
			/* Exchange sets RRULE even on the children, which is broken */
			e_cal_util_component_remove_property_by_kind (icomp, I_CAL_RRULE_PROPERTY, TRUE);
		}

		/* The EXDATE sent by the server can be date-time format with timezone, while
		   the event start time can be date-only. This breaks the rules, thus correct
		   it and make also EXDATE date-only. */
		itt = i_cal_component_get_dtstart (icomp);
		if (itt && i_cal_time_is_valid_time (itt) && i_cal_time_is_date (itt)) {
			for (prop = i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY);
			     prop;
			     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_EXDATE_PROPERTY)) {
				g_clear_object (&itt);

				itt = i_cal_property_get_exdate (prop);
				i_cal_time_set_is_date (itt, TRUE);
				i_cal_property_set_exdate (prop, itt);

				i_cal_property_remove_parameter_by_kind (prop, I_CAL_TZID_PARAMETER);
			}

			for (prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY);
			     prop;
			     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_RECURRENCEID_PROPERTY)) {
				g_clear_object (&itt);

				itt = i_cal_property_get_recurrenceid (prop);
				i_cal_time_set_is_date (itt, TRUE);
				i_cal_property_set_recurrenceid (prop, itt);

				i_cal_property_remove_parameter_by_kind (prop, I_CAL_TZID_PARAMETER);
			}
		}
		g_clear_object (&itt);

		/* Exchange sets an ORGANIZER on all events. RFC2445 says:
		 *
		 *   This property MUST NOT be specified in an iCalendar
		 *   object that specifies only a time zone definition or
		 *   that defines calendar entities that are not group
		 *   scheduled entities, but are entities only on a single
		 *   user's calendar.
		 */
		if (!e_cal_util_component_has_property (icomp, I_CAL_ATTENDEE_PROPERTY)) {
			e_cal_util_component_remove_property_by_kind (icomp, I_CAL_ORGANIZER_PROPERTY, TRUE);
		} else if (i_cal_component_count_properties (icomp, I_CAL_ORGANIZER_PROPERTY) == 1 &&
			   i_cal_component_count_properties (icomp, I_CAL_ATTENDEE_PROPERTY) == 1) {
			/* Outlook 2019 adds the ORGANIZER and the ATTENDEE for plain events, having
			   them both address the same user, thus not a real meeting, verify that
			   and remove the properties if it's so. */
			ICalProperty *organizer, *attendee;

			organizer = i_cal_component_get_first_property (icomp, I_CAL_ORGANIZER_PROPERTY);
			attendee = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);

			if (organizer && attendee &&
			    i_cal_property_get_organizer (organizer) &&
			    i_cal_property_get_attendee (attendee) &&
			    g_ascii_strcasecmp (i_cal_property_get_organizer (organizer), i_cal_property_get_attendee (attendee)) == 0) {
				e_cal_util_component_remove_property_by_kind (icomp, I_CAL_ORGANIZER_PROPERTY, TRUE);
				e_cal_util_component_remove_property_by_kind (icomp, I_CAL_ATTENDEE_PROPERTY, TRUE);
			}

			g_clear_object (&organizer);
			g_clear_object (&attendee);
		}

		if (e_ews_item_get_event_url (item)) {
			prop = i_cal_component_get_first_property (icomp, I_CAL_URL_PROPERTY);
			if (prop) {
				i_cal_property_set_url (prop, e_ews_item_get_event_url (item));
				g_object_unref (prop);
			} else {
				prop = i_cal_property_new_url (e_ews_item_get_event_url (item));
				i_cal_component_take_property (icomp, prop);
			}
		}

		i_cal_component_set_uid (icomp, uid ? uid : item_id->id);

		e_cal_util_component_set_x_property (icomp, "X-EVOLUTION-ITEMID", item_id->id);
		e_cal_util_component_set_x_property (icomp, "X-EVOLUTION-CHANGEKEY", item_id->change_key);

		if (e_ews_item_get_body_type (item) == E_EWS_BODY_TYPE_HTML) {
			const gchar *html_body = e_ews_item_get_body (item);

			if (html_body && *html_body) {
				prop = i_cal_component_get_first_property (icomp, I_CAL_DESCRIPTION_PROPERTY);

				/* The server can return empty HTML (with "<html><body></body></html>" only),
				   thus add it only if there was any DESCRIPTION provided as well. */
				if (prop) {
					g_clear_object (&prop);

					prop = i_cal_property_new_x (html_body);
					i_cal_property_set_x_name (prop, "X-ALT-DESC");
					i_cal_property_set_parameter_from_string (prop, "FMTTYPE", "text/html");
					i_cal_component_take_property (icomp, prop);
				}
			}
		}

		res_component = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icomp));

		/* Categories */
		e_cal_component_set_categories_list (res_component, (GSList *) e_ews_item_get_categories (item));

		/*
		 * There is no API to set/get alarm description on the server side.
		 * However, for some reason, the alarm description has been set to "REMINDER"
		 * automatically (and with no i18n). Instead of show it to the user, let's
		 * set the summary as the alarm description.
		 */
		if (e_cal_component_has_alarms (res_component)) {
			ECalComponentText *text;

			text = e_cal_component_get_summary (res_component);
			if (text) {
				GSList *alarms, *link;

				alarms = e_cal_component_get_all_alarms (res_component);
				e_cal_component_remove_all_alarms (res_component);

				for (link = alarms; link; link = g_slist_next (link)) {
					ECalComponentAlarm *alarm = link->data;

					e_cal_component_alarm_set_description (alarm, text);
					e_cal_component_add_alarm (res_component, alarm);
				}

				g_slist_free_full (alarms, e_cal_component_alarm_free);
				e_cal_component_text_free (text);
			}
		}

		g_object_unref (icomp);
	}

	g_object_unref (vcomp);

	if (res_component) {
		const GSList *attachment_ids, *aid, *l;
		const gchar *uid;
		GSList *info_attachments = NULL;
		gboolean has_attachment = FALSE;

		e_ews_item_has_attachments (item, &has_attachment);
		if (!has_attachment)
			return res_component;

		uid = e_cal_component_get_uid (res_component);

		attachment_ids = e_ews_item_get_attachments_ids (item);

		if (e_ews_connection_get_attachments_sync (
			cbews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			uid,
			attachment_ids,
			cbews->priv->attachments_dir,
			TRUE,
			&info_attachments,
			NULL, NULL,
			cancellable,
			NULL)) {
			ICalProperty *prop;
			ICalParameter *param;
			GSList *attaches = NULL;

			for (l = info_attachments; l; l = l->next) {
				EEwsAttachmentInfo *info = l->data;

				/* ignore non-uri attachments, because it's an exception */
				if (e_ews_attachment_info_get_type (info) == E_EWS_ATTACHMENT_INFO_TYPE_URI) {
					const gchar *uri = e_ews_attachment_info_get_uri (info);

					if (uri) {
						attaches = g_slist_prepend (attaches,
							i_cal_attach_new_from_url (uri));
					}
				}
			}

			attaches = g_slist_reverse (attaches);
			e_cal_component_set_attachments (res_component, attaches);

			icomp = e_cal_component_get_icalcomponent (res_component);
			for (aid = attachment_ids, prop = i_cal_component_get_first_property (icomp, I_CAL_ATTACH_PROPERTY);
			     aid && prop;
			     aid = aid->next, g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_ATTACH_PROPERTY)) {
				param = i_cal_parameter_new_x (aid->data);
				i_cal_parameter_set_xname (param, "X-EWS-ATTACHMENTID");
				i_cal_property_take_parameter (prop, param);
			}

			g_slist_free_full (attaches, g_object_unref);
			g_slist_free_full (info_attachments, (GDestroyNotify) e_ews_attachment_info_free);
		}
	}

	return res_component;
}

static void
ecb_ews_store_original_comp (ECalComponent *comp)
{
	gchar *comp_str;
	gchar *base64;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	/* This makes sure it's not saved also in the original component */
	e_cal_util_component_remove_x_property (e_cal_component_get_icalcomponent (comp), X_EWS_ORIGINAL_COMP);

	comp_str = e_cal_component_get_as_string (comp);
	g_return_if_fail (comp_str != NULL);

	/* Include NUL-terminator */
	base64 = g_base64_encode ((const guchar *) comp_str, strlen (comp_str) + 1);

	e_cal_util_component_set_x_property (e_cal_component_get_icalcomponent (comp),
		X_EWS_ORIGINAL_COMP, base64);

	g_free (base64);
	g_free (comp_str);
}

static ECalComponent * /* free with g_object_unref(), if not NULL */
ecb_ews_restore_original_comp (ECalComponent *from_comp)
{
	ECalComponent *comp = NULL;
	gchar *original_base64;
	guchar *decoded;
	gsize len = -1;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (from_comp), NULL);

	original_base64 = e_cal_util_component_dup_x_property (e_cal_component_get_icalcomponent (from_comp), X_EWS_ORIGINAL_COMP);

	if (!original_base64 || !*original_base64) {
		g_free (original_base64);
		return NULL;
	}

	decoded = g_base64_decode (original_base64, &len);

	g_free (original_base64);

	if (!decoded || !*decoded || len <= 0) {
		g_free (decoded);
		return NULL;
	}

	if (decoded[len - 1] != '\0') {
		gchar *tmp;

		tmp = g_strndup ((const gchar *) decoded, len);

		g_free (decoded);
		decoded = (guchar *) tmp;
	}

	if (decoded && *decoded)
		comp = e_cal_component_new_from_string ((const gchar *) decoded);

	g_free (decoded);

	return comp;
}

static gboolean
ecb_ews_get_items_sync (ECalBackendEws *cbews,
			const GSList *item_ids, /* gchar * */
			const gchar *default_props,
			const EEwsAdditionalProps *add_props,
			GSList **out_components, /* ECalComponent * */
			GCancellable *cancellable,
			GError **error)
{
	GSList *items = NULL, *link, *retry_ids = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (out_components != NULL, FALSE);

	while (success = success && !g_cancellable_set_error_if_cancelled (cancellable, error), success) {
		GSList *received = NULL, *new_retry_ids = NULL, *ids_link;

		success = e_ews_connection_get_items_sync (
			cbews->priv->cnc,
			EWS_PRIORITY_MEDIUM,
			item_ids,
			default_props,
			add_props,
			FALSE,
			NULL,
			E_EWS_BODY_TYPE_BEST,
			&received,
			NULL, NULL,
			cancellable,
			error);

		for (link = received, ids_link = (GSList *) item_ids; success && link && ids_link; link = g_slist_next (link), ids_link = g_slist_next (ids_link)) {
			EEwsItem *item = link->data;

			if (!item)
				continue;

			if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
				const GError *item_error;

				item_error = e_ews_item_get_error (item);
				if (g_error_matches (item_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_BATCHPROCESSINGSTOPPED)) {
					new_retry_ids = g_slist_prepend (new_retry_ids, g_strdup (ids_link->data));
					g_object_unref (item);
				} else {
					items = g_slist_prepend (items, item);
				}
			} else {
				items = g_slist_prepend (items, item);
			}
		}

		g_slist_free_full (retry_ids, g_free);
		g_slist_free (received);
		retry_ids = new_retry_ids;

		if (!retry_ids)
			break;

		item_ids = retry_ids;
	}

	g_slist_free_full (retry_ids, g_free);

	items = g_slist_reverse (items);

	if (!success)
		goto exit;

	/* fetch modified occurrences */
	for (link = items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const GSList *modified_occurrences;

		if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		modified_occurrences = e_ews_item_get_modified_occurrences (item);
		if (modified_occurrences) {
			EEwsAdditionalProps *modified_add_props;
			EEwsExtendedFieldURI *ext_uri;

			modified_add_props = e_ews_additional_props_new ();
			if (e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010)) {
				modified_add_props->field_uri = g_strdup (GET_ITEMS_SYNC_PROPERTIES_2010);

				ext_uri = e_ews_extended_field_uri_new ();
				ext_uri->distinguished_prop_set_id = g_strdup ("PublicStrings");
				ext_uri->prop_name = g_strdup ("EvolutionEWSStartTimeZone");
				ext_uri->prop_type = g_strdup ("String");
				modified_add_props->extended_furis = g_slist_append (modified_add_props->extended_furis, ext_uri);

				ext_uri = e_ews_extended_field_uri_new ();
				ext_uri->distinguished_prop_set_id = g_strdup ("PublicStrings");
				ext_uri->prop_name = g_strdup ("EvolutionEWSEndTimeZone");
				ext_uri->prop_type = g_strdup ("String");
				modified_add_props->extended_furis = g_slist_append (modified_add_props->extended_furis, ext_uri);
			} else {
				modified_add_props->field_uri = g_strdup (GET_ITEMS_SYNC_PROPERTIES_2007);
			}

			ext_uri = e_ews_extended_field_uri_new ();
			ext_uri->distinguished_prop_set_id = g_strdup ("PublicStrings");
			ext_uri->prop_name = g_strdup ("EvolutionEWSURL");
			ext_uri->prop_type = g_strdup ("String");
			modified_add_props->extended_furis = g_slist_append (modified_add_props->extended_furis, ext_uri);

			success = ecb_ews_get_items_sync (cbews, modified_occurrences, "IdOnly", modified_add_props, out_components, cancellable, error);

			e_ews_additional_props_free (modified_add_props);

			if (!success)
				goto exit;
		}
	}

	for (link = items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		ECalComponent *comp;
		GError *local_error = NULL;

		if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		comp = ecb_ews_item_to_component_sync (cbews, item, cancellable, &local_error);
		if (!comp) {
			if (!local_error)
				continue;

			g_propagate_error (error, local_error);
			success = FALSE;
			break;
		}

		ecb_ews_store_original_comp (comp);

		*out_components = g_slist_prepend (*out_components, comp);
	}

 exit:
	g_slist_free_full (items, g_object_unref);

	return success;
}

static gboolean
ecb_ews_fetch_items_sync (ECalBackendEws *cbews,
			  const GSList *items, /* EEwsItem * */
			  GSList **out_components, /* ECalComponent * */
			  GCancellable *cancellable,
			  GError **error)
{
	GSList *event_ids = NULL, *task_memo_ids = NULL, *link;
	gboolean success = TRUE;

	for (link = (GSList *) items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const EwsId *id = e_ews_item_get_id (item);
		EEwsItemType type = e_ews_item_get_item_type (item);

		if (type == E_EWS_ITEM_TYPE_EVENT)
			event_ids = g_slist_prepend (event_ids, g_strdup (id->id));
		else if (type == E_EWS_ITEM_TYPE_TASK || type == E_EWS_ITEM_TYPE_MEMO) {
			task_memo_ids = g_slist_prepend (task_memo_ids, g_strdup (id->id));
		}
	}

	if (event_ids) {
		EEwsAdditionalProps *add_props;
		EEwsExtendedFieldURI *ext_uri;

		add_props = e_ews_additional_props_new ();
		if (e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010)) {
			add_props->field_uri = g_strdup (GET_ITEMS_SYNC_PROPERTIES_2010);

			ext_uri = e_ews_extended_field_uri_new ();
			ext_uri->distinguished_prop_set_id = g_strdup ("PublicStrings");
			ext_uri->prop_name = g_strdup ("EvolutionEWSStartTimeZone");
			ext_uri->prop_type = g_strdup ("String");
			add_props->extended_furis = g_slist_append (add_props->extended_furis, ext_uri);

			ext_uri = e_ews_extended_field_uri_new ();
			ext_uri->distinguished_prop_set_id = g_strdup ("PublicStrings");
			ext_uri->prop_name = g_strdup ("EvolutionEWSEndTimeZone");
			ext_uri->prop_type = g_strdup ("String");
			add_props->extended_furis = g_slist_append (add_props->extended_furis, ext_uri);
		} else {
			add_props->field_uri = g_strdup (GET_ITEMS_SYNC_PROPERTIES_2007);
		}

		ext_uri = e_ews_extended_field_uri_new ();
		ext_uri->distinguished_prop_set_id = g_strdup ("PublicStrings");
		ext_uri->prop_name = g_strdup ("EvolutionEWSURL");
		ext_uri->prop_type = g_strdup ("String");
		add_props->extended_furis = g_slist_append (add_props->extended_furis, ext_uri);

		success = ecb_ews_get_items_sync (cbews, event_ids, "IdOnly", add_props, out_components, cancellable, error);

		e_ews_additional_props_free (add_props);
	}

	if (task_memo_ids && success)
		success = ecb_ews_get_items_sync (cbews, task_memo_ids, "AllProperties", NULL, out_components, cancellable, error);

	g_slist_free_full (event_ids, g_free);
	g_slist_free_full (task_memo_ids, g_free);

	return success;
}

static gint
ecb_ews_compare_time_and_free (ICalTime *itt1,
			       ICalTime *itt2)
{
	gint res;

	if (!itt1 || !itt2) {
		res = itt1 == itt2 ? 0 : itt1 ? 1 : -1;
	} else {
		res = i_cal_time_compare (itt1, itt2);
	}

	g_clear_object (&itt1);
	g_clear_object (&itt2);

	return res;
}

static gboolean
ecb_ews_freebusy_ecomp_changed (ECalComponent *ecomp,
				ICalComponent *vevent)
{
	ICalComponent *icomp;
	gboolean changed = FALSE;

	g_return_val_if_fail (vevent != NULL, FALSE);

	if (!ecomp)
		return TRUE;

	icomp = e_cal_component_get_icalcomponent (ecomp);
	if (!icomp)
		return TRUE;

	if (!changed)
		changed = g_strcmp0 (i_cal_component_get_summary (icomp), i_cal_component_get_summary (vevent)) != 0;
	if (!changed)
		changed = g_strcmp0 (i_cal_component_get_location (icomp), i_cal_component_get_location (vevent)) != 0;
	if (!changed)
		changed = ecb_ews_compare_time_and_free (i_cal_component_get_dtstart (icomp), i_cal_component_get_dtstart (vevent)) != 0;
	if (!changed)
		changed = ecb_ews_compare_time_and_free (i_cal_component_get_dtend (icomp), i_cal_component_get_dtend (vevent)) != 0;

	return changed;
}

static gboolean
ecb_ews_changekey_equal (ICalComponent *icomp,
			 const gchar *changekey)
{
	gchar *stored;
	gboolean res;

	stored = e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
	res = g_strcmp0 (stored, changekey) == 0;
	g_free (stored);

	return res;
}

static GSList * /* the possibly modified 'in_items' */
ecb_ews_verify_changes (ECalCache *cal_cache,
			ICalComponentKind kind,
			GSList *in_items, /* EEwsItem * */
			GCancellable *cancellable)
{
	GSList *items = NULL, *link;

	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), in_items);

	for (link = in_items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const EwsId *id = e_ews_item_get_id (item);
		EEwsItemType type = e_ews_item_get_item_type (item);

		if (!g_cancellable_is_cancelled (cancellable) && (
		    (type == E_EWS_ITEM_TYPE_EVENT && kind == I_CAL_VEVENT_COMPONENT) ||
		    (type == E_EWS_ITEM_TYPE_MEMO && kind == I_CAL_VJOURNAL_COMPONENT) ||
		    (type == E_EWS_ITEM_TYPE_TASK && kind == I_CAL_VTODO_COMPONENT) )) {
			ECalComponent *existing = NULL;

			if (e_cal_cache_get_component (cal_cache, id->id, NULL, &existing, cancellable, NULL) &&
			    existing && ecb_ews_changekey_equal (e_cal_component_get_icalcomponent (existing), id->change_key)) {
				g_object_unref (item);
			} else {
				items = g_slist_prepend (items, item);
			}

			g_clear_object (&existing);
		} else if (type == E_EWS_ITEM_TYPE_EVENT ||
			   type == E_EWS_ITEM_TYPE_MEMO ||
			   type == E_EWS_ITEM_TYPE_TASK) {
			g_object_unref (item);
		} else {
			items = g_slist_prepend (items, item);
		}
	}

	g_slist_free (in_items);

	return items;
}

static GSList * /* ECalMetaBackendInfo */
ecb_ews_components_to_infos (ECalMetaBackend *meta_backend,
			     const GSList *components, /* ECalComponent * */
			     ICalComponentKind kind)
{
	GSList *nfos = NULL, *link;
	GHashTable *sorted_by_uids; /* gchar * ~> GSList { ECalComponent * } */
	GHashTableIter iter;
	gpointer key, value;

	sorted_by_uids = g_hash_table_new (g_str_hash, g_str_equal);

	for (link = (GSList *) components; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ICalComponent *icomp;
		const gchar *uid;
		GSList *instances;

		if (!comp)
			continue;

		icomp = e_cal_component_get_icalcomponent (comp);
		uid = i_cal_component_get_uid (icomp);

		if (!uid)
			continue;

		ecb_ews_store_original_comp (comp);

		instances = g_hash_table_lookup (sorted_by_uids, uid);
		g_hash_table_insert (sorted_by_uids, (gpointer) uid, g_slist_prepend (instances, comp));
	}

	g_hash_table_iter_init (&iter, sorted_by_uids);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *uid = key;
		GSList *instances = value;
		ICalComponent *icomp, *merged;
		ECalComponent *comp;
		ECalMetaBackendInfo *nfo;
		gchar *revision, *itemid;

		if (!uid || !instances) {
			g_slist_free (instances);
			continue;
		}

		/* Try to find master object, to have seves marter's itemid in ECalMetaBackendInfo::extra,
		   thus the re-load of the event is done for the whole series and not for a detached instance */
		comp = NULL;
		for (link = instances; link && !comp; link = g_slist_next (link)) {
			comp = link->data;

			if (!comp)
				continue;

			if (e_cal_component_is_instance (comp))
				comp = NULL;
		}

		if (!comp)
			comp = instances->data;

		if (!comp) {
			g_slist_free (instances);
			continue;
		}

		icomp = e_cal_component_get_icalcomponent (comp);
		itemid = e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-ITEMID");
		revision = e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
		merged = e_cal_meta_backend_merge_instances (meta_backend, instances, FALSE);

		if (!merged) {
			g_slist_free (instances);
			g_free (revision);
			g_free (itemid);
			continue;
		}

		nfo = e_cal_meta_backend_info_new (uid, revision, NULL, itemid);
		nfo->object = i_cal_component_as_ical_string (merged);

		nfos = g_slist_prepend (nfos, nfo);

		g_slist_free (instances);
		g_object_unref (merged);
		g_free (revision);
		g_free (itemid);
	}

	g_hash_table_destroy (sorted_by_uids);

	return nfos;
}

static void
ecb_ews_extract_item_id (ECalComponent *comp,
			 gchar **out_id,
			 gchar **out_change_key)
{
	ICalComponent *icomp;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_if_fail (icomp != NULL);

	if (out_id)
		*out_id = e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-ITEMID");
	if (out_change_key)
		*out_change_key = e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
}

static gboolean
ecb_ews_organizer_is_user (ECalBackendEws *cbews,
			   ECalComponent *comp)
{
	ECalComponentOrganizer *organizer;
	gboolean is_organizer = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	if (!e_cal_component_has_organizer (comp))
		return FALSE;

	organizer = e_cal_component_get_organizer (comp);
	if (organizer && e_cal_component_organizer_get_value (organizer)) {
		CamelEwsSettings *ews_settings;
		const gchar *email = e_cal_component_organizer_get_value (organizer);
		gchar *user_email;

		ews_settings = ecb_ews_get_collection_settings (cbews);

		user_email = camel_ews_settings_dup_email (ews_settings);

		email = e_cal_util_strip_mailto (email);

		is_organizer = user_email && g_ascii_strcasecmp (email, user_email) == 0;

		g_free (user_email);

		if (!is_organizer) {
			GHashTable *aliases;

			aliases = e_ews_common_utils_dup_mail_addresses (e_cal_backend_get_registry (E_CAL_BACKEND (cbews)),
				e_backend_get_source (E_BACKEND (cbews)), NULL);

			if (aliases) {
				is_organizer = g_hash_table_contains (aliases, email);

				g_hash_table_unref (aliases);
			}
		}
	}

	e_cal_component_organizer_free (organizer);

	return is_organizer;
}

static gboolean
ecb_ews_can_send_invitations (ECalBackendEws *cbews,
			      guint32 opflags,
			      ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	if ((opflags & E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE) != 0)
		return FALSE;

	return ecb_ews_organizer_is_user (cbews, comp);
}

static void
ecb_ews_subscription_id_changed_cb (EEwsConnection *cnc,
				    const gchar *subscription_id,
				    gpointer user_data)
{
	ECalBackendEws *cbews = user_data;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (cbews));

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	if (g_strcmp0 (cbews->priv->last_subscription_id, subscription_id) != 0) {
		g_free (cbews->priv->last_subscription_id);
		cbews->priv->last_subscription_id = g_strdup (subscription_id);
	}

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);
}

static gboolean
ecb_ews_connect_sync (ECalMetaBackend *meta_backend,
		      const ENamedParameters *credentials,
		      ESourceAuthenticationResult *out_auth_result,
		      gchar **out_certificate_pem,
		      GTlsCertificateFlags *out_certificate_errors,
		      GCancellable *cancellable,
		      GError **error)
{
	ECalBackendEws *cbews;
	CamelEwsSettings *ews_settings;
	gchar *hosturl;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	cbews = E_CAL_BACKEND_EWS (meta_backend);

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	if (cbews->priv->cnc) {
		g_rec_mutex_unlock (&cbews->priv->cnc_lock);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	}

	ews_settings = ecb_ews_get_collection_settings (cbews);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);

	cbews->priv->cnc = e_ews_connection_new_for_backend (E_BACKEND (cbews), e_cal_backend_get_registry (E_CAL_BACKEND (cbews)), hosturl, ews_settings);

	e_binding_bind_property (
		cbews, "proxy-resolver",
		cbews->priv->cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	*out_auth_result = e_ews_connection_try_credentials_sync (cbews->priv->cnc, credentials, NULL,
		out_certificate_pem, out_certificate_errors, cancellable, error);

	if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		ESource *source = e_backend_get_source (E_BACKEND (cbews));
		ESourceEwsFolder *ews_folder;

		ews_folder = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);

		g_free (cbews->priv->folder_id);
		cbews->priv->folder_id = e_source_ews_folder_dup_id (ews_folder);
		cbews->priv->is_freebusy_calendar = cbews->priv->folder_id && g_str_has_prefix (cbews->priv->folder_id, "freebusy-calendar::");

		g_signal_connect_swapped (cbews->priv->cnc, "server-notification",
			G_CALLBACK (ecb_ews_server_notification_cb), cbews);

		if (!cbews->priv->is_freebusy_calendar &&
		    camel_ews_settings_get_listen_notifications (ews_settings) &&
		    e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010_SP1) &&
		    !e_source_ews_folder_get_foreign (ews_folder) &&
		    !e_source_ews_folder_get_public (ews_folder)) {
			GSList *folders = NULL;

			folders = g_slist_prepend (folders, cbews->priv->folder_id);

			e_ews_connection_set_last_subscription_id (cbews->priv->cnc, cbews->priv->last_subscription_id);

			g_signal_connect_object (cbews->priv->cnc, "subscription-id-changed",
				G_CALLBACK (ecb_ews_subscription_id_changed_cb), cbews, 0);

			e_ews_connection_enable_notifications_sync (cbews->priv->cnc,
				folders, &cbews->priv->subscription_key);

			g_slist_free (folders);
		}

		e_cal_backend_set_writable (E_CAL_BACKEND (cbews), !cbews->priv->is_freebusy_calendar);
		success = TRUE;
	} else {
		ecb_ews_convert_error_to_edc_error (error);
		g_clear_object (&cbews->priv->cnc);
	}

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);

	g_free (hosturl);

	return success;
}

static gboolean
ecb_ews_disconnect_sync (ECalMetaBackend *meta_backend,
			 GCancellable *cancellable,
			 GError **error)
{
	ECalBackendEws *cbews;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (meta_backend), FALSE);

	cbews = E_CAL_BACKEND_EWS (meta_backend);

	ecb_ews_unset_connection (cbews, TRUE);

	return TRUE;
}

static gboolean
ecb_ews_get_changes_sync (ECalMetaBackend *meta_backend,
			  const gchar *last_sync_tag,
			  gboolean is_repeat,
			  gchar **out_new_sync_tag,
			  gboolean *out_repeat,
			  GSList **out_created_objects,
			  GSList **out_modified_objects,
			  GSList **out_removed_objects,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalBackendEws *cbews;
	ECalCache *cal_cache;
	gboolean sync_tag_stamp_changed;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	cbews = E_CAL_BACKEND_EWS (meta_backend);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	sync_tag_stamp_changed = ecb_ews_get_sync_tag_stamp_changed (cbews);
	if (sync_tag_stamp_changed)
		last_sync_tag = NULL;

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	if (cbews->priv->is_freebusy_calendar) {
		ESourceEwsFolder *ews_folder;
		EEWSFreeBusyData fbdata;
		GSList *free_busy = NULL, *link;
		time_t today;

		ews_folder = e_source_get_extension (e_backend_get_source (E_BACKEND (cbews)), E_SOURCE_EXTENSION_EWS_FOLDER);

		today = time_day_begin (time (NULL));

		fbdata.period_start = time_add_week (today, -e_source_ews_folder_get_freebusy_weeks_before (ews_folder));
		fbdata.period_end = time_day_end (time_add_week (today, e_source_ews_folder_get_freebusy_weeks_after (ews_folder)));
		fbdata.user_mails = g_slist_prepend (NULL, e_source_ews_folder_dup_foreign_mail (ews_folder));

		success = e_ews_connection_get_free_busy_sync (cbews->priv->cnc, G_PRIORITY_DEFAULT,
			e_ews_cal_utils_prepare_free_busy_request, &fbdata,
			&free_busy, cancellable, &local_error);

		if (success) {
			ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();
			GSList *comps = NULL;
			GHashTable *known;
			GHashTableIter iter;
			gpointer key;

			known = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

			if (e_cal_cache_search_components (cal_cache, NULL, &comps, cancellable, NULL)) {
				for (link = comps; link; link = g_slist_next (link)) {
					ECalComponent *comp = link->data;
					ICalComponent *icomp;
					const gchar *uid;

					if (!comp)
						continue;

					icomp = e_cal_component_get_icalcomponent (comp);
					if (!icomp)
						continue;

					uid = i_cal_component_get_uid (icomp);

					if (uid && *uid)
						g_hash_table_insert (known, g_strdup (uid), g_object_ref (comp));
				}

				g_slist_free_full (comps, g_object_unref);
			}

			for (link = free_busy; link; link = g_slist_next (link)) {
				ICalComponent *fbcomp = link->data;
				ICalProperty *fbprop;
				ICalParameter *param;
				ICalPeriod *fb;
				ICalParameterFbtype fbtype;

				if (!fbcomp || i_cal_component_isa (fbcomp) != I_CAL_VFREEBUSY_COMPONENT)
					continue;

				for (fbprop = i_cal_component_get_first_property (fbcomp, I_CAL_FREEBUSY_PROPERTY);
				     fbprop;
				     g_object_unref (fbprop), fbprop = i_cal_component_get_next_property (fbcomp, I_CAL_FREEBUSY_PROPERTY)) {
					ECalComponent *ecomp;
					ICalComponent *vevent;
					ICalTime *itt;
					gchar *id, *summary, *location;

					param = i_cal_property_get_first_parameter (fbprop, I_CAL_FBTYPE_PARAMETER);
					if (!param)
						continue;

					fbtype = i_cal_parameter_get_fbtype (param);
					g_clear_object (&param);

					if (fbtype != I_CAL_FBTYPE_FREE &&
					    fbtype != I_CAL_FBTYPE_BUSY &&
					    fbtype != I_CAL_FBTYPE_BUSYUNAVAILABLE &&
					    fbtype != I_CAL_FBTYPE_BUSYTENTATIVE)
						continue;

					fb = i_cal_property_get_freebusy (fbprop);
					id = i_cal_property_get_parameter_as_string (fbprop, "X-EWS-ID");
					summary = i_cal_property_get_parameter_as_string (fbprop, "X-SUMMARY");
					location = i_cal_property_get_parameter_as_string (fbprop, "X-LOCATION");

					vevent = i_cal_component_new_vevent ();

					if (id && *id) {
						i_cal_component_set_uid (vevent, id);
					} else {
						gchar *uid, *start, *end;

						itt = i_cal_period_get_start (fb);
						start = i_cal_time_as_ical_string (itt);
						g_clear_object (&itt);

						itt = i_cal_period_get_end (fb);
						end = i_cal_time_as_ical_string (itt);
						g_clear_object (&itt);

						uid = g_strdup_printf ("%s-%s-%d", start, end, (gint) fbtype);

						i_cal_component_set_uid (vevent, uid);

						g_free (start);
						g_free (end);
						g_free (uid);
					}

					itt = i_cal_period_get_start (fb);
					i_cal_time_set_timezone (itt, utc_zone);
					i_cal_component_set_dtstart (vevent, itt);
					g_clear_object (&itt);

					itt = i_cal_period_get_end (fb);
					i_cal_time_set_timezone (itt, utc_zone);
					i_cal_component_set_dtend (vevent, itt);
					g_clear_object (&itt);

					itt = i_cal_time_new_current_with_zone (utc_zone);
					i_cal_component_take_property (vevent, i_cal_property_new_created (itt));
					g_clear_object (&itt);

					if (fbtype == I_CAL_FBTYPE_FREE) {
						i_cal_component_set_summary (vevent, C_("FreeBusyType", "Free"));
						i_cal_component_take_property (vevent, i_cal_property_new_transp (I_CAL_TRANSP_TRANSPARENT));
					} else if (fbtype == I_CAL_FBTYPE_BUSY) {
						i_cal_component_set_summary (vevent, C_("FreeBusyType", "Busy"));
					} else if (fbtype == I_CAL_FBTYPE_BUSYUNAVAILABLE) {
						i_cal_component_set_summary (vevent, C_("FreeBusyType", "Out of Office"));
					} else if (fbtype == I_CAL_FBTYPE_BUSYTENTATIVE) {
						i_cal_component_set_summary (vevent, C_("FreeBusyType", "Tentative"));
					}

					if (summary && *summary)
						i_cal_component_set_summary (vevent, summary);

					if (location && *location)
						i_cal_component_set_location (vevent, location);

					ecomp = g_hash_table_lookup (known, i_cal_component_get_uid (vevent));
					if (ecomp) {
						g_object_ref (ecomp);

						/* This dereferences the ecomp, thus the ref() call above to keep it alive */
						g_hash_table_remove (known, i_cal_component_get_uid (vevent));

						if (ecb_ews_freebusy_ecomp_changed (ecomp, vevent)) {
							ECalMetaBackendInfo *nfo;
							gchar *revision = e_util_generate_uid ();

							e_cal_util_component_set_x_property (vevent, "X-EVOLUTION-CHANGEKEY", revision);

							nfo = e_cal_meta_backend_info_new (i_cal_component_get_uid (vevent), NULL, NULL, NULL);
							nfo->revision = revision;
							nfo->object = i_cal_component_as_ical_string (vevent);

							*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
						}

						g_clear_object (&ecomp);
					} else {
						ECalMetaBackendInfo *nfo;
						gchar *revision = e_util_generate_uid ();

						e_cal_util_component_set_x_property (vevent, "X-EVOLUTION-CHANGEKEY", revision);

						nfo = e_cal_meta_backend_info_new (i_cal_component_get_uid (vevent), NULL, NULL, NULL);
						nfo->revision = revision;
						nfo->object = i_cal_component_as_ical_string (vevent);

						*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
					}

					g_free (id);
					g_free (summary);
					g_free (location);
					g_object_unref (vevent);
				}
			}

			g_hash_table_iter_init (&iter, known);
			while (g_hash_table_iter_next (&iter, &key, NULL)) {
				const gchar *uid = key;

				*out_removed_objects = g_slist_prepend (*out_removed_objects,
					e_cal_meta_backend_info_new (uid, NULL, NULL, NULL));
			}

			g_hash_table_destroy (known);
		} else if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NOFREEBUSYACCESS)) {
			e_cal_meta_backend_empty_cache_sync (meta_backend, cancellable, NULL);

			e_cal_backend_notify_error (E_CAL_BACKEND (cbews), local_error->message);
			g_clear_error (&local_error);
			success = TRUE;
		} else {
			g_propagate_error (error, local_error);
		}

		g_slist_free_full (free_busy, g_object_unref);
		g_slist_free_full (fbdata.user_mails, g_free);
	} else {
		GSList *items_created = NULL, *items_modified = NULL, *items_deleted = NULL, *link;
		EEwsAdditionalProps *add_props;
		gboolean includes_last_item = TRUE;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup ("item:ItemClass");

		success = e_ews_connection_sync_folder_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
			last_sync_tag, cbews->priv->folder_id, "IdOnly", add_props, EWS_MAX_FETCH_COUNT,
			out_new_sync_tag, &includes_last_item, &items_created, &items_modified, &items_deleted,
			cancellable, &local_error);

		if (!success &&
		    g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
			g_clear_error (&local_error);

			e_cal_meta_backend_empty_cache_sync (meta_backend, cancellable, NULL);

			success = e_ews_connection_sync_folder_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
				NULL, cbews->priv->folder_id, "IdOnly", add_props, EWS_MAX_FETCH_COUNT,
				out_new_sync_tag, &includes_last_item, &items_created, &items_modified, &items_deleted,
				cancellable, &local_error);
		}

		e_ews_additional_props_free (add_props);

		if (success) {
			GSList *components_created = NULL, *components_modified = NULL;
			ICalComponentKind kind;

			kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbews));

			/* The sync state doesn't cover changes made by save_component_sync(),
			   thus verify the changes, instead of re-donwloading the component again */
			items_created = ecb_ews_verify_changes (cal_cache, kind, items_created, cancellable);
			items_modified = ecb_ews_verify_changes (cal_cache, kind, items_modified, cancellable);

			if (items_created) {
				success = ecb_ews_fetch_items_sync (cbews, items_created, &components_created, cancellable, error);
				if (success)
					*out_created_objects = ecb_ews_components_to_infos (meta_backend, components_created, kind);
			}

			if (items_modified) {
				success = ecb_ews_fetch_items_sync (cbews, items_modified, &components_modified, cancellable, error);
				if (success)
					*out_modified_objects = ecb_ews_components_to_infos (meta_backend, components_modified, kind);
			}

			for (link = items_deleted; link; link = g_slist_next (link)) {
				const gchar *item_id = link->data;
				GSList *ids = NULL, *ilink;

				if (!e_cal_cache_get_ids_with_extra (cal_cache, item_id, &ids, cancellable, NULL))
					continue;

				for (ilink = ids; ilink; ilink = g_slist_next (ilink)) {
					ECalComponentId *id = ilink->data;

					/* Use the master object */
					if (id && e_cal_component_id_get_uid (id) && e_cal_component_id_get_uid (id)[0] && !e_cal_component_id_get_rid (id)) {
						*out_removed_objects = g_slist_prepend (*out_removed_objects,
							e_cal_meta_backend_info_new (e_cal_component_id_get_uid (id), NULL, NULL, NULL));
						break;
					}
				}

				g_slist_free_full (ids, e_cal_component_id_free);
			}

			g_slist_free_full (components_created, g_object_unref);
			g_slist_free_full (components_modified, g_object_unref);

			*out_repeat = !includes_last_item;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		g_slist_free_full (items_created, g_object_unref);
		g_slist_free_full (items_modified, g_object_unref);
		g_slist_free_full (items_deleted, g_free);
	}

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);
	g_clear_object (&cal_cache);

	if (success && sync_tag_stamp_changed)
		ecb_ews_update_sync_tag_stamp (cbews);

	return success;
}

static gboolean
ecb_ews_load_component_sync (ECalMetaBackend *meta_backend,
			     const gchar *uid,
			     const gchar *extra,
			     ICalComponent **out_component,
			     gchar **out_extra,
			     GCancellable *cancellable,
			     GError **error)
{
	ECalBackendEws *cbews;
	GSList *ids, *items = NULL, *components = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	cbews = E_CAL_BACKEND_EWS (meta_backend);

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	ids = g_slist_prepend (NULL, (gpointer) (extra && *extra ? extra : uid));

	success = e_ews_connection_get_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, ids, "IdOnly",
		NULL, FALSE, NULL, E_EWS_BODY_TYPE_TEXT, &items, NULL, NULL, cancellable, error);

	g_slist_free (ids);

	if (success && items) {
		success = ecb_ews_fetch_items_sync (cbews, items, &components, cancellable, error);

		if (components) {
			const EwsId *ews_id = e_ews_item_get_id (items->data);

			if (ews_id)
				*out_extra = g_strdup (ews_id->id);

			if (components->next) {
				GSList *link;

				*out_component = i_cal_component_new_vcalendar ();

				for (link = components; link; link = g_slist_next (link)) {
					ECalComponent *comp = link->data;

					if (!comp)
						continue;

					i_cal_component_take_component (*out_component,
						i_cal_component_clone (e_cal_component_get_icalcomponent (comp)));
				}
			} else {
				*out_component = i_cal_component_clone (e_cal_component_get_icalcomponent (components->data));
			}
		} else {
			success = FALSE;
		}
	}

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);

	if (!components && e_cal_meta_backend_refresh_sync (meta_backend, cancellable, NULL)) {
		ECalCache *cal_cache;

		cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
		if (cal_cache) {
			success = e_cal_cache_get_components_by_uid (cal_cache, uid, &components, cancellable, NULL);
			if (success) {
				*out_component = e_cal_meta_backend_merge_instances (meta_backend, components, FALSE);

				if (!e_cal_cache_get_component_extra (cal_cache, uid, NULL, out_extra, cancellable, NULL))
					*out_extra = NULL;

				g_clear_error (error);
			}
			g_object_unref (cal_cache);
		}
	}

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);
	g_slist_free_full (components, g_object_unref);
	g_slist_free_full (items, g_object_unref);

	return success;
}

/* Very simple and naive component comparator, to avoid
   unnecessary uploads and changes on the server. */
static gboolean
ecb_ews_components_equal (ECalComponent *comp1,
			  ECalComponent *comp2)
{
	ICalComponent *icomp1, *icomp2;
	ICalProperty *prop1;
	GHashTable *processed_props;
	gboolean equal = TRUE;

	if (!comp1 && !comp2)
		return TRUE;
	else if (!comp1 || !comp2)
		return FALSE;

	icomp1 = e_cal_component_get_icalcomponent (comp1);
	icomp2 = e_cal_component_get_icalcomponent (comp2);

	if (!icomp1 || !icomp2)
		return FALSE;

	if (g_strcmp0 (i_cal_component_get_uid (icomp1), i_cal_component_get_uid (icomp2)) != 0)
		return FALSE;

	if (i_cal_component_count_properties (icomp1, I_CAL_ANY_PROPERTY) !=
	    i_cal_component_count_properties (icomp2, I_CAL_ANY_PROPERTY))
		return FALSE;

	processed_props = g_hash_table_new (g_direct_hash, g_direct_equal);

	for (prop1 = i_cal_component_get_first_property (icomp1, I_CAL_ANY_PROPERTY);
	     prop1 && equal;
	     g_object_unref (prop1), prop1 = i_cal_component_get_next_property (icomp1, I_CAL_ANY_PROPERTY)) {
		ICalPropertyKind kind = i_cal_property_isa (prop1);
		ICalProperty *prop2;

		for (prop2 = i_cal_component_get_first_property (icomp2, kind);
		     prop2;
		     g_object_unref (prop2), prop2 = i_cal_component_get_next_property (icomp2, kind)) {
			gchar *str1, *str2;
			gboolean same;

			if (g_hash_table_contains (processed_props, i_cal_object_get_native (I_CAL_OBJECT (prop2))))
				continue;

			if (i_cal_property_count_parameters (prop1) != i_cal_property_count_parameters (prop2))
				continue;

			str1 = i_cal_property_as_ical_string (prop1);
			str2 = i_cal_property_as_ical_string (prop2);

			same = g_strcmp0 (str1, str2) == 0;

			g_free (str1);
			g_free (str2);

			if (same) {
				g_hash_table_insert (processed_props, i_cal_object_get_native (I_CAL_OBJECT (prop2)), NULL);
				break;
			}
		}

		if (!prop2)
			equal = FALSE;

		g_clear_object (&prop2);
	}

	g_clear_object (&prop1);

	g_hash_table_destroy (processed_props);

	return equal;
}

typedef struct _ChangeData {
	ECalComponent *old_component;
	ECalComponent *new_component;
} ChangeData;

static void
change_data_free (gpointer ptr)
{
	ChangeData *cd = ptr;

	if (cd) {
		g_clear_object (&cd->old_component);
		g_clear_object (&cd->new_component);
		g_free (cd);
	}
}

static void
ecb_ews_filter_out_unchanged_instances (const GSList *to_save_instances,
					const GSList *existing_instances,
					GSList **out_changed_instances, /* ChangeData * */
					GSList **out_removed_instances) /* ECalComponent * */
{
	GSList *link = NULL;
	GHashTable *existing_hash;
	GHashTableIter iter;
	gpointer value;

	g_return_if_fail (to_save_instances != NULL);
	g_return_if_fail (existing_instances != NULL);
	g_return_if_fail (out_changed_instances != NULL);
	g_return_if_fail (out_removed_instances != NULL);

	*out_changed_instances = NULL;
	*out_removed_instances = NULL;

	existing_hash = g_hash_table_new_full ((GHashFunc) e_cal_component_id_hash, (GEqualFunc) e_cal_component_id_equal,
		e_cal_component_id_free, NULL);

	for (link = (GSList *) existing_instances; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ECalComponentId *id;

		id = e_cal_component_get_id (comp);
		if (id)
			g_hash_table_insert (existing_hash, id, comp);
	}

	for (link = (GSList *) to_save_instances; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ECalComponentId *id;

		id = e_cal_component_get_id (comp);
		if (id) {
			ECalComponent *old_comp;

			old_comp = g_hash_table_lookup (existing_hash, id);

			if (!ecb_ews_components_equal (comp, old_comp)) {
				ChangeData *cd;

				cd = g_new0 (ChangeData, 1);
				cd->old_component = old_comp ? g_object_ref (old_comp) : NULL;
				cd->new_component = g_object_ref (comp);

				*out_changed_instances = g_slist_prepend (*out_changed_instances, cd);
			}

			g_hash_table_remove (existing_hash, id);
			e_cal_component_id_free (id);
		}
	}

	g_hash_table_iter_init (&iter, existing_hash);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		*out_removed_instances = g_slist_prepend (*out_removed_instances, g_object_ref (value));
	}

	g_hash_table_destroy (existing_hash);
}

static gboolean
ecb_ews_extract_attachments (ICalComponent *icomp,
			     GSList **out_attachments) /* EEwsAttachmentInfo * */
{
	ICalProperty *prop;
	GSList *props = NULL, *link;

	g_return_val_if_fail (icomp != NULL, FALSE);
	g_return_val_if_fail (out_attachments != NULL, FALSE);

	*out_attachments = NULL;

	for (prop = i_cal_component_get_first_property (icomp, I_CAL_ATTACH_PROPERTY);
	     prop;
	     prop = i_cal_component_get_next_property (icomp, I_CAL_ATTACH_PROPERTY)) {
		props = g_slist_prepend (props, prop);
	}

	for (link = props; link; link = g_slist_next (link)) {
		EEwsAttachmentInfo *info;
		ICalAttach *attach;
		ICalParameter *param;
		const gchar *stored_filename;
		gchar *attachid;

		prop = link->data;
		param = i_cal_property_get_first_parameter (prop, I_CAL_FILENAME_PARAMETER);
		stored_filename = param ? i_cal_parameter_get_filename (param) : NULL;

		attach = i_cal_property_get_attach (prop);
		if (i_cal_attach_get_is_url (attach)) {
			const gchar *uri;

			uri = i_cal_attach_get_url (attach);

			if (!uri || !*uri) {
				g_clear_object (&param);
				continue;
			}

			info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_URI);

			e_ews_attachment_info_set_uri (info, uri);
			if (stored_filename && *stored_filename) {
				e_ews_attachment_info_set_prefer_filename (info, stored_filename);
			} else {
				gchar *uri_filename;

				uri_filename = g_filename_from_uri (uri, NULL, NULL);
				if (uri_filename && *uri_filename) {
					gchar *basename;

					basename = g_path_get_basename (uri_filename);
					if (basename && *basename && basename[0] != '.' && basename[0] != G_DIR_SEPARATOR) {
						const gchar *uid;

						uid = i_cal_component_get_uid (icomp);

						if (uid && g_str_has_prefix (basename, uid) && basename[strlen (uid)] == '-') {
							e_ews_attachment_info_set_prefer_filename (info, basename + strlen (uid) + 1);
						}
					}

					g_free (basename);
				}

				g_free (uri_filename);
			}
		} else {
			gsize len = -1;
			guchar *decoded = NULL;
			const gchar *content;

			content = (const gchar *) i_cal_attach_get_data (attach);
			decoded = g_base64_decode (content, &len);

			info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_INLINED);
			e_ews_attachment_info_set_inlined_data (info, decoded, len);

			if (stored_filename && *stored_filename)
				e_ews_attachment_info_set_prefer_filename (info, stored_filename);

			g_free (decoded);
		}

		attachid = i_cal_property_get_parameter_as_string (prop, "X-EWS-ATTACHMENTID");
		e_ews_attachment_info_set_id (info, attachid);
		g_free (attachid);

		*out_attachments = g_slist_prepend (*out_attachments, info);

		g_clear_object (&param);
	}

	g_slist_free_full (props, g_object_unref);

	return *out_attachments != NULL;
}

static ICalTimezone *
ecb_ews_get_timezone_from_icomponent (ECalBackendEws *cbews,
				      ICalComponent *icomp)
{
	ETimezoneCache *timezone_cache;
	ICalProperty *prop = NULL;
	const gchar *tzid = NULL;

	timezone_cache = E_TIMEZONE_CACHE (cbews);

	prop = i_cal_component_get_first_property (icomp, I_CAL_DTSTART_PROPERTY);
	if (prop != NULL) {
		ICalParameter *param = NULL;

		param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
		if (param) {
			tzid = i_cal_parameter_get_tzid (param);
			g_object_unref (param);
		} else {
			ICalTime *dtstart;

			dtstart = i_cal_property_get_dtstart (prop);
			if (i_cal_time_is_utc (dtstart))
				tzid = "UTC";
			g_clear_object (&dtstart);
		}

		g_object_unref (prop);
	}

	if (tzid)
		return e_timezone_cache_get_timezone (timezone_cache, tzid);

	return NULL;
}

static gboolean
ecb_ews_remove_item_sync (ECalBackendEws *cbews,
			  ECalCache *cal_cache,
			  guint32 opflags,
			  GHashTable *removed_indexes,
			  const gchar *uid,
			  const gchar *rid,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalComponent *comp = NULL, *parent = NULL;
	EwsId item_id = { 0 };
	gint index = 0;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (rid && !*rid)
		rid = NULL;

	if (!e_cal_cache_get_component (cal_cache, uid, rid, &comp, cancellable, NULL) ||
	    (rid && !e_cal_cache_get_component (cal_cache, uid, NULL, &parent, cancellable, NULL))) {
		if (!parent && !comp) {
			if (!g_cancellable_set_error_if_cancelled (cancellable, error))
				g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));
			return FALSE;
		}
	}

	ecb_ews_extract_item_id (comp ? comp : parent, &item_id.id, &item_id.change_key);

	if (!item_id.id) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Cannot determine EWS ItemId")));
		success = FALSE;
	} else {
		if (parent) {
			index = e_cal_backend_ews_rid_to_index (
				ecb_ews_get_timezone_from_icomponent (cbews,
					e_cal_component_get_icalcomponent (parent)),
				rid,
				e_cal_component_get_icalcomponent (parent),
				error);
			if (index == 0)
				success = comp != NULL;
		}

		if (index && removed_indexes && g_hash_table_contains (removed_indexes, GINT_TO_POINTER (index))) {
			/* Do nothing, it's already deleted from the server */
		} else {
			if (removed_indexes && index)
				g_hash_table_insert (removed_indexes, GINT_TO_POINTER (index), NULL);

			success = success && e_ews_connection_delete_item_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, &item_id, index, EWS_HARD_DELETE,
				ecb_ews_can_send_invitations (cbews, opflags, comp) ? EWS_SEND_TO_ALL_AND_SAVE_COPY : EWS_SEND_TO_NONE,
				EWS_ALL_OCCURRENCES, cancellable, error);
		}
	}

	g_free (item_id.id);
	g_free (item_id.change_key);

	g_clear_object (&comp);
	g_clear_object (&parent);

	return success;
}

static void
ecb_ews_get_attach_differences (ECalComponent *oldcomp,
				ECalComponent *newcomp,
				GSList **out_removed_attachment_ids, /* gchar * */
				GSList **out_added_attachments) /* EEwsAttachmentInfo * */
{
	GSList *old_attachments = NULL, *new_attachments = NULL, *link;

	g_return_if_fail (out_removed_attachment_ids != NULL);
	g_return_if_fail (out_added_attachments != NULL);

	*out_removed_attachment_ids = NULL;
	*out_added_attachments = NULL;

	if (!ecb_ews_extract_attachments (e_cal_component_get_icalcomponent (oldcomp), &old_attachments))
		old_attachments = NULL;

	if (!ecb_ews_extract_attachments (e_cal_component_get_icalcomponent (newcomp), &new_attachments))
		new_attachments = NULL;

	for (link = old_attachments; link; link = g_slist_next (link)) {
		EEwsAttachmentInfo *old_nfo = link->data;
		GSList *nlink;

		if (!old_nfo)
			continue;

		for (nlink = new_attachments; nlink; nlink = g_slist_next (nlink)) {
			EEwsAttachmentInfo *new_nfo = nlink->data;
			gboolean same = FALSE;

			if (!new_nfo ||
			    e_ews_attachment_info_get_type (old_nfo) != e_ews_attachment_info_get_type (new_nfo))
				continue;

			if (e_ews_attachment_info_get_type (old_nfo) == E_EWS_ATTACHMENT_INFO_TYPE_INLINED) {
				const gchar *old_data, *new_data;
				gsize old_len = -1, new_len = -1;

				old_data = e_ews_attachment_info_get_inlined_data (old_nfo, &old_len);
				new_data = e_ews_attachment_info_get_inlined_data (new_nfo, &new_len);

				same = old_len == new_len && (old_len == 0 ||
					(old_len > 0 && old_data && new_data && memcmp (old_data, new_data, old_len) == 0));
			} else if (e_ews_attachment_info_get_type (old_nfo) == E_EWS_ATTACHMENT_INFO_TYPE_URI) {
				same = g_strcmp0 (e_ews_attachment_info_get_uri (old_nfo), e_ews_attachment_info_get_uri (new_nfo)) == 0;
			}

			if (same) {
				new_attachments = g_slist_remove (new_attachments, new_nfo);
				e_ews_attachment_info_free (new_nfo);
				break;
			}
		}

		if (!nlink) {
			/* Did not find in the new_attachments, thus it's removed */
			g_warn_if_fail (e_ews_attachment_info_get_id (old_nfo) != NULL);
			*out_removed_attachment_ids = g_slist_prepend (*out_removed_attachment_ids,
				g_strdup (e_ews_attachment_info_get_id (old_nfo)));
		}
	}

	*out_added_attachments = new_attachments;

	g_slist_free_full (old_attachments, (GDestroyNotify) e_ews_attachment_info_free);
}

struct TzidCbData {
	ICalComponent *comp;
	ECalBackendEws *cbews;
};

static void
tzid_cb (ICalParameter *param,
	 gpointer data)
{
	struct TzidCbData *cbd = data;
	const gchar *tzid;
	ICalTimezone *zone;
	ICalComponent *new_comp;

	tzid = i_cal_parameter_get_tzid (param);
	if (!tzid)
		return;

	zone = e_timezone_cache_get_timezone (E_TIMEZONE_CACHE (cbd->cbews), tzid);
	if (!zone)
		return;

	new_comp = i_cal_timezone_get_component (zone);
	if (!new_comp)
		return;

	i_cal_component_take_component (cbd->comp, i_cal_component_clone (new_comp));

	g_object_unref (new_comp);
}

static void
ecb_ews_pick_all_tzids_out (ECalBackendEws *cbews,
			    ICalComponent *icomp)
{

	/* pick all the tzids out of the component and resolve
	 * them using the vtimezones in the current calendar */
	struct TzidCbData cbd;

	cbd.cbews = cbews;
	cbd.comp = icomp;

	i_cal_component_foreach_tzid (icomp, tzid_cb, &cbd);
}

static gboolean
ecb_ews_get_change_type_is_instance (ECalBackendEws *cbews,
				     ICalComponent *vcalendar,
				     ICalComponent *subcomp,
				     GCancellable *cancellable,
				     EEwsItemChangeType *out_change_type,
				     gint *out_index)
{
	gboolean res = FALSE;
	gchar *rid;

	g_return_val_if_fail (out_change_type != NULL, FALSE);
	g_return_val_if_fail (out_index != NULL, FALSE);

	if (!e_cal_util_component_is_instance (subcomp))
		return FALSE;

	rid = e_cal_util_component_get_recurid_as_string (subcomp);

	if (rid && *rid) {
		ICalComponent *main_comp = NULL;

		if (vcalendar) {
			ICalComponentKind kind;
			ICalCompIter *iter;
			const gchar *uid;

			uid = i_cal_component_get_uid (subcomp);
			kind = i_cal_component_isa (subcomp);

			iter = i_cal_component_begin_component (vcalendar, kind);
			main_comp = i_cal_comp_iter_deref (iter);
			while (main_comp) {
				if (g_strcmp0 (uid, i_cal_component_get_uid (main_comp)) == 0 &&
				    !e_cal_util_component_is_instance (main_comp) &&
				    e_cal_util_component_has_rrules (main_comp)) {
					break;
				}

				g_object_unref (main_comp);
				main_comp = i_cal_comp_iter_next (iter);
			}

			g_clear_object (&iter);
		}

		if (!main_comp) {
			ECalCache *cal_cache;
			ECalComponent *existing = NULL;

			cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbews));

			if (cal_cache && e_cal_cache_get_component (cal_cache, i_cal_component_get_uid (subcomp), NULL, &existing, cancellable, NULL) && existing) {
				main_comp = e_cal_component_get_icalcomponent (existing);
				g_object_ref (main_comp);
				g_object_unref (existing);
			}

			g_clear_object (&cal_cache);
		}

		if (main_comp) {
			gchar *mid, *sid;

			mid = e_cal_util_component_dup_x_property (main_comp, "X-EVOLUTION-ITEMID");
			sid = e_cal_util_component_dup_x_property (subcomp, "X-EVOLUTION-ITEMID");

			/* Already detached instances do not need to do this */
			if (mid && g_strcmp0 (mid, sid) == 0) {
				gint index;

				index = e_cal_backend_ews_rid_to_index (
					ecb_ews_get_timezone_from_icomponent (cbews, main_comp),
					rid,
					main_comp,
					NULL);

				if (index > 0) {
					res = TRUE;
					*out_change_type = E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM;
					*out_index = index;
				}
			}

			g_clear_object (&main_comp);
			g_free (mid);
			g_free (sid);
		}
	}

	g_free (rid);

	return res;
}

static gboolean
ecb_ews_modify_item_sync (ECalBackendEws *cbews,
			  guint32 opflags,
			  GHashTable *removed_indexes,
			  ICalComponent *old_icomp,
			  ICalComponent *new_icomp,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalComponent *comp = NULL, *oldcomp = NULL;
	ICalComponent *icomp;
	gchar *itemid = NULL, *changekey = NULL;
	GSList *added_attachments = NULL, *removed_attachment_ids = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (I_CAL_IS_COMPONENT (new_icomp), FALSE);

	icomp = i_cal_component_clone (new_icomp);

	ecb_ews_pick_all_tzids_out (cbews, icomp);

	comp = e_cal_component_new_from_icalcomponent (icomp);
	if (!comp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	ecb_ews_extract_item_id (comp, &itemid, &changekey);
	if (!itemid) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Cannot determine EWS ItemId")));
		g_object_unref (comp);
		return FALSE;
	}

	if (old_icomp) {
		oldcomp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (old_icomp));
	} else {
		oldcomp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (new_icomp));
	}

	ecb_ews_pick_all_tzids_out (cbews, e_cal_component_get_icalcomponent (oldcomp));

	/* In case we have updated attachments we have to run update attachments
	 * before update items so attendees will receive mails with already updated attachments */

	ecb_ews_get_attach_differences (oldcomp, comp, &removed_attachment_ids, &added_attachments);

	/* preform sync delete attachemnt operation*/
	if (removed_attachment_ids) {
		g_free (changekey);
		changekey = NULL;

		success = e_ews_connection_delete_attachments_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
			removed_attachment_ids, &changekey, cancellable, error);

		g_slist_free_full (removed_attachment_ids, g_free);
	}

	/* in case we have a new attachments add them before update */
	if (added_attachments && success) {
		EwsId item_id;

		item_id.id = itemid;
		item_id.change_key = changekey;

		changekey = NULL;

		success = e_ews_connection_create_attachments_sync (
			cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
			&item_id, added_attachments,
			FALSE, &changekey, NULL, cancellable, error);

		g_free (item_id.change_key);
	}

	if (success && old_icomp &&
	    e_cal_util_component_has_property (new_icomp, I_CAL_RRULE_PROPERTY) &&
	    !e_cal_util_component_has_property (new_icomp, I_CAL_RECURRENCEID_PROPERTY)) {
		ICalProperty *prop, *old_prop;
		GSList *exceptions = NULL, *link;
		EwsId item_id;

		item_id.id = itemid;
		item_id.change_key = changekey;

		/* Excluded occurrences */
		for (prop = i_cal_component_get_first_property (new_icomp, I_CAL_EXDATE_PROPERTY);
		     prop;
		     g_object_unref (prop), prop = i_cal_component_get_next_property (new_icomp, I_CAL_EXDATE_PROPERTY)) {
			gchar *new_rid;

			new_rid = i_cal_property_get_value_as_string (prop);

			for (old_prop = i_cal_component_get_first_property (old_icomp, I_CAL_EXDATE_PROPERTY);
			     old_prop;
			     g_object_unref (old_prop), old_prop = i_cal_component_get_next_property (old_icomp, I_CAL_EXDATE_PROPERTY)) {
				gchar *old_rid = i_cal_property_get_value_as_string (old_prop);
				if (g_strcmp0 (new_rid, old_rid) == 0) {
					g_object_unref (old_prop);
					g_free (old_rid);
					break;
				}

				g_free (old_rid);
			}

			if (!old_prop)
				exceptions = g_slist_prepend (exceptions, g_object_ref (prop));

			g_free (new_rid);
		}

		exceptions = g_slist_reverse (exceptions);

		for (link = exceptions; link && success; link = g_slist_next (link)) {
			gchar *rid;
			guint index;

			prop = link->data;

			rid = i_cal_property_get_value_as_string (prop);
			index = e_cal_backend_ews_rid_to_index (
				ecb_ews_get_timezone_from_icomponent (cbews, new_icomp),
				rid,
				new_icomp,
				error);
			g_free (rid);

			if (index == 0) {
				success = FALSE;
			} else if (!removed_indexes || !g_hash_table_contains (removed_indexes, GINT_TO_POINTER (index))) {
				if (removed_indexes)
					g_hash_table_insert (removed_indexes, GINT_TO_POINTER (index), NULL);

				success = e_ews_connection_delete_item_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, &item_id, index,
					EWS_HARD_DELETE, EWS_SEND_TO_NONE, EWS_ALL_OCCURRENCES, cancellable, error);
			}
		}

		g_slist_free_full (exceptions, g_object_unref);
	}

	if (success) {
		EwsCalendarConvertData convert_data = { 0 };
		CamelEwsSettings *ews_settings;
		const gchar *send_meeting_invitations;
		const gchar *send_or_save;

		if (!ecb_ews_get_change_type_is_instance (cbews, NULL, e_cal_component_get_icalcomponent (comp),
			cancellable, &convert_data.change_type, &convert_data.index)) {
			convert_data.change_type = E_EWS_ITEMCHANGE_TYPE_ITEM;
			convert_data.index = -1;
		}

		ews_settings = ecb_ews_get_collection_settings (cbews);

		convert_data.connection = cbews->priv->cnc;
		convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
		convert_data.user_email = camel_ews_settings_dup_email (ews_settings);
		convert_data.comp = comp;
		convert_data.old_comp = oldcomp;
		convert_data.item_id = itemid;
		convert_data.change_key = changekey;
		convert_data.default_zone = e_ews_common_utils_get_configured_icaltimezone ();

		if (!(opflags & E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE) &&
		    e_cal_component_has_attendees (comp) &&
		    ecb_ews_can_send_invitations (cbews, opflags, comp)) {
			send_meeting_invitations = "SendToAllAndSaveCopy";
			send_or_save = "SendAndSaveCopy";
		} else {
			/*In case of appointment we have to set SendMeetingInvites to SendToNone */
			send_meeting_invitations = "SendToNone";
			send_or_save = "SaveOnly";
		}

		success = e_ews_connection_update_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
			"AlwaysOverwrite", send_or_save, send_meeting_invitations, cbews->priv->folder_id,
			e_cal_backend_ews_convert_component_to_updatexml, &convert_data,
			NULL, cancellable, error);

		g_free (convert_data.user_email);
	}

	if (success && i_cal_component_isa (new_icomp) == I_CAL_VTODO_COMPONENT &&
	    e_cal_util_component_has_property (new_icomp, I_CAL_RRULE_PROPERTY)) {
		ICalProperty *prop;

		prop = i_cal_component_get_first_property (new_icomp, I_CAL_STATUS_PROPERTY);
		if (prop && i_cal_property_get_status (prop) == I_CAL_STATUS_COMPLETED) {
			/* Setting a recurring task completed will mark the existing task
			   as completed and also add a new task, thus force refresh here,
			   thus the user sees an up-to-date view of the server content. */
			e_cal_meta_backend_schedule_refresh (E_CAL_META_BACKEND (cbews));
		}

		g_clear_object (&prop);
	}

	g_slist_free_full (added_attachments, (GDestroyNotify) e_ews_attachment_info_free);
	g_clear_object (&oldcomp);
	g_clear_object (&comp);
	g_free (changekey);
	g_free (itemid);

	return success;
}

static gboolean
ecb_ews_save_component_sync (ECalMetaBackend *meta_backend,
			     gboolean overwrite_existing,
			     EConflictResolution conflict_resolution,
			     const GSList *instances,
			     const gchar *extra,
			     guint32 opflags,
			     gchar **out_new_uid,
			     gchar **out_new_extra,
			     GCancellable *cancellable,
			     GError **error)
{
	ECalBackendEws *cbews;
	ECalCache *cal_cache;
	ECalComponent *master = NULL;
	EwsFolderId *fid;
	GSList *link;
	const gchar *uid;
	gboolean is_online_meeting;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (meta_backend), FALSE);

	cbews = E_CAL_BACKEND_EWS (meta_backend);

	for (link = (GSList *) instances; link && !master; link = g_slist_next (link)) {
		master = link->data;

		if (!master)
			continue;

		if (e_cal_component_is_instance (master))
			master = NULL;
	}

	if (!master) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (cal_cache != NULL, FALSE);

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	is_online_meeting = e_cal_util_component_has_x_property (e_cal_component_get_icalcomponent (master), "X-M365-ONLINE-MEETING");
	uid = e_cal_component_get_uid (master);
	fid = e_ews_folder_id_new (cbews->priv->folder_id, NULL, FALSE);

	if (overwrite_existing) {
		GSList *existing = NULL, *changed_instances = NULL, *removed_instances = NULL;

		success = uid && e_cal_cache_get_components_by_uid (cal_cache, uid, &existing, cancellable, error) && existing;

		if (success) {
			/* This is for offline changes, where the component in the cache
			   is already modified, while the original, the one on the server,
			   is different. Using the cached component in this case generates
			   empty UpdateItem request and nothing is saved. */
			for (link = existing; link; link = g_slist_next (link)) {
				ECalComponent *comp = link->data;

				comp = ecb_ews_restore_original_comp (comp);
				if (comp) {
					g_object_unref (link->data);
					link->data = comp;
				}
			}
		}

		if (success)
			ecb_ews_filter_out_unchanged_instances (instances, existing, &changed_instances, &removed_instances);

		if (success) {
			GHashTable *removed_indexes;

			removed_indexes = g_hash_table_new (g_direct_hash, g_direct_equal);

			for (link = changed_instances; link && success; link = g_slist_next (link)) {
				ChangeData *cd = link->data;

				if (!cd)
					continue;

				success = ecb_ews_modify_item_sync (cbews, opflags, removed_indexes,
					e_cal_component_get_icalcomponent (cd->old_component ? cd->old_component : master),
					e_cal_component_get_icalcomponent (cd->new_component),
					cancellable, error);
			}

			for (link = removed_instances; link && success; link = g_slist_next (link)) {
				ECalComponent *comp = link->data;
				ECalComponentId *id = NULL;

				if (!comp)
					continue;

				id = e_cal_component_get_id (comp);

				if (id) {
					success = ecb_ews_remove_item_sync (cbews, cal_cache, opflags, removed_indexes,
						e_cal_component_id_get_uid (id), e_cal_component_id_get_rid (id), cancellable, error);
					e_cal_component_id_free (id);
				}
			}

			g_hash_table_destroy (removed_indexes);
		}

		if (success)
			ecb_ews_extract_item_id (master, out_new_uid, NULL);

		g_slist_free_full (existing, g_object_unref);
		g_slist_free_full (changed_instances, change_data_free);
		g_slist_free_full (removed_instances, g_object_unref);
	} else if (e_cal_component_has_organizer (master) &&
		   !ecb_ews_organizer_is_user (cbews, master)) {
		success = FALSE;
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_PERMISSION_DENIED, _("Cannot create meetings organized by other users in an Exchange Web Services calendar.")));
	} else if (is_online_meeting && !instances->next) {
		/* Check whether the folder is the user calendar */
		if (cbews->priv->is_user_calendar == E_THREE_STATE_INCONSISTENT) {
			GSList folder_ids, *folders = NULL;
			EwsFolderId dfid;

			memset (&dfid, 0, sizeof (dfid));
			memset (&folder_ids, 0, sizeof (folder_ids));

			dfid.id = (gchar *) "calendar";
			dfid.is_distinguished_id = TRUE;

			folder_ids.data = &dfid;

			success = e_ews_connection_get_folder_sync (cbews->priv->cnc, G_PRIORITY_DEFAULT, "IdOnly", NULL, &folder_ids, &folders, cancellable, error);
			if (success) {
				gboolean is_user_calendar = FALSE;

				if (folders) {
					EEwsFolder *folder = folders->data;
					const EwsFolderId *tmp_fid = folder ? e_ews_folder_get_id (folder) : NULL;

					is_user_calendar = tmp_fid && g_strcmp0 (cbews->priv->folder_id, tmp_fid->id) == 0;
				}

				g_slist_free_full (folders, g_object_unref);

				cbews->priv->is_user_calendar = is_user_calendar ? E_THREE_STATE_ON : E_THREE_STATE_OFF;
			}
		}

		if (success) {
			/* Verify the folder is the user calendar, the online meeting cannot be created elsewhere */
			success = cbews->priv->is_user_calendar == E_THREE_STATE_ON;

			if (!success)
				g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_PERMISSION_DENIED, _("Online meeting can be created only in the main user Calendar.")));
		}

		success = success && ecb_ews_save_as_online_meeting_sync (e_cal_backend_get_registry (E_CAL_BACKEND (cbews)),
			cbews->priv->cnc, E_TIMEZONE_CACHE (cbews), master, out_new_uid, cancellable, error);
	} else {
		GHashTable *removed_indexes;
		EwsCalendarConvertData convert_data = { 0 };
		EEwsItem *item = NULL;
		EwsId *ews_id = NULL;
		const gchar *send_meeting_invitations;
		ICalComponent *icomp;
		ICalProperty *prop;
		GSList *items = NULL;

		icomp = i_cal_component_clone (e_cal_component_get_icalcomponent (master));

		e_ews_clean_icomponent (icomp);

		if (!e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010))
			ecb_ews_pick_all_tzids_out (cbews, icomp);

		/*
		 * In case we are creating a meeting with attendees and attachments.
		 * We have to preform 3 steps in order to allow attendees to receive attachments in their invite mails.
		 * 1. create meeting and do not send invites
		 * 2. create attachments
		 * 3. dummy update meeting and send invites to all
		 */
		if (!(opflags & E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE) &&
		    e_cal_component_has_attendees (master)) {
			if (!ecb_ews_can_send_invitations (cbews, opflags, master) ||
			    e_cal_component_has_attachments (master))
				send_meeting_invitations = "SendToNone";
			else
				send_meeting_invitations = "SendToAllAndSaveCopy";
		} else {
			/* In case of appointment we have to set SendMeetingInvites to SendToNone */
			send_meeting_invitations = "SendToNone";
		}

		convert_data.connection = cbews->priv->cnc;
		convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
		convert_data.icomp = icomp;
		convert_data.default_zone = e_ews_common_utils_get_configured_icaltimezone ();

		success = e_ews_connection_create_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, "SaveOnly", send_meeting_invitations,
			fid, e_cal_backend_ews_convert_calcomp_to_xml, &convert_data,
			&items, cancellable, error);

		if (success && items) {
			item = items->data;
			if (item) {
				g_object_ref (item);

				ews_id = e_ews_id_copy (e_ews_item_get_id (item));
			}
		}

		if (success && item && e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_EVENT) {
			EEwsAdditionalProps *add_props;
			GSList *subitems, *items_req = NULL;

			add_props = e_ews_additional_props_new ();
			add_props->field_uri = g_strdup ("calendar:UID");

			subitems = g_slist_append (NULL, ews_id->id);

			/* get calender uid from server*/
			success = e_ews_connection_get_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
				subitems, "IdOnly", add_props, FALSE, NULL, E_EWS_BODY_TYPE_TEXT,
				&items_req, NULL, NULL, cancellable, error) && items_req != NULL;

			e_ews_additional_props_free (add_props);

			if (success) {
				g_clear_object (&item);

				item = items_req->data;

				e_ews_id_free (ews_id);
				ews_id = NULL;

				if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
					g_propagate_error (error, g_error_copy (e_ews_item_get_error (item)));
					item = NULL;
					success = FALSE;
				} else {
					item = g_object_ref (item);
					ews_id = e_ews_id_copy (e_ews_item_get_id (item));
				}
			}

			g_slist_free_full (items_req, g_object_unref);
			g_slist_free (subitems);
		}

		/* attachments */
		if (success && e_cal_component_has_attachments (master) > 0) {
			GSList *info_attachments = NULL;

			g_warn_if_fail (ews_id != NULL);

			if (ews_id && ecb_ews_extract_attachments (icomp, &info_attachments)) {
				gchar *changekey = NULL;
				GSList *ids = NULL;

				success = e_ews_connection_create_attachments_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
					ews_id, info_attachments, FALSE, &changekey, &ids, cancellable, error);

				g_slist_free_full (info_attachments, (GDestroyNotify) e_ews_attachment_info_free);
				g_slist_free_full (ids, g_free);

				if (success && changekey) {
					g_free (ews_id->change_key);
					ews_id->change_key = changekey;
				} else {
					g_free (changekey);
				}
			}
		}

		removed_indexes = g_hash_table_new (g_direct_hash, g_direct_equal);

		if (success && e_cal_util_component_has_property (icomp, I_CAL_RRULE_PROPERTY)) {
			GSList *exceptions = NULL;

			/* Excluded occurrences */
			for (prop = i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY);
			     prop;
			     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_EXDATE_PROPERTY)) {
				exceptions = g_slist_prepend (exceptions, i_cal_property_get_value_as_string (prop));
			}

			for (link = exceptions; link && success; link = g_slist_next (link)) {
				success = ecb_ews_remove_item_sync (cbews, cal_cache, opflags, removed_indexes, uid, link->data, cancellable, error);
			}

			g_slist_free_full (exceptions, g_free);
		}

		if (success && e_cal_component_has_attendees (master) && e_cal_component_has_attachments (master)) {
			if (ews_id) {
				e_cal_util_component_set_x_property (icomp, "X-EVOLUTION-ITEMID", ews_id->id);
				e_cal_util_component_set_x_property (icomp, "X-EVOLUTION-CHANGEKEY", ews_id->change_key);
			}

			/* In case we have attendees and atachemnts we have to fake update items,
			 * this is the only way to pass attachments in meeting invite mail */
			success = ecb_ews_modify_item_sync (cbews, opflags, removed_indexes, NULL, icomp, cancellable, error);
		}

		g_object_unref (icomp);
		e_ews_id_free (ews_id);
		g_clear_object (&item);

		for (link = (GSList *) instances; link && success; link = g_slist_next (link)) {
			ECalComponent *comp = link->data;

			if (comp == master)
				continue;

			icomp = e_cal_component_get_icalcomponent (comp);

			success = ecb_ews_modify_item_sync (cbews, opflags, removed_indexes, NULL, icomp, cancellable, error);
		}

		if (success && items) {
			const EwsId *item_id;

			item = items->data;
			item_id = e_ews_item_get_id (item);
			*out_new_uid = g_strdup (item_id->id);
		}

		g_slist_free_full (items, g_object_unref);
		g_hash_table_destroy (removed_indexes);
	}

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);

	g_clear_object (&cal_cache);
	e_ews_folder_id_free (fid);

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);

	return success;
}

static gboolean
ecb_ews_remove_component_sync (ECalMetaBackend *meta_backend,
			       EConflictResolution conflict_resolution,
			       const gchar *uid,
			       const gchar *extra,
			       const gchar *object,
			       guint32 opflags,
			       GCancellable *cancellable,
			       GError **error)
{
	ECalBackendEws *cbews;
	ECalComponent *comp;
	EwsId item_id;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (meta_backend), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbews = E_CAL_BACKEND_EWS (meta_backend);

	comp = e_cal_component_new_from_string (object);
	if (!comp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	ecb_ews_extract_item_id (comp, &item_id.id, &item_id.change_key);

	success = e_ews_connection_delete_item_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, &item_id, 0, EWS_HARD_DELETE,
		ecb_ews_can_send_invitations (cbews, opflags, comp) ? EWS_SEND_TO_ALL_AND_SAVE_COPY : EWS_SEND_TO_NONE,
		EWS_ALL_OCCURRENCES, cancellable, error);

	g_free (item_id.id);
	g_free (item_id.change_key);

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);
	g_object_unref (comp);

	return success;
}

static void
ecb_ews_discard_alarm_sync (ECalBackendSync *cal_backend_sync,
			    EDataCal *cal,
			    GCancellable *cancellable,
			    const gchar *uid,
			    const gchar *rid,
			    const gchar *auid,
			    guint32 opflags,
			    GError **error)
{
	ECalBackendEws *cbews;
	ECalCache *cal_cache;
	ECalComponent *comp = NULL;
	EwsCalendarConvertData convert_data = { 0 };

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (cal_backend_sync));

	cbews = E_CAL_BACKEND_EWS (cal_backend_sync);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbews));
	g_return_if_fail (cal_cache != NULL);

	if (!e_cal_cache_get_component (cal_cache, uid, NULL, &comp, cancellable, NULL) || !comp) {
		g_object_unref (cal_cache);
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));
		return;
	}

	g_object_unref (cal_cache);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbews), cancellable, error)) {
		g_clear_object (&comp);
		return;
	}

	convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
	convert_data.change_type = E_EWS_ITEMCHANGE_TYPE_ITEM;
	convert_data.index = -1;

	if (rid && *rid) {
		ICalComponent *icomp;
		gint index;

		icomp = e_cal_component_get_icalcomponent (comp);
		index = e_cal_backend_ews_rid_to_index (
			ecb_ews_get_timezone_from_icomponent (cbews, icomp),
			rid,
			icomp,
			NULL);

		if (index > 0) {
			convert_data.change_type = E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM;
			convert_data.index = index;
		}
	}

	ecb_ews_extract_item_id (comp, &convert_data.item_id, &convert_data.change_key);

	if (e_ews_connection_update_items_sync (
		cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
		"AlwaysOverwrite", NULL,
		"SendToNone", NULL,
		e_cal_backend_ews_clear_reminder_is_set,
		&convert_data,
		NULL,
		cancellable,
		error)) {
		ICalComponent *icomp = e_cal_component_get_icalcomponent (comp);
		GSList *modified_objects;
		gchar *itemid_str;

		itemid_str = e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-ITEMID");
		modified_objects = g_slist_prepend (NULL,
			e_cal_meta_backend_info_new (i_cal_component_get_uid (icomp), NULL, NULL, itemid_str));
		g_free (itemid_str);

		/* Refresh the local cache, to have up-to-date ChangeKey */
		e_cal_meta_backend_process_changes_sync (E_CAL_META_BACKEND (cbews), NULL, modified_objects, NULL, cancellable, error);

		g_slist_free_full (modified_objects, e_cal_meta_backend_info_free);
	}

	g_object_unref (comp);
	g_free (convert_data.item_id);
	g_free (convert_data.change_key);

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);
}

static gboolean
ecb_ews_send_cancellation_email_sync (ECalBackendEws *cbews,
				      CamelAddress *from,
				      CamelInternetAddress *recipients,
				      const gchar *subject,
				      const gchar *body,
				      const gchar *calobj,
				      GCancellable *cancellable,
				      GError **error)
{
	CamelMimeMessage *message;
	CamelContentType *mime_type;
	CamelMultipart *multi;
	CamelMimePart *text_part, *vcal_part;
	gchar *ical_str;
	ICalComponent *vcal, *vevent, *vtz;
	ICalProperty *prop;
	ICalTimezone *icaltz;
	ICalTime *dt;
	gboolean success;

	vcal = i_cal_component_new (I_CAL_VCALENDAR_COMPONENT);
	i_cal_component_take_property (vcal, i_cal_property_new_version ("2.0"));
	i_cal_component_take_property (vcal, i_cal_property_new_prodid ("-//Evolution EWS backend//EN"));
	i_cal_component_take_property (vcal, i_cal_property_new_method (I_CAL_METHOD_CANCEL));

	vevent = i_cal_component_new_from_string (calobj);

	prop = i_cal_component_get_first_property (vevent, I_CAL_STATUS_PROPERTY);
	if (prop) {
		i_cal_component_remove_property (vevent, prop);
		g_object_unref (prop);
	}
	i_cal_component_take_property (vevent, i_cal_property_new_status (I_CAL_STATUS_CANCELLED));

	prop = i_cal_component_get_first_property (vevent, I_CAL_METHOD_PROPERTY);
	if (prop) {
		i_cal_component_remove_property (vevent, prop);
		g_object_unref (prop);
	}

	dt = e_cal_backend_ews_get_datetime_with_zone (E_TIMEZONE_CACHE (cbews), NULL, vevent, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);
	icaltz = dt ? i_cal_time_get_timezone (dt) : NULL;
	if (!icaltz) {
		icaltz = ecb_ews_get_timezone_from_icomponent (cbews, vevent);
	}

	if (icaltz) {
		vtz = i_cal_timezone_get_component (icaltz);
		if (vtz)
			i_cal_component_take_component (vcal, i_cal_component_clone (vtz));
		g_clear_object (&vtz);
	}

	i_cal_component_take_component (vcal, vevent);

	g_clear_object (&dt);

	text_part = camel_mime_part_new ();
	camel_mime_part_set_content (text_part, body, strlen (body), "text/plain");

	vcal_part = camel_mime_part_new ();
	mime_type = camel_data_wrapper_get_mime_type_field (CAMEL_DATA_WRAPPER (vcal_part));
	camel_content_type_set_param (mime_type, "charset", "utf-8");
	camel_content_type_set_param (mime_type, "method", "CANCEL");
	ical_str = i_cal_component_as_ical_string (vcal);
	camel_mime_part_set_content (vcal_part, ical_str, strlen (ical_str), "text/calendar; method=CANCEL");
	g_free (ical_str);

	multi = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multi), "multipart/alternative");
	camel_multipart_add_part (multi, text_part);
	camel_multipart_set_boundary (multi, NULL);
	camel_multipart_add_part (multi, vcal_part);
	g_object_unref (text_part);
	g_object_unref (vcal_part);

	message = camel_mime_message_new ();
	camel_mime_message_set_subject (message, subject);
	camel_mime_message_set_from (message, CAMEL_INTERNET_ADDRESS (from));
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, recipients);

	camel_medium_set_content ((CamelMedium *) message, (CamelDataWrapper *) multi);
	g_object_unref (multi);

	success = camel_ews_utils_create_mime_message (cbews->priv->cnc, "SendAndSaveCopy", NULL, message, NULL, from, NULL, NULL, NULL, cancellable, error);

	g_object_unref (message);
	g_object_unref (vcal);

	return success;
}

static void
ecb_ews_receive_objects_no_exchange_mail (ECalBackendEws *cbews,
					  ICalComponent *vcalendar,
					  ICalComponent *subcomp,
					  GSList **ids,
					  GCancellable *cancellable,
					  GError **error)
{
	EwsCalendarConvertData convert_data = { 0 };
	EwsFolderId *fid;

	convert_data.connection = cbews->priv->cnc;
	convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
	convert_data.icomp = subcomp;
	convert_data.vcalendar = vcalendar;
	convert_data.default_zone = e_ews_common_utils_get_configured_icaltimezone ();

	fid = e_ews_folder_id_new (cbews->priv->folder_id, NULL, FALSE);

	e_ews_connection_create_items_sync (
		cbews->priv->cnc,
		EWS_PRIORITY_MEDIUM,
		"SaveOnly",
		"SendToNone",
		fid,
		e_cal_backend_ews_convert_calcomp_to_xml,
		&convert_data,
		ids,
		cancellable,
		error);

	e_ews_folder_id_free (fid);
}

static void
ecb_ews_get_rsvp (ICalProperty *attendee,
		  gboolean *out_rsvp_requested)
{
	if (out_rsvp_requested)
		*out_rsvp_requested = FALSE;
	else
		return;

	if (attendee) {
		ICalParameter *rsvp;

		rsvp = i_cal_property_get_first_parameter (attendee, I_CAL_RSVP_PARAMETER);
		if (rsvp) {
			*out_rsvp_requested = i_cal_parameter_get_rsvp (rsvp) == I_CAL_RSVP_TRUE;
			g_object_unref (rsvp);
		}
	}
}

static gchar *
ecb_ews_get_current_user_meeting_reponse (ECalBackendEws *cbews,
					  ICalComponent *icomp,
					  const gchar *current_user_mail,
					  GHashTable *aliases,
					  gboolean *out_rsvp_requested)
{
	ICalProperty *attendee;
	const gchar *attendee_str = NULL, *attendee_mail = NULL;
	gint attendees_count = 0;
	gchar *response = NULL;
	gboolean found = FALSE;

	if (out_rsvp_requested)
		*out_rsvp_requested = FALSE;

	attendee = i_cal_component_get_first_property (icomp, I_CAL_ORGANIZER_PROPERTY);
	if (attendee) {
		attendee_str = i_cal_property_get_organizer (attendee);

		if (attendee_str) {
			attendee_mail = e_cal_util_strip_mailto (attendee_str);

			if (attendee_mail && ((current_user_mail && g_ascii_strcasecmp (attendee_mail, current_user_mail) == 0) ||
			    (aliases && g_hash_table_contains (aliases, attendee_mail)))) {
				g_object_unref (attendee);
				/* Empty string means it's an organizer, NULL is when not found */
				return g_strdup ("");
			}
		}

		g_object_unref (attendee);
	}

	for (attendee = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);
	     attendee;
	     g_object_unref (attendee), attendee = i_cal_component_get_next_property (icomp, I_CAL_ATTENDEE_PROPERTY), attendees_count++) {
		attendee_str = i_cal_property_get_attendee (attendee);

		if (attendee_str != NULL) {
			attendee_mail = e_cal_util_strip_mailto (attendee_str);

			if (attendee_mail && ((current_user_mail && g_ascii_strcasecmp (attendee_mail, current_user_mail) == 0) ||
			    (aliases && g_hash_table_contains (aliases, attendee_mail)))) {
				g_free (response);
				response = i_cal_property_get_parameter_as_string (attendee, "PARTSTAT");
				ecb_ews_get_rsvp (attendee, out_rsvp_requested);
				found = response && g_ascii_strcasecmp (response, "NEEDS-ACTION") != 0;
				if (found) {
					/* stop early when have a response, in case
					   the user is in the attendees multiple times */
					g_clear_object (&attendee);
					break;
				} else {
					g_clear_pointer (&response, g_free);
					found = FALSE;
				}
			}
		}
	}

	/* this should not happen, but if the user's configured email does not match the one
	   used in the invitation, like when the invitation comes to a mailing list... */
	if (!found && attendees_count == 1) {
		attendee = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);
		g_return_val_if_fail (attendee != NULL, NULL);

		response = i_cal_property_get_parameter_as_string (attendee, "PARTSTAT");
		ecb_ews_get_rsvp (attendee, out_rsvp_requested);
		found = TRUE;
		g_clear_object (&attendee);
	} else if (!found) {
		attendee = e_ews_common_utils_find_attendee (E_CAL_BACKEND (cbews), icomp, aliases);
		if (attendee) {
			response = i_cal_property_get_parameter_as_string (attendee, "PARTSTAT");
			ecb_ews_get_rsvp (attendee, out_rsvp_requested);
			found = TRUE;
			g_clear_object (&attendee);
		}
	}

	if (found && !response) {
		response = g_strdup ("NEEDS-ACTION");
	}

	return response;
}

/* changekey can be NULL if you don't want it. itemid cannot. */
static void
ecb_ews_get_item_accept_id (ECalComponent *comp,
			    gchar **itemid,
			    gchar **changekey,
			    gchar **mail_id)
{
	ICalComponent *icomp;
	ICalProperty *prop;
	gchar *id_item = NULL;
	gchar *id_accept = NULL;
	gchar *ck = NULL;

	icomp = e_cal_component_get_icalcomponent (comp);

	for (prop = i_cal_component_get_first_property (icomp, I_CAL_X_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_X_PROPERTY)) {
		const gchar *x_name, *x_val;

		x_name = i_cal_property_get_x_name (prop);
		if (!x_name)
			continue;

		x_val = i_cal_property_get_x (prop);

		if (!id_item && g_ascii_strcasecmp (x_name, "X-EVOLUTION-ITEMID") == 0)
			id_item = g_strdup (x_val);
		else if (!id_accept && g_ascii_strcasecmp (x_name, "X-EVOLUTION-ACCEPT-ID") == 0)
			id_accept = g_strdup (x_val);
		else if (changekey && !ck && !g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY"))
			ck = g_strdup (x_val);
	}

	if (!id_item)
		id_item = g_strdup (id_accept);

	*itemid = id_item;
	*mail_id = id_accept;
	if (changekey)
		*changekey = ck;
}

static gboolean
ecb_ews_do_method_request_publish_reply (ECalBackendEws *cbews,
					 ICalComponent *vcalendar,
					 ECalComponent *comp,
					 ICalComponent *subcomp,
					 const gchar *response_type,
					 const gchar *user_email,
					 gboolean rsvp_requested,
					 GCancellable *cancellable,
					 GError **error)
{
	GError *local_error = NULL;
	gchar *item_id = NULL;
	gchar *change_key = NULL;
	gchar *mail_id = NULL;
	gint pass = 0;
	GSList *ids = NULL;

	if (!response_type &&
	    e_cal_util_component_has_organizer (subcomp) &&
	    e_cal_util_component_has_attendee (subcomp)) {
		g_set_error (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_UNKNOWN_USER, _("Cannot find user “%s” between attendees"), user_email ? user_email : "NULL");
		return FALSE;
	}

	if (response_type && *response_type)
		ecb_ews_get_item_accept_id (comp, &item_id, &change_key, &mail_id);
	else
		response_type = NULL;

	if (!item_id && e_cal_util_component_has_organizer (subcomp) &&
	    e_cal_util_component_has_attendee (subcomp) &&
	    !ecb_ews_organizer_is_user (cbews, comp)) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_PERMISSION_DENIED, _("Cannot create meetings organized by other users in an Exchange Web Services calendar.")));
		return FALSE;
	}

	while (pass < 2) {
		/*in case we do not have item id we will create item with mime content only*/
		if (!item_id || (response_type && g_ascii_strcasecmp (response_type, "NEEDS-ACTION") == 0)) {
			ecb_ews_receive_objects_no_exchange_mail (cbews, vcalendar, subcomp, &ids, cancellable, &local_error);
		} else {
			EwsCalendarConvertData convert_data = { 0 };
			EEwsItemChangeType change_type = E_EWS_ITEMCHANGE_TYPE_ITEM;
			gint index = -1;

			/* Need to detach an instance before modifying the response on it */
			if (ecb_ews_get_change_type_is_instance (cbews, vcalendar, subcomp, cancellable, &change_type, &index)) {
				EwsCalendarConvertData sub_convert_data = { 0 };
				CamelEwsSettings *ews_settings;

				ews_settings = ecb_ews_get_collection_settings (cbews);

				sub_convert_data.change_type = change_type;
				sub_convert_data.index = index;
				sub_convert_data.connection = cbews->priv->cnc;
				sub_convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
				sub_convert_data.user_email = camel_ews_settings_dup_email (ews_settings);
				sub_convert_data.comp = comp;
				sub_convert_data.old_comp = comp; /* no change, just detach the instance */
				sub_convert_data.item_id = item_id;
				sub_convert_data.change_key = change_key;
				sub_convert_data.default_zone = e_ews_common_utils_get_configured_icaltimezone ();

				e_ews_connection_update_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
					"AlwaysOverwrite", "SaveOnly", "SendToNone", cbews->priv->folder_id,
					e_cal_backend_ews_convert_component_to_updatexml, &sub_convert_data,
					&ids, cancellable, &local_error);

				g_free (sub_convert_data.user_email);

				if (!local_error && ids && !ids->next) {
					EEwsItem *item = ids->data;
					const EwsId *id = e_ews_item_get_id (item);

					if (id) {
						g_free (item_id);
						g_free (change_key);

						item_id = g_strdup (id->id);
						change_key = g_strdup (id->change_key);
					}
				}

				g_clear_error (&local_error);
				g_slist_free_full (ids, g_object_unref);
				ids = NULL;
			}

			convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
			convert_data.response_type = (gchar *) response_type;
			convert_data.icomp = subcomp;
			convert_data.item_id = item_id;
			convert_data.change_key = change_key;

			e_ews_connection_create_items_sync (
				cbews->priv->cnc,
				EWS_PRIORITY_MEDIUM,
				rsvp_requested ? "SendAndSaveCopy" : "SaveOnly",
				rsvp_requested ? NULL : "SendToNone",
				NULL,
				e_cal_backend_ews_prepare_accept_item_request,
				&convert_data,
				&ids,
				cancellable,
				&local_error);
		}

		if (pass == 0 && mail_id != NULL && item_id != NULL &&
		    g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ITEMNOTFOUND)) {
			/*
			 * maybe the associated accept calendar item changed
			 * on the server, thus retry with updated values
			 */
			GSList *my_ids = NULL;

			g_clear_error (&local_error);

			my_ids = g_slist_append (my_ids, mail_id);

			if (e_ews_connection_get_items_sync (
				cbews->priv->cnc,
				EWS_PRIORITY_MEDIUM,
				my_ids,
				"AllProperties",
				NULL,
				FALSE,
				NULL,
				E_EWS_BODY_TYPE_ANY,
				&ids,
				NULL,
				NULL,
				cancellable,
				&local_error) &&
			    ids != NULL &&
			    ids->data != NULL) {
				EEwsItem *item = ids->data;
				const EwsId *id = e_ews_item_get_id (item);

				if (id != NULL && g_strcmp0 (id->id, mail_id) == 0) {
					const EwsId *cal_item_accepted_id;

					cal_item_accepted_id = e_ews_item_get_calendar_item_accept_id (item);
					if (cal_item_accepted_id != NULL) {
						g_clear_error (&local_error);
						pass++;

						g_free (item_id);
						g_free (change_key);

						item_id = g_strdup (cal_item_accepted_id->id);
						change_key = g_strdup (cal_item_accepted_id->change_key);
					}
				}
			}

			g_slist_free (my_ids);

			if (pass == 0)
				break;
		} else {
			break;
		}

		g_slist_free_full (ids, g_object_unref);
		ids = NULL;
	}

	if (local_error == NULL) {
		ICalProperty *prop;
		ICalPropertyTransp transp = I_CAL_TRANSP_NONE;

		prop = i_cal_component_get_first_property (subcomp, I_CAL_TRANSP_PROPERTY);
		if (prop) {
			transp = i_cal_property_get_transp (prop);
			g_object_unref (prop);
		}

		if (transp == I_CAL_TRANSP_TRANSPARENT && g_strcmp0 (response_type, "ACCEPTED") == 0) {
			EwsCalendarConvertData convert_data = { 0 };
			GSList *l;

			/*
			 * user can accept meeting but mark it as free in its calendar
			 * the following code is updating the exchange meeting status to free
			 */
			for (l = ids; l != NULL; l = g_slist_next (l)) {
				EEwsItem *item = l->data;

				if (item != NULL) {
					const EwsId *id = e_ews_item_get_id (item);

					convert_data.item_id = id->id;
					convert_data.change_key = id->change_key;
					break;
				}
			}

			convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
			convert_data.vcalendar = vcalendar;

			e_ews_connection_update_items_sync (
				cbews->priv->cnc,
				EWS_PRIORITY_MEDIUM,
				"AlwaysOverwrite",
				NULL,
				"SendToNone",
				NULL,
				e_cal_backend_ews_prepare_set_free_busy_status,
				&convert_data,
				NULL,
				cancellable,
				&local_error);
		}
	}

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	g_free (item_id);
	g_free (change_key);
	g_free (mail_id);
	g_slist_free_full (ids, g_object_unref);

	return !local_error;
}

static void
ecb_ews_receive_objects_sync (ECalBackendSync *sync_backend,
			      EDataCal *cal,
			      GCancellable *cancellable,
			      const gchar *calobj,
			      guint32 opflags,
			      GError **error)
{
	ECalBackendEws *cbews;
	ECalBackend *cal_backend;
	CamelEwsSettings *ews_settings;
	ICalComponent *icomp, *subcomp, *decline_main = NULL;
	ICalComponentKind kind;
	GHashTable *aliases;
	gchar *user_email;
	gboolean success = TRUE, do_refresh = FALSE;
	gboolean decline_main_rsvp_requested = FALSE;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (sync_backend));

	cbews = E_CAL_BACKEND_EWS (sync_backend);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbews), cancellable, error))
		return;

	icomp = calobj ? i_cal_parser_parse_string (calobj) : NULL;

	if (!icomp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	/* make sure ical data we parse is actually a vCalendar component */
	if (i_cal_component_isa (icomp) != I_CAL_VCALENDAR_COMPONENT) {
		g_object_unref (icomp);
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	cal_backend = E_CAL_BACKEND (cbews);
	kind = e_cal_backend_get_kind (cal_backend);

	ews_settings = ecb_ews_get_collection_settings (cbews);
	user_email = camel_ews_settings_dup_email (ews_settings);
	aliases = e_ews_common_utils_dup_mail_addresses (e_cal_backend_get_registry (E_CAL_BACKEND (cbews)),
		e_backend_get_source (E_BACKEND (cbews)), &user_email);

	switch (i_cal_component_get_method (icomp)) {
	case I_CAL_METHOD_REQUEST:
	case I_CAL_METHOD_PUBLISH:
	case I_CAL_METHOD_REPLY:
		for (subcomp = i_cal_component_get_first_component (icomp, kind);
		     subcomp && success;
		     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icomp, kind)) {
			ECalComponent *comp;
			gchar *response_type;
			gboolean rsvp_requested = FALSE;

			/* getting a data for meeting request response */
			response_type = ecb_ews_get_current_user_meeting_reponse (cbews, subcomp, user_email, aliases, &rsvp_requested);
			rsvp_requested = rsvp_requested && !(opflags & E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE);

			/* When the main component is declined, then decline also all detached instances and do them
			   first, because the main component decline removes the whole series from the calendar. */
			if (!decline_main && response_type && g_ascii_strcasecmp (response_type, "DECLINED") == 0 &&
			    !e_cal_util_component_is_instance (subcomp)) {
				decline_main = g_object_ref (subcomp);
				decline_main_rsvp_requested = rsvp_requested;
				g_free (response_type);
				continue;
			}

			comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (subcomp));

			success = ecb_ews_do_method_request_publish_reply (cbews, icomp, comp, subcomp,
				decline_main ? "DECLINED" : response_type, user_email, rsvp_requested, cancellable, error);

			do_refresh = TRUE;

			g_object_unref (comp);
			g_free (response_type);
		}
		g_clear_object (&subcomp);

		if (decline_main && success) {
			ECalComponent *comp;

			comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (decline_main));

			success = ecb_ews_do_method_request_publish_reply (cbews, icomp, comp, decline_main,
				"DECLINED", user_email, decline_main_rsvp_requested, cancellable, error);

			do_refresh = TRUE;

			g_object_unref (comp);
		}

		g_clear_object (&decline_main);
		break;
	case I_CAL_METHOD_COUNTER:
		/*
		 * this is a new time proposal mail from one of the attendees
		 * if we decline the proposal, nothing have to be done
		 * if we accept it we will call to modify_object
		 */
		for (subcomp = i_cal_component_get_first_component (icomp, kind);
		     subcomp && success;
		     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icomp, kind)) {
			gchar *response_type;

			/* getting a data for meeting request response */
			response_type = ecb_ews_get_current_user_meeting_reponse (cbews, subcomp, user_email, aliases, NULL);

			if (g_strcmp0 (response_type, "ACCEPTED") == 0) {
				gchar **split_subject;
				ICalProperty *summary;

				/* we have to edit the meeting subject to remove exchange header */
				summary = i_cal_component_get_first_property (subcomp, I_CAL_SUMMARY_PROPERTY);
				if (summary) {
					split_subject = g_strsplit (i_cal_property_get_summary (summary), ":", -1);
					if (split_subject && split_subject[0] && split_subject[1])
						i_cal_property_set_summary (summary, split_subject[1]);
					g_strfreev (split_subject);
					g_object_unref (summary);
				}

				success = ecb_ews_modify_item_sync (cbews, opflags, NULL, NULL, subcomp, cancellable, error);

				do_refresh = TRUE;
			}

			g_free (response_type);
		}
		g_clear_object (&subcomp);
		break;
	case I_CAL_METHOD_CANCEL: {
		ECalObjModType mod_type = E_CAL_OBJ_MOD_ALL;
		GSList *ids = NULL;

		for (subcomp = i_cal_component_get_first_component (icomp, kind);
		     subcomp && success;
		     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icomp, kind)) {
			const gchar *uid;
			gchar *rid;

			uid = i_cal_component_get_uid (subcomp);
			rid = e_cal_util_component_get_recurid_as_string (subcomp);

			if (rid && *rid)
				mod_type = E_CAL_OBJ_MOD_THIS;

			ids = g_slist_prepend (ids, e_cal_component_id_new (uid, rid));

			g_free (rid);
		}

		g_clear_object (&subcomp);

		if (ids) {
			GSList *old_comps = NULL, *new_comps = NULL;
			GError *local_error = NULL;

			e_cal_backend_sync_remove_objects (sync_backend, cal, cancellable, ids, mod_type, opflags, &old_comps, &new_comps, &local_error);

			do_refresh = !local_error;

			e_util_free_nullable_object_slist (old_comps);
			e_util_free_nullable_object_slist (new_comps);
			g_clear_error (&local_error);
		}

		g_slist_free_full (ids, e_cal_component_id_free);
		} break;
	default:
		break;
	}

	g_clear_pointer (&aliases, g_hash_table_unref);
	g_object_unref (icomp);
	g_free (user_email);

	if (success && do_refresh)
		e_cal_meta_backend_schedule_refresh (E_CAL_META_BACKEND (cbews));

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);
}

static void
ecb_ews_send_objects_sync (ECalBackendSync *sync_backend,
			   EDataCal *cal,
			   GCancellable *cancellable,
			   const gchar *calobj,
			   guint32 opflags,
			   GSList **users,
			   gchar **modified_calobj,
			   GError **error)
{
	ECalBackendEws *cbews;
	ICalComponentKind kind;
	ICalComponent *icomp, *subcomp = NULL;
	gboolean success = TRUE;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (sync_backend));

	cbews = E_CAL_BACKEND_EWS (sync_backend);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbews), cancellable, error))
		return;

	icomp = calobj ? i_cal_parser_parse_string (calobj) : NULL;

	/* make sure data was parsed properly */
	if (!icomp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	/* make sure ical data we parse is actually a vcal component */
	if (i_cal_component_isa (icomp) != I_CAL_VCALENDAR_COMPONENT &&
	    i_cal_component_isa (icomp) != I_CAL_VEVENT_COMPONENT) {
		g_object_unref (icomp);
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	if (i_cal_component_get_method (icomp) != I_CAL_METHOD_CANCEL) {
		g_object_unref (icomp);
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_NOT_SUPPORTED));
		return;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbews));

	if (i_cal_component_isa (icomp) == I_CAL_VCALENDAR_COMPONENT) {
		kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbews));
		subcomp = i_cal_component_get_first_component (icomp, kind);
	} else if (i_cal_component_isa (icomp) == I_CAL_VEVENT_COMPONENT) {
		subcomp = g_object_ref (icomp);
	}

	while (subcomp && success) {
		const gchar *subject = NULL, *org_email = NULL;
		const gchar *org = NULL, *attendee = NULL;
		const gchar *comment;
		gchar *org_cn;
		ICalProperty *prop, *org_prop;
		CamelInternetAddress *org_addr = camel_internet_address_new ();
		CamelInternetAddress *attendees_addr = camel_internet_address_new ();

		comment = i_cal_component_get_comment (subcomp);
		if (comment && !*comment)
			comment = NULL;

		prop = i_cal_component_get_first_property (subcomp, I_CAL_SUMMARY_PROPERTY);
		if (prop) {
			subject = i_cal_property_get_summary (prop);
			g_object_unref (prop);
		}

		org_prop = i_cal_component_get_first_property (subcomp, I_CAL_ORGANIZER_PROPERTY);
		org = i_cal_property_get_organizer (org_prop);
		org_email = e_cal_util_strip_mailto (org);
		org_cn = i_cal_property_get_parameter_as_string (org_prop, "CN");

		camel_internet_address_add (org_addr, org_cn, org_email);

		/* iterate over every attendee property */
		for (prop = i_cal_component_get_first_property (subcomp, I_CAL_ATTENDEE_PROPERTY);
		     prop && success;
		     g_object_unref (prop), prop = i_cal_component_get_next_property (subcomp, I_CAL_ATTENDEE_PROPERTY)) {
			gchar *att_cn;

			attendee = e_cal_util_strip_mailto (i_cal_property_get_attendee (prop));
			if (!attendee || g_ascii_strcasecmp (org_email, attendee) == 0)
				continue;

			att_cn = i_cal_property_get_parameter_as_string (prop, "CN");

			camel_internet_address_add (attendees_addr, att_cn, attendee);

			g_free (att_cn);
		}

		if (camel_address_length (CAMEL_ADDRESS (attendees_addr)) > 0) {
			gchar *subcalobj;

			subcalobj = i_cal_component_as_ical_string (subcomp);

			success = ecb_ews_send_cancellation_email_sync (cbews, CAMEL_ADDRESS (org_addr), attendees_addr,
				subject, comment, subcalobj, cancellable, error);

			g_free (subcalobj);
		}

		g_free (org_cn);
		g_clear_object (&org_prop);
		g_object_unref (attendees_addr);
		g_object_unref (org_addr);
		g_object_unref (subcomp);
		subcomp = i_cal_component_get_next_component (icomp, kind);
	}

	g_clear_object (&subcomp);
	g_object_unref (icomp);

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);
}

static void
ecb_ews_get_free_busy_sync (ECalBackendSync *sync_backend,
			    EDataCal *cal,
			    GCancellable *cancellable,
			    const GSList *users,
			    time_t start,
			    time_t end,
			    GSList **freebusyobjs,
			    GError **error)
{
	ECalBackendEws *cbews;
	EEWSFreeBusyData fbdata = { 0 };
	GSList *freebusy = NULL;
	gboolean success;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (sync_backend));
	g_return_if_fail (freebusyobjs != NULL);

	cbews = E_CAL_BACKEND_EWS (sync_backend);

	*freebusyobjs = NULL;

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbews), cancellable, error))
		return;

	/* EWS can support only 100 identities, which is the maximum number of identities that the Web service method can request
	 see http://msdn.microsoft.com / en - us / library / aa564001 % 28v = EXCHG.140 % 29.aspx */
	if (g_slist_length ((GSList *) users) > 100) {
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED));
		return;
	}

	fbdata.period_start = start;
	fbdata.period_end = end;
	fbdata.user_mails = (GSList *) users;

	success = e_ews_connection_get_free_busy_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
		e_ews_cal_utils_prepare_free_busy_request, &fbdata,
		&freebusy, cancellable, error);

	if (success) {
		GSList *fblink, *ulink;

		for (fblink = freebusy, ulink = (GSList *) users;
		     fblink && ulink;
		     fblink = g_slist_next (fblink), ulink = g_slist_next (ulink)) {
			ICalComponent *icomp = fblink->data;
			gchar *mailto;

			/* add attendee property */
			mailto = g_strconcat ("mailto:", ulink->data, NULL);
			i_cal_component_take_property (icomp, i_cal_property_new_attendee (mailto));
			g_free (mailto);

			*freebusyobjs = g_slist_prepend (*freebusyobjs, i_cal_component_as_ical_string (icomp));
		}

		*freebusyobjs = g_slist_reverse (*freebusyobjs);
	}

	g_slist_free_full (freebusy, g_object_unref);

	ecb_ews_convert_error_to_edc_error (error);
	ecb_ews_maybe_disconnect_sync (cbews, error, cancellable);
}

static gchar *
ecb_ews_get_backend_property (ECalBackend *cal_backend,
			      const gchar *prop_name)
{
	ECalBackendEws *cbews;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	cbews = E_CAL_BACKEND_EWS (cal_backend);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (
			",",
			E_CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS,
			E_CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY,
			E_CAL_STATIC_CAPABILITY_REMOVE_ALARMS,
			E_CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			E_CAL_STATIC_CAPABILITY_NO_THISANDFUTURE,
			E_CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK,
			E_CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT,
			E_CAL_STATIC_CAPABILITY_SAVE_SCHEDULES,
			E_CAL_STATIC_CAPABILITY_CREATE_MESSAGES,
			E_CAL_STATIC_CAPABILITY_NO_ALARM_AFTER_START,
			E_CAL_STATIC_CAPABILITY_NO_MEMO_START_DATE,
			E_CAL_STATIC_CAPABILITY_ALL_DAY_EVENT_AS_TIME,
			E_CAL_STATIC_CAPABILITY_TASK_DATE_ONLY,
			E_CAL_STATIC_CAPABILITY_TASK_NO_ALARM,
			E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR,
			E_CAL_STATIC_CAPABILITY_TASK_HANDLE_RECUR,
			E_CAL_STATIC_CAPABILITY_RETRACT_SUPPORTED,
			E_CAL_STATIC_CAPABILITY_USER_IS_ORGANIZER_ONLY,
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cbews)),
			NULL);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		/* return email address of the person who opened the calendar */
		CamelEwsSettings *ews_settings;

		ews_settings = ecb_ews_get_collection_settings (cbews);

		return camel_ews_settings_dup_email (ews_settings);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* ews does not support email based alarms */
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_ews_parent_class)->impl_get_backend_property (cal_backend, prop_name);
}

static void
ecb_ews_get_timezone_sync (ECalBackendSync *sync_backend,
			   EDataCal *cal,
			   GCancellable *cancellable,
			   const gchar *tzid,
			   gchar **tzobject,
			   GError **error)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (sync_backend));
	g_return_if_fail (tzid != NULL);
	g_return_if_fail (tzobject != NULL);

	*tzobject = NULL;

	E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_ews_parent_class)->get_timezone_sync (sync_backend, cal, cancellable, tzid, tzobject, &local_error);

	if (!*tzobject) {
		/* The timezone can be sometimes the Windows zone, try to convert it to libical */
		const gchar *ical_location = e_cal_backend_ews_tz_util_get_ical_equivalent (tzid);

		if (ical_location)
			E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_ews_parent_class)->get_timezone_sync (sync_backend, cal, cancellable, ical_location, tzobject, NULL);
	}

	if (*tzobject)
		g_clear_error (&local_error);
	else if (local_error)
		g_propagate_error (error, local_error);
}

static gboolean
ecb_ews_get_destination_address (EBackend *backend,
				 gchar **host,
				 guint16 *port)
{
	CamelEwsSettings *ews_settings;
	GUri *uri;
	gchar *host_url;
	gboolean result = FALSE;

	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	/* Sanity checking */
	if (!e_cal_backend_get_registry (E_CAL_BACKEND (backend)) ||
	    !e_backend_get_source (backend))
		return FALSE;

	ews_settings = ecb_ews_get_collection_settings (E_CAL_BACKEND_EWS (backend));
	g_return_val_if_fail (ews_settings != NULL, FALSE);

	host_url = camel_ews_settings_dup_hosturl (ews_settings);
	g_return_val_if_fail (host_url != NULL, FALSE);

	uri = g_uri_parse (host_url, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
	if (uri) {
		*host = g_strdup (g_uri_get_host (uri));
		*port = g_uri_get_port (uri) > 0 ? g_uri_get_port (uri) : 0;

		result = *host && **host;
		if (!result) {
			g_free (*host);
			*host = NULL;
		}

		g_uri_unref (uri);
	}

	g_free (host_url);

	return result;
}

static gchar *
ecb_ews_dup_component_revision (ECalCache *cal_cache,
				ICalComponent *icomp,
				gpointer user_data)
{
	g_return_val_if_fail (icomp != NULL, NULL);

	return e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
}

static void
ecb_ews_constructed (GObject *object)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (object);
	ECalCache *cal_cache;
	CamelEwsSettings *ews_settings;
	gchar *user_email;
	gchar *cache_dirname;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->constructed (object);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (E_BACKEND (object), NULL);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbews));
	g_return_if_fail (cal_cache != NULL);

	cache_dirname = g_path_get_dirname (e_cache_get_filename (E_CACHE (cal_cache)));
	g_signal_connect (cal_cache, "dup-component-revision", G_CALLBACK (ecb_ews_dup_component_revision), NULL);

	g_clear_object (&cal_cache);

	cbews->priv->attachments_dir = g_build_filename (cache_dirname, "attachments", NULL);
	g_mkdir_with_parents (cbews->priv->attachments_dir, 0777);

	g_free (cache_dirname);

	/* ensure the user email is set */
	ews_settings = ecb_ews_get_collection_settings (cbews);
	user_email = camel_ews_settings_dup_email (ews_settings);

	if (!user_email || !*user_email) {
		GHashTable *addresses;

		g_clear_pointer (&user_email, g_free);

		addresses = e_ews_common_utils_dup_mail_addresses (e_cal_backend_get_registry (E_CAL_BACKEND (cbews)),
			e_backend_get_source (E_BACKEND (cbews)), &user_email);
		g_clear_pointer (&addresses, g_hash_table_unref);

		if (user_email && *user_email)
			camel_ews_settings_set_email (ews_settings, user_email);
	}

	g_free (user_email);
}

static void
ecb_ews_dispose (GObject *object)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (object);

	ecb_ews_unset_connection (cbews, FALSE);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->dispose (object);
}

static void
ecb_ews_finalize (GObject *object)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (object);

	g_free (cbews->priv->folder_id);
	g_free (cbews->priv->attachments_dir);
	g_free (cbews->priv->last_subscription_id);

	g_rec_mutex_clear (&cbews->priv->cnc_lock);

	e_cal_backend_ews_unref_windows_zones ();

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->finalize (object);
}

static void
e_cal_backend_ews_init (ECalBackendEws *cbews)
{
	cbews->priv = e_cal_backend_ews_get_instance_private (cbews);
	cbews->priv->is_user_calendar = E_THREE_STATE_INCONSISTENT;

	g_rec_mutex_init (&cbews->priv->cnc_lock);

	e_cal_backend_ews_populate_windows_zones ();
}

static void
e_cal_backend_ews_class_init (ECalBackendEwsClass *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *cal_backend_sync_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_ews_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_ews_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_ews_get_changes_sync;
	cal_meta_backend_class->load_component_sync = ecb_ews_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_ews_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_ews_remove_component_sync;

	cal_backend_sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);
	cal_backend_sync_class->discard_alarm_sync = ecb_ews_discard_alarm_sync;
	cal_backend_sync_class->receive_objects_sync = ecb_ews_receive_objects_sync;
	cal_backend_sync_class->send_objects_sync = ecb_ews_send_objects_sync;
	cal_backend_sync_class->get_free_busy_sync = ecb_ews_get_free_busy_sync;
	cal_backend_sync_class->get_timezone_sync = ecb_ews_get_timezone_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_ews_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ecb_ews_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ecb_ews_constructed;
	object_class->dispose = ecb_ews_dispose;
	object_class->finalize = ecb_ews_finalize;
}

GType e_cal_backend_ews_get_type_for_testing_sources (void);

G_MODULE_EXPORT GType
e_cal_backend_ews_get_type_for_testing_sources (void)
{
	return e_cal_backend_ews_get_type ();
}
