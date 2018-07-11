/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
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

#include <libical/icaltz-util.h>
#include <libical/icalcomponent.h>
#include <libical/icalproperty.h>
#include <libical/icalparameter.h>

#include <calendar/gui/calendar-config.h>
#include <calendar/gui/itip-utils.h>

#include "server/e-source-ews-folder.h"
#include "server/e-ews-calendar-utils.h"
#include "server/e-ews-connection-utils.h"
#include "server/e-ews-camel-common.h"

#include "e-cal-backend-ews.h"
#include "e-cal-backend-ews-utils.h"

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

/* Private part of the CalBackendEws structure */
struct _ECalBackendEwsPrivate {
	GRecMutex cnc_lock;
	EEwsConnection *cnc;
	gchar *folder_id;

	guint subscription_key;
	gboolean is_freebusy_calendar;

	gchar *attachments_dir;
};

#define X_EWS_ORIGINAL_COMP "X-EWS-ORIGINAL-COMP"

#define EWS_MAX_FETCH_COUNT 100

#define GET_ITEMS_SYNC_PROPERTIES \
	"item:Attachments" \
	" item:Categories" \
	" item:HasAttachments" \
	" item:MimeContent" \
	" calendar:UID" \
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

G_DEFINE_TYPE (ECalBackendEws, e_cal_backend_ews, E_TYPE_CAL_META_BACKEND)

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

