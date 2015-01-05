/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

#include <calendar/gui/itip-utils.h>

#include "server/e-source-ews-folder.h"
#include "server/e-ews-connection-utils.h"

#include "utils/ews-camel-common.h"

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
	/* Fields required for online server requests */
	EEwsConnection *cnc;
	gchar *folder_id;
	gchar *user_email;
	gchar *storage_path;

	ECalBackendStore *store;
	gboolean read_only;

	/* A mutex to control access to the private structure for the following */
	GRecMutex rec_mutex;
	icaltimezone *default_zone;
	guint refresh_timeout;
	guint refreshing;
	EFlag *refreshing_done;
	GHashTable *item_id_hash;

	GCancellable *cancellable;

	guint subscription_key;
	gboolean listen_notifications;
};

#define PRIV_LOCK(p)   (g_rec_mutex_lock (&(p)->rec_mutex))
#define PRIV_UNLOCK(p) (g_rec_mutex_unlock (&(p)->rec_mutex))

#define SYNC_KEY "sync-state"
#define EWS_MAX_FETCH_COUNT 100
#define REFRESH_INTERVAL 600

#define GET_ITEMS_SYNC_PROPERTIES \
	"item:Attachments" \
	" item:Categories" \
	" item:HasAttachments" \
	" item:MimeContent" \
	" calendar:UID" \
	" calendar:Resources" \
	" calendar:ModifiedOccurrences" \
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

/* Forward Declarations */
static void ews_cal_component_get_item_id (ECalComponent *comp, gchar **itemid, gchar **changekey);
static gboolean ews_start_sync	(gpointer data);
static gpointer ews_start_sync_thread (gpointer data);
static void	e_cal_backend_ews_authenticator_init
				(ESourceAuthenticatorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (
	ECalBackendEws,
	e_cal_backend_ews,
	E_TYPE_CAL_BACKEND,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_cal_backend_ews_authenticator_init))

static CamelEwsSettings *
cal_backend_ews_get_collection_settings (ECalBackendEws *backend)
{
	ESource *source;
	ESource *collection;
	ESourceCamel *extension;
	ESourceRegistry *registry;
	CamelSettings *settings;
	const gchar *extension_name;

	source = e_backend_get_source (E_BACKEND (backend));
	registry = e_cal_backend_get_registry (E_CAL_BACKEND (backend));

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
convert_error_to_edc_error (GError **perror)
{
	GError *error = NULL;

	g_return_if_fail (perror != NULL);

	if (!*perror || (*perror)->domain == E_DATA_CAL_ERROR)
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
		}
	}

	if (!error)
		error = EDC_ERROR_EX (OtherError, (*perror)->message);

	g_error_free (*perror);
	*perror = error;
}

static void
switch_offline (ECalBackendEws *cbews)
{
	ECalBackendEwsPrivate *priv;

	priv= cbews->priv;
	priv->read_only = TRUE;

	if (priv->refresh_timeout) {
		g_source_remove (priv->refresh_timeout);
		priv->refresh_timeout = 0;
	}

	if (priv->cancellable) {
		g_cancellable_cancel (priv->cancellable);
		g_object_unref (priv->cancellable);
		priv->cancellable = NULL;
	}

	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}
}

static gboolean
cal_backend_ews_ensure_connected (ECalBackendEws *cbews,
				  GCancellable *cancellable,
				  GError **perror)
{
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_EWS (cbews), FALSE);

	PRIV_LOCK (cbews->priv);

	if (cbews->priv->cnc) {
		PRIV_UNLOCK (cbews->priv);
		return TRUE;
	}

	PRIV_UNLOCK (cbews->priv);

	e_backend_authenticate_sync (
		E_BACKEND (cbews),
		E_SOURCE_AUTHENTICATOR (cbews),
		cancellable, &local_error);

	if (!local_error)
		return TRUE;

	g_propagate_error (perror, local_error);

	return FALSE;
}

static void
e_cal_backend_ews_add_timezone (ECalBackend *backend,
                                EDataCal *cal,
                                guint32 context,
                                GCancellable *cancellable,
                                const gchar *tzobj)
{
	ETimezoneCache *timezone_cache;
	icalcomponent *tz_comp;
	ECalBackendEws *cbews;
	GError *error = NULL;

	cbews = (ECalBackendEws *) backend;
	timezone_cache = E_TIMEZONE_CACHE (backend);

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);
	e_data_cal_error_if_fail (tzobj != NULL, InvalidArg);

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);
		e_timezone_cache_add_timezone (timezone_cache, zone);
		icaltimezone_free (zone, 1);
	}

exit:
	/*FIXME pass tzid here */
	convert_error_to_edc_error (&error);
	e_data_cal_respond_add_timezone (cal, context, error);
}

typedef enum {
	E_EWS_ATTACHMENT_TYPE_NOTHING = 0,
	E_EWS_ATTACHMENT_TYPE_CREATE,
	E_EWS_ATTACHMENT_TYPE_UPDATE
} EEwsAttachmentType;

typedef struct {
	ECalBackendEws *cbews; /* Create, Remove, Modify, FreeBusy, Attachments, DiscardAlarm */
	ECalComponent *comp; /* Create, Remove, Modify, FreeBusy, Attachments */
	ECalComponent *extra_comp; /* Modify, Attachments: used as old_comp. Remove: used as parent_comp */
	EDataCal *cal; /* Create, Remove, Modify, FreeBusy, Attachments, DiscardAlarm */
	GSList *users; /* FreeBusy */
	gchar *item_id; /* Accept, Remove, Modify, Attachments, DiscardAlarm */
	gchar *uid; /* Remove */
	gchar *rid; /* Remove */
	EEwsAttachmentType cb_type; /* Attachments */
	ECalObjModType mod; /* Remove */
	guint32 context; /* Create, Remove, Modify, FreeBusy, Attachments, DiscardAlarm */
} EwsCalendarAsyncData;

static void
e_cal_backend_ews_async_data_free (EwsCalendarAsyncData *async_data)
{
	if (async_data != NULL) {
		if (async_data->cbews != NULL)
			g_clear_object (&async_data->cbews);
		if (async_data->comp != NULL)
			g_clear_object (&async_data->comp);
		if (async_data->extra_comp != NULL)
			g_clear_object (&async_data->extra_comp);
		if (async_data->cal != NULL)
			g_clear_object (&async_data->cal);

		g_slist_free_full (async_data->users, g_free);
		g_free (async_data->item_id);
		g_free (async_data->uid);
		g_free (async_data->rid);

		g_free (async_data);
	}
}

static void
ews_cal_discard_alarm_cb (GObject *object,
                          GAsyncResult *res,
                          gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCalendarAsyncData *edad = user_data;
	GError *error = NULL;

	if (!e_ews_connection_update_items_finish (cnc, res, NULL, &error)) {
		convert_error_to_edc_error (&error);
	}

	convert_error_to_edc_error (&error);
	e_data_cal_respond_discard_alarm (edad->cal, edad->context, error);

	e_cal_backend_ews_async_data_free (edad);
}

static void
e_cal_backend_ews_discard_alarm (ECalBackend *backend,
                                 EDataCal *cal,
                                 guint32 context,
                                 GCancellable *cancellable,
                                 const gchar *uid,
                                 const gchar *rid,
                                 const gchar *auid)
{
	ECalBackendEws *cbews = (ECalBackendEws *) backend;
	ECalBackendEwsPrivate *priv;
	EwsCalendarAsyncData *edad;
	EwsCalendarConvertData convert_data = { 0 };
	ECalComponent *comp;
	GError *local_error = NULL;

	priv = cbews->priv;

	PRIV_LOCK (priv);

	comp = e_cal_backend_store_get_component (priv->store, uid, NULL);
	if (!comp) {
		e_data_cal_respond_discard_alarm (
			cal, context,
			EDC_ERROR (ObjectNotFound));
		PRIV_UNLOCK (priv);
		return;
	}

	PRIV_UNLOCK (priv);

	if (!cal_backend_ews_ensure_connected (cbews, cancellable, &local_error)) {
		convert_error_to_edc_error (&local_error);
		e_data_cal_respond_discard_alarm (cal, context, local_error);
		return;
	}

	/* FIXME: Can't there be multiple alarms for each event? Or does
	 * Exchange not support that? */
	edad = g_new0 (EwsCalendarAsyncData, 1);
	edad->cbews = g_object_ref (cbews);
	edad->cal = g_object_ref (cal);
	edad->context = context;

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

	ews_cal_component_get_item_id (comp, &convert_data.item_id, &convert_data.change_key);

	e_ews_connection_update_items (
		priv->cnc, EWS_PRIORITY_MEDIUM,
		"AlwaysOverwrite", NULL,
		"SendToNone", NULL,
		e_cal_backend_ews_clear_reminder_is_set,
		&convert_data,
		priv->cancellable,
		ews_cal_discard_alarm_cb,
		edad);
}

static gchar *
cal_backend_ews_get_builtin_zone_object (const gchar *tzid)
{
	icalcomponent *icalcomp = NULL, *free_comp = NULL;
	icaltimezone *zone;
	gchar *object = NULL;

	zone = icaltimezone_get_builtin_timezone (tzid);
	if (!zone) {
		icalcomp = free_comp = icaltzutil_fetch_timezone (tzid);
	}

	if (zone)
		icalcomp = icaltimezone_get_component (zone);

	if (icalcomp) {
		icalcomponent *clone = icalcomponent_new_clone (icalcomp);
		icalproperty *prop;

		prop = icalcomponent_get_first_property (clone, ICAL_TZID_PROPERTY);
		if (prop) {
			/* change tzid to our, because the component has the buildin tzid */
			icalproperty_set_tzid (prop, tzid);

			object = icalcomponent_as_ical_string_r (clone);
		}
		icalcomponent_free (clone);
	}

	if (free_comp)
		icalcomponent_free (free_comp);

	return object;
}

static void
e_cal_backend_ews_get_timezone (ECalBackend *backend,
                                EDataCal *cal,
                                guint32 context,
                                GCancellable *cancellable,
                                const gchar *tzid)
{
	ETimezoneCache *timezone_cache;
	icalcomponent *icalcomp;
	icaltimezone *zone;
	gchar *object = NULL;
	GError *error = NULL;

	timezone_cache = E_TIMEZONE_CACHE (backend);

	zone = e_timezone_cache_get_timezone (timezone_cache, tzid);
	if (zone) {
		icalcomp = icaltimezone_get_component (zone);

		if (!icalcomp)
			g_propagate_error (&error, e_data_cal_create_error (InvalidObject, NULL));
		else
			object = icalcomponent_as_ical_string_r (icalcomp);
	} else {
		/* TODO Implement in ECalBackend base class */
		/* fallback if tzid contains only the location of timezone */
		gint i, slashes = 0;

		for (i = 0; tzid[i]; i++) {
			if (tzid[i] == '/')
				slashes++;
		}

		if (slashes == 1)
			object = cal_backend_ews_get_builtin_zone_object (tzid);

		if (!object) {
			/* The timezone can be sometimes the Windows zone, try to convert it to libical */
			const gchar *ical_location = e_cal_backend_ews_tz_util_get_ical_equivalent (tzid);
			if (ical_location) {
				object = cal_backend_ews_get_builtin_zone_object (ical_location);
			}
		}
	}

	if (!object && !error)
		g_propagate_error (&error, e_data_cal_create_error (ObjectNotFound, NULL));

	convert_error_to_edc_error (&error);
	e_data_cal_respond_get_timezone (cal, context, error, object);
	g_free (object);

}

/* changekey can be NULL if you don't want it. itemid cannot. */
static void
ews_cal_component_get_item_id (ECalComponent *comp,
                               gchar **itemid,
                               gchar **changekey)
{
	icalproperty *prop;
	gchar *ck = NULL;
	gchar *id = NULL;

	prop = icalcomponent_get_first_property (
		e_cal_component_get_icalcomponent (comp),
		ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name, *x_val;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!id && !g_ascii_strcasecmp (x_name, "X-EVOLUTION-ITEMID"))
			id = g_strdup (x_val);
		 else if (changekey && !ck && !g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY"))
			ck = g_strdup (x_val);

		prop = icalcomponent_get_next_property (
			e_cal_component_get_icalcomponent (comp),
			ICAL_X_PROPERTY);
	}

	*itemid = id;
	if (changekey)
		*changekey = ck;
}