static void
ecb_ews_convert_error_to_edc_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror || (*perror)->domain == E_DATA_CAL_ERROR)
		return;

	if ((*perror)->domain == EWS_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EDC_ERROR_EX (AuthenticationFailed, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_FOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = EDC_ERROR_EX (NoSuchCal, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_EVENTNOTFOUND:
		case EWS_CONNECTION_ERROR_ITEMNOTFOUND:
			error = EDC_ERROR_EX (ObjectNotFound, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_UNAVAILABLE:
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND, (*perror)->message);
			break;
		}

		if (!error)
			error = EDC_ERROR_EX (OtherError, (*perror)->message);
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

	if (in_perror && g_error_matches (*in_perror, E_DATA_CAL_ERROR, AuthenticationFailed)) {
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
ecb_ews_unset_connection (ECalBackendEws *cbews)
{
	g_return_if_fail (E_IS_CAL_BACKEND_EWS (cbews));

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	if (cbews->priv->cnc) {
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

static icaltimezone *
ecb_ews_get_timezone (ETimezoneCache *timezone_cache,
		      const gchar *msdn_tzid,
		      const gchar *tzid,
		      const gchar *evo_ews_tzid)
{
	icaltimezone *zone = NULL;
	const gchar *evo_ews_msdn_tzid;

	zone = e_timezone_cache_get_timezone (timezone_cache, tzid);
	if (zone == NULL)
		zone = icaltimezone_get_builtin_timezone (tzid);

	if (g_strcmp0 (tzid, evo_ews_tzid) == 0)
		return zone;

	if (evo_ews_tzid != NULL) {
		evo_ews_msdn_tzid = e_cal_backend_ews_tz_util_get_msdn_equivalent (evo_ews_tzid);

		if (g_strcmp0 (msdn_tzid, evo_ews_msdn_tzid) == 0) {
			zone = e_timezone_cache_get_timezone (timezone_cache, evo_ews_tzid);
			if (zone == NULL)
				zone = icaltimezone_get_builtin_timezone (evo_ews_tzid);
		}
	}

	return zone;
}

static icalparameter *
ecb_ews_responsetype_to_partstat (const gchar *responsetype)
{
	icalparameter *param = NULL;

	if (!responsetype)
		return icalparameter_new_partstat (ICAL_PARTSTAT_NONE);

	if (g_ascii_strcasecmp (responsetype, "Organizer") == 0)
		param = icalparameter_new_partstat (ICAL_PARTSTAT_ACCEPTED);
	else if (g_ascii_strcasecmp (responsetype, "Tentative") == 0)
		param = icalparameter_new_partstat (ICAL_PARTSTAT_TENTATIVE);
	else if (g_ascii_strcasecmp (responsetype, "Accept") == 0)
		param = icalparameter_new_partstat (ICAL_PARTSTAT_ACCEPTED);
	else if (g_ascii_strcasecmp (responsetype, "Decline") == 0)
		param = icalparameter_new_partstat (ICAL_PARTSTAT_DECLINED);
	else if (g_ascii_strcasecmp (responsetype, "NoResponseReceived") == 0)
		param = icalparameter_new_partstat (ICAL_PARTSTAT_NEEDSACTION);
	else if (g_ascii_strcasecmp (responsetype, "Unknown") == 0)
		param = icalparameter_new_partstat (ICAL_PARTSTAT_NONE);

	if (!param)
		param = icalparameter_new_partstat (ICAL_PARTSTAT_NONE);

	return param;
}

static ECalComponent *
ecb_ews_item_to_component_sync (ECalBackendEws *cbews,
				EEwsItem *item,
				GCancellable *cancellable,
				GError **error)
{
	ECalComponent *res_component = NULL;
	ETimezoneCache *timezone_cache;
	icalcomponent_kind kind;
	EEwsItemType item_type;
	icalcomponent *icalcomp, *vcomp;
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();
	CamelEwsSettings *ews_settings;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), NULL);
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	timezone_cache = E_TIMEZONE_CACHE (cbews);

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbews));
	ews_settings = ecb_ews_get_collection_settings (cbews);

	item_type = e_ews_item_get_item_type (item);
	if (item_type == E_EWS_ITEM_TYPE_TASK || item_type == E_EWS_ITEM_TYPE_MEMO) {
		icalproperty *icalprop;
		icaltimetype due_date, start_date, complete_date, created;
		icalproperty_status status  = ICAL_STATUS_NONE;
		icalproperty_class class = ICAL_CLASS_NONE;
		const gchar *ews_task_status, *sensitivity;
		EwsImportance item_importance;
		gint priority = 5;
		gboolean has_this_date = FALSE;

		vcomp = icalcomponent_new (ICAL_VCALENDAR_COMPONENT);
		/*subject*/
		icalcomp = icalcomponent_new (item_type == E_EWS_ITEM_TYPE_TASK ? ICAL_VTODO_COMPONENT : ICAL_VJOURNAL_COMPONENT);
		icalprop = icalproperty_new_summary (e_ews_item_get_subject (item));
		icalcomponent_add_property (icalcomp, icalprop);

		/*date time created*/
		created = icaltime_from_timet_with_zone (e_ews_item_get_date_created (item), 0, utc_zone);
		icalprop = icalproperty_new_created (created);
		icalcomponent_add_property (icalcomp, icalprop);

		/*sensitivity*/
		sensitivity = e_ews_item_get_sensitivity (item);
		if (g_strcmp0 (sensitivity, "Normal") == 0)
			class = ICAL_CLASS_PUBLIC;
		else if (g_strcmp0 (sensitivity, "Private") == 0)
			class = ICAL_CLASS_PRIVATE;
		else if ((g_strcmp0 (sensitivity, "Confidential") == 0) ||
			 (g_strcmp0 (sensitivity, "Personal") == 0))
			class = ICAL_CLASS_CONFIDENTIAL;
		icalprop = icalproperty_new_class (class);
		icalcomponent_add_property (icalcomp, icalprop);

		/*description*/
		icalprop = icalproperty_new_description (e_ews_item_get_body (item));
		icalcomponent_add_property (icalcomp, icalprop);

		/*task assaingments*/
		if (e_ews_item_get_delegator (item) != NULL) {
			const gchar *task_owner = e_ews_item_get_delegator (item);
			GSList *mailboxes = NULL, *l;
			gboolean includes_last_item;
			gchar *mailtoname, *user_email;
			icalparameter *param;

			/*The task owner according to Exchange is current user, even that the task was assigned by
			 *someone else. I'm making the current user attendee and task delegator will be a task organizer */

			user_email = camel_ews_settings_dup_email (ews_settings);
			mailtoname = g_strdup_printf ("mailto:%s", user_email);
			icalprop = icalproperty_new_attendee (mailtoname);
			g_free (mailtoname);
			g_free (user_email);

			param = icalparameter_new_cn (e_ews_item_get_owner (item));
			icalproperty_add_parameter (icalprop, param);
			icalcomponent_add_property (icalcomp, icalprop);

			/* get delegator mail box*/
			e_ews_connection_resolve_names_sync (
				cbews->priv->cnc, EWS_PRIORITY_MEDIUM, task_owner,
				EWS_SEARCH_AD, NULL, FALSE, &mailboxes, NULL,
				&includes_last_item, cancellable, error);

			for (l = mailboxes; l != NULL; l = g_slist_next (l)) {
				EwsMailbox *mb = l->data;

				mailtoname = g_strdup_printf ("mailto:%s", mb->email);
				icalprop = icalproperty_new_organizer (mailtoname);
				param = icalparameter_new_cn (mb->name);
				icalproperty_add_parameter (icalprop, param);
				icalcomponent_add_property (icalcomp, icalprop);

				g_free (mailtoname);
				e_ews_mailbox_free (mb);
			}
			g_slist_free (mailboxes);
		}

		if (item_type == E_EWS_ITEM_TYPE_TASK) {
			icaltimezone *user_timezone = calendar_config_get_icaltimezone ();
			const gchar *percent_complete;

			/*start date*/
			has_this_date = FALSE;
			e_ews_item_task_has_start_date (item, &has_this_date);
			if (has_this_date) {
				start_date = icaltime_from_timet_with_zone (e_ews_item_get_start_date (item), 0, user_timezone);
				start_date.is_date = 1;
				icalprop = icalproperty_new_dtstart (start_date);
				icalcomponent_add_property (icalcomp, icalprop);
			}

			/*status*/
			ews_task_status = e_ews_item_get_status (item);
			if (g_strcmp0 (ews_task_status, "NotStarted") != 0) {
				if (g_strcmp0 (ews_task_status, "Completed") == 0)
					status = ICAL_STATUS_COMPLETED;
				else if (g_strcmp0 (ews_task_status, "InProgress") == 0)
					status = ICAL_STATUS_INPROCESS;
				else if (g_strcmp0 (ews_task_status, "WaitingOnOthers") == 0)
					status = ICAL_STATUS_NEEDSACTION;
				else if (g_strcmp0 (ews_task_status, "Deferred") == 0)
					status = ICAL_STATUS_CANCELLED;
				icalprop = icalproperty_new_status (status);
				icalcomponent_add_property (icalcomp, icalprop);
			}

			/*precent complete*/
			percent_complete = e_ews_item_get_percent_complete (item);
			icalprop  = icalproperty_new_percentcomplete (atoi (percent_complete ? percent_complete : "0"));
			icalcomponent_add_property (icalcomp, icalprop);

			/*due date*/
			e_ews_item_task_has_due_date (item, &has_this_date);
			if (has_this_date) {
				due_date = icaltime_from_timet_with_zone (e_ews_item_get_due_date (item), 0, user_timezone);
				due_date.is_date = 1;
				icalprop = icalproperty_new_due (due_date);
				icalcomponent_add_property (icalcomp, icalprop);
			}

			/*complete date*/
			has_this_date = FALSE;
			e_ews_item_task_has_complete_date (item, &has_this_date);
			if (has_this_date) {
				complete_date = icaltime_from_timet_with_zone (e_ews_item_get_complete_date (item), 0, user_timezone);
				icalprop = icalproperty_new_completed (complete_date);
				icalcomponent_add_property (icalcomp, icalprop);
			}

			/*priority*/
			item_importance = e_ews_item_get_importance (item);
			if (item_importance == EWS_ITEM_HIGH)
				priority = 3;
			else if (item_importance == EWS_ITEM_LOW)
				priority = 7;
			icalprop = icalproperty_new_priority (priority);
			icalcomponent_add_property (icalcomp, icalprop);
		}

		icalcomponent_add_component (vcomp, icalcomp);
	} else {
		struct icaltimetype dt;
		const gchar *mime_content;
		const gchar *tzid;
		gboolean timezone_set = FALSE;

		mime_content = e_ews_item_get_mime_content (item);
		vcomp = mime_content && *mime_content ? icalparser_parse_string (mime_content) : NULL;

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
				vcomp = icalparser_parse_string (str);
				g_free (str);
			}
		}

		if (!vcomp) {
			if (mime_content)
				g_warning ("%s: Failed to parse mime content:---%s---", G_STRFUNC, mime_content);
			return NULL;
		}

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
			icaltimezone *start_zone, *end_zone;
			const gchar *start_tzid, *end_tzid;
			const gchar *ical_start_tzid, *ical_end_tzid;
			const gchar *evo_ews_start_tzid, *evo_ews_end_tzid;

			start_tzid = e_ews_item_get_start_tzid (item);
			end_tzid = e_ews_item_get_end_tzid (item);

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
				icalcomp = icalcomponent_get_first_component (vcomp, kind);

				dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, icalcomp, ICAL_DTSTART_PROPERTY, icalproperty_get_dtstart);
				dt = icaltime_convert_to_zone (dt, start_zone);
				icalcomponent_set_dtstart (icalcomp, dt);

				timezone_set = TRUE;
				e_timezone_cache_add_timezone (timezone_cache, start_zone);

				if (end_zone != NULL) {
					dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, icalcomp, ICAL_DTEND_PROPERTY, icalproperty_get_dtend);
					dt = icaltime_convert_to_zone (dt, end_zone);
					icalcomponent_set_dtend (icalcomp, dt);

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
			icaltimezone *zone;
			gchar *new_tzid = NULL;

			icalcomp = icalcomponent_get_first_component (vcomp, kind);

			if (!icaltimezone_get_builtin_timezone (tzid) &&
			    icalcomponent_get_uid (icalcomp)) {
				icalcomponent *vtimezone;

				/* Add the timezone */
				vtimezone = icalcomponent_get_first_component (vcomp, ICAL_VTIMEZONE_COMPONENT);
				if (vtimezone != NULL) {
					icalproperty *prop;

					new_tzid = g_strconcat ("/evolution/ews/tzid/", icalcomponent_get_uid (icalcomp), NULL);

					zone = icaltimezone_new ();
					vtimezone = icalcomponent_new_clone (vtimezone);
					prop = icalcomponent_get_first_property (vtimezone, ICAL_TZID_PROPERTY);
					if (prop) {
						icalproperty_set_tzid (prop, new_tzid);

						prop = icalcomponent_get_first_property (vtimezone, ICAL_LOCATION_PROPERTY);
						if (!prop) {
							/* Use the original tzid as the timezone Location, to not expose
							   evolution-ews TZID. */
							prop = icalproperty_new_location (tzid);
							icalcomponent_add_property (vtimezone, prop);
						}
					} else {
						g_free (new_tzid);
						new_tzid = NULL;
					}
					icaltimezone_set_component (zone, vtimezone);
					e_timezone_cache_add_timezone (timezone_cache, zone);
					icaltimezone_free (zone, TRUE);
				}
			}

			zone = e_timezone_cache_get_timezone (timezone_cache, new_tzid ? new_tzid : tzid);

			if (!zone && new_tzid)
				zone = e_timezone_cache_get_timezone (timezone_cache, tzid);

			if (zone == NULL)
				zone = icaltimezone_get_builtin_timezone (tzid);

			if (zone != NULL) {
				dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, icalcomp, ICAL_DTSTART_PROPERTY, icalproperty_get_dtstart);
				dt = icaltime_convert_to_zone (dt, zone);
				icalcomponent_set_dtstart (icalcomp, dt);

				dt = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, icalcomp, ICAL_DTEND_PROPERTY, icalproperty_get_dtend);
				dt = icaltime_convert_to_zone (dt, zone);
				icalcomponent_set_dtend (icalcomp, dt);
			}

			g_free (new_tzid);
		}
	}

	/* Vevent or Vtodo */
	icalcomp = icalcomponent_get_first_component (vcomp, kind);
	if (icalcomp) {
		icalproperty *icalprop, *freebusy;
		struct icaltimetype itt;
		const EwsId *item_id;
		const GSList *l = NULL;
		const gchar *uid = e_ews_item_get_uid (item);

		item_id = e_ews_item_get_id (item);

		if (e_ews_item_get_is_meeting (item)) {
			gboolean is_response_requested = e_ews_item_get_is_response_requested (item);
			gchar *user_email;

			user_email = camel_ews_settings_dup_email (ews_settings);

			/* Attendees */
			for (l = e_ews_item_get_attendees (item); l != NULL; l = g_slist_next (l)) {
				icalparameter *param, *cu_type;
				gchar *mailtoname;
				const gchar *email = NULL;
				EwsAttendee *attendee = (EwsAttendee *) l->data;

				if (!attendee->mailbox)
					continue;

				if (g_strcmp0 (attendee->mailbox->routing_type, "EX") == 0)
					email = e_ews_item_util_strip_ex_address (attendee->mailbox->email);

				mailtoname = g_strdup_printf ("mailto:%s", email ? email : attendee->mailbox->email);
				icalprop = icalproperty_new_attendee (mailtoname);
				g_free (mailtoname);

				param = icalparameter_new_cn (attendee->mailbox->name);
				icalproperty_add_parameter (icalprop, param);

				if (g_ascii_strcasecmp (attendee->attendeetype, "Required") == 0) {
					param = icalparameter_new_role (ICAL_ROLE_REQPARTICIPANT);
					cu_type = icalparameter_new_cutype (ICAL_CUTYPE_INDIVIDUAL);
				}
				else if (g_ascii_strcasecmp (attendee->attendeetype, "Resource") == 0) {
					param = icalparameter_new_role (ICAL_ROLE_NONPARTICIPANT);
					cu_type = icalparameter_new_cutype (ICAL_CUTYPE_RESOURCE);
				}
				else {
					param = icalparameter_new_role ( ICAL_ROLE_OPTPARTICIPANT);
					cu_type = icalparameter_new_cutype (ICAL_CUTYPE_INDIVIDUAL);
				}
				icalproperty_add_parameter (icalprop, cu_type);
				icalproperty_add_parameter (icalprop, param);

				if (is_response_requested) {
					param = icalparameter_new_rsvp (ICAL_RSVP_TRUE);
					icalproperty_add_parameter (icalprop, param);
				}

				if (user_email && (email || attendee->mailbox->email) && e_ews_item_get_my_response_type (item) &&
				    g_ascii_strcasecmp (email ? email : attendee->mailbox->email, user_email) == 0) {
					param = ecb_ews_responsetype_to_partstat (e_ews_item_get_my_response_type (item));
				} else {
					param = ecb_ews_responsetype_to_partstat (attendee->responsetype);
				}
				icalproperty_add_parameter (icalprop, param);

				icalcomponent_add_property (icalcomp, icalprop);
			}

			g_free (user_email);
		}

		/* Free/Busy */
		freebusy = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
		if (!freebusy && (e_ews_item_get_item_type (item) != E_EWS_ITEM_TYPE_TASK)) {
			/* Busy by default */
			freebusy = icalproperty_new_transp (ICAL_TRANSP_OPAQUE);
			icalcomponent_add_property (icalcomp, freebusy);
		}
		for (icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
		     icalprop != NULL;
		     icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY)) {
			if (g_strcmp0 (icalproperty_get_x_name (icalprop), "X-MICROSOFT-CDO-BUSYSTATUS") == 0) {
				if (g_strcmp0 (icalproperty_get_value_as_string (icalprop), "BUSY") == 0) {
					icalproperty_set_transp (freebusy, ICAL_TRANSP_OPAQUE);
				} else {
					icalproperty_set_transp (freebusy, ICAL_TRANSP_TRANSPARENT);
				}

				break;
			}
		}

		/* AllDayEvent */
		for (icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
		     icalprop != NULL;
		     icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY)) {
			if (g_strcmp0 (icalproperty_get_x_name (icalprop), "X-MICROSOFT-CDO-ALLDAYEVENT") == 0) {
				if (g_strcmp0 (icalproperty_get_value_as_string (icalprop), "TRUE") == 0) {
					struct icaltimetype dtend, dtstart;
					dtstart = icalcomponent_get_dtstart (icalcomp);
					dtstart.is_date = 1;
					icalcomponent_set_dtstart (icalcomp, dtstart);

					dtend = icalcomponent_get_dtend (icalcomp);
					dtend.is_date = 1;
					icalcomponent_set_dtend (icalcomp, dtend);
				}
				break;
			}
		}

		if (icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY)) {
			/* Exchange sets RRULE even on the children, which is broken */
			icalprop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
			if (icalprop) {
				icalcomponent_remove_property (icalcomp, icalprop);
				icalproperty_free (icalprop);
			}
		}

		/* The EXDATE sent by the server can be date-time format with timezone, while
		   the event start time can be date-only. This breaks the rules, thus correct
		   it and make also EXDATE date-only. */
		itt = icalcomponent_get_dtstart (icalcomp);
		if (icaltime_is_valid_time (itt) && itt.is_date) {
			for (icalprop = icalcomponent_get_first_property (icalcomp, ICAL_EXDATE_PROPERTY);
			     icalprop;
			     icalprop = icalcomponent_get_next_property (icalcomp, ICAL_EXDATE_PROPERTY)) {
				itt = icalproperty_get_exdate (icalprop);
				itt.is_date = 1;
				icalproperty_set_exdate (icalprop, itt);

				icalproperty_remove_parameter_by_kind (icalprop, ICAL_TZID_PARAMETER);
			}
		}

		/* Exchange sets an ORGANIZER on all events. RFC2445 says:
		 *
		 *   This property MUST NOT be specified in an iCalendar
		 *   object that specifies only a time zone definition or
		 *   that defines calendar entities that are not group
		 *   scheduled entities, but are entities only on a single
		 *   user's calendar.
		 */
		if (!icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY)) {
			if ((icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ORGANIZER_PROPERTY))) {
				icalcomponent_remove_property (icalcomp, icalprop);
				icalproperty_free (icalprop);
			}
		}

		icalcomponent_set_uid (icalcomp, uid ? uid : item_id->id);

		e_cal_util_set_x_property (icalcomp, "X-EVOLUTION-ITEMID", item_id->id);
		e_cal_util_set_x_property (icalcomp, "X-EVOLUTION-CHANGEKEY", item_id->change_key);

		res_component = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (icalcomp));

		/* Categories */
		e_cal_component_set_categories_list (res_component, (GSList *) e_ews_item_get_categories (item));

		/*
		 * There is no API to set/get alarm description on the server side.
		 * However, for some reason, the alarm description has been set to "REMINDER"
		 * automatically (and with no i18n). Instead of show it to the user, let's
		 * set the summary as the alarm description.
		 */
		if (e_cal_component_has_alarms (res_component)) {
			GList *alarm_uids, *l;

			alarm_uids = e_cal_component_get_alarm_uids (res_component);
			for (l = alarm_uids; l != NULL; l = l->next) {
				ECalComponentAlarm *alarm;
				ECalComponentText text;

				alarm = e_cal_component_get_alarm (res_component, l->data);
				e_cal_component_get_summary (res_component, &text);
				e_cal_component_alarm_set_description (alarm, &text);

				e_cal_component_alarm_free (alarm);
			}
			cal_obj_uid_list_free (alarm_uids);
		}
	}

	icalcomponent_free (vcomp);

	if (res_component) {
		const GSList *attachment_ids, *aid, *l;
		const gchar *uid = NULL;
		GSList *info_attachments = NULL, *uris = NULL;
		gboolean has_attachment = FALSE;

		e_ews_item_has_attachments (item, &has_attachment);
		if (!has_attachment)
			return res_component;

		e_cal_component_get_uid (res_component, &uid);

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
			icalcomponent *icalcomp;
			icalproperty *icalprop;
			icalparameter *icalparam;

			for (l = info_attachments; l; l = l->next) {
				EEwsAttachmentInfo *info = l->data;

				/* ignore non-uri attachments, because it's an exception */
				if (e_ews_attachment_info_get_type (info) == E_EWS_ATTACHMENT_INFO_TYPE_URI) {
					const gchar *uri = e_ews_attachment_info_get_uri (info);

					if (uri)
						uris = g_slist_append (uris, g_strdup (uri));
				}
			}

			e_cal_component_set_attachment_list (res_component, uris);

			icalcomp = e_cal_component_get_icalcomponent (res_component);
			icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
			for (aid = attachment_ids; aid && icalprop; aid = aid->next, icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
				icalparam = icalparameter_new_x (aid->data);
				icalparameter_set_xname (icalparam, "X-EWS-ATTACHMENTID");
				icalproperty_add_parameter (icalprop, icalparam);
			}

			g_slist_free_full (uris, g_free);
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

	comp_str = e_cal_component_get_as_string (comp);
	g_return_if_fail (comp_str != NULL);

	/* Include NUL-terminator */
	base64 = g_base64_encode ((const guchar *) comp_str, strlen (comp_str) + 1);

	e_cal_util_set_x_property (e_cal_component_get_icalcomponent (comp),
		X_EWS_ORIGINAL_COMP, base64);

	g_free (base64);
	g_free (comp_str);
}

static ECalComponent * /* free with g_object_unref(), if not NULL */
ecb_ews_restore_original_comp (ECalComponent *from_comp)
{
	ECalComponent *comp = NULL;
	const gchar *original_base64;
	guchar *decoded;
	gsize len = -1;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (from_comp), NULL);

	original_base64 = e_cal_util_get_x_property (e_cal_component_get_icalcomponent (from_comp), X_EWS_ORIGINAL_COMP);

	if (!original_base64 || !*original_base64)
		return NULL;

	decoded = g_base64_decode (original_base64, &len);
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
	GSList *items = NULL, *link;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (out_components != NULL, FALSE);

	success = e_ews_connection_get_items_sync (
		cbews->priv->cnc,
		EWS_PRIORITY_MEDIUM,
		item_ids,
		default_props,
		add_props,
		FALSE,
		NULL,
		E_EWS_BODY_TYPE_TEXT,
		&items,
		NULL, NULL,
		cancellable,
		error);

	if (!success)
		return FALSE;

	/* fetch modified occurrences */
	for (link = items; link; link = g_slist_next (link)) {
		EEwsItem *item = link->data;
		const GSList *modified_occurrences;

		if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		modified_occurrences = e_ews_item_get_modified_occurrences (item);
		if (modified_occurrences) {
			EEwsAdditionalProps *modified_add_props;

			modified_add_props = e_ews_additional_props_new ();
			if (e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010)) {
				EEwsExtendedFieldURI *ext_uri;

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

		add_props = e_ews_additional_props_new ();
		if (e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010)) {
			EEwsExtendedFieldURI *ext_uri;

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

		success = ecb_ews_get_items_sync (cbews, event_ids, "IdOnly", add_props, out_components, cancellable, error);

		e_ews_additional_props_free (add_props);
	}

	if (task_memo_ids && success)
		success = ecb_ews_get_items_sync (cbews, task_memo_ids, "AllProperties", NULL, out_components, cancellable, error);

	g_slist_free_full (event_ids, g_free);
	g_slist_free_full (task_memo_ids, g_free);

	return success;
}

static gboolean
ecb_ews_freebusy_ecomp_changed (ECalComponent *ecomp,
				icalcomponent *vevent)
{
	icalcomponent *icomp;
	gboolean changed = FALSE;

	g_return_val_if_fail (vevent != NULL, FALSE);

	if (!ecomp)
		return TRUE;

	icomp = e_cal_component_get_icalcomponent (ecomp);
	if (!icomp)
		return TRUE;

	if (!changed)
		changed = g_strcmp0 (icalcomponent_get_summary (icomp), icalcomponent_get_summary (vevent)) != 0;
	if (!changed)
		changed = g_strcmp0 (icalcomponent_get_location (icomp), icalcomponent_get_location (vevent)) != 0;
	if (!changed)
		changed = icaltime_compare (icalcomponent_get_dtstart (icomp), icalcomponent_get_dtstart (vevent)) != 0;
	if (!changed)
		changed = icaltime_compare (icalcomponent_get_dtend (icomp), icalcomponent_get_dtend (vevent)) != 0;

	return changed;
}