/* changekey can be NULL if you don't want it. itemid cannot. */
static void
ews_cal_component_get_calendar_item_accept_id (ECalComponent *comp,
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

static void
add_comps_to_item_id_hash (ECalBackendEws *cbews)
{
	ECalBackendEwsPrivate *priv;
	GSList *comps, *l;

	priv = cbews->priv;

	PRIV_LOCK (priv);

	comps = e_cal_backend_store_get_components (priv->store);
	for (l = comps; l != NULL; l = g_slist_next (l)) {
		ECalComponent *comp = (ECalComponent *)	l->data;
		gchar *item_id = NULL;

		ews_cal_component_get_item_id (comp, &item_id, NULL);
		if (!item_id) {
			const gchar *uid;

			/* This should never happen, but sometimes when our
			 * use of X- fields has changed it has triggered. Make
			 * it cope, and not crash */
			e_cal_component_get_uid (comp, &uid);
			g_warning (
				"EWS calendar item %s had no EWS ItemID!",
				uid);
			continue;
		}
		g_hash_table_insert (priv->item_id_hash, item_id, comp);
	}

	PRIV_UNLOCK (priv);

	g_slist_free (comps);
}

static gpointer
handle_notifications_thread (gpointer data)
{
	ECalBackendEws *cbews = data;

	PRIV_LOCK (cbews->priv);

	if (cbews->priv->cnc == NULL)
		goto exit;

	if (cbews->priv->listen_notifications) {
		GSList *folders = NULL;

		if (cbews->priv->subscription_key != 0)
			goto exit;

		folders = g_slist_prepend (folders, cbews->priv->folder_id);

		e_ews_connection_enable_notifications_sync (
				cbews->priv->cnc,
				folders,
				&cbews->priv->subscription_key);

		g_slist_free (folders);
	} else {
		if (cbews->priv->subscription_key == 0)
			goto exit;

		e_ews_connection_disable_notifications_sync (
				cbews->priv->cnc,
				cbews->priv->subscription_key);

		cbews->priv->subscription_key = 0;
	}

exit:
	PRIV_UNLOCK (cbews->priv);
	g_object_unref (cbews);
	return NULL;
}

static void
cbews_listen_notifications_cb (ECalBackendEws *cbews,
			       GParamSpec *spec,
			       CamelEwsSettings *ews_settings)
{
	GThread *thread;

	PRIV_LOCK (cbews->priv);
	if (cbews->priv->cnc == NULL) {
		PRIV_UNLOCK (cbews->priv);
		return;
	}

	if (!e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010_SP1)) {
		PRIV_UNLOCK (cbews->priv);
		return;
	}

	cbews->priv->listen_notifications = camel_ews_settings_get_listen_notifications (ews_settings);
	PRIV_UNLOCK (cbews->priv);

	thread = g_thread_new (NULL, handle_notifications_thread, g_object_ref (cbews));
	g_thread_unref (thread);
}

static void
cbews_server_notification_cb (ECalBackendEws *cbews,
			      GSList *events,
			      EEwsConnection *cnc)
{
	GSList *l;
	gboolean update_folder = FALSE;

	g_return_if_fail (cbews != NULL);
	g_return_if_fail (cbews->priv != NULL);

	for (l = events; l != NULL; l = l->next) {
		EEwsNotificationEvent *event = l->data;

		switch (event->type) {
			case E_EWS_NOTIFICATION_EVENT_CREATED:
			case E_EWS_NOTIFICATION_EVENT_DELETED:
			case E_EWS_NOTIFICATION_EVENT_MODIFIED:
				PRIV_LOCK (cbews->priv);
				if (g_strcmp0 (event->folder_id, cbews->priv->folder_id) == 0)
					update_folder = TRUE;
				PRIV_UNLOCK (cbews->priv);
				break;
			case E_EWS_NOTIFICATION_EVENT_MOVED:
			case E_EWS_NOTIFICATION_EVENT_COPIED:
				PRIV_LOCK (cbews->priv);
				if (g_strcmp0 (event->folder_id, cbews->priv->folder_id) == 0 ||
				    g_strcmp0 (event->old_folder_id, cbews->priv->folder_id) == 0)
					update_folder = TRUE;
				PRIV_UNLOCK (cbews->priv);
				break;
			default:
				return;
		}
	}

	if (update_folder)
		ews_start_sync (cbews);
}

static void
e_cal_backend_ews_open (ECalBackend *backend,
                        EDataCal *cal,
                        guint32 opid,
                        GCancellable *cancellable,
                        gboolean only_if_exists)
{
	CamelEwsSettings *ews_settings;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	ESource *source;
	const gchar *cache_dir;
	gboolean need_to_authenticate;
	gboolean ret = TRUE;
	GError *error = NULL;

	if (e_cal_backend_is_opened (backend))
		return;

	cbews = (ECalBackendEws *) backend;
	priv = cbews->priv;

	cache_dir = e_cal_backend_get_cache_dir (backend);
	source = e_backend_get_source (E_BACKEND (cbews));
	ews_settings = cal_backend_ews_get_collection_settings (cbews);

	PRIV_LOCK (priv);

	if (!priv->store) {
		ESourceEwsFolder *extension;
		const gchar *extension_name;

		extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
		extension = e_source_get_extension (source, extension_name);
		priv->folder_id = e_source_ews_folder_dup_id (extension);

		priv->storage_path = g_build_filename (cache_dir, priv->folder_id, NULL);

		priv->store = e_cal_backend_store_new (
			priv->storage_path,
			E_TIMEZONE_CACHE (backend));
		e_cal_backend_store_load (priv->store);
		add_comps_to_item_id_hash (cbews);

		if (priv->default_zone)
			e_cal_backend_store_set_default_timezone (
				priv->store, priv->default_zone);
	}

	need_to_authenticate =
		(priv->cnc == NULL) &&
		(e_backend_is_destination_reachable (E_BACKEND (backend), cancellable, NULL));

	PRIV_UNLOCK (priv);

	if (need_to_authenticate)
		ret = e_backend_authenticate_sync (
				E_BACKEND (backend),
				E_SOURCE_AUTHENTICATOR (backend),
				cancellable, &error);

	if (ret) {
		e_cal_backend_set_writable (backend, TRUE);

		PRIV_LOCK (priv);
		if (priv->cnc != NULL) {
			priv->listen_notifications = camel_ews_settings_get_listen_notifications (ews_settings);

			if (priv->listen_notifications)
				cbews_listen_notifications_cb (cbews, NULL, ews_settings);
		}
		PRIV_UNLOCK (priv);
	}

	convert_error_to_edc_error (&error);
	e_data_cal_respond_open (cal, opid, error);

	g_signal_connect_swapped (
		ews_settings,
		"notify::listen-notifications",
		G_CALLBACK (cbews_listen_notifications_cb),
		cbews);
}

static void
e_cal_backend_ews_get_object (ECalBackend *backend,
                              EDataCal *cal,
                              guint32 context,
                              GCancellable *cancellable,
                              const gchar *uid,
                              const gchar *rid)
{
	ECalBackendEwsPrivate *priv;
	ECalBackendEws *cbews = (ECalBackendEws *) backend;
	gchar *object = NULL;
	GError *error = NULL;

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);

	priv = cbews->priv;

	PRIV_LOCK (priv);

	if (e_backend_get_online (E_BACKEND (backend))) {
		/* make sure any pending refreshing is done */
		while (priv->refreshing) {
			PRIV_UNLOCK (priv);
			e_flag_wait (priv->refreshing_done);
			PRIV_LOCK (priv);
		}
	}

	/* search the object in the cache */
	if (rid && *rid) {
		ECalComponent *comp;

		comp = e_cal_backend_store_get_component (priv->store, uid, rid);
		if (!comp && e_backend_get_online (E_BACKEND (backend))) {
			/* maybe a meeting invitation, for which the calendar item is not downloaded yet,
			 * thus synchronize local cache first */
			ews_start_sync (cbews);

			PRIV_UNLOCK (priv);
			e_flag_wait (priv->refreshing_done);
			PRIV_LOCK (priv);

			comp = e_cal_backend_store_get_component (priv->store, uid, rid);
		}

		if (comp) {
			object = e_cal_component_get_as_string (comp);

			g_object_unref (comp);

			if (!object)
				g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
		} else {
			g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
		}
	} else {
		object = e_cal_backend_store_get_components_by_uid_as_ical_string (priv->store, uid);
		if (!object && e_backend_get_online (E_BACKEND (backend))) {
			/* maybe a meeting invitation, for which the calendar item is not downloaded yet,
			 * thus synchronize local cache first */
			ews_start_sync (cbews);

			PRIV_UNLOCK (priv);
			e_flag_wait (priv->refreshing_done);
			PRIV_LOCK (priv);

			object = e_cal_backend_store_get_components_by_uid_as_ical_string (priv->store, uid);
		}

		if (!object)
			g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
	}

	PRIV_UNLOCK (priv);

 exit:
	convert_error_to_edc_error (&error);
	e_data_cal_respond_get_object (cal, context, error, object);
	g_free (object);
}

static void
cal_backend_ews_get_object_list (ECalBackend *backend,
                                 const gchar *sexp,
                                 GSList **objects,
                                 GError **error)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GSList *components, *l;
	ECalBackendSExp *cbsexp;
	gboolean search_needed = TRUE;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	if (!strcmp (sexp, "#t"))
		search_needed = FALSE;

	cbsexp = e_cal_backend_sexp_new (sexp);
	if (!cbsexp) {
		g_propagate_error (error, EDC_ERROR (InvalidQuery));
		return;
	}

	*objects = NULL;

	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (
		cbsexp, &occur_start, &occur_end);
	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = l->next) {
		ECalComponent *comp = E_CAL_COMPONENT (l->data);

		if (e_cal_backend_get_kind (backend) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			if ((!search_needed) ||
			    (e_cal_backend_sexp_match_comp (cbsexp, comp, E_TIMEZONE_CACHE (backend)))) {
				*objects = g_slist_append (*objects, e_cal_component_get_as_string (comp));
			}
		}
	}

	g_object_unref (cbsexp);
	g_slist_free_full (components, g_object_unref);
}

static void
e_cal_backend_ews_get_object_list (ECalBackend *backend,
                                   EDataCal *cal,
                                   guint32 context,
                                   GCancellable *cancellable,
                                   const gchar *sexp)
{
	GSList *objects = NULL, *l;
	GError *error = NULL;

	cal_backend_ews_get_object_list (backend, sexp, &objects, &error);

	convert_error_to_edc_error (&error);
	e_data_cal_respond_get_object_list (cal, context, error, objects);
	if (objects) {
		for (l = objects; l != NULL; l = g_slist_next (l))
			g_free (l->data);
		g_slist_free (objects);
	}
}

static gboolean
ews_cal_delete_comp (ECalBackendEws *cbews,
                     ECalComponent *comp,
                     const gchar *item_id)
{
	ECalBackendEwsPrivate *priv = cbews->priv;
	ECalComponentId *uid;
	gboolean ret;

	uid = e_cal_component_get_id (comp);
	ret = e_cal_backend_store_remove_component (priv->store, uid->uid, uid->rid);
	if (!ret)
		goto exit;

	e_cal_backend_notify_component_removed (E_CAL_BACKEND (cbews), uid, comp, NULL);

	PRIV_LOCK (priv);
	g_hash_table_remove (priv->item_id_hash, item_id);
	PRIV_UNLOCK (priv);

exit:
	e_cal_component_free_id (uid);
	return ret;
}

static void
ews_cal_append_exdate (ECalBackendEws *cbews,
                       ECalComponent *comp,
                       const gchar *rid,
                       ECalObjModType mod)
{
	ECalComponent *old_comp;

	old_comp = e_cal_component_clone (comp);
	e_cal_util_remove_instances (e_cal_component_get_icalcomponent (comp), icaltime_from_string (rid), mod);

	e_cal_backend_notify_component_modified (E_CAL_BACKEND (cbews), old_comp, comp);

	g_object_unref (old_comp);
}

static icaltimezone *
e_cal_backend_ews_get_timezone_from_ical_component (ECalBackend *backend,
						    icalcomponent *comp)
{
	ETimezoneCache *timezone_cache;
	icalproperty *prop = NULL;
	const gchar *tzid = NULL;

	timezone_cache = E_TIMEZONE_CACHE (backend);

	prop = icalcomponent_get_first_property (comp, ICAL_DTSTART_PROPERTY);
	if (prop != NULL) {
		icalparameter *param = NULL;

		param = icalproperty_get_first_parameter (prop, ICAL_TZID_PARAMETER);
		if (param != NULL) {
			tzid = icalparameter_get_tzid (param);
		} else {
			struct icaltimetype dtstart;

			dtstart = icalproperty_get_dtstart (prop);
			if (dtstart.is_utc)
				tzid = "UTC";
		}
	}

	if (tzid != NULL)
		return e_timezone_cache_get_timezone (timezone_cache, tzid);

	return NULL;
}

static void
ews_cal_remove_object_cb (GObject *object,
                          GAsyncResult *res,
                          gpointer user_data)
{
	EwsCalendarAsyncData *remove_data = user_data;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (!g_simple_async_result_propagate_error (simple, &error) || error->code == EWS_CONNECTION_ERROR_ITEMNOTFOUND) {
		/* remove detached instances first */
		if (remove_data->mod == E_CAL_OBJ_MOD_ALL) {
			ECalBackendEws *cbews = remove_data->cbews;
			GSList *with_detached, *iter;

			with_detached = e_cal_backend_store_get_components_by_uid (cbews->priv->store, remove_data->uid);
			for (iter = with_detached; iter; iter = g_slist_next (iter)) {
				ECalComponent *comp;
				ECalComponentId *id;

				comp = iter->data;
				id = e_cal_component_get_id (comp);

				/* notify separately only detached instances - the master object will be removed below */
				if (id && id->rid && *id->rid &&
				    e_cal_backend_store_remove_component (cbews->priv->store, id->uid, id->rid)) {
					gchar *item_id = NULL;

					e_cal_backend_notify_component_removed (E_CAL_BACKEND (cbews), id, comp, NULL);

					ews_cal_component_get_item_id (comp, &item_id, NULL);
					if (item_id) {
						PRIV_LOCK (cbews->priv);
						g_hash_table_remove (cbews->priv->item_id_hash, item_id);
						PRIV_UNLOCK (cbews->priv);

						g_free (item_id);
					}
				}

				e_cal_component_free_id (id);
			}

			g_slist_free_full (with_detached, g_object_unref);
		}

		/* FIXME: This is horrid. Will bite us when we start to delete
		 * more than one item at a time... */
		if (remove_data->comp != NULL)
			ews_cal_delete_comp (remove_data->cbews, remove_data->comp, remove_data->item_id);
		if (remove_data->extra_comp != NULL)
			ews_cal_append_exdate (
				remove_data->cbews, remove_data->extra_comp, remove_data->rid, remove_data->mod);
	}

	convert_error_to_edc_error (&error);

	if (remove_data->context)
		e_data_cal_respond_remove_objects (remove_data->cal, remove_data->context, error, NULL, NULL, NULL);
	else if (error) {
		g_warning ("Remove object error :  %s\n", error->message);
		g_clear_error (&error);
	}

	e_cal_backend_ews_async_data_free (remove_data);
}

static void
e_cal_backend_ews_remove_object (ECalBackend *backend,
                                 EDataCal *cal,
                                 guint32 context,
                                 GCancellable *cancellable,
                                 const gchar *uid,
                                 const gchar *rid,
                                 ECalObjModType mod)
{
	EwsCalendarAsyncData *remove_data;
	ECalBackendEws *cbews = (ECalBackendEws *) backend;
	ECalBackendEwsPrivate *priv;
	ECalComponent *comp = NULL, *parent = NULL;
	GError *error = NULL;
	EwsId item_id;
	guint index = 0;

	/* There are 3 scenarios where this function is called:
	 * 1. An item with no recurrence - rid is NULL. Nothing special here.
	 * 2. A modified occurrence of a recurring event - rid isnt NULL. The store will contain the object which will have to be removed from it.
	 * 3. A non modified occurrence of a recurring event - rid isnt NULL. The store will only have a reference to the master event.
	 *        This is actually an update event where an exception date will have to be appended to the master. 
	 */
	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);

	if (!cal_backend_ews_ensure_connected (cbews, cancellable, &error)) {
		convert_error_to_edc_error (&error);
		e_data_cal_respond_remove_objects (cal, context, error, NULL, NULL, NULL);
		return;
	}

	priv = cbews->priv;

	PRIV_LOCK (priv);

	comp = e_cal_backend_store_get_component (priv->store, uid, rid);

	if (!rid || !*rid)
		rid = NULL;

	if (rid) {
		parent = e_cal_backend_store_get_component (priv->store, uid, NULL);
		if (!parent && !comp) {
			g_warning ("EEE Cant find master component with uid:%s\n", uid);
			g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
			PRIV_UNLOCK (priv);
			goto exit;
		}
	}

	if (!comp && !parent) {
		g_warning ("EEE Cant find component with uid:%s & rid:%s\n", uid, rid);
		g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
		PRIV_UNLOCK (priv);
		goto exit;
	}

	ews_cal_component_get_item_id ((comp ? comp : parent), &item_id.id, &item_id.change_key);

	PRIV_UNLOCK (priv);

	if (!item_id.id) {
		g_propagate_error (
			&error, EDC_ERROR_EX (OtherError,
			"Cannot determine EWS ItemId"));
		goto exit;
	}

	if (parent && !comp) {
		index = e_cal_backend_ews_rid_to_index (
			e_cal_backend_ews_get_timezone_from_ical_component (
				backend,
				e_cal_component_get_icalcomponent (parent)),
			rid,
			e_cal_component_get_icalcomponent (parent),
			&error);

		if (error != NULL)
			goto exit;
	}

	remove_data = g_new0 (EwsCalendarAsyncData, 1);
	remove_data->cbews = g_object_ref (cbews);
	remove_data->comp = comp != NULL ? g_object_ref (comp) : NULL;
	remove_data->extra_comp = parent != NULL ? g_object_ref (parent) : NULL;
	remove_data->cal = g_object_ref (cal);
	remove_data->context = context;
	remove_data->item_id = g_strdup (item_id.id);
	remove_data->uid = g_strdup (uid);
	remove_data->rid = (rid ? g_strdup (rid) : NULL);
	remove_data->mod = mod;

	e_ews_connection_delete_item (
		priv->cnc, EWS_PRIORITY_MEDIUM, &item_id, index,
		EWS_HARD_DELETE, EWS_SEND_TO_NONE, EWS_ALL_OCCURRENCES,
		priv->cancellable,
		ews_cal_remove_object_cb,
		remove_data);

	return;

exit:
	if (comp != NULL)
		g_object_unref (comp);

	if (parent != NULL)
		g_object_unref (parent);

	convert_error_to_edc_error (&error);

	if (context)
		e_data_cal_respond_remove_objects (cal, context, error, NULL, NULL, NULL);
	else if (error != NULL) {
		g_warning ("Remove object error :  %s\n", error->message);
		g_clear_error (&error);
	}
}

static void
e_cal_backend_ews_remove_objects (ECalBackend *backend,
                                  EDataCal *cal,
                                  guint32 context,
                                  GCancellable *cancellable,
                                  const GSList *ids,
                                  ECalObjModType mod)
{
	GError *error = NULL;
	const ECalComponentId *id;

	if (!ids) {
		if (context) {
			g_propagate_error (&error, EDC_ERROR (InvalidArg));
			e_data_cal_respond_remove_objects (cal, context, error, NULL, NULL, NULL);
		}
		return;
	}

	if (ids->next) {
		if (context) {
			g_propagate_error (&error, EDC_ERROR_EX (UnsupportedMethod, _("EWS does not support bulk removals")));
			e_data_cal_respond_remove_objects (cal, context, error, NULL, NULL, NULL);
		}
		return;
	}

	id = ids->data;
	if (!id) {
		if (context) {
			g_propagate_error (&error, EDC_ERROR (InvalidArg));
			e_data_cal_respond_remove_objects (cal, context, error, NULL, NULL, NULL);
		}
		return;
	}

	e_cal_backend_ews_remove_object (backend, cal, context, cancellable, id->uid, id->rid, mod);
}

static icaltimezone * resolve_tzid (const gchar *tzid, gpointer user_data);
static void put_component_to_store (ECalBackendEws *cbews,ECalComponent *comp);

static void
e_cal_backend_ews_modify_object (ECalBackend *backend,
                                 EDataCal *cal,
                                 guint32 context,
                                 GCancellable *cancellable,
                                 const gchar *calobj,
                                 ECalObjModType mod);

static void ews_cal_modify_object_cb (GObject *object,
                                 GAsyncResult *res,
                                 gpointer user_data);

static void
ews_create_attachments_cb (GObject *object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCalendarAsyncData *create_data = user_data;
	ECalBackendEwsPrivate *priv = create_data->cbews->priv;
	gchar *change_key;
	GSList *ids, *i;
	GError *error = NULL;
	icalproperty *icalprop;
	icalcomponent *icalcomp;
	icalparameter *icalparam;
	const gchar *comp_uid;

	if (!e_ews_connection_create_attachments_finish (cnc, &change_key, &ids, res, &error)) {
		g_warning ("Error while creating attachments: %s\n", error ? error->message : "Unknown error");
		if (error != NULL)
			g_clear_error (&error);

		e_cal_backend_ews_async_data_free (create_data);

		return;
	}

	/* get exclusive access to the store */
	e_cal_backend_store_freeze_changes (priv->store);

	/* Update change key. id remains the same, but change key changed.*/
	icalcomp = e_cal_component_get_icalcomponent (create_data->comp);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		const gchar *x_name;
		x_name = icalproperty_get_x_name (icalprop);
		if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY")) {
			icalproperty_set_value_from_string (icalprop, change_key, "NO");
			break;
		}
		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	i = ids;
	for (; i && icalprop; i = i->next, icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
		icalparam = icalparameter_new_x (i->data);
		icalparameter_set_xname (icalparam, "X-EWS-ATTACHMENTID");
		icalproperty_add_parameter (icalprop, icalparam);
		g_free (i->data);
	}

	e_cal_component_commit_sequence (create_data->comp);
	/* update changes and release access to the store */
	e_cal_backend_store_thaw_changes (priv->store);

	e_cal_component_get_uid (create_data->comp, &comp_uid);
	if (create_data->cb_type == E_EWS_ATTACHMENT_TYPE_UPDATE) {
		const gchar *send_meeting_invitations;
		const gchar *send_or_save;
		EwsCalendarAsyncData *modify_data;
		EwsCalendarConvertData convert_data = { 0 };

		modify_data = g_new0 (EwsCalendarAsyncData, 1);
		modify_data->cbews = g_object_ref (create_data->cbews);
		modify_data->comp = g_object_ref (create_data->comp);
		modify_data->extra_comp = g_object_ref (create_data->extra_comp);
		modify_data->cal = g_object_ref (create_data->cal);
		modify_data->context = create_data->context;
		modify_data->item_id = g_strdup (create_data->item_id);

		convert_data.connection = create_data->cbews->priv->cnc;
		convert_data.user_email = create_data->cbews->priv->user_email;
		convert_data.comp = create_data->comp;
		convert_data.old_comp = create_data->extra_comp;
		convert_data.item_id = create_data->item_id;
		convert_data.change_key = change_key;
		convert_data.default_zone = create_data->cbews->priv->default_zone;

		if (e_cal_component_has_attendees (create_data->comp)) {
			send_meeting_invitations = "SendToAllAndSaveCopy";
			send_or_save = "SendAndSaveCopy";
		} else {
			/*In case of appointment we have to set SendMeetingInvites to SendToNone */
			send_meeting_invitations = "SendToNone";
			send_or_save = "SaveOnly";
		}

		e_ews_connection_update_items (
			priv->cnc, EWS_PRIORITY_MEDIUM,
			"AlwaysOverwrite",
			send_or_save,
			send_meeting_invitations,
			priv->folder_id,
			e_cal_backend_ews_convert_component_to_updatexml,
			&convert_data,
			priv->cancellable,
			ews_cal_modify_object_cb,
			modify_data);
	} else {
		if (create_data->cb_type == E_EWS_ATTACHMENT_TYPE_CREATE) {
			/*In case we have attendees we have to fake update items,
			* this is the only way to pass attachments in meeting invite mail*/
			if (e_cal_component_has_attendees (create_data->comp)) {
				icalcomponent *icalcomp = e_cal_component_get_icalcomponent (create_data->comp);
				e_cal_backend_ews_modify_object (
					E_CAL_BACKEND (create_data->cbews),
					create_data->cal,
					0,
					NULL,
					icalcomponent_as_ical_string (icalcomp),
					E_CAL_OBJ_MOD_ALL);
			}
		}
	}

	e_cal_backend_ews_async_data_free (create_data);
	g_slist_free (ids);
}

static void
ews_create_object_cb (GObject *object,
                      GAsyncResult *res,
                      gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCalendarAsyncData *create_data = user_data;
	ECalBackendEws *cbews = create_data->cbews;
	ECalBackendEwsPrivate *priv = cbews->priv;
	GError *error = NULL;
	GSList *ids = NULL, *attachments = NULL, *i, *exceptions = NULL, *items_req = NULL, *items = NULL;
	GSList *new_uids, *new_comps;
	const gchar *comp_uid;
	const EwsId *item_id;
	icalproperty *icalprop;
	icalcomponent *icalcomp;
	guint n_attach;
	EEwsItem *item;

	/* get a list of ids from server (single item) */
	if (!e_ews_connection_create_items_finish (cnc, res, &ids, &error)) {
		if (error != NULL) {
			convert_error_to_edc_error (&error);
			e_data_cal_respond_create_objects (create_data->cal, create_data->context, error, NULL, NULL);
		} else {
			e_data_cal_respond_create_objects (
					create_data->cal, create_data->context, EDC_ERROR_EX (OtherError, _("Unknown error")), NULL, NULL);
		}
		return;
	}

	item = (EEwsItem *) ids->data;
	item_id = e_ews_item_get_id (item);
	g_slist_free (ids);

	if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_EVENT) {
		EEwsAdditionalProps *add_props;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup ("calendar:UID");

		items = g_slist_append (items, item_id->id);

		/* get calender uid from server*/
		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			items,
			"IdOnly",
			add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_TEXT,
			&items_req,
			NULL, NULL, priv->cancellable, &error);

		e_ews_additional_props_free (add_props);
		if (!res && error != NULL) {
			if (items_req)
				g_slist_free_full (items_req, g_object_unref);
			convert_error_to_edc_error (&error);
			e_data_cal_respond_create_objects (create_data->cal, create_data->context, error, NULL, NULL);
			return;
		}

		item = (EEwsItem *) items_req->data;
		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
			error = g_error_copy (e_ews_item_get_error (item));
			g_slist_free_full (items_req, g_object_unref);

			convert_error_to_edc_error (&error);
			e_data_cal_respond_create_objects (create_data->cal, create_data->context, error, NULL, NULL);
			return;
		}

		item_id = e_ews_item_get_id (item);

		g_slist_free (items);
		g_slist_free (items_req);
	}

	/* attachments */
	n_attach = e_cal_component_get_num_attachments (create_data->comp);
	if (n_attach > 0) {
		GSList *info_attachments = NULL;
		EwsCalendarAsyncData *attach_data = g_new0 (EwsCalendarAsyncData, 1);

		attach_data->cbews = g_object_ref (create_data->cbews);
		attach_data->comp = g_object_ref (create_data->comp);
		attach_data->cal = g_object_ref (create_data->cal);
		attach_data->context = create_data->context;
		attach_data->cb_type = 1;

		e_cal_component_get_attachment_list (create_data->comp, &attachments);

		for (i = attachments; i; i = i->next) {
			EEwsAttachmentInfo *info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_URI);
			e_ews_attachment_info_set_uri (info, i->data);
			info_attachments = g_slist_append (info_attachments, info);
		}

		e_ews_connection_create_attachments (
			cnc, EWS_PRIORITY_MEDIUM,
			item_id, info_attachments,
			FALSE, priv->cancellable,
			ews_create_attachments_cb,
			attach_data);

		g_slist_free_full (info_attachments, (GDestroyNotify) e_ews_attachment_info_free);
		g_slist_free_full (attachments, g_free);
	}

	/* get exclusive access to the store */
	e_cal_backend_store_freeze_changes (priv->store);

	/* set a new ical property containing the change key we got from the exchange server for future use */
	if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_EVENT)
		e_cal_component_set_uid (create_data->comp, e_ews_item_get_uid (item));
	else
		e_cal_component_set_uid (create_data->comp, item_id->id);

	icalprop = icalproperty_new_x (item_id->id);
	icalproperty_set_x_name (icalprop, "X-EVOLUTION-ITEMID");
	icalcomp = e_cal_component_get_icalcomponent (create_data->comp);
	icalcomponent_add_property (icalcomp, icalprop);

	icalprop = icalproperty_new_x (item_id->change_key);
	icalproperty_set_x_name (icalprop, "X-EVOLUTION-CHANGEKEY");
	icalcomp = e_cal_component_get_icalcomponent (create_data->comp);
	icalcomponent_add_property (icalcomp, icalprop);

	/* update component internal data */
	e_cal_component_commit_sequence (create_data->comp);
	put_component_to_store (create_data->cbews, create_data->comp);

	e_cal_component_get_uid (create_data->comp, &comp_uid);

	new_uids = g_slist_append (NULL, (gpointer) comp_uid);
	new_comps = g_slist_append (NULL, create_data->comp);

	convert_error_to_edc_error (&error);
	e_data_cal_respond_create_objects (create_data->cal, create_data->context, error, new_uids, new_comps);
	error = NULL;

	g_slist_free (new_uids);
	g_slist_free (new_comps);

	/* notify the backend and the application that a new object was created */
	e_cal_backend_notify_component_created (E_CAL_BACKEND (create_data->cbews), create_data->comp);

	/* place new component in our cache */
	PRIV_LOCK (priv);
	g_hash_table_insert (priv->item_id_hash, g_strdup (item_id->id), g_object_ref (create_data->comp));
	PRIV_UNLOCK (priv);

	/* update changes and release access to the store */
	e_cal_backend_store_thaw_changes (priv->store);

	/* Excluded occurrences */
	g_clear_error (&error);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	if (icalprop != NULL) {
		icalprop = icalcomponent_get_first_property (icalcomp, ICAL_EXDATE_PROPERTY);
		for (; icalprop; icalprop = icalcomponent_get_next_property (icalcomp, ICAL_EXDATE_PROPERTY)) {
			exceptions = g_slist_prepend (exceptions, g_strdup (icalproperty_get_value_as_string (icalprop)));
		}

		for (i = exceptions; i; i = i->next) {
			e_cal_backend_ews_remove_object (
				E_CAL_BACKEND (create_data->cbews), create_data->cal, 0, NULL,
				comp_uid, i->data, E_CAL_OBJ_MOD_THIS);
		}

		g_slist_free_full (exceptions, g_free);
	}

	e_cal_backend_ews_async_data_free (create_data);
}