static GSList * /* the possibly modified 'in_items' */
ecb_ews_verify_changes (ECalCache *cal_cache,
			icalcomponent_kind kind,
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
		    (type == E_EWS_ITEM_TYPE_EVENT && kind == ICAL_VEVENT_COMPONENT) ||
		    (type == E_EWS_ITEM_TYPE_MEMO && kind == ICAL_VJOURNAL_COMPONENT) ||
		    (type == E_EWS_ITEM_TYPE_TASK && kind == ICAL_VTODO_COMPONENT) )) {
			ECalComponent *existing = NULL;

			if (e_cal_cache_get_component (cal_cache, id->id, NULL, &existing, cancellable, NULL) &&
			    existing && g_strcmp0 (e_cal_util_get_x_property (e_cal_component_get_icalcomponent (existing),
			    "X-EVOLUTION-CHANGEKEY"), id->change_key) == 0) {
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
			     icalcomponent_kind kind)
{
	GSList *nfos = NULL, *link;
	GHashTable *sorted_by_uids; /* gchar * ~> GSList { ECalComponent * } */
	GHashTableIter iter;
	gpointer key, value;

	sorted_by_uids = g_hash_table_new (g_str_hash, g_str_equal);

	for (link = (GSList *) components; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		icalcomponent *icomp;
		const gchar *uid;
		GSList *instances;

		if (!comp)
			continue;

		icomp = e_cal_component_get_icalcomponent (comp);
		uid = icalcomponent_get_uid (icomp);

		if (!uid)
			continue;

		ecb_ews_store_original_comp (comp);

		instances = g_hash_table_lookup (sorted_by_uids, uid);
		g_hash_table_insert (sorted_by_uids, (gpointer) uid, g_slist_prepend (instances, comp));
	}

	g_hash_table_iter_init (&iter, sorted_by_uids);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *uid = key;
		GSList *instances = value, *link;
		icalcomponent *icomp, *merged;
		ECalComponent *comp;
		ECalMetaBackendInfo *nfo;
		const gchar *revision, *itemid;

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
		itemid = e_cal_util_get_x_property (icomp, "X-EVOLUTION-ITEMID");
		revision = e_cal_util_get_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
		merged = e_cal_meta_backend_merge_instances (meta_backend, instances, FALSE);

		if (!merged) {
			g_warn_if_fail (merged != NULL);
			g_slist_free (instances);
			continue;
		}

		nfo = e_cal_meta_backend_info_new (uid, revision, NULL, itemid);
		nfo->object = icalcomponent_as_ical_string_r (merged);

		nfos = g_slist_prepend (nfos, nfo);

		icalcomponent_free (merged);
		g_slist_free (instances);
	}

	g_hash_table_destroy (sorted_by_uids);

	return nfos;
}

static void
ecb_ews_extract_item_id (ECalComponent *comp,
			 gchar **out_id,
			 gchar **out_change_key)
{
	icalcomponent *icalcomp;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_return_if_fail (icalcomp != NULL);

	if (out_id)
		*out_id = e_cal_util_dup_x_property (icalcomp, "X-EVOLUTION-ITEMID");
	if (out_change_key)
		*out_change_key = e_cal_util_dup_x_property (icalcomp, "X-EVOLUTION-CHANGEKEY");
}

static gboolean
ecb_ews_is_organizer (ECalBackendEws *cbews,
		      ECalComponent *comp)
{
	ECalComponentOrganizer organizer;
	gboolean is_organizer = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	if (!e_cal_component_has_organizer (comp))
		return FALSE;

	organizer.value = NULL;

	e_cal_component_get_organizer (comp, &organizer);
	if (organizer.value) {
		CamelEwsSettings *ews_settings;
		const gchar *email = organizer.value;
		gchar *user_email;

		ews_settings = ecb_ews_get_collection_settings (cbews);

		user_email = camel_ews_settings_dup_email (ews_settings);

		if (!g_ascii_strncasecmp (email, "mailto:", 7))
			email += 7;

		is_organizer = user_email && g_ascii_strcasecmp (email, user_email) == 0;

		g_free (user_email);
	}

	return is_organizer;
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

	*out_auth_result = e_ews_connection_try_credentials_sync (cbews->priv->cnc, credentials, cancellable, error);

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
		    e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010_SP1)) {
			GSList *folders = NULL;

			folders = g_slist_prepend (folders, cbews->priv->folder_id);

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

	ecb_ews_unset_connection (cbews);

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

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	if (cbews->priv->is_freebusy_calendar) {
		ESourceEwsFolder *ews_folder;
		EEWSFreeBusyData fbdata;
		GSList *free_busy = NULL, *link;
		gboolean success;
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
			icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();
			GSList *comps = NULL;
			GHashTable *known;
			GHashTableIter iter;
			gpointer key;

			known = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

			if (e_cal_cache_search_components (cal_cache, NULL, &comps, cancellable, NULL)) {
				for (link = comps; link; link = g_slist_next (link)) {
					ECalComponent *comp = link->data;
					icalcomponent *icomp;
					const gchar *uid;

					if (!comp)
						continue;

					icomp = e_cal_component_get_icalcomponent (comp);
					if (!icomp)
						continue;

					uid = icalcomponent_get_uid (icomp);

					if (uid && *uid)
						g_hash_table_insert (known, g_strdup (uid), g_object_ref (comp));
				}

				g_slist_free_full (comps, g_object_unref);
			}

			for (link = free_busy; link; link = g_slist_next (link)) {
				icalcomponent *fbcomp = link->data;
				icalproperty *fbprop;
				icalparameter *param;
				struct icalperiodtype fb;
				icalparameter_fbtype fbtype;

				if (!fbcomp || icalcomponent_isa (fbcomp) != ICAL_VFREEBUSY_COMPONENT)
					continue;

				for (fbprop = icalcomponent_get_first_property (fbcomp, ICAL_FREEBUSY_PROPERTY);
				     fbprop;
				     fbprop = icalcomponent_get_next_property (fbcomp, ICAL_FREEBUSY_PROPERTY)) {
					ECalComponent *ecomp;
					icalcomponent *vevent;
					const gchar *id, *summary, *location;

					param = icalproperty_get_first_parameter (fbprop, ICAL_FBTYPE_PARAMETER);
					if (!param)
						continue;

					fbtype = icalparameter_get_fbtype (param);

					if (fbtype != ICAL_FBTYPE_FREE &&
					    fbtype != ICAL_FBTYPE_BUSY &&
					    fbtype != ICAL_FBTYPE_BUSYUNAVAILABLE &&
					    fbtype != ICAL_FBTYPE_BUSYTENTATIVE)
						continue;

					fb = icalproperty_get_freebusy (fbprop);
					id = icalproperty_get_parameter_as_string (fbprop, "X-EWS-ID");
					summary = icalproperty_get_parameter_as_string (fbprop, "X-SUMMARY");
					location = icalproperty_get_parameter_as_string (fbprop, "X-LOCATION");

					vevent = icalcomponent_new_vevent ();

					if (id && *id) {
						icalcomponent_set_uid (vevent, id);
					} else {
						gchar *uid;

						uid = g_strdup_printf ("%s-%s-%d",
							icaltime_as_ical_string (fb.start),
							icaltime_as_ical_string (fb.end),
							(gint) fbtype);

						icalcomponent_set_uid (vevent, uid);

						g_free (uid);
					}

					fb.start.zone = utc_zone;
					fb.end.zone = utc_zone;

					icalcomponent_set_dtstart (vevent, fb.start);
					icalcomponent_set_dtend (vevent, fb.end);

					icalcomponent_add_property (vevent, icalproperty_new_created (icaltime_current_time_with_zone (utc_zone)));

					if (fbtype == ICAL_FBTYPE_FREE) {
						icalcomponent_set_summary (vevent, C_("FreeBusyType", "Free"));
						icalcomponent_add_property (vevent, icalproperty_new_transp (ICAL_TRANSP_TRANSPARENT));
					} else if (fbtype == ICAL_FBTYPE_BUSY) {
						icalcomponent_set_summary (vevent, C_("FreeBusyType", "Busy"));
					} else if (fbtype == ICAL_FBTYPE_BUSYUNAVAILABLE) {
						icalcomponent_set_summary (vevent, C_("FreeBusyType", "Out of Office"));
					} else if (fbtype == ICAL_FBTYPE_BUSYTENTATIVE) {
						icalcomponent_set_summary (vevent, C_("FreeBusyType", "Tentative"));
					}

					if (summary && *summary)
						icalcomponent_set_summary (vevent, summary);

					if (location && *location)
						icalcomponent_set_location (vevent, location);

					ecomp = g_hash_table_lookup (known, icalcomponent_get_uid (vevent));
					if (ecomp) {
						g_object_ref (ecomp);

						/* This dereferences the ecomp, thus the ref() call above to keep it alive */
						g_hash_table_remove (known, icalcomponent_get_uid (vevent));

						if (ecb_ews_freebusy_ecomp_changed (ecomp, vevent)) {
							ECalMetaBackendInfo *nfo;
							gchar *revision = e_util_generate_uid ();

							e_cal_util_set_x_property (vevent, "X-EVOLUTION-CHANGEKEY", revision);

							nfo = e_cal_meta_backend_info_new (icalcomponent_get_uid (vevent), NULL, NULL, NULL);
							nfo->revision = revision;
							nfo->object = icalcomponent_as_ical_string_r (vevent);

							*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
						} else {
							icalcomponent_free (vevent);
						}

						g_clear_object (&ecomp);
					} else {
						ECalMetaBackendInfo *nfo;
						gchar *revision = e_util_generate_uid ();

						e_cal_util_set_x_property (vevent, "X-EVOLUTION-CHANGEKEY", revision);

						nfo = e_cal_meta_backend_info_new (icalcomponent_get_uid (vevent), NULL, NULL, NULL);
						nfo->revision = revision;
						nfo->object = icalcomponent_as_ical_string_r (vevent);

						*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
					}
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
		} else {
			g_propagate_error (error, local_error);
		}

		g_slist_free_full (free_busy, (GDestroyNotify) icalcomponent_free);
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
			icalcomponent_kind kind;

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
					if (id && id->uid && *id->uid && (!id->rid || !*id->rid)) {
						*out_removed_objects = g_slist_prepend (*out_removed_objects,
							e_cal_meta_backend_info_new (id->uid, NULL, NULL, NULL));
						break;
					}
				}

				g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
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

	return success;
}

static gboolean
ecb_ews_load_component_sync (ECalMetaBackend *meta_backend,
			     const gchar *uid,
			     const gchar *extra,
			     icalcomponent **out_component,
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

				*out_component = icalcomponent_new_vcalendar ();

				for (link = components; link; link = g_slist_next (link)) {
					ECalComponent *comp = link->data;

					if (!comp)
						continue;

					icalcomponent_add_component (*out_component,
						icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
				}
			} else {
				*out_component = icalcomponent_new_clone (e_cal_component_get_icalcomponent (components->data));
			}
		} else {
			success = FALSE;
		}
	}

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

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);

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
	icalcomponent *icomp1, *icomp2;
	icalproperty *prop1;
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

	if (g_strcmp0 (icalcomponent_get_uid (icomp1), icalcomponent_get_uid (icomp2)) != 0)
		return FALSE;

	if (icalcomponent_count_properties (icomp1, ICAL_ANY_PROPERTY) !=
	    icalcomponent_count_properties (icomp2, ICAL_ANY_PROPERTY))
		return FALSE;

	processed_props = g_hash_table_new (g_direct_hash, g_direct_equal);

	for (prop1 = icalcomponent_get_first_property (icomp1, ICAL_ANY_PROPERTY);
	     prop1 && equal;
	     prop1 = icalcomponent_get_next_property (icomp1, ICAL_ANY_PROPERTY)) {
		icalproperty_kind kind = icalproperty_isa (prop1);
		icalproperty *prop2;

		for (prop2 = icalcomponent_get_first_property (icomp2, kind);
		     prop2;
		     prop2 = icalcomponent_get_next_property (icomp2, kind)) {
			gchar *str1, *str2;
			gboolean same;

			if (g_hash_table_contains (processed_props, prop2))
				continue;

			if (icalproperty_count_parameters (prop1) != icalproperty_count_parameters (prop2))
				continue;

			str1 = icalproperty_as_ical_string_r (prop1);
			str2 = icalproperty_as_ical_string_r (prop2);

			same = g_strcmp0 (str1, str2) == 0;

			g_free (str1);
			g_free (str2);

			if (same) {
				g_hash_table_insert (processed_props, prop2, NULL);
				break;
			}
		}

		if (!prop2)
			equal = FALSE;
	}

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

	existing_hash = g_hash_table_new_full ((GHashFunc)e_cal_component_id_hash, (GEqualFunc) e_cal_component_id_equal,
		(GDestroyNotify) e_cal_component_free_id, NULL);

	for (link = (GSList *) existing_instances; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ECalComponentId *id;

		id = e_cal_component_get_id (comp);
		if (id)
			g_hash_table_insert (existing_hash, id, comp);
	}

	for (link = (GSList *) to_save_instances; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ECalComponentId *id = NULL;

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
			e_cal_component_free_id (id);
		}
	}

	g_hash_table_iter_init (&iter, existing_hash);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		*out_removed_instances = g_slist_prepend (*out_removed_instances, g_object_ref (value));
	}

	g_hash_table_destroy (existing_hash);
}