struct TzidCbData {
	icalcomponent *comp;
	ECalBackendEws *cbews;
};

static void tzid_cb (icalparameter *param, gpointer data)
{
	struct TzidCbData *cbd = data;
	const gchar *tzid;
	icaltimezone *zone;
	icalcomponent *new_comp;

	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	zone = resolve_tzid (tzid, cbd->cbews);
	if (!zone)
		return;

	new_comp = icaltimezone_get_component (zone);
	if (!new_comp)
		return;

	icalcomponent_add_component (cbd->comp, icalcomponent_new_clone (new_comp));
}

static void
e_cal_backend_ews_pick_all_tzids_out (ECalBackendEws *cbews,
				      icalcomponent *icalcomp)
{

	/* pick all the tzids out of the component and resolve
	 * them using the vtimezones in the current calendar */
	struct TzidCbData cbd;

	cbd.cbews = cbews;
	cbd.comp = icalcomp;
	icalcomponent_foreach_tzid (icalcomp, tzid_cb, &cbd);
}

static void
e_cal_backend_ews_create_objects (ECalBackend *backend,
                                  EDataCal *cal,
                                  guint32 context,
                                  GCancellable *cancellable,
                                  const GSList *calobjs)
{
	EwsCalendarAsyncData *create_data;
	EwsCalendarConvertData convert_data = { 0 };
	EwsFolderId *fid;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	icalcomponent *icalcomp;
	ECalComponent *comp = NULL;
	struct icaltimetype current;
	GError *error = NULL;
	const gchar *send_meeting_invitations, *calobj;

	/* sanity check */
	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (backend), InvalidArg);
	e_data_cal_error_if_fail (calobjs != NULL, InvalidArg);

	if (calobjs->next) {
		g_propagate_error (&error, EDC_ERROR_EX (UnsupportedMethod, _("EWS does not support bulk additions")));
		goto exit;
	}

	calobj = calobjs->data;
	e_data_cal_error_if_fail (calobj != NULL && *calobj != '\0', InvalidArg);

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));

	/* make sure we're not offline */
	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
		goto exit;
	}

	if (!cal_backend_ews_ensure_connected (cbews, cancellable, &error)) {
		goto exit;
	}

	/* parse ical data */
	comp = e_cal_component_new_from_string (calobj);
	if (comp == NULL) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}
	icalcomp = e_cal_component_get_icalcomponent (comp);

	/* make sure data was parsed properly */
	if (!icalcomp) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	/* make sure ical data we parse is actually an ical component */
	if (kind != icalcomponent_isa (icalcomp)) {
		icalcomponent_free (icalcomp);
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	e_ews_clean_icalcomponent (icalcomp);

	if (!e_ews_connection_satisfies_server_version (cbews->priv->cnc, E_EWS_EXCHANGE_2010))
		e_cal_backend_ews_pick_all_tzids_out (cbews, icalcomp);

	/* prepare new calender component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	create_data = g_new0 (EwsCalendarAsyncData, 1);
	create_data->cbews = g_object_ref (cbews);
	create_data->comp = g_object_ref (comp);
	create_data->cal = g_object_ref (cal);
	create_data->context = context;

	convert_data.connection = cbews->priv->cnc;
	convert_data.icalcomp = icalcomp;
	convert_data.default_zone = cbews->priv->default_zone;

	/*
	 * In case we are creating a meeting with attendees and attachments.
	 * We have to preform 3 steps in order to allow attendees to receive attachments in their invite mails.
	 * 1. create meeting and do not send invites
	 * 2. create attachments
	 * 3. dummy update meeting and send invites to all
	 */
	if (e_cal_component_has_attendees (comp)) {
		if (e_cal_component_has_attachments (comp))
			send_meeting_invitations = "SendToNone";
		else
			send_meeting_invitations = "SendToAllAndSaveCopy";
	} else {
		/*In case of appointment we have to set SendMeetingInvites to SendToNone */
		send_meeting_invitations = "SendToNone";
	}

	fid = e_ews_folder_id_new (priv->folder_id, NULL, FALSE);

	e_ews_connection_create_items (
		priv->cnc,
		EWS_PRIORITY_MEDIUM,
		"SaveOnly",
		send_meeting_invitations,
		fid,
		e_cal_backend_ews_convert_calcomp_to_xml,
		&convert_data,
		cancellable,
		ews_create_object_cb,
		create_data);

	e_ews_folder_id_free (fid);

	return;

exit:
	if (comp != NULL)
		g_object_unref (comp);

	convert_error_to_edc_error (&error);
	e_data_cal_respond_create_objects (cal, context, error, NULL, NULL);
}

static void
ews_cal_modify_object_cb (GObject *object,
                          GAsyncResult *res,
                          gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCalendarAsyncData *modify_data = user_data;
	ECalBackendEws *cbews = modify_data->cbews;
	ECalBackendEwsPrivate *priv = cbews->priv;
	GError *error = NULL;
	GSList *ids = NULL;
	const EwsId *item_id;
	icalproperty *icalprop = NULL;
	icalcomponent *icalcomp;
	ECalComponentId *id = NULL;
	const gchar *x_name;

	if (!e_ews_connection_update_items_finish (cnc, res, &ids, &error)) {
		convert_error_to_edc_error (&error);
		if (modify_data->context)
			e_data_cal_respond_modify_objects (modify_data->cal, modify_data->context, error, NULL, NULL);
		else if (error != NULL) {
			g_warning ("Modify object error :  %s\n", error->message);
			g_clear_error (&error);
		}

		goto exit;
	}

	e_cal_backend_store_freeze_changes (priv->store);

	item_id = e_ews_item_get_id ((EEwsItem *) ids->data);

	/* Update change key. id remains the same, but change key changed.*/
	icalcomp = e_cal_component_get_icalcomponent (modify_data->comp);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		x_name = icalproperty_get_x_name (icalprop);
		if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY")) {
			icalproperty_set_value_from_string (icalprop, item_id->change_key, "NO");
			break;
		}
		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	e_cal_component_commit_sequence (modify_data->comp);
	id = e_cal_component_get_id (modify_data->comp);
	e_cal_backend_store_remove_component (cbews->priv->store, id->uid, id->rid);
	put_component_to_store (cbews, modify_data->comp);

	if (modify_data->context) {
		GSList *old_components, *new_components;

		e_cal_backend_notify_component_modified (E_CAL_BACKEND (cbews), modify_data->extra_comp, modify_data->comp);

		old_components = g_slist_append (NULL, modify_data->extra_comp);
		new_components = g_slist_append (NULL, modify_data->comp);

		e_data_cal_respond_modify_objects (modify_data->cal, modify_data->context, NULL, old_components, new_components);

		g_slist_free (old_components);
		g_slist_free (new_components);
	}

	ews_start_sync (cbews);

	PRIV_LOCK (priv);
	g_hash_table_replace (priv->item_id_hash, g_strdup (modify_data->item_id), g_object_ref (modify_data->comp));
	PRIV_UNLOCK (priv);

	e_cal_backend_store_thaw_changes (priv->store);

	icalproperty_free (icalprop);
	e_cal_component_free_id (id);

exit:
	e_cal_backend_ews_async_data_free (modify_data);
}

static void
e_cal_backend_ews_modify_objects (ECalBackend *backend,
                                  EDataCal *cal,
                                  guint32 context,
                                  GCancellable *cancellable,
                                  const GSList *calobjs,
                                  ECalObjModType mod)
{
	GError *error = NULL;

	if (!calobjs) {
		if (context) {
			g_propagate_error (&error, EDC_ERROR (InvalidArg));
			e_data_cal_respond_modify_objects (cal, context, error, NULL, NULL);
		}
		return;
	}

	if (calobjs->next) {
		if (context) {
			g_propagate_error (&error, EDC_ERROR_EX (UnsupportedMethod, _("EWS does not support bulk modifications")));
			e_data_cal_respond_modify_objects (cal, context, error, NULL, NULL);
		}
		return;
	}

	e_cal_backend_ews_modify_object (backend, cal, context, cancellable, calobjs->data, mod);
}