static gboolean
ecb_ews_extract_attachments (icalcomponent *icalcomp,
			     GSList **out_attachments) /* EEwsAttachmentInfo * */
{
	icalproperty *prop;
	GSList *props = NULL, *link;

	g_return_val_if_fail (icalcomp != NULL, FALSE);
	g_return_val_if_fail (out_attachments != NULL, FALSE);

	*out_attachments = NULL;

	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
		props = g_slist_prepend (props, prop);
	}

	for (link = props; link; link = g_slist_next (link)) {
		EEwsAttachmentInfo *info;
		icalattach *attach;
		icalparameter *param;
		const gchar *stored_filename;

		prop = link->data;
		param = icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER);
		stored_filename = param ? icalparameter_get_filename (param) : NULL;

		attach = icalproperty_get_attach (prop);
		if (icalattach_get_is_url (attach)) {
			const gchar *uri;

			uri = icalattach_get_url (attach);

			if (!uri || !*uri)
				continue;

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

						uid = icalcomponent_get_uid (icalcomp);

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

			content = (const gchar *) icalattach_get_data (attach);
			decoded = g_base64_decode (content, &len);

			info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_INLINED);
			e_ews_attachment_info_set_inlined_data (info, decoded, len);

			if (stored_filename && *stored_filename)
				e_ews_attachment_info_set_prefer_filename (info, stored_filename);

			g_free (decoded);
		}

		e_ews_attachment_info_set_id (info, icalproperty_get_parameter_as_string (prop, "X-EWS-ATTACHMENTID"));
		*out_attachments = g_slist_prepend (*out_attachments, info);
	}

	g_slist_free (props);

	return *out_attachments != NULL;
}

static icaltimezone *
ecb_ews_get_timezone_from_ical_component (ECalBackendEws *cbews,
					  icalcomponent *icalcomp)
{
	ETimezoneCache *timezone_cache;
	icalproperty *prop = NULL;
	const gchar *tzid = NULL;

	timezone_cache = E_TIMEZONE_CACHE (cbews);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
	if (prop != NULL) {
		icalparameter *param = NULL;

		param = icalproperty_get_first_parameter (prop, ICAL_TZID_PARAMETER);
		if (param) {
			tzid = icalparameter_get_tzid (param);
		} else {
			struct icaltimetype dtstart;

			dtstart = icalproperty_get_dtstart (prop);
			if (icaltime_is_utc (dtstart))
				tzid = "UTC";
		}
	}

	if (tzid)
		return e_timezone_cache_get_timezone (timezone_cache, tzid);

	return NULL;
}