static void
e_cal_backend_ews_modify_object (ECalBackend *backend,
                                 EDataCal *cal,
                                 guint32 context,
                                 GCancellable *cancellable,
                                 const gchar *calobj,
                                 ECalObjModType mod)
{
	EwsCalendarAsyncData *modify_data;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	ECalComponent *comp = NULL, *oldcomp = NULL;
	icalcomponent *icalcomp;
	gchar *itemid = NULL, *changekey = NULL;
	struct icaltimetype current;
	GError *error = NULL;
	GSList *original_attachments = NULL, *modified_attachments = NULL, *added_attachments = NULL, *removed_attachments = NULL, *removed_attachments_ids = NULL, *i;
	EwsCalendarAsyncData *attach_data;

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (backend), InvalidArg);
	e_data_cal_error_if_fail (calobj != NULL && *calobj != '\0', InvalidArg);

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));

	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
		goto exit;
	}

	if (!cal_backend_ews_ensure_connected (cbews, cancellable, &error)) {
		goto exit;
	}

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}
	if (kind != icalcomponent_isa (icalcomp)) {
		icalcomponent_free (icalcomp);
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	e_cal_backend_ews_pick_all_tzids_out (cbews, icalcomp);

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &current);

	ews_cal_component_get_item_id (comp, &itemid, &changekey);
	if (!itemid) {
		g_propagate_error (
			&error, EDC_ERROR_EX (OtherError,
			"Cannot determine EWS ItemId"));
		goto exit;
	}

	PRIV_LOCK (priv);
	oldcomp = g_hash_table_lookup (priv->item_id_hash, itemid);
	if (!oldcomp) {
		g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
		PRIV_UNLOCK (priv);
		goto exit;
	}
	g_object_ref (oldcomp);
	PRIV_UNLOCK (priv);

	e_cal_backend_ews_pick_all_tzids_out (cbews, e_cal_component_get_icalcomponent (oldcomp));

	/*In case we have updated attachments we have to run update attachments
	 *before update items so attendees will receive mails with already updated attachments */

	e_cal_component_get_attachment_list (oldcomp, &original_attachments);
	e_cal_component_get_attachment_list (comp, &modified_attachments);

	ewscal_get_attach_differences (original_attachments, modified_attachments, &removed_attachments, &added_attachments);
	g_slist_free (original_attachments);
	g_slist_free (modified_attachments);

	/* preform sync delete attachemnt operation*/
	if (removed_attachments) {
		icalproperty *icalprop;
		GSList *items;

		/* convert attachment uri to attachment id, should have used a hash table somehow */
		icalcomp = e_cal_component_get_icalcomponent (oldcomp);
		icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
		while (icalprop) {
			const gchar *attachment_url = icalproperty_get_value_as_string (icalprop);

			for (items = removed_attachments; items; items = items->next) {
				if (g_strcmp0 (attachment_url, items->data) == 0) {
					break;
				}
			}

			/* not NULL means the attachment was found in removed attachments */
			if (items != NULL)
				removed_attachments_ids = g_slist_append (removed_attachments_ids, icalproperty_get_parameter_as_string_r (icalprop, "X-EWS-ATTACHMENTID"));

			icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY);
		}

		items = NULL;

		if (removed_attachments_ids) {
			if (e_ews_connection_delete_attachments_sync (
				priv->cnc, EWS_PRIORITY_MEDIUM,
				removed_attachments_ids, &items, cancellable, &error) && items)
				changekey = items->data;
		}

		g_slist_free_full (removed_attachments_ids, g_free);
		g_slist_free (removed_attachments);
	}

	/*in case we have a new attachmetns the update item will be preformed in ews_create_attachments_cb*/
	if (added_attachments) {
		const gchar *old_uid = NULL;
		gint old_uid_len = 0;
		GSList *info_attachments = NULL;
		EwsId *item_id = g_new0 (EwsId, 1);
		item_id->id = itemid;
		item_id->change_key = changekey;
		attach_data = g_new0 (EwsCalendarAsyncData, 1);

		attach_data->cbews = g_object_ref (cbews);
		attach_data->comp = g_object_ref (comp);
		attach_data->cb_type = E_EWS_ATTACHMENT_TYPE_UPDATE;
		attach_data->extra_comp = g_object_ref (oldcomp);
		attach_data->cal = g_object_ref (cal);
		attach_data->context = 0;
		attach_data->item_id = itemid;

		e_cal_component_get_uid (oldcomp, &old_uid);
		if (old_uid)
			old_uid_len = strlen (old_uid);

		for (i = added_attachments; i; i = i->next) {
			EEwsAttachmentInfo *info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_URI);

			e_ews_attachment_info_set_uri (info, i->data);

			if (old_uid) {
				gchar *filename = g_filename_from_uri (i->data, NULL, NULL);
				if (filename) {
					const gchar *slash = strrchr (filename, G_DIR_SEPARATOR);
					if (slash && g_str_has_prefix (slash + 1, old_uid) &&
					    slash[1 + old_uid_len] == '-') {
						e_ews_attachment_info_set_prefer_filename (info, slash + 1 + old_uid_len + 1);
					}

					g_free (filename);
				}
			}

			info_attachments = g_slist_append (info_attachments, info);
		}

		if (context) {
			convert_error_to_edc_error (&error);
			e_data_cal_respond_modify_objects (cal, context, error, NULL, NULL);
		}

		e_ews_connection_create_attachments (
			priv->cnc, EWS_PRIORITY_MEDIUM,
			item_id, info_attachments,
			FALSE, cancellable,
			ews_create_attachments_cb,
			attach_data);

		g_slist_free_full (info_attachments, (GDestroyNotify) e_ews_attachment_info_free);
		g_slist_free (added_attachments);
		g_free (item_id);

	} else {
		EwsCalendarConvertData convert_data = { 0 };
		const gchar *send_meeting_invitations;
		const gchar *send_or_save;

		modify_data = g_new0 (EwsCalendarAsyncData, 1);
		modify_data->cbews = g_object_ref (cbews);
		modify_data->comp = g_object_ref (comp);
		modify_data->extra_comp = g_object_ref (oldcomp);
		modify_data->cal = g_object_ref (cal);
		modify_data->context = context;
		modify_data->item_id = itemid;

		convert_data.connection = cbews->priv->cnc;
		convert_data.user_email = cbews->priv->user_email;
		convert_data.comp = comp;
		convert_data.old_comp = oldcomp;
		convert_data.item_id = itemid;
		convert_data.change_key = changekey;
		convert_data.default_zone = cbews->priv->default_zone;

		if (e_cal_component_has_attendees (comp)) {
			send_meeting_invitations = "SendToAllAndSaveCopy";
			send_or_save = "SendAndSaveCopy";
		} else {
			/*In case of appointment we have to set SendMeetingInvites to SendToNone */
			send_meeting_invitations = "SendToNone";
			send_or_save = "SaveOnly";
		}

		e_ews_connection_update_items (
			priv->cnc, EWS_PRIORITY_MEDIUM,
			"AlwaysOverwrite",
			send_or_save,
			send_meeting_invitations,
			priv->folder_id,
			e_cal_backend_ews_convert_component_to_updatexml,
			&convert_data,
			cancellable,
			ews_cal_modify_object_cb,
			modify_data);
	}

	g_clear_object (&oldcomp);

	return;

exit:
	g_clear_object (&oldcomp);
	g_clear_object (&comp);

	convert_error_to_edc_error (&error);
	if (context)
		e_data_cal_respond_modify_objects (cal, context, error, NULL, NULL);
	else if (error) {
		g_warning ("Modify object error :  %s\n", error->message);
		g_clear_error (&error);
	}
}

static void
e_ews_receive_objects_no_exchange_mail (ECalBackendEws *cbews,
                                        icalcomponent *subcomp,
                                        GSList **ids,
                                        GCancellable *cancellable,
                                        GError **error)
{
	EwsCalendarConvertData convert_data = { 0 };
	EwsFolderId *fid;

	convert_data.connection = cbews->priv->cnc;
	convert_data.icalcomp = subcomp;
	convert_data.default_zone = cbews->priv->default_zone;

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

static const gchar *
e_ews_get_current_user_meeting_reponse (ECalBackendEws *cbews,
					icalcomponent *icalcomp,
                                        const gchar *current_user_mail)
{
	icalproperty *attendee;
	const gchar *attendee_str = NULL, *attendee_mail = NULL;
	gint attendees_count = 0;
	const gchar *response = NULL;

	for (attendee = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
		attendee != NULL;
		attendee = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY), attendees_count++) {
		attendee_str = icalproperty_get_attendee (attendee);

		if (attendee_str != NULL) {
			if (!strncasecmp (attendee_str, "MAILTO:", 7))
				attendee_mail = attendee_str + 7;
			else
				attendee_mail = attendee_str;
			if (attendee_mail && current_user_mail && g_ascii_strcasecmp (attendee_mail, current_user_mail) == 0)
				return icalproperty_get_parameter_as_string (attendee, "PARTSTAT");
		}
	}

	/* this should not happen, but if the user's configured email does not match the one
	   used in the invitation, like when the invitation comes to a mailing list... */
	if (attendees_count == 1) {
		attendee = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
		g_return_val_if_fail (attendee != NULL, NULL);

		return icalproperty_get_parameter_as_string (attendee, "PARTSTAT");
	} else {
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

			if (attendee)
				response = icalproperty_get_parameter_as_string (attendee, "PARTSTAT");

			g_free (my_address);
		}

		g_object_unref (comp);
	}

	return response;
}

static void
ews_cal_do_method_request_publish_reply (ECalBackendEws *cbews,
					 ECalComponent *comp,
					 icalcomponent *subcomp,
					 const gchar *response_type,
					 GCancellable *cancellable,
					 GError **error)
{
	GError *local_error = NULL;
	gchar *item_id = NULL;
	gchar *change_key = NULL;
	gchar *mail_id = NULL;
	gint pass = 0;
	GSList *ids = NULL;

	if (!response_type) {
		g_propagate_error (error, EDC_ERROR (UnknownUser));
		return;
	}

	ews_cal_component_get_calendar_item_accept_id (comp, &item_id, &change_key, &mail_id);

	while (pass < 2) {
		/*in case we do not have item id we will create item with mime content only*/
		if (item_id == NULL) {
			e_ews_receive_objects_no_exchange_mail (cbews, subcomp, &ids, cancellable, &local_error);
		} else {
			EwsCalendarConvertData convert_data = { 0 };

			convert_data.response_type = (gchar *) response_type;
			convert_data.item_id = item_id;
			convert_data.change_key = change_key;

			e_ews_connection_create_items_sync (
				cbews->priv->cnc,
				EWS_PRIORITY_MEDIUM,
				"SendAndSaveCopy",
				NULL,
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

	/*We have to run sync before any other operations */
	ews_start_sync (cbews);
}

static void
e_cal_backend_ews_receive_objects (ECalBackend *backend,
                                   EDataCal *cal,
                                   guint32 context,
                                   GCancellable *cancellable,
                                   const gchar *calobj)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	icalcomponent *icalcomp, *subcomp;
	GError *error = NULL;
	icalproperty_method method;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	/* make sure we're not offline */
	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
		goto exit;
	}

	if (!cal_backend_ews_ensure_connected (cbews, cancellable, &error)) {
		goto exit;
	}

	icalcomp = icalparser_parse_string (calobj);

	/* make sure data was parsed properly */
	if (!icalcomp) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	/* make sure ical data we parse is actually an vcal component */
	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	method = icalcomponent_get_method (icalcomp);
	subcomp = icalcomponent_get_first_component (icalcomp, kind);

	while (subcomp) {
		ECalComponent *comp = e_cal_component_new ();
		const gchar *response_type;

		/* duplicate the ical component */
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp));

		/*getting a data for meeting request response*/
		response_type = e_ews_get_current_user_meeting_reponse (cbews,
			e_cal_component_get_icalcomponent (comp),
			priv->user_email);

		switch (method) {
			case ICAL_METHOD_REQUEST:
			case ICAL_METHOD_PUBLISH:
			case ICAL_METHOD_REPLY:
				ews_cal_do_method_request_publish_reply (
					cbews,
					comp,
					subcomp,
					response_type,
					cancellable,
					&error);
				break;
			case ICAL_METHOD_CANCEL: {
				const gchar *uid = NULL;
				gchar *rid = NULL;
				ECalObjModType mod;

				e_cal_component_get_uid (comp, &uid);
				rid = e_cal_component_get_recurid_as_string (comp);
				mod = e_cal_component_is_instance (comp) ? E_CAL_OBJ_MOD_THIS : E_CAL_OBJ_MOD_ALL;

				e_cal_backend_ews_remove_object (backend, cal, 0, cancellable, uid, rid, mod);
				g_free (rid);
				break;
			}
			case ICAL_METHOD_COUNTER:
				/*
				 * this is a new time proposal mail from one of the attendees
				 * if we decline the proposal, nothing have to be done
				 * if we accept it we will call to modify_object
				 */
				if (g_strcmp0 (response_type, "ACCEPTED") == 0) {
					gchar **split_subject;
					icalproperty *summary;

					/*
					 * we have to edit the meeting subject to remove exchange header
					 */
					summary = icalcomponent_get_first_property (subcomp, ICAL_SUMMARY_PROPERTY);
					split_subject =
						g_strsplit (icalproperty_get_value_as_string (summary), ":", -1);
					icalproperty_set_value_from_string (summary, split_subject[1] , "NO");
					g_strfreev (split_subject);

					e_cal_backend_ews_modify_object (backend, cal, 0, cancellable, icalcomponent_as_ical_string (subcomp), E_CAL_OBJ_MOD_ALL);
				}
				break;
			default:
				break;
		}

		g_object_unref (comp);
		subcomp = icalcomponent_get_next_component (icalcomp, kind);
	}

	icalcomponent_free (icalcomp);

exit:
	convert_error_to_edc_error (&error);
	e_data_cal_respond_receive_objects (cal, context, error);
}

static const gchar *
e_cal_get_meeting_cancellation_comment (ECalComponent *comp)
{
	icalproperty *prop;
	prop = icalcomponent_get_first_property (
		e_cal_component_get_icalcomponent (comp),
		ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name, *x_val;
		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-RETRACT-COMMENT"))
			return x_val;

		prop = icalcomponent_get_next_property (
			e_cal_component_get_icalcomponent (comp),
			ICAL_X_PROPERTY);
	}

	return NULL;
}

static void
ewscal_send_cancellation_email (ECalBackend *backend,
                                EEwsConnection *cnc,
                                CamelAddress *from,
                                CamelInternetAddress *recipient,
                                const gchar *subject,
                                const gchar *body,
                                const gchar *calobj)
{
	CamelMimeMessage *message;
	GError *error = NULL;
	CamelMultipart *multi;
	CamelMimePart *text_part, *vcal_part;
	gchar *ical_str;
	icalcomponent *vcal, *vevent, *vtz;
	icalproperty *prop;
	icaltimezone *icaltz;
	struct icaltimetype dt;

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
	dt = icalcomponent_get_dtstart (vevent);
	icaltz = (icaltimezone *)
		(dt.zone ? dt.zone : e_cal_backend_ews_get_timezone_from_ical_component (backend, vevent));
	vtz = icaltimezone_get_component (icaltz);
	icalcomponent_add_component (vcal, icalcomponent_new_clone (vtz));
	icalcomponent_add_component (vcal, vevent);
	text_part = camel_mime_part_new ();
	camel_mime_part_set_content (text_part, body, strlen (body), "text/plain");

	vcal_part = camel_mime_part_new ();
	camel_content_type_set_param (CAMEL_DATA_WRAPPER (vcal_part)->mime_type, "charset", "utf-8");
	camel_content_type_set_param (CAMEL_DATA_WRAPPER (vcal_part)->mime_type, "method", "CANCEL");
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

	camel_ews_utils_create_mime_message (cnc, "SendOnly", NULL, message, NULL, from, NULL, NULL, NULL, NULL, &error);

	if (error) {
		g_warning ("Failed to send cancellation email: %s", error->message);
		g_clear_error (&error);
	}

	g_object_unref (message);
	icalcomponent_free (vcal);
}

static void
e_cal_backend_ews_send_objects (ECalBackend *backend,
                                EDataCal *cal,
                                guint32 context,
                                GCancellable *cancellable,
                                const gchar *calobj)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	icalcomponent *icalcomp, *subcomp = NULL;
	GError *error = NULL;
	gchar *subcalobj;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	/* make sure we're not offline */
	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
		goto exit;
	}

	if (!cal_backend_ews_ensure_connected (cbews, cancellable, &error)) {
		goto exit;
	}

	icalcomp = icalparser_parse_string (calobj);

	/* make sure data was parsed properly */
	if (!icalcomp) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}
	/* make sure ical data we parse is actually an vcal component */
	if ((icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) && (icalcomponent_isa (icalcomp) != ICAL_VEVENT_COMPONENT)) {
		icalcomponent_free (icalcomp);
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
		subcomp = icalcomponent_get_first_component (icalcomp, kind);
	}
	if (icalcomponent_isa (icalcomp) == ICAL_VEVENT_COMPONENT)
		subcomp = icalcomp;
	while (subcomp) {
		ECalComponent *comp = e_cal_component_new ();
		const gchar *new_body_content = NULL, *subject = NULL, *org_email = NULL;
		const gchar *org = NULL, *attendee = NULL;
		icalproperty *prop, *org_prop = NULL;
		CamelInternetAddress *org_addr = camel_internet_address_new ();

		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp));

		new_body_content = e_cal_get_meeting_cancellation_comment (comp);
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
			prop != NULL;
			prop = icalcomponent_get_next_property (subcomp, ICAL_ATTENDEE_PROPERTY)) {

			CamelInternetAddress *attendee_addr = camel_internet_address_new ();
			attendee = icalproperty_get_attendee (prop);
			if (g_ascii_strcasecmp (org_email, attendee) == 0) continue;
			if (!g_ascii_strncasecmp (attendee, "mailto:", 7)) attendee = (attendee) + 7;

			subcalobj = icalcomponent_as_ical_string_r (subcomp);
			camel_internet_address_add (attendee_addr, icalproperty_get_parameter_as_string (prop, "CN"), attendee);
			ewscal_send_cancellation_email (backend, priv->cnc, CAMEL_ADDRESS (org_addr), attendee_addr, subject, new_body_content, subcalobj);
			g_object_unref (attendee_addr);
			free (subcalobj);
		}

		g_object_unref (org_addr);
		g_object_unref (comp);
		subcomp = icalcomponent_get_next_component (icalcomp, kind);
	}

	icalcomponent_free (icalcomp);

exit:
	convert_error_to_edc_error (&error);
	e_data_cal_respond_send_objects (cal, context, error,  NULL, calobj);
}

/* TODO Do not replicate this in every backend */
static icaltimezone *
resolve_tzid (const gchar *tzid,
              gpointer user_data)
{
	ETimezoneCache *timezone_cache;

	timezone_cache = E_TIMEZONE_CACHE (user_data);

	return e_timezone_cache_get_timezone (timezone_cache, tzid);
}

static void
put_component_to_store (ECalBackendEws *cbews,
                        ECalComponent *comp)
{
	time_t time_start, time_end;
	ECalBackendEwsPrivate *priv;

	priv = cbews->priv;

	e_cal_util_get_component_occur_times (
		comp, &time_start, &time_end,
		resolve_tzid, cbews, priv->default_zone,
		e_cal_backend_get_kind (E_CAL_BACKEND (cbews)));

	e_cal_backend_store_put_component_with_time_range (priv->store, comp, time_start, time_end);
}

static void
ews_get_attachments (ECalBackendEws *cbews,
                     EEwsItem *item)
{
	gboolean has_attachment = FALSE;
	const GSList *attachment_ids, *aid, *l;
	const EwsId *item_id;
	ECalComponent *comp;
	const gchar *uid;
	GSList *uris = NULL, *info_attachments = NULL;

	e_ews_item_has_attachments (item, &has_attachment);
	if (!has_attachment)
		return;

	item_id = e_ews_item_get_id (item);
	g_return_if_fail (item_id != NULL);

	PRIV_LOCK (cbews->priv);
	comp = g_hash_table_lookup (cbews->priv->item_id_hash, item_id->id);
	if (!comp) {
		PRIV_UNLOCK (cbews->priv);
		g_warning ("%s: Failed to get component from item_id_hash", G_STRFUNC);
		return;
	}

	e_cal_component_get_uid (comp, &uid);

	attachment_ids = e_ews_item_get_attachments_ids (item);
	if (e_ews_connection_get_attachments_sync (
		cbews->priv->cnc,
		EWS_PRIORITY_MEDIUM,
		uid,
		attachment_ids,
		cbews->priv->storage_path,
		TRUE,
		&info_attachments,
		NULL, NULL,
		cbews->priv->cancellable,
		NULL)) {
		icalcomponent *icalcomp;
		icalproperty *icalprop;
		icalparameter *icalparam;
		ECalComponentId *id;
		ECalComponent *cache_comp;

		for (l = info_attachments; l; l = l->next) {
			EEwsAttachmentInfo *info = l->data;

			/* ignore non-uri attachments, because it's an exception */
			if (e_ews_attachment_info_get_type (info) == E_EWS_ATTACHMENT_INFO_TYPE_URI) {
				const gchar *uri = e_ews_attachment_info_get_uri (info);

				if (uri)
					uris = g_slist_append (uris, g_strdup (uri));
			}
		}

		e_cal_component_set_attachment_list (comp, uris);

		icalcomp = e_cal_component_get_icalcomponent (comp);
		icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
		for (aid = attachment_ids; aid && icalprop; aid = aid->next, icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
			icalparam = icalparameter_new_x (aid->data);
			icalparameter_set_xname (icalparam, "X-EWS-ATTACHMENTID");
			icalproperty_add_parameter (icalprop, icalparam);
		}

		id = e_cal_component_get_id (comp);
		if (!id) {
			g_warn_if_reached ();
		} else {
			cache_comp = e_cal_backend_store_get_component (cbews->priv->store, id->uid, id->rid);
			e_cal_component_free_id (id);

			put_component_to_store (cbews, comp);

			if (cache_comp)
				e_cal_backend_notify_component_modified (E_CAL_BACKEND (cbews), cache_comp, comp);
		}

		g_slist_free_full (uris, g_free);
		g_slist_free_full (info_attachments, (GDestroyNotify) e_ews_attachment_info_free);
	}

	PRIV_UNLOCK (cbews->priv);
}

static icaltimezone *
get_timezone (ETimezoneCache *timezone_cache,
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

static void
add_item_to_cache (ECalBackendEws *cbews,
                   EEwsItem *item)
{
	ECalBackendEwsPrivate *priv;
	ETimezoneCache *timezone_cache;
	icalcomponent_kind kind;
	EEwsItemType item_type;
	icalcomponent *vtimezone, *icalcomp, *vcomp;
	const gchar *mime_content;

	timezone_cache = E_TIMEZONE_CACHE (cbews);

	kind = e_cal_backend_get_kind ((ECalBackend *) cbews);
	priv = cbews->priv;

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
		created = icaltime_from_timet_with_zone (e_ews_item_get_date_created (item), 0, priv->default_zone);
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
			GError *error = NULL;
			gboolean includes_last_item;
			gchar *mailtoname;
			icalparameter *param;

			/*The task owner according to Exchange is current user, even that the task was assigned by
			 *someone else. I'm making the current user attendee and task delegator will be a task organizer */

			mailtoname = g_strdup_printf ("mailto:%s", priv->user_email);
			icalprop = icalproperty_new_attendee (mailtoname);
			g_free (mailtoname);

			param = icalparameter_new_cn (e_ews_item_get_owner (item));
			icalproperty_add_parameter (icalprop, param);
			icalcomponent_add_property (icalcomp, icalprop);

			/* get delegator mail box*/
			e_ews_connection_resolve_names_sync (
				priv->cnc, EWS_PRIORITY_MEDIUM, task_owner,
				EWS_SEARCH_AD, NULL, FALSE, &mailboxes, NULL,
				&includes_last_item, priv->cancellable, &error);

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
			const gchar *percent_complete;

			/*start date*/
			has_this_date = FALSE;
			e_ews_item_task_has_start_date (item, &has_this_date);
			if (has_this_date) {
				start_date = icaltime_from_timet_with_zone (e_ews_item_get_start_date (item), 0, priv->default_zone);
				start_date.is_date = 1;
				icalprop = icalproperty_new_dtstart (start_date);
				icalcomponent_add_property (icalcomp, icalprop);
			}

			/*status*/
			ews_task_status = e_ews_item_get_status (item);
			if (!g_strcmp0 (ews_task_status, "NotStarted") == 0) {
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
				due_date = icaltime_from_timet_with_zone (e_ews_item_get_due_date (item), 0, priv->default_zone);
				due_date.is_date = 1;
				icalprop = icalproperty_new_due (due_date);
				icalcomponent_add_property (icalcomp, icalprop);
			}

			/*complete date*/
			has_this_date = FALSE;
			e_ews_item_task_has_complete_date (item, &has_this_date);
			if (has_this_date) {
				complete_date = icaltime_from_timet_with_zone (e_ews_item_get_complete_date (item), 0, priv->default_zone);
				complete_date.is_date = 1;
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

		icalcomponent_add_component (vcomp,icalcomp);
	} else {
		struct icaltimetype dt;
		const gchar *tzid;

		mime_content = e_ews_item_get_mime_content (item);
		vcomp = icalparser_parse_string (mime_content);

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
			start_zone = get_timezone (
				timezone_cache,
				start_tzid,
				ical_start_tzid != NULL ? ical_start_tzid : start_tzid,
				evo_ews_start_tzid);
			end_zone = get_timezone (
				timezone_cache,
				end_tzid,
				ical_end_tzid != NULL ? ical_end_tzid : end_tzid,
				evo_ews_end_tzid);

			if (start_zone != NULL) {
				icalcomp = icalcomponent_get_first_component (vcomp, kind);

				icalcomponent_add_component (
					vcomp,
					icalcomponent_new_clone (icaltimezone_get_component (start_zone)));

				dt = icalcomponent_get_dtstart (icalcomp);
				dt = icaltime_convert_to_zone (dt, start_zone);
				icalcomponent_set_dtstart (icalcomp, dt);

				e_timezone_cache_add_timezone (timezone_cache, start_zone);

				if (end_zone != NULL) {
					dt = icalcomponent_get_dtend (icalcomp);
					dt = icaltime_convert_to_zone (dt, end_zone);
					icalcomponent_set_dtend (icalcomp, dt);

					e_timezone_cache_add_timezone (timezone_cache, end_zone);
				}
			}
		} else {
			/*
			 * When we are working with Exchange server older than 2010, we don't set different
			 * DTSTART and DTEND properties in VTIMEZONE. The reason of that is we don't use
			 * those properties settings/changing a meeting timezone.
			 * So, for older servers, here, we only set the DTSTART and DTEND properties with
			 * the same values.
			 */
			icaltimezone *zone;

			/* Add the timezone */
			vtimezone = icalcomponent_get_first_component (vcomp, ICAL_VTIMEZONE_COMPONENT);
			if (vtimezone != NULL) {
				zone = icaltimezone_new ();
				vtimezone = icalcomponent_new_clone (vtimezone);
				icaltimezone_set_component (zone, vtimezone);
				e_timezone_cache_add_timezone (timezone_cache, zone);
				icaltimezone_free (zone, TRUE);
			}

			zone = e_timezone_cache_get_timezone (timezone_cache, tzid);

			if (zone == NULL)
				zone = icaltimezone_get_builtin_timezone (tzid);

			if (zone != NULL) {
				icalcomp = icalcomponent_get_first_component (vcomp, kind);

				icalcomponent_add_component (vcomp, icalcomponent_new_clone (icaltimezone_get_component (zone)));

				dt = icalcomponent_get_dtstart (icalcomp);
				dt = icaltime_convert_to_zone (dt, zone);
				icalcomponent_set_dtstart (icalcomp, dt);

				dt = icalcomponent_get_dtend (icalcomp);
				dt = icaltime_convert_to_zone (dt, zone);
				icalcomponent_set_dtend (icalcomp, dt);
			}
		}
	}
	/* Vevent or Vtodo */
	icalcomp = icalcomponent_get_first_component (vcomp, kind);
	if (icalcomp) {
		ECalComponent *comp, *cache_comp = NULL;
		icalproperty *icalprop, *freebusy;
		const EwsId *item_id;
		ECalComponentId *id;
		const GSList *l = NULL;
		const gchar *uid = e_ews_item_get_uid (item);

		item_id = e_ews_item_get_id (item);

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

			if (g_ascii_strcasecmp (attendee->responsetype, "Organizer") == 0)
				param = icalparameter_new_partstat (ICAL_PARTSTAT_ACCEPTED);
			else if (g_ascii_strcasecmp (attendee->responsetype, "Tentative") == 0)
				param = icalparameter_new_partstat (ICAL_PARTSTAT_TENTATIVE);
			else if (g_ascii_strcasecmp (attendee->responsetype, "Accept") == 0)
				param = icalparameter_new_partstat (ICAL_PARTSTAT_ACCEPTED);
			else if (g_ascii_strcasecmp (attendee->responsetype, "Decline") == 0)
				param = icalparameter_new_partstat (ICAL_PARTSTAT_DECLINED);
			else if (g_ascii_strcasecmp (attendee->responsetype, "NoResponseReceived") == 0)
				param = icalparameter_new_partstat (ICAL_PARTSTAT_NEEDSACTION);
			else if (g_ascii_strcasecmp (attendee->responsetype, "Unknown") == 0)
				param = icalparameter_new_partstat (ICAL_PARTSTAT_NONE);
			icalproperty_add_parameter (icalprop, param);

			icalcomponent_add_property (icalcomp, icalprop);
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

		/*AllDayEvent*/
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

		icalcomponent_set_uid (icalcomp,uid ? uid : item_id->id);

		icalprop = icalproperty_new_x (item_id->id);
		icalproperty_set_x_name (icalprop, "X-EVOLUTION-ITEMID");
		icalcomponent_add_property (icalcomp, icalprop);

		icalprop = icalproperty_new_x (item_id->change_key);
		icalproperty_set_x_name (icalprop, "X-EVOLUTION-CHANGEKEY");
		icalcomponent_add_property (icalcomp, icalprop);

		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

		/* Categories */
		e_cal_component_set_categories_list (comp, (GSList *) e_ews_item_get_categories (item));

		/*
		 * There is no API to set/get alarm description on the server side.
		 * However, for some reason, the alarm description has been set to "REMINDER"
		 * automatically (and with no i18n). Instead of show it to the user, let's
		 * set the summary as the alarm description.
		 */
		if (e_cal_component_has_alarms (comp)) {
			GList *alarm_uids, *l;

			alarm_uids = e_cal_component_get_alarm_uids (comp);
			for (l = alarm_uids; l != NULL; l = l->next) {
				ECalComponentAlarm *alarm;
				ECalComponentText text;

				alarm = e_cal_component_get_alarm (comp, l->data);
				e_cal_component_get_summary (comp, &text);
				e_cal_component_alarm_set_description (alarm, &text);

				e_cal_component_alarm_free (alarm);
			}
			cal_obj_uid_list_free (alarm_uids);
		}

		id = e_cal_component_get_id (comp);
		cache_comp = e_cal_backend_store_get_component (priv->store, id->uid, id->rid);
		e_cal_component_free_id (id);

		put_component_to_store (cbews, comp);

		if (!cache_comp) {
			e_cal_backend_notify_component_created (E_CAL_BACKEND (cbews), comp);
		} else {
			e_cal_backend_notify_component_modified (E_CAL_BACKEND (cbews), cache_comp, comp);
		}

		PRIV_LOCK (priv);
		g_hash_table_insert (priv->item_id_hash, g_strdup (item_id->id), g_object_ref (comp));
		PRIV_UNLOCK (priv);

		g_object_unref (comp);
	}
	icalcomponent_free (vcomp);
}