static gboolean
ecb_ews_remove_item_sync (ECalBackendEws *cbews,
			  ECalCache *cal_cache,
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

	if (!e_cal_cache_get_component (cal_cache, uid, rid, &comp, cancellable, error) ||
	    (rid && !e_cal_cache_get_component (cal_cache, uid, NULL, &parent, cancellable, error))) {
		if (!parent && !comp) {
			g_propagate_error (error, EDC_ERROR (ObjectNotFound));
			return FALSE;
		}
	}

	ecb_ews_extract_item_id (comp ? comp : parent, &item_id.id, &item_id.change_key);

	if (!item_id.id) {
		g_propagate_error (error, EDC_ERROR_EX (OtherError, "Cannot determine EWS ItemId"));
		success = FALSE;
	} else {
		if (parent) {
			index = e_cal_backend_ews_rid_to_index (
				ecb_ews_get_timezone_from_ical_component (cbews,
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
				ecb_ews_is_organizer (cbews, comp) ? EWS_SEND_TO_ALL_AND_SAVE_COPY : EWS_SEND_TO_NONE,
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
	icalcomponent *comp;
	ECalBackendEws *cbews;
};

static void
tzid_cb (icalparameter *param,
	 gpointer data)
{
	struct TzidCbData *cbd = data;
	const gchar *tzid;
	icaltimezone *zone;
	icalcomponent *new_comp;

	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	zone = e_timezone_cache_get_timezone (E_TIMEZONE_CACHE (cbd->cbews), tzid);
	if (!zone)
		return;

	new_comp = icaltimezone_get_component (zone);
	if (!new_comp)
		return;

	icalcomponent_add_component (cbd->comp, icalcomponent_new_clone (new_comp));
}

static void
ecb_ews_pick_all_tzids_out (ECalBackendEws *cbews,
			    icalcomponent *icalcomp)
{

	/* pick all the tzids out of the component and resolve
	 * them using the vtimezones in the current calendar */
	struct TzidCbData cbd;

	cbd.cbews = cbews;
	cbd.comp = icalcomp;

	icalcomponent_foreach_tzid (icalcomp, tzid_cb, &cbd);
}

static gboolean
ecb_ews_modify_item_sync (ECalBackendEws *cbews,
			  GHashTable *removed_indexes,
			  icalcomponent *old_icalcomp,
			  icalcomponent *new_icalcomp,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalComponent *comp = NULL, *oldcomp = NULL;
	icalcomponent *icalcomp;
	gchar *itemid = NULL, *changekey = NULL;
	GSList *added_attachments = NULL, *removed_attachment_ids = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);
	g_return_val_if_fail (new_icalcomp != NULL, FALSE);

	icalcomp = icalcomponent_new_clone (new_icalcomp);

	ecb_ews_pick_all_tzids_out (cbews, icalcomp);

	comp = e_cal_component_new_from_icalcomponent (icalcomp);
	if (!comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return FALSE;
	}

	ecb_ews_extract_item_id (comp, &itemid, &changekey);
	if (!itemid) {
		g_propagate_error (error, EDC_ERROR_EX (OtherError, "Cannot determine EWS ItemId"));
		g_object_unref (comp);
		return FALSE;
	}

	if (old_icalcomp) {
		oldcomp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (old_icalcomp));
	} else {
		oldcomp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (new_icalcomp));
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

	if (success && old_icalcomp &&
	    icalcomponent_get_first_property (new_icalcomp, ICAL_RRULE_PROPERTY) &&
	    !icalcomponent_get_first_property (new_icalcomp, ICAL_RECURRENCEID_PROPERTY)) {
		icalproperty *prop, *old_prop;
		GSList *exceptions = NULL, *link;
		EwsId item_id;

		item_id.id = itemid;
		item_id.change_key = changekey;

		/* Excluded occurrences */
		for (prop = icalcomponent_get_first_property (new_icalcomp, ICAL_EXDATE_PROPERTY);
		     prop;
		     prop = icalcomponent_get_next_property (new_icalcomp, ICAL_EXDATE_PROPERTY)) {
			const gchar *new_rid;

			new_rid = icalproperty_get_value_as_string (prop);

			for (old_prop = icalcomponent_get_first_property (old_icalcomp, ICAL_EXDATE_PROPERTY);
			     old_prop;
			     old_prop = icalcomponent_get_next_property (old_icalcomp, ICAL_EXDATE_PROPERTY)) {
				if (g_strcmp0 (new_rid, icalproperty_get_value_as_string (old_prop)) == 0)
					break;
			}

			if (!old_prop)
				exceptions = g_slist_prepend (exceptions, prop);
		}

		exceptions = g_slist_reverse (exceptions);

		for (link = exceptions; link && success; link = g_slist_next (link)) {
			guint index;

			prop = link->data;

			index = e_cal_backend_ews_rid_to_index (
				ecb_ews_get_timezone_from_ical_component (cbews, new_icalcomp),
				icalproperty_get_value_as_string (prop),
				new_icalcomp,
				error);

			if (index == 0) {
				success = FALSE;
			} else if (!removed_indexes || !g_hash_table_contains (removed_indexes, GINT_TO_POINTER (index))) {
				if (removed_indexes)
					g_hash_table_insert (removed_indexes, GINT_TO_POINTER (index), NULL);

				success = e_ews_connection_delete_item_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, &item_id, index,
					EWS_HARD_DELETE, EWS_SEND_TO_NONE, EWS_ALL_OCCURRENCES, cancellable, error);
			}
		}

		g_slist_free (exceptions);
	}

	if (success) {
		EwsCalendarConvertData convert_data = { 0 };
		CamelEwsSettings *ews_settings;
		const gchar *send_meeting_invitations;
		const gchar *send_or_save;

		ews_settings = ecb_ews_get_collection_settings (cbews);

		convert_data.connection = cbews->priv->cnc;
		convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
		convert_data.user_email = camel_ews_settings_dup_email (ews_settings);
		convert_data.comp = comp;
		convert_data.old_comp = oldcomp;
		convert_data.item_id = itemid;
		convert_data.change_key = changekey;
		convert_data.default_zone = icaltimezone_get_utc_timezone ();

		if (e_cal_component_has_attendees (comp)) {
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
	const gchar *uid = NULL;
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
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return FALSE;
	}

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (cal_cache != NULL, FALSE);

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	e_cal_component_get_uid (master, &uid);
	fid = e_ews_folder_id_new (cbews->priv->folder_id, NULL, FALSE);

	if (overwrite_existing) {
		GSList *existing = NULL, *changed_instances = NULL, *removed_instances = NULL;

		success = uid && e_cal_cache_get_components_by_uid (cal_cache, uid, &existing, cancellable, error) && existing;

		if (success) {
			GSList *link;

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

				success = ecb_ews_modify_item_sync (cbews, removed_indexes,
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
					success = ecb_ews_remove_item_sync (cbews, cal_cache, removed_indexes, id->uid, id->rid, cancellable, error);
					e_cal_component_free_id (id);
				}
			}

			g_hash_table_destroy (removed_indexes);
		}

		if (success)
			ecb_ews_extract_item_id (master, out_new_uid, NULL);

		g_slist_free_full (existing, g_object_unref);
		g_slist_free_full (changed_instances, change_data_free);
		g_slist_free_full (removed_instances, g_object_unref);
	} else {
		GHashTable *removed_indexes;
		EwsCalendarConvertData convert_data = { 0 };
		EEwsItem *item = NULL;
		const EwsId *ews_id = NULL;
		const gchar *send_meeting_invitations;
		icalcomponent *icalcomp;
		icalproperty *prop;
		GSList *items = NULL;

		icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (master));

		e_ews_clean_icalcomponent (icalcomp);

		if (!e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010))
			ecb_ews_pick_all_tzids_out (cbews, icalcomp);

		/*
		 * In case we are creating a meeting with attendees and attachments.
		 * We have to preform 3 steps in order to allow attendees to receive attachments in their invite mails.
		 * 1. create meeting and do not send invites
		 * 2. create attachments
		 * 3. dummy update meeting and send invites to all
		 */
		if (e_cal_component_has_attendees (master)) {
			if (e_cal_component_has_attachments (master))
				send_meeting_invitations = "SendToNone";
			else
				send_meeting_invitations = "SendToAllAndSaveCopy";
		} else {
			/* In case of appointment we have to set SendMeetingInvites to SendToNone */
			send_meeting_invitations = "SendToNone";
		}

		convert_data.connection = cbews->priv->cnc;
		convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
		convert_data.icalcomp = icalcomp;
		convert_data.default_zone = icaltimezone_get_utc_timezone ();

		success = e_ews_connection_create_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, "SaveOnly", send_meeting_invitations,
			fid, e_cal_backend_ews_convert_calcomp_to_xml, &convert_data,
			&items, cancellable, error);

		if (success && items) {
			item = items->data;
			if (item) {
				g_object_ref (item);

				ews_id = e_ews_item_get_id (item);
			}
		}

		if (success && item && e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_EVENT) {
			EEwsAdditionalProps *add_props;
			GSList *items, *items_req = NULL;

			add_props = e_ews_additional_props_new ();
			add_props->field_uri = g_strdup ("calendar:UID");

			items = g_slist_append (NULL, ews_id->id);

			/* get calender uid from server*/
			success = e_ews_connection_get_items_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
				items, "IdOnly", add_props, FALSE, NULL, E_EWS_BODY_TYPE_TEXT,
				&items_req, NULL, NULL, cancellable, error) && items_req != NULL;

			e_ews_additional_props_free (add_props);

			if (success) {
				g_clear_object (&item);

				item = items_req->data;
				ews_id = NULL;

				if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
					g_propagate_error (error, g_error_copy (e_ews_item_get_error (item)));
					item = NULL;
					success = FALSE;
				} else {
					item = g_object_ref (item);
					ews_id = e_ews_item_get_id (item);
				}
			}

			g_slist_free_full (items_req, g_object_unref);
			g_slist_free (items);
		}

		/* attachments */
		if (success && e_cal_component_has_attachments (master) > 0) {
			GSList *info_attachments = NULL;

			g_warn_if_fail (ews_id != NULL);

			if (ews_id && ecb_ews_extract_attachments (icalcomp, &info_attachments)) {
				GSList *ids = NULL;

				success = e_ews_connection_create_attachments_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM,
					ews_id, info_attachments, FALSE, NULL, &ids, cancellable, error);

				g_slist_free_full (info_attachments, (GDestroyNotify) e_ews_attachment_info_free);
				g_slist_free_full (ids, g_free);
			}
		}

		removed_indexes = g_hash_table_new (g_direct_hash, g_direct_equal);

		if (success && icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY)) {
			GSList *exceptions = NULL;

			/* Excluded occurrences */
			for (prop = icalcomponent_get_first_property (icalcomp, ICAL_EXDATE_PROPERTY);
			     prop;
			     prop = icalcomponent_get_next_property (icalcomp, ICAL_EXDATE_PROPERTY)) {
				exceptions = g_slist_prepend (exceptions, g_strdup (icalproperty_get_value_as_string (prop)));
			}

			for (link = exceptions; link && success; link = g_slist_next (link)) {
				success = ecb_ews_remove_item_sync (cbews, cal_cache, removed_indexes, uid, link->data, cancellable, error);
			}

			g_slist_free_full (exceptions, g_free);
		}

		if (success && e_cal_component_has_attendees (master) && e_cal_component_has_attachments (master)) {
			if (ews_id) {
				e_cal_util_set_x_property (icalcomp, "X-EVOLUTION-ITEMID", ews_id->id);
				e_cal_util_set_x_property (icalcomp, "X-EVOLUTION-CHANGEKEY", ews_id->change_key);
			}

			/* In case we have attendees and atachemnts we have to fake update items,
			 * this is the only way to pass attachments in meeting invite mail */
			success = ecb_ews_modify_item_sync (cbews, removed_indexes, NULL, icalcomp, cancellable, error);
		}

		icalcomponent_free (icalcomp);
		g_clear_object (&item);

		for (link = (GSList *) instances; link && success; link = g_slist_next (link)) {
			ECalComponent *comp = link->data;

			if (comp == master)
				continue;

			icalcomp = e_cal_component_get_icalcomponent (comp);

			success = ecb_ews_modify_item_sync (cbews, removed_indexes, NULL, icalcomp, cancellable, error);
		}

		if (success && items) {
			EEwsItem *item = items->data;
			const EwsId *item_id;

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
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return FALSE;
	}

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	ecb_ews_extract_item_id (comp, &item_id.id, &item_id.change_key);

	success = e_ews_connection_delete_item_sync (cbews->priv->cnc, EWS_PRIORITY_MEDIUM, &item_id, 0, EWS_HARD_DELETE,
		ecb_ews_is_organizer (cbews, comp) ? EWS_SEND_TO_ALL_AND_SAVE_COPY : EWS_SEND_TO_NONE,
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
		g_propagate_error (error, EDC_ERROR (ObjectNotFound));
		return;
	}

	g_object_unref (cal_cache);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbews), cancellable, error)) {
		g_clear_object (&comp);
		return;
	}

	convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);

	if (e_cal_component_has_recurrences (comp)) {
		gint *index;

		convert_data.change_type = E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM;
		e_cal_component_get_sequence (comp, &index);

		if (index != NULL) {
			/*Microsoft is counting the occurrences starting from 1
			 where EcalComponent is starting from zerro */
			convert_data.index = *index + 1;
			e_cal_component_free_sequence (index);
		} else {
			convert_data.change_type = E_EWS_ITEMCHANGE_TYPE_ITEM;
			convert_data.index = -1;
		}
	} else {
		convert_data.change_type = E_EWS_ITEMCHANGE_TYPE_ITEM;
		convert_data.index = -1;
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
		icalcomponent *icomp = e_cal_component_get_icalcomponent (comp);
		GSList *modified_objects;

		modified_objects = g_slist_prepend (NULL,
			e_cal_meta_backend_info_new (icalcomponent_get_uid (icomp), NULL, NULL,
				e_cal_util_get_x_property (icomp, "X-EVOLUTION-ITEMID")));

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
				      CamelInternetAddress *recipient,
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
	icalcomponent *vcal, *vevent, *vtz;
	icalproperty *prop;
	icaltimezone *icaltz;
	struct icaltimetype dt;
	gboolean success;

	vcal = icalcomponent_new (ICAL_VCALENDAR_COMPONENT);
	icalcomponent_add_property (vcal, icalproperty_new_version ("2.0"));
	icalcomponent_add_property (vcal, icalproperty_new_prodid ("-//Evolution EWS backend//EN"));
	icalcomponent_add_property (vcal, icalproperty_new_method (ICAL_METHOD_CANCEL));
	vevent = icalcomponent_new_from_string (calobj);
	prop = icalcomponent_get_first_property (vevent, ICAL_STATUS_PROPERTY);
	if (prop != NULL) icalcomponent_remove_property (vevent, prop);
	icalcomponent_add_property (vevent, icalproperty_new_status (ICAL_STATUS_CANCELLED));
	prop = icalcomponent_get_first_property (vevent, ICAL_METHOD_PROPERTY);
	if (prop != NULL) icalcomponent_remove_property (vevent, prop);
	dt = e_cal_backend_ews_get_datetime_with_zone (E_TIMEZONE_CACHE (cbews), vevent, ICAL_DTSTART_PROPERTY, icalproperty_get_dtstart);
	icaltz = (icaltimezone *)
		(dt.zone ? dt.zone : ecb_ews_get_timezone_from_ical_component (cbews, vevent));
	vtz = icaltimezone_get_component (icaltz);
	icalcomponent_add_component (vcal, icalcomponent_new_clone (vtz));
	icalcomponent_add_component (vcal, vevent);
	text_part = camel_mime_part_new ();
	camel_mime_part_set_content (text_part, body, strlen (body), "text/plain");

	vcal_part = camel_mime_part_new ();
	mime_type = camel_data_wrapper_get_mime_type_field (CAMEL_DATA_WRAPPER (vcal_part));
	camel_content_type_set_param (mime_type, "charset", "utf-8");
	camel_content_type_set_param (mime_type, "method", "CANCEL");
	ical_str = icalcomponent_as_ical_string_r ((icalcomponent *) vcal);
	camel_mime_part_set_content (vcal_part, ical_str, strlen (ical_str), "text/calendar; method=CANCEL");
	free (ical_str);

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
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, recipient);

	camel_medium_set_content ((CamelMedium *) message, (CamelDataWrapper *) multi);
	g_object_unref (multi);

	success = camel_ews_utils_create_mime_message (cbews->priv->cnc, "SendOnly", NULL, message, NULL, from, NULL, NULL, NULL, cancellable, error);

	g_object_unref (message);
	icalcomponent_free (vcal);

	return success;
}