static void
ews_refreshing_inc (ECalBackendEws *cbews)
{
	PRIV_LOCK (cbews->priv);
	if (!cbews->priv->refreshing)
		e_flag_clear (cbews->priv->refreshing_done);
	cbews->priv->refreshing++;
	PRIV_UNLOCK (cbews->priv);
}

static void
ews_refreshing_dec (ECalBackendEws *cbews)
{
	PRIV_LOCK (cbews->priv);
	if (!cbews->priv->refreshing) {
		e_flag_set (cbews->priv->refreshing_done);
		PRIV_UNLOCK (cbews->priv);

		g_warning ("%s: Invalid call, currently not refreshing", G_STRFUNC);
		return;
	}
	cbews->priv->refreshing--;
	if (!cbews->priv->refreshing) {
		e_flag_set (cbews->priv->refreshing_done);
	}
	PRIV_UNLOCK (cbews->priv);
}

static gboolean
ews_cal_sync_get_items_sync (ECalBackendEws *cbews,
                             const GSList *item_ids,
                             const gchar *default_props,
                             const EEwsAdditionalProps *add_props)
{
	ECalBackendEwsPrivate *priv;
	gboolean ret = FALSE;
	GSList *items = NULL, *l;
	GError *error = NULL;

	priv = cbews->priv;

	e_ews_connection_get_items_sync (
		priv->cnc,
		EWS_PRIORITY_MEDIUM,
		item_ids,
		default_props,
		add_props,
		FALSE,
		NULL,
		E_EWS_BODY_TYPE_TEXT,
		&items,
		NULL, NULL,
		priv->cancellable,
		&error);

	if (error != NULL) {
		g_debug ("%s: Unable to get items: %s", G_STRFUNC, error->message);
		g_clear_error (&error);

		goto exit;
	}

	/* fetch modified occurrences */
	for (l = items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = l->data;
		const GSList *modified_occurrences;

		if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
			continue;

		modified_occurrences = e_ews_item_get_modified_occurrences (item);
		if (modified_occurrences) {
			EEwsAdditionalProps *modified_add_props;

			modified_add_props = e_ews_additional_props_new ();
			if (e_ews_connection_satisfies_server_version (priv->cnc, E_EWS_EXCHANGE_2010)) {
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

			ret = ews_cal_sync_get_items_sync (
				cbews, modified_occurrences,
				"IdOnly",
				modified_add_props);

			e_ews_additional_props_free (modified_add_props);

			if (!ret)
				goto exit;
		}
	}

	e_cal_backend_store_freeze_changes (priv->store);
	for (l = items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;

		if (!item)
			continue;

		if (e_ews_item_get_item_type (item) != E_EWS_ITEM_TYPE_ERROR) {
			add_item_to_cache (cbews, item);
			ews_get_attachments (cbews, item);
		}
	}
	e_cal_backend_store_thaw_changes (priv->store);
	ret = TRUE;

exit:
	g_slist_free_full (items, g_object_unref);
	return ret;
}

static gboolean
cal_backend_ews_process_folder_items (ECalBackendEws *cbews,
                                      const gchar *sync_state,
                                      GSList *items_created,
                                      GSList *items_updated,
                                      GSList *items_deleted)
{
	ECalBackendEwsPrivate *priv;
	GSList *l[2], *m, *cal_item_ids = NULL, *task_memo_item_ids = NULL;
	gint i;
	gboolean ret = FALSE;

	priv = cbews->priv;

	l[0] = items_created;
	l[1] = items_updated;

	for (i = 0; i < 2; i++) {
		for (; l[i] != NULL; l[i] = g_slist_next (l[i])) {
			EEwsItem *item = (EEwsItem *) l[i]->data;
			EEwsItemType type = e_ews_item_get_item_type (item);
			const EwsId *id;

			id = e_ews_item_get_id (item);
			if (type == E_EWS_ITEM_TYPE_EVENT)
				cal_item_ids = g_slist_prepend (cal_item_ids, id->id);
			else if (type == E_EWS_ITEM_TYPE_TASK || type == E_EWS_ITEM_TYPE_MEMO)
				task_memo_item_ids = g_slist_prepend (task_memo_item_ids, id->id);
		}
	}

	e_cal_backend_store_freeze_changes (priv->store);
	for (m = items_deleted; m != NULL; m = g_slist_next (m)) {
		gchar *item_id = (gchar *) m->data;
		ECalComponent *comp;

		PRIV_LOCK (priv);
		comp = g_hash_table_lookup (priv->item_id_hash, item_id);
		if (comp)
			g_object_ref (comp);
		PRIV_UNLOCK (priv);

		if (comp) {
			if (!ews_cal_delete_comp (cbews, comp, item_id)) {
				g_object_unref (comp);
				goto exit;
			}

			g_object_unref (comp);
		}
	}
	e_cal_backend_store_thaw_changes (priv->store);


	if (cal_item_ids) {
		EEwsAdditionalProps *add_props;

		add_props = e_ews_additional_props_new ();
		if (e_ews_connection_satisfies_server_version (priv->cnc, E_EWS_EXCHANGE_2010)) {
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

		ews_cal_sync_get_items_sync (
			cbews,
			cal_item_ids,
			"IdOnly",
			add_props);

		e_ews_additional_props_free (add_props);
	}

	if (task_memo_item_ids) {
		ews_cal_sync_get_items_sync (
			cbews,
			task_memo_item_ids,
			"AllProperties",
			NULL);
	}
	ret = TRUE;

exit:
	g_slist_free (cal_item_ids);
	g_slist_free (task_memo_item_ids);
	return ret;
}

static void
cbews_forget_all_components (ECalBackendEws *cbews)
{
	ECalBackend *backend;
	GSList *ids, *ii;

	g_return_if_fail (E_IS_CAL_BACKEND_EWS (cbews));

	backend = E_CAL_BACKEND (cbews);
	g_return_if_fail (backend != NULL);

	ids = e_cal_backend_store_get_component_ids (cbews->priv->store);
	for (ii = ids; ii; ii = ii->next) {
		ECalComponentId *id = ii->data;

		if (!id)
			continue;

		e_cal_backend_store_remove_component (cbews->priv->store, id->uid, id->rid);
		e_cal_backend_notify_component_removed (backend, id, NULL, NULL);
	}

	g_slist_free_full (ids, (GDestroyNotify) e_cal_component_free_id);
}

static gpointer
ews_start_sync_thread (gpointer data)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GSList *items_created = NULL;
	GSList *items_updated = NULL;
	GSList *items_deleted = NULL;
	gboolean includes_last_item;
	gboolean ret;
	gchar *old_sync_state = NULL;
	gchar *new_sync_state = NULL;
	GError *error = NULL;

	cbews = (ECalBackendEws *) data;
	priv = cbews->priv;

	old_sync_state = g_strdup (e_cal_backend_store_get_key_value (priv->store, SYNC_KEY));
	do {
		EEwsAdditionalProps *add_props;

		includes_last_item = TRUE;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup ("item:ItemClass");

		ret = e_ews_connection_sync_folder_items_sync (
			priv->cnc,
			EWS_PRIORITY_MEDIUM,
			old_sync_state,
			priv->folder_id,
			"IdOnly",
			add_props,
			EWS_MAX_FETCH_COUNT,
			&new_sync_state,
			&includes_last_item,
			&items_created,
			&items_updated,
			&items_deleted,
			priv->cancellable,
			&error);

		e_ews_additional_props_free (add_props);
		g_free (old_sync_state);
		old_sync_state = NULL;

		if (!ret) {
			if (g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
				g_clear_error (&error);
				e_cal_backend_store_put_key_value (priv->store, SYNC_KEY, NULL);
				cbews_forget_all_components (cbews);

				if (!e_ews_connection_sync_folder_items_sync (
							priv->cnc,
							EWS_PRIORITY_MEDIUM,
							NULL,
							priv->folder_id,
							"IdOnly",
							NULL,
							EWS_MAX_FETCH_COUNT,
							&new_sync_state,
							&includes_last_item,
							&items_created,
							&items_updated,
							&items_deleted,
							priv->cancellable,
							&error)) {
					if (!g_error_matches (
							error,
							EWS_CONNECTION_ERROR,
							EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED)) {
						e_cal_backend_set_writable (E_CAL_BACKEND (cbews), TRUE);
						break;
					}
				}
			} else {
				break;
			}
		}

		ret = cal_backend_ews_process_folder_items (
				cbews,
				new_sync_state,
				items_created,
				items_updated,
				items_deleted);

		if (!ret)
			break;

		g_slist_free_full (items_created, g_object_unref);
		g_slist_free_full (items_updated, g_object_unref);
		g_slist_free_full (items_deleted, g_free);
		items_created = NULL;
		items_updated = NULL;
		items_deleted = NULL;

		e_cal_backend_store_put_key_value (priv->store, SYNC_KEY, new_sync_state);

		old_sync_state = new_sync_state;
		new_sync_state = NULL;
	} while (!includes_last_item);

	ews_refreshing_dec (cbews);

	g_slist_free_full (items_created, g_object_unref);
	g_slist_free_full (items_updated, g_object_unref);
	g_slist_free_full (items_deleted, g_free);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_clear_error (&error);
	}

	g_free (new_sync_state);
	g_free (old_sync_state);

	g_object_unref (cbews);

	return NULL;
}

static gboolean
ews_start_sync (gpointer data)
{
	ECalBackendEws *cbews = data;
	GThread *thread;

	PRIV_LOCK (cbews->priv);
	if (cbews->priv->refreshing) {
		PRIV_UNLOCK (cbews->priv);
		return TRUE;
	}

	ews_refreshing_inc (cbews);

	if (!cbews->priv->cnc) {
		ews_refreshing_dec (cbews);
		PRIV_UNLOCK (cbews->priv);
		return FALSE;
	}
	PRIV_UNLOCK (cbews->priv);

	/* run the actual operation in thread,
	 * to not block main thread of the factory */
	thread = g_thread_new (NULL, ews_start_sync_thread, g_object_ref (cbews));
	g_thread_unref (thread);

	return TRUE;
}

static void
ews_cal_start_refreshing (ECalBackendEws *cbews)
{
	ECalBackendEwsPrivate *priv;

	priv = cbews->priv;

	PRIV_LOCK (priv);

	if (!priv->refresh_timeout &&
	    e_backend_get_online (E_BACKEND (cbews)) &&
	    priv->cnc) {
		ews_start_sync (cbews);
		priv->refresh_timeout = e_named_timeout_add_seconds (
			REFRESH_INTERVAL, (GSourceFunc) ews_start_sync, cbews);
	}

	PRIV_UNLOCK (priv);
}

static void
e_cal_backend_ews_start_query (ECalBackend *backend,
                               EDataCalView *query)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GSList *components, *l;
	ECalBackendSExp *cbsexp;
	const gchar *sexp;
	gboolean search_needed = TRUE;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;
	GError *err = NULL;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	ews_cal_start_refreshing (cbews);
	cbsexp = e_data_cal_view_get_sexp (query);
	if (!cbsexp) {
		err = EDC_ERROR (InvalidQuery);
		e_data_cal_view_notify_complete (query, err);
		g_error_free (err);
		return;
	}

	sexp = e_cal_backend_sexp_text (cbsexp);
	if (!sexp || !strcmp (sexp, "#t"))
		search_needed = FALSE;

	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (
		cbsexp, &occur_start, &occur_end);
	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = l->next) {
		ECalComponent *comp = E_CAL_COMPONENT (l->data);

		if (e_cal_backend_get_kind (backend) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			if ((!search_needed) ||
			    (e_cal_backend_sexp_match_comp (cbsexp, comp, E_TIMEZONE_CACHE (backend)))) {
				e_data_cal_view_notify_components_added_1 (query, comp);
			}
		}
	}

	g_slist_free_full (components, g_object_unref);
	e_data_cal_view_notify_complete (query, NULL);
}