static void
ecb_ews_receive_objects_no_exchange_mail (ECalBackendEws *cbews,
					  icalcomponent *subcomp,
					  GSList **ids,
					  GCancellable *cancellable,
					  GError **error)
{
	EwsCalendarConvertData convert_data = { 0 };
	EwsFolderId *fid;

	convert_data.connection = cbews->priv->cnc;
	convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
	convert_data.icalcomp = subcomp;
	convert_data.default_zone = icaltimezone_get_utc_timezone ();

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

static icalproperty *
find_attendee (icalcomponent *ical_comp,
               const gchar *address)
{
	icalproperty *prop;

	if (address == NULL)
		return NULL;

	for (prop = icalcomponent_get_first_property (ical_comp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (ical_comp, ICAL_ATTENDEE_PROPERTY)) {
		gchar *attendee;
		gchar *text;

		attendee = icalproperty_get_value_as_string_r (prop);

		 if (!attendee)
			continue;

		text = g_strdup (itip_strip_mailto (attendee));
		text = g_strstrip (text);
		if (text && !g_ascii_strcasecmp (address, text)) {
			g_free (text);
			g_free (attendee);
			break;
		}
		g_free (text);
		g_free (attendee);
	}

	return prop;
}

static icalproperty *
find_attendee_if_sentby (icalcomponent *ical_comp,
                         const gchar *address)
{
	icalproperty *prop;

	if (address == NULL)
		return NULL;

	for (prop = icalcomponent_get_first_property (ical_comp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (ical_comp, ICAL_ATTENDEE_PROPERTY)) {
		icalparameter *param;
		const gchar *attendee_sentby;
		gchar *text;

		param = icalproperty_get_first_parameter (prop, ICAL_SENTBY_PARAMETER);
		if (!param)
			continue;

		attendee_sentby = icalparameter_get_sentby (param);

		if (!attendee_sentby)
			continue;

		text = g_strdup (itip_strip_mailto (attendee_sentby));
		text = g_strstrip (text);
		if (text && !g_ascii_strcasecmp (address, text)) {
			g_free (text);
			break;
		}
		g_free (text);
	}

	return prop;
}

static void
ecb_ews_get_rsvp (icalproperty *attendee,
		  gboolean *out_rsvp_requested)
{
	if (out_rsvp_requested)
		*out_rsvp_requested = FALSE;
	else
		return;

	if (attendee) {
		icalparameter *rsvp;

		rsvp = icalproperty_get_first_parameter (attendee, ICAL_RSVP_PARAMETER);
		if (rsvp) {
			*out_rsvp_requested = icalparameter_get_rsvp (rsvp) == ICAL_RSVP_TRUE;
		}
	}
}

static const gchar *
ecb_ews_get_current_user_meeting_reponse (ECalBackendEws *cbews,
					  icalcomponent *icalcomp,
					  const gchar *current_user_mail,
					  gboolean *out_rsvp_requested)
{
	icalproperty *attendee;
	const gchar *attendee_str = NULL, *attendee_mail = NULL;
	gint attendees_count = 0;
	const gchar *response = NULL;
	gboolean found = FALSE;

	if (out_rsvp_requested)
		*out_rsvp_requested = FALSE;

	attendee = icalcomponent_get_first_property (icalcomp, ICAL_ORGANIZER_PROPERTY);
	if (attendee) {
		attendee_str = icalproperty_get_organizer (attendee);

		if (attendee_str) {
			if (!strncasecmp (attendee_str, "MAILTO:", 7))
				attendee_mail = attendee_str + 7;
			else
				attendee_mail = attendee_str;
			if (attendee_mail && current_user_mail && g_ascii_strcasecmp (attendee_mail, current_user_mail) == 0) {
				/* Empty string means it's an organizer, NULL is when not found */
				return "";
			}
		}
	}

	for (attendee = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
		attendee != NULL;
		attendee = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY), attendees_count++) {
		attendee_str = icalproperty_get_attendee (attendee);

		if (attendee_str != NULL) {
			if (!strncasecmp (attendee_str, "MAILTO:", 7))
				attendee_mail = attendee_str + 7;
			else
				attendee_mail = attendee_str;
			if (attendee_mail && current_user_mail && g_ascii_strcasecmp (attendee_mail, current_user_mail) == 0) {
				response = icalproperty_get_parameter_as_string (attendee, "PARTSTAT");
				ecb_ews_get_rsvp (attendee, out_rsvp_requested);
				found = TRUE;
			}
		}
	}

	/* this should not happen, but if the user's configured email does not match the one
	   used in the invitation, like when the invitation comes to a mailing list... */
	if (!found && attendees_count == 1) {
		attendee = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
		g_return_val_if_fail (attendee != NULL, NULL);

		response = icalproperty_get_parameter_as_string (attendee, "PARTSTAT");
		ecb_ews_get_rsvp (attendee, out_rsvp_requested);
		found = TRUE;
	} else if (!found) {
		ESourceRegistry *registry;
		ECalComponent *comp;

		registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbews));
		comp = e_cal_component_new ();
		if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp))) {
			gchar *my_address;

			my_address = itip_get_comp_attendee (registry, comp, NULL);

			attendee = find_attendee (icalcomp, my_address);
			if (!attendee)
				attendee = find_attendee_if_sentby (icalcomp, my_address);

			if (attendee) {
				response = icalproperty_get_parameter_as_string (attendee, "PARTSTAT");
				ecb_ews_get_rsvp (attendee, out_rsvp_requested);
				found = TRUE;
			}

			g_free (my_address);
		}

		g_object_unref (comp);
	}

	if (found && !response) {
		response = "NEEDS-ACTION";
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
	icalproperty *prop;
	gchar *id_item = NULL;
	gchar *id_accept = NULL;
	gchar *ck = NULL;

	prop = icalcomponent_get_first_property (
		e_cal_component_get_icalcomponent (comp),
		ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name, *x_val;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!id_item && g_ascii_strcasecmp (x_name, "X-EVOLUTION-ITEMID") == 0)
			id_item = g_strdup (x_val);
		else if (!id_accept && g_ascii_strcasecmp (x_name, "X-EVOLUTION-ACCEPT-ID") == 0)
			id_accept = g_strdup (x_val);
		else if (changekey && !ck && !g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY"))
			ck = g_strdup (x_val);

		prop = icalcomponent_get_next_property (
			e_cal_component_get_icalcomponent (comp),
			ICAL_X_PROPERTY);
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
					 ECalComponent *comp,
					 icalcomponent *subcomp,
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
		g_set_error (error, E_DATA_CAL_ERROR, UnknownUser, _("Cannot find user “%s” between attendees"), user_email ? user_email : "NULL");
		return FALSE;
	}

	if (response_type && *response_type)
		ecb_ews_get_item_accept_id (comp, &item_id, &change_key, &mail_id);
	else
		response_type = NULL;

	while (pass < 2) {
		/*in case we do not have item id we will create item with mime content only*/
		if (!item_id || (response_type && g_ascii_strcasecmp (response_type, "NEEDS-ACTION") == 0)) {
			ecb_ews_receive_objects_no_exchange_mail (cbews, subcomp, &ids, cancellable, &local_error);
		} else {
			EwsCalendarConvertData convert_data = { 0 };

			convert_data.timezone_cache = E_TIMEZONE_CACHE (cbews);
			convert_data.response_type = (gchar *) response_type;
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
		icalproperty *transp;

		transp = icalcomponent_get_first_property (subcomp, ICAL_TRANSP_PROPERTY);

		if (g_strcmp0 (icalproperty_get_value_as_string (transp), "TRANSPARENT") == 0 &&
		    g_strcmp0 (response_type, "ACCEPTED") == 0) {
			EwsCalendarConvertData convert_data = { 0 };
			GSList *l;

			/*
			 * user can accept meeting but mark it as free in it's calendar
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
			      GError **error)
{
	ECalBackendEws *cbews;
	ECalBackend *cal_backend;
	CamelEwsSettings *ews_settings;
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	gchar *user_email;
	gboolean success = TRUE, do_refresh = FALSE;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (sync_backend));

	cbews = E_CAL_BACKEND_EWS (sync_backend);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbews), cancellable, error))
		return;

	icalcomp = calobj ? icalparser_parse_string (calobj) : NULL;

	if (!icalcomp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	/* make sure ical data we parse is actually a vCalendar component */
	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	cal_backend = E_CAL_BACKEND (cbews);
	kind = e_cal_backend_get_kind (cal_backend);

	ews_settings = ecb_ews_get_collection_settings (cbews);
	user_email = camel_ews_settings_dup_email (ews_settings);

	switch (icalcomponent_get_method (icalcomp)) {
	case ICAL_METHOD_REQUEST:
	case ICAL_METHOD_PUBLISH:
	case ICAL_METHOD_REPLY:
		for (subcomp = icalcomponent_get_first_component (icalcomp, kind);
		     subcomp && success;
		     subcomp = icalcomponent_get_next_component (icalcomp, kind)) {
			ECalComponent *comp;
			const gchar *response_type;
			gboolean rsvp_requested = FALSE;

			/* getting a data for meeting request response */
			response_type = ecb_ews_get_current_user_meeting_reponse (cbews, subcomp, user_email, &rsvp_requested);

			comp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (subcomp));

			success = ecb_ews_do_method_request_publish_reply (cbews, comp, subcomp, response_type, user_email, rsvp_requested, cancellable, error);

			do_refresh = TRUE;

			g_object_unref (comp);
		}
		break;
	case ICAL_METHOD_COUNTER:
		/*
		 * this is a new time proposal mail from one of the attendees
		 * if we decline the proposal, nothing have to be done
		 * if we accept it we will call to modify_object
		 */
		for (subcomp = icalcomponent_get_first_component (icalcomp, kind);
		     subcomp && success;
		     subcomp = icalcomponent_get_next_component (icalcomp, kind)) {
			const gchar *response_type;

			/* getting a data for meeting request response */
			response_type = ecb_ews_get_current_user_meeting_reponse (cbews, subcomp, user_email, NULL);

			if (g_strcmp0 (response_type, "ACCEPTED") == 0) {
				gchar **split_subject;
				icalproperty *summary;

				/* we have to edit the meeting subject to remove exchange header */
				summary = icalcomponent_get_first_property (subcomp, ICAL_SUMMARY_PROPERTY);
				split_subject =
					g_strsplit (icalproperty_get_value_as_string (summary), ":", -1);
				icalproperty_set_value_from_string (summary, split_subject[1] , "NO");
				g_strfreev (split_subject);

				success = ecb_ews_modify_item_sync (cbews, NULL, NULL, subcomp, cancellable, error);

				do_refresh = TRUE;
			}
		}
		break;
	default:
		break;
	}

	icalcomponent_free (icalcomp);
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
			   GSList **users,
			   gchar **modified_calobj,
			   GError **error)
{
	ECalBackendEws *cbews;
	icalcomponent_kind kind;
	icalcomponent *icalcomp, *subcomp = NULL;
	gchar *subcalobj;
	gboolean success = TRUE;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (sync_backend));

	cbews = E_CAL_BACKEND_EWS (sync_backend);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbews), cancellable, error))
		return;

	icalcomp = calobj ? icalparser_parse_string (calobj) : NULL;

	/* make sure data was parsed properly */
	if (!icalcomp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	/* make sure ical data we parse is actually an vcal component */
	if ((icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) && (icalcomponent_isa (icalcomp) != ICAL_VEVENT_COMPONENT)) {
		icalcomponent_free (icalcomp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbews));

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbews));
		subcomp = icalcomponent_get_first_component (icalcomp, kind);
	}
	if (icalcomponent_isa (icalcomp) == ICAL_VEVENT_COMPONENT)
		subcomp = icalcomp;
	while (subcomp && success) {
		const gchar *new_body_content = NULL, *subject = NULL, *org_email = NULL;
		const gchar *org = NULL, *attendee = NULL;
		icalproperty *prop, *org_prop = NULL;
		CamelInternetAddress *org_addr = camel_internet_address_new ();

		new_body_content = e_cal_util_get_x_property (subcomp, "X-EVOLUTION-RETRACT-COMMENT");
		subject = icalproperty_get_value_as_string (icalcomponent_get_first_property (subcomp, ICAL_SUMMARY_PROPERTY));

		org_prop = icalcomponent_get_first_property (subcomp, ICAL_ORGANIZER_PROPERTY);
		org = icalproperty_get_organizer (org_prop);
		if (!g_ascii_strncasecmp (org, "MAILTO:", 7))
			org_email = (org) + 7;
		else
			org_email = org;

		camel_internet_address_add (org_addr, icalproperty_get_parameter_as_string (org_prop, "CN"), org_email);

		/* iterate over every attendee property */
		for (prop = icalcomponent_get_first_property (subcomp, ICAL_ATTENDEE_PROPERTY);
		     prop && success;
		     prop = icalcomponent_get_next_property (subcomp, ICAL_ATTENDEE_PROPERTY)) {
			CamelInternetAddress *attendee_addr = camel_internet_address_new ();
			attendee = icalproperty_get_attendee (prop);
			if (g_ascii_strcasecmp (org_email, attendee) == 0) continue;
			if (!g_ascii_strncasecmp (attendee, "mailto:", 7)) attendee = (attendee) + 7;

			subcalobj = icalcomponent_as_ical_string_r (subcomp);
			camel_internet_address_add (attendee_addr, icalproperty_get_parameter_as_string (prop, "CN"), attendee);
			success = ecb_ews_send_cancellation_email_sync (cbews, CAMEL_ADDRESS (org_addr), attendee_addr,
				subject, new_body_content, subcalobj, cancellable, error);
			g_object_unref (attendee_addr);
			free (subcalobj);
		}

		g_object_unref (org_addr);
		subcomp = icalcomponent_get_next_component (icalcomp, kind);
	}

	icalcomponent_free (icalcomp);

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
		g_propagate_error (error, EDC_ERROR (SearchSizeLimitExceeded));
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
			icalcomponent *icalcomp = fblink->data;
			gchar *mailto;

			/* add attendee property */
			mailto = g_strconcat ("mailto:", ulink->data, NULL);
			icalcomponent_add_property (icalcomp, icalproperty_new_attendee (mailto));
			g_free (mailto);

			*freebusyobjs = g_slist_prepend (*freebusyobjs, icalcomponent_as_ical_string_r (icalcomp));
		}

		*freebusyobjs = g_slist_reverse (*freebusyobjs);
	}

	g_slist_free_full (freebusy, (GDestroyNotify) icalcomponent_free);

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
			CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS,
			CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY,
			CAL_STATIC_CAPABILITY_REMOVE_ALARMS,
			CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			CAL_STATIC_CAPABILITY_NO_THISANDFUTURE,
			CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK,
			CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT,
			CAL_STATIC_CAPABILITY_SAVE_SCHEDULES,
			CAL_STATIC_CAPABILITY_NO_ALARM_AFTER_START,
			CAL_STATIC_CAPABILITY_NO_MEMO_START_DATE,
			CAL_STATIC_CAPABILITY_ALL_DAY_EVENT_AS_TIME,
			CAL_STATIC_CAPABILITY_TASK_DATE_ONLY,
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cbews)),
			NULL);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		/* return email address of the person who opened the calendar */
		CamelEwsSettings *ews_settings;

		ews_settings = ecb_ews_get_collection_settings (cbews);

		return camel_ews_settings_dup_email (ews_settings);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* ews does not support email based alarms */
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_ews_parent_class)->get_backend_property (cal_backend, prop_name);
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
	SoupURI *soup_uri;
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

	soup_uri = soup_uri_new (host_url);
	if (soup_uri) {
		*host = g_strdup (soup_uri_get_host (soup_uri));
		*port = soup_uri_get_port (soup_uri);

		result = *host && **host;
		if (!result) {
			g_free (*host);
			*host = NULL;
		}

		soup_uri_free (soup_uri);
	}

	g_free (host_url);

	return result;
}

static gchar *
ecb_ews_dup_component_revision (ECalCache *cal_cache,
				icalcomponent *icalcomp,
				gpointer user_data)
{
	g_return_val_if_fail (icalcomp != NULL, NULL);

	return e_cal_util_dup_x_property (icalcomp, "X-EVOLUTION-CHANGEKEY");
}

static void
ecb_ews_constructed (GObject *object)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (object);
	ECalCache *cal_cache;
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
}

static void
ecb_ews_dispose (GObject *object)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (object);

	g_rec_mutex_lock (&cbews->priv->cnc_lock);

	g_clear_object (&cbews->priv->cnc);

	g_rec_mutex_unlock (&cbews->priv->cnc_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->dispose (object);
}

static void
ecb_ews_finalize (GObject *object)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (object);

	g_free (cbews->priv->folder_id);
	g_free (cbews->priv->attachments_dir);

	g_rec_mutex_clear (&cbews->priv->cnc_lock);

	e_cal_backend_ews_unref_windows_zones ();

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->finalize (object);
}

static void
e_cal_backend_ews_init (ECalBackendEws *cbews)
{
	cbews->priv = G_TYPE_INSTANCE_GET_PRIVATE (cbews, E_TYPE_CAL_BACKEND_EWS, ECalBackendEwsPrivate);

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

	g_type_class_add_private (klass, sizeof (ECalBackendEwsPrivate));

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
	cal_backend_class->get_backend_property = ecb_ews_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ecb_ews_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ecb_ews_constructed;
	object_class->dispose = ecb_ews_dispose;
	object_class->finalize = ecb_ews_finalize;
}