static void
e_cal_backend_ews_refresh (ECalBackend *backend,
                           EDataCal *cal,
                           guint32 context,
                           GCancellable *cancellable)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GError *error = NULL;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	/* make sure we're not offline */
	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
		goto exit;
	}

	PRIV_LOCK (priv);
	ews_start_sync (cbews);
	PRIV_UNLOCK (priv);

exit:
	convert_error_to_edc_error (&error);
	e_data_cal_respond_refresh (cal, context, error);
}

static void
ews_cal_get_free_busy_cb (GObject *obj,
                          GAsyncResult *res,
                          gpointer user_data)
{
	EEwsConnection *cnc = (EEwsConnection *) obj;
	EwsCalendarAsyncData *free_busy_data = user_data;
	GSList *free_busy_sl = NULL, *i;
	GSList *free_busy = NULL, *j;
	GError *error = NULL;

	if (!e_ews_connection_get_free_busy_finish (cnc, res, &free_busy_sl, &error)) {
		goto done;
	}

	for (i = free_busy_sl, j = free_busy_data->users; i && j; i = i->next, j = j->next) {
		/* add attendee property */
		icalcomponent_add_property ((icalcomponent *) i->data, icalproperty_new_attendee (j->data));

		free_busy = g_slist_append (free_busy, icalcomponent_as_ical_string_r (i->data));
	}
	g_slist_free (free_busy_sl);

done:
	if (free_busy)
		e_data_cal_report_free_busy_data (free_busy_data->cal, free_busy);
	convert_error_to_edc_error (&error);
	e_data_cal_respond_get_free_busy (free_busy_data->cal, free_busy_data->context, error);

	g_slist_free_full (free_busy, g_free);
	e_cal_backend_ews_async_data_free (free_busy_data);
}

static void
e_cal_backend_ews_get_free_busy (ECalBackend *backend,
                                 EDataCal *cal,
                                 guint32 context,
                                 GCancellable *cancellable,
                                 const GSList *users,
                                 time_t start,
                                 time_t end)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (backend);
	ECalBackendEwsPrivate *priv = cbews->priv;
	GError *error = NULL;
	EwsCalendarAsyncData *free_busy_data;
	EwsCalendarConvertData convert_data = { 0 };
	GSList *users_copy = NULL;

	/* make sure we're not offline */
	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
		goto exit;
	}

	if (!cal_backend_ews_ensure_connected (cbews, cancellable, &error)) {
		goto exit;
	}

	/* EWS can support only 100 identities, which is the maximum number of identities that the Web service method can request
	 see http://msdn.microsoft.com / en - us / library / aa564001 % 28v = EXCHG.140 % 29.aspx */
	if (g_slist_length ((GSList *) users) > 100)
	{
		g_propagate_error (&error, EDC_ERROR (SearchSizeLimitExceeded));
		goto exit;
	}

	for (; users; users = users->next)
	    users_copy = g_slist_append (users_copy, g_strdup (users->data));

	free_busy_data = g_new0 (EwsCalendarAsyncData, 1);
	free_busy_data->cbews = g_object_ref (cbews);
	free_busy_data->cal = g_object_ref (cal);
	free_busy_data->context = context;
	free_busy_data->users = users_copy;

	convert_data.users = users_copy;
	convert_data.start = start;
	convert_data.end = end;

	e_ews_connection_get_free_busy (
		priv->cnc,
		EWS_PRIORITY_MEDIUM,
		e_cal_backend_ews_prepare_free_busy_request,
		&convert_data,
		cancellable,
		ews_cal_get_free_busy_cb,
		free_busy_data);

	return;

exit:
	convert_error_to_edc_error (&error);
	e_data_cal_respond_get_free_busy (cal, context, error);

}

static gchar *
e_cal_backend_ews_get_backend_property (ECalBackend *backend,
                                        const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (
			",",
			CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS,
			CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY,
			CAL_STATIC_CAPABILITY_REMOVE_ALARMS,
			CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED,
			CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			CAL_STATIC_CAPABILITY_NO_THISANDFUTURE,
			CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK,
			//	 CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR,
			CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT,
			CAL_STATIC_CAPABILITY_SAVE_SCHEDULES,
			CAL_STATIC_CAPABILITY_NO_ALARM_AFTER_START,
			CAL_STATIC_CAPABILITY_NO_MEMO_START_DATE,
			NULL);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		/* return email address of the person who opened the calendar */
		ECalBackendEws *cbews;

		cbews = E_CAL_BACKEND_EWS (backend);

		return g_strdup (cbews->priv->user_email);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* ews does not support email based alarms */
		return NULL;
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		ECalComponent *comp;
		gchar *prop_value;

		comp = e_cal_component_new ();

		switch (e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		case ICAL_VEVENT_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
			break;
		case ICAL_VTODO_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
			break;
		case ICAL_VJOURNAL_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
			break;
		default:
			g_object_unref (comp);
			return NULL;
		}

		prop_value = e_cal_component_get_as_string (comp);

		g_object_unref (comp);

		return prop_value;
	}

	/* Chain up to parent's get_backend_property() method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_ews_parent_class)->
		get_backend_property (backend, prop_name);
}

static void
e_cal_backend_ews_notify_online_cb (EBackend *backend,
                                    GParamSpec *spec)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	PRIV_LOCK (priv);

	if (e_backend_get_online (backend)) {
		if (priv->cancellable) {
			g_cancellable_cancel (priv->cancellable);
			g_object_unref (priv->cancellable);
			priv->cancellable = NULL;
		}
		priv->cancellable = g_cancellable_new ();

		priv->read_only = FALSE;
	} else {
		switch_offline (cbews);
	}

	e_cal_backend_set_writable (E_CAL_BACKEND (backend), !priv->read_only);

	PRIV_UNLOCK (priv);
}

static gboolean
e_cal_backend_ews_get_destination_address (EBackend *backend,
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

	ews_settings = cal_backend_ews_get_collection_settings (E_CAL_BACKEND_EWS (backend));
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

static void
e_cal_backend_ews_constructed (GObject *object)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (object);
	CamelEwsSettings *ews_settings;

	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->constructed (object);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (E_BACKEND (object), NULL);

	ews_settings = cal_backend_ews_get_collection_settings (cbews);
	g_warn_if_fail (cbews->priv->user_email == NULL);
	cbews->priv->user_email = camel_ews_settings_dup_email (ews_settings);
}

static void
e_cal_backend_ews_dispose (GObject *object)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	CamelEwsSettings *ews_settings;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_EWS (object));

	cbews = E_CAL_BACKEND_EWS (object);
	priv = cbews->priv;

	ews_settings = cal_backend_ews_get_collection_settings (cbews);
	g_signal_handlers_disconnect_by_func (ews_settings, cbews_listen_notifications_cb, cbews);

	if (priv->refresh_timeout) {
		g_source_remove (priv->refresh_timeout);
		priv->refresh_timeout = 0;
	}

	if (priv->cancellable) {
		g_cancellable_cancel (priv->cancellable);
		g_object_unref (priv->cancellable);
		priv->cancellable = NULL;
	}

	if (priv->cnc) {
		g_signal_handlers_disconnect_by_func (priv->cnc, cbews_server_notification_cb, object);

		if (priv->listen_notifications) {
			if (priv->subscription_key != 0) {
				e_ews_connection_disable_notifications_sync (
					priv->cnc,
					priv->subscription_key);
				priv->subscription_key = 0;
			}

			priv->listen_notifications = FALSE;
		}

		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->dispose (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_ews_finalize (GObject *object)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_EWS (object));

	cbews = E_CAL_BACKEND_EWS (object);
	priv = cbews->priv;

	/* Clean up */
	g_rec_mutex_clear (&priv->rec_mutex);

	if (priv->store) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	if (priv->folder_id) {
		g_free (priv->folder_id);
		priv->folder_id = NULL;
	}

	if (priv->user_email) {
		g_free (priv->user_email);
		priv->user_email = NULL;
	}

	if (priv->storage_path) {
		g_free (priv->storage_path);
		priv->storage_path = NULL;
	}

	if (priv->default_zone && priv->default_zone != icaltimezone_get_utc_timezone ()) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}

	g_hash_table_destroy (priv->item_id_hash);

	if (priv->refreshing_done) {
		e_flag_free (priv->refreshing_done);
		priv->refreshing_done = NULL;
	}

	e_cal_backend_ews_unref_windows_zones ();

	G_OBJECT_CLASS (e_cal_backend_ews_parent_class)->finalize (object);
}

static gboolean
cal_backend_ews_get_without_password (ESourceAuthenticator *authenticator)
{
	ECalBackendEws *backend;
	CamelEwsSettings *ews_settings;

	backend = E_CAL_BACKEND_EWS (authenticator);
	ews_settings = cal_backend_ews_get_collection_settings (backend);

	return e_ews_connection_utils_get_without_password (ews_settings);
}

static ESourceAuthenticationResult
cal_backend_ews_try_password_sync (ESourceAuthenticator *authenticator,
                                   const GString *password,
                                   GCancellable *cancellable,
                                   GError **error)
{
	ECalBackendEws *backend;
	EEwsConnection *connection;
	ESourceAuthenticationResult result;
	CamelEwsSettings *ews_settings;
	gchar *hosturl;

	backend = E_CAL_BACKEND_EWS (authenticator);
	ews_settings = cal_backend_ews_get_collection_settings (backend);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);

	connection = e_ews_connection_new (hosturl, ews_settings);

	g_object_bind_property (
		backend, "proxy-resolver",
		connection, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	result = e_source_authenticator_try_password_sync (
		E_SOURCE_AUTHENTICATOR (connection),
		password, cancellable, error);

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {

		PRIV_LOCK (backend->priv);

		g_free (backend->priv->user_email);
		backend->priv->user_email = camel_ews_settings_dup_email (ews_settings);

		if (backend->priv->cnc != NULL)
			g_object_unref (backend->priv->cnc);
		backend->priv->cnc = g_object_ref (connection);

		g_signal_connect_swapped (
			backend->priv->cnc,
			"server-notification",
			G_CALLBACK (cbews_server_notification_cb),
			backend);

		PRIV_UNLOCK (backend->priv);

		ews_start_sync (backend);
	}

	g_object_unref (connection);

	g_free (hosturl);

	return result;
}

static void
e_cal_backend_ews_class_init (ECalBackendEwsClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;

	g_type_class_add_private (class, sizeof (ECalBackendEwsPrivate));

	object_class = G_OBJECT_CLASS (class);
	backend_class = E_BACKEND_CLASS (class);
	cal_backend_class = E_CAL_BACKEND_CLASS (class);

	object_class->constructed = e_cal_backend_ews_constructed;
	object_class->dispose = e_cal_backend_ews_dispose;
	object_class->finalize = e_cal_backend_ews_finalize;

	backend_class->get_destination_address = e_cal_backend_ews_get_destination_address;

	/* Property accessors */
	cal_backend_class->get_backend_property = e_cal_backend_ews_get_backend_property;

	cal_backend_class->start_view = e_cal_backend_ews_start_query;

	/* Many of these can be moved to Base class */
	cal_backend_class->add_timezone = e_cal_backend_ews_add_timezone;
	cal_backend_class->get_timezone = e_cal_backend_ews_get_timezone;

	cal_backend_class->open = e_cal_backend_ews_open;
	cal_backend_class->refresh = e_cal_backend_ews_refresh;
	cal_backend_class->get_object = e_cal_backend_ews_get_object;
	cal_backend_class->get_object_list = e_cal_backend_ews_get_object_list;

	cal_backend_class->discard_alarm = e_cal_backend_ews_discard_alarm;

	cal_backend_class->create_objects = e_cal_backend_ews_create_objects;
	cal_backend_class->modify_objects = e_cal_backend_ews_modify_objects;
	cal_backend_class->remove_objects = e_cal_backend_ews_remove_objects;

	cal_backend_class->receive_objects = e_cal_backend_ews_receive_objects;
	cal_backend_class->send_objects = e_cal_backend_ews_send_objects;
	cal_backend_class->get_free_busy = e_cal_backend_ews_get_free_busy;
}

static void
e_cal_backend_ews_authenticator_init (ESourceAuthenticatorInterface *iface)
{
	iface->get_without_password = cal_backend_ews_get_without_password;
	iface->try_password_sync = cal_backend_ews_try_password_sync;
}

static void
e_cal_backend_ews_init (ECalBackendEws *cbews)
{
	cbews->priv = G_TYPE_INSTANCE_GET_PRIVATE (cbews, E_TYPE_CAL_BACKEND_EWS, ECalBackendEwsPrivate);

	/* create the mutex for thread safety */
	g_rec_mutex_init (&cbews->priv->rec_mutex);
	cbews->priv->refreshing_done = e_flag_new ();
	cbews->priv->item_id_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	cbews->priv->default_zone = icaltimezone_get_utc_timezone ();
	cbews->priv->cancellable = g_cancellable_new ();

	g_signal_connect (
		cbews, "notify::online",
		G_CALLBACK (e_cal_backend_ews_notify_online_cb), NULL);

	e_cal_backend_ews_populate_windows_zones ();
}
