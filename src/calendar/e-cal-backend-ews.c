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
#include "libedataserver/e-xml-hash-utils.h"
#include "libedataserver/e-url.h"
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-file-store.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>
#include <libical/icaltz-util.h>
#include <libical/icalcomponent.h>
#include <libical/icalproperty.h>
#include <glib-2.0/glib/gerror.h>
#include <glib-2.0/glib/glist.h>
#include <camel/camel.h>
#include <ews-camel-common.h>
#include "e-cal-backend-ews.h"
#include "e-cal-backend-ews-utils.h"
#include "e-ews-connection.h"
#include "e-soap-response.h"
#include "e-ews-message.h"
#include "e-ews-item-change.h"

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

G_DEFINE_TYPE (ECalBackendEws, e_cal_backend_ews, E_TYPE_CAL_BACKEND)

/* Private part of the CalBackendEws structure */
struct _ECalBackendEwsPrivate {
	/* Fields required for online server requests */
	EEwsConnection *cnc;
	gchar *folder_id;
	gchar *user_email;

	EDataCal *opening_cal;
	EServerMethodContext opening_ctx;

	CalMode mode;
	ECalBackendStore *store;
	gboolean read_only;

	/* A mutex to control access to the private structure for the following */
	GStaticRecMutex rec_mutex;
	icaltimezone *default_zone;
	guint refresh_timeout;
	gboolean refreshing;
	GHashTable *item_id_hash;
};

#define PRIV_LOCK(p)   (g_static_rec_mutex_lock (&(p)->rec_mutex))
#define PRIV_UNLOCK(p) (g_static_rec_mutex_unlock (&(p)->rec_mutex))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

#define SYNC_KEY "sync-state"
#define EWS_MAX_FETCH_COUNT 100
#define REFRESH_INTERVAL 600

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

#define PARENT_TYPE E_TYPE_CAL_BACKEND
static ECalBackendClass *parent_class = NULL;
static void ews_cal_sync_items_ready_cb (GObject *obj, GAsyncResult *res, gpointer user_data);
static void ews_cal_component_get_item_id (ECalComponent *comp, gchar **itemid, gchar **changekey);
static gboolean ews_start_sync	(gpointer data);

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

	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}
}

/* Property Accessors */
static void
e_cal_backend_ews_is_read_only (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendEws *cbews;

	cbews = E_CAL_BACKEND_EWS (backend);

	e_data_cal_notify_read_only (cal, NULL, cbews->priv->read_only);
}

static void
e_cal_backend_ews_get_cal_address (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	e_data_cal_notify_cal_address (cal, context, NULL, priv->user_email);
}

static void
e_cal_backend_ews_get_static_capabilities (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	const gchar *capabilities;

	capabilities =	(CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
			 CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY ","
			 CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","
			 CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED ","
			 CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
			 CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
			 CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK ","
			 CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR ","
			 CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);

	e_data_cal_notify_static_capabilities (cal, context, NULL, capabilities);
}

static gboolean
e_cal_backend_ews_is_loaded (ECalBackend *backend)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	return priv->store ? TRUE : FALSE;
}

static icaltimezone *
e_cal_backend_ews_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (backend);

	return cbews->priv->default_zone;
}

static CalMode
e_cal_backend_ews_get_mode (ECalBackend *backend)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	return priv->mode;
}
static void
e_cal_backend_ews_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	if (priv->mode == mode) {
		e_cal_backend_notify_mode (backend, ModeSet,
					   cal_mode_to_corba (mode));
		return;
	}

	PRIV_LOCK (priv);

	switch (mode) {
	case CAL_MODE_REMOTE :/* go online */
		priv->mode = CAL_MODE_REMOTE;
		priv->read_only = FALSE;
		e_cal_backend_notify_mode (backend, ModeSet, Remote);
		e_cal_backend_notify_readonly (backend, priv->read_only);
		if (e_cal_backend_ews_is_loaded (backend))
			      e_cal_backend_notify_auth_required (backend);
		break;

	case CAL_MODE_LOCAL : /* go offline */
		priv->mode = CAL_MODE_LOCAL;
		switch_offline (cbews);
		e_cal_backend_notify_readonly (backend, priv->read_only);
		e_cal_backend_notify_mode (backend, ModeSet, Local);

		break;
	default :
		e_cal_backend_notify_mode (backend, ModeNotSupported,
					   cal_mode_to_corba (mode));
	}

	PRIV_UNLOCK (priv);
}

static void
e_cal_backend_ews_set_default_zone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icaltimezone *zone;
	GError *error = NULL;

	cbews = (ECalBackendEws *) backend;

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);
	e_data_cal_error_if_fail (tzobj != NULL, InvalidArg);

	priv = cbews->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	PRIV_LOCK (priv);

	if (priv->default_zone)
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	PRIV_UNLOCK (priv);

exit:
	e_data_cal_notify_default_timezone_set (cal, context, error);
}

static icaltimezone *
e_cal_backend_ews_internal_get_timezone (ECalBackend *backend, const gchar *tzid)
{
	icaltimezone *zone = NULL;
	ECalBackendEws *cbews;

	cbews = E_CAL_BACKEND_EWS (backend);
	g_return_val_if_fail (cbews != NULL, NULL);
	g_return_val_if_fail (cbews->priv != NULL, NULL);

	if (cbews->priv->store)
		zone = (icaltimezone *) e_cal_backend_store_get_timezone (cbews->priv->store, tzid);

	if (!zone && E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone)
		zone = E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone (backend, tzid);

	return zone;
}

static void
e_cal_backend_ews_add_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GError *error = NULL;

	cbews = (ECalBackendEws *) backend;

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);
	e_data_cal_error_if_fail (tzobj != NULL, InvalidArg);

	priv = cbews->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (&error, EDC_ERROR (InvalidObject));
		goto exit;
	}

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);
		if (e_cal_backend_store_put_timezone (priv->store, zone) == FALSE) {
			icaltimezone_free (zone, 1);
			g_propagate_error (&error, EDC_ERROR_EX (OtherError, "Put timezone failed"));
			goto exit;
		}
		icaltimezone_free (zone, 1);
	}

exit:
	e_data_cal_notify_timezone_added (cal, context, error, tzobj);
}

static void e_cal_backend_ews_get_ldap_attribute (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	e_data_cal_notify_ldap_attribute (cal, context, NULL, NULL);
}

typedef struct {
	ECalBackendEws *cbews;
	EDataCal *cal;
	EServerMethodContext *context;
	gchar *itemid;
	gchar *changekey;
	gboolean is_occurrence;
	gint instance_index;
} EwsDiscardAlarmData;

static void clear_reminder_is_set (ESoapMessage *msg, gpointer user_data)
{
	EwsDiscardAlarmData *edad = user_data;
	EEwsItemChangeType change_type;

	if (edad->is_occurrence)
		change_type = E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM;
	else
		change_type = E_EWS_ITEMCHANGE_TYPE_ITEM;

	e_ews_message_start_item_change (msg, change_type,
					 edad->itemid, edad->changekey, edad->instance_index);

	e_ews_message_start_set_item_field (msg, "ReminderIsSet","item", "CalendarItem");

	e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false");

	e_ews_message_end_set_item_field (msg);

	e_ews_message_end_item_change (msg);
}


static void
ews_cal_discard_alarm_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsDiscardAlarmData *edad = user_data;
	GError *error = NULL;

	if (!e_ews_connection_update_items_finish (cnc, res, NULL, &error)) {
		/* The calendar UI doesn't *display* errors unless they have
		   the OtherError code */
		error->code = OtherError;
	}

	e_data_cal_notify_alarm_discarded (edad->cal, edad->context, error);

	g_free(edad->itemid);
	g_free(edad->changekey);
	g_object_unref(edad->cbews);
	g_object_unref(edad->cal);
	g_free(edad);
}

static void
e_cal_backend_ews_discard_alarm (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *uid, const gchar *auid)
{
	ECalBackendEws *cbews = (ECalBackendEws *) backend;
	ECalBackendEwsPrivate *priv;
	EwsDiscardAlarmData *edad;
	ECalComponent *comp;

	priv = cbews->priv;

	PRIV_LOCK (priv);

	comp = e_cal_backend_store_get_component (priv->store, uid, NULL);
	if (!comp) {
		e_data_cal_notify_alarm_discarded (cal, context,
						   EDC_ERROR(ObjectNotFound));
		return;
	}

	PRIV_UNLOCK (priv);

	/* FIXME: Can't there be multiple alarms for each event? Or does
	   Exchange not support that? */
	edad = g_new0 (EwsDiscardAlarmData, 1);
	edad->cbews = g_object_ref (cbews);
	edad->cal = g_object_ref (cal);
	edad->context = context;

	if (e_cal_component_has_recurrences (comp)) {
		gint *index;

		edad->is_occurrence = TRUE;
		e_cal_component_get_sequence (comp, &index);

		if (index != NULL) {
			/*Microsoft is counting the occurrences starting from 1
			 where EcalComponent is starting from zerro*/
			edad->instance_index = *index + 1;
			e_cal_component_free_sequence (index);
		} else {
			edad->is_occurrence = FALSE;
			edad->instance_index = -1;
		}
	}
	else {
		edad->is_occurrence = FALSE;
		edad->instance_index = -1;
	}
	
	ews_cal_component_get_item_id (comp, &edad->itemid, &edad->changekey);

	e_ews_connection_update_items_start (priv->cnc, EWS_PRIORITY_MEDIUM,
					     "AlwaysOverwrite", NULL,
					     "SendToNone", NULL,
					     clear_reminder_is_set, edad,
					     ews_cal_discard_alarm_cb, NULL,
					     edad);
}

static void
e_cal_backend_ews_get_timezone (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *tzid)
{
	icalcomponent *icalcomp;
	icaltimezone *zone;
	gchar *object = NULL;
	GError *error = NULL;

	zone = e_cal_backend_ews_internal_get_timezone (backend, tzid);
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

		if (slashes == 1) {
			icalcomponent *icalcomp = NULL, *free_comp = NULL;

			icaltimezone *zone = icaltimezone_get_builtin_timezone (tzid);
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
					g_clear_error (&error);
				}
				icalcomponent_free (clone);
			}

			if (free_comp)
				icalcomponent_free (free_comp);
		}
	}

	e_data_cal_notify_timezone_requested (cal, context, error, object);
	g_free (object);

}


static void
e_cal_backend_ews_get_default_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{

	ECalComponent *comp;
	GError *error = NULL;
	gchar *object = NULL;

	comp = e_cal_component_new ();

	switch (e_cal_backend_get_kind (backend)) {
	case ICAL_VEVENT_COMPONENT:
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
		break;
	case ICAL_VTODO_COMPONENT:
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		break;
	default:
		g_object_unref (comp);
		g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
		goto exit;
	}

	object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

exit:
	e_data_cal_notify_default_object (cal, context, error, object);
	g_free (object);
}

/* changekey can be NULL if you don't want it. itemid cannot. */
static void
ews_cal_component_get_item_id (ECalComponent *comp, gchar **itemid, gchar **changekey)
{
	icalproperty *prop;
	gchar *ck = NULL;
	const gchar *id = NULL;

	if (changekey) {
		prop = icalcomponent_get_first_property (e_cal_component_get_icalcomponent (comp),
						 ICAL_X_PROPERTY);
		while (prop) {
			const gchar *x_name, *x_val;

			x_name = icalproperty_get_x_name (prop);
			x_val = icalproperty_get_x (prop);
			if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY")) {
				ck = g_strdup (x_val);
				break;
			}

			prop = icalcomponent_get_next_property (e_cal_component_get_icalcomponent (comp),
							ICAL_X_PROPERTY);
		}
	}

	e_cal_component_get_uid(comp, &id);
	*itemid = g_strdup (id);
	if (changekey)
		*changekey = ck;
}

/* changekey can be NULL if you don't want it. itemid cannot. */
static void
ews_cal_component_get_calendar_item_accept_id (ECalComponent *comp, gchar **itemid, gchar **changekey)
{
	icalproperty *prop;
	const gchar *id = NULL;
	*itemid = NULL;

	prop = icalcomponent_get_first_property (e_cal_component_get_icalcomponent (comp),
		ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name, *x_val;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY")) {
			*changekey = g_strdup (x_val);
		} else if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-ACCEPT-ID")) {
			*itemid = g_strdup (x_val);
		}

		prop = icalcomponent_get_next_property (e_cal_component_get_icalcomponent (comp),
			ICAL_X_PROPERTY);
	}
	if (!*itemid){
		e_cal_component_get_uid(comp, &id);
		*itemid = g_strdup (id);
	}
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
		gchar *item_id;

		ews_cal_component_get_item_id (comp, &item_id, NULL);

		g_hash_table_insert (priv->item_id_hash, item_id, comp);
	}

	PRIV_UNLOCK (priv);

	g_slist_free (comps);
}
static void
e_cal_backend_ews_open (ECalBackend *backend, EDataCal *cal, EServerMethodContext context,
			gboolean only_if_exists, const gchar *username, const gchar *password)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	const gchar *cache_dir;
	GError *error = NULL;

	cbews = (ECalBackendEws *) backend;
	priv = cbews->priv;

	cache_dir = e_cal_backend_get_cache_dir (backend);

	PRIV_LOCK (priv);
	if (!priv->store) {
		priv->store = e_cal_backend_file_store_new (cache_dir);
		e_cal_backend_store_load (priv->store);
		add_comps_to_item_id_hash (cbews);
		e_cal_backend_store_set_default_timezone (priv->store, priv->default_zone);
	}

	if (priv->mode != CAL_MODE_LOCAL && !priv->cnc) {
		ESource *esource;
		const gchar *host_url;

		/* If we can be called a second time while the first is still
		   "outstanding", we need a bit of a rethink... */
		g_assert (!priv->opening_ctx && !priv->opening_cal);

		esource = e_cal_backend_get_source (E_CAL_BACKEND (cbews));

		priv->folder_id = e_source_get_duped_property (esource, "folder-id");
		priv->user_email = e_source_get_duped_property (esource, "email");

		host_url = e_source_get_property (esource, "hosturl");

		priv->opening_cal = cal;
		priv->opening_ctx = context;

		priv->cnc = e_ews_connection_new (host_url, username, password,
						  NULL, NULL, &error);
		if (priv->cnc) {
			/* Trigger an update request, which will test our authentication */
			ews_start_sync (cbews);

			PRIV_UNLOCK (priv);
			return;
		}
	}

	PRIV_UNLOCK (priv);
	e_data_cal_notify_open (cal, context, error);
}

static void
e_cal_backend_ews_remove (ECalBackend *backend, EDataCal *cal, EServerMethodContext context)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	PRIV_LOCK (priv);

	/* remove the cache */
	if (priv->store)
		e_cal_backend_store_remove (priv->store);

	PRIV_UNLOCK (priv);

	e_data_cal_notify_remove (cal, context, NULL);
}

static void
e_cal_backend_ews_get_object	(ECalBackend *backend, EDataCal *cal, EServerMethodContext context,
	 			 const gchar *uid, const gchar *rid)
{
	ECalComponent *comp;
	ECalBackendEwsPrivate *priv;
	ECalBackendEws *cbews = (ECalBackendEws *) backend;
	gchar *object = NULL;
	GError *error = NULL;

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);

	priv = cbews->priv;

	PRIV_LOCK (priv);

	/* search the object in the cache */
	comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	if (comp) {
		PRIV_UNLOCK (priv);
		if (e_cal_backend_get_kind (backend) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)))
			object = e_cal_component_get_as_string (comp);
		else
			object = NULL;

		g_object_unref (comp);

		if (!object)
			g_propagate_error (&error, EDC_ERROR (ObjectNotFound));
		goto exit;
	}

	PRIV_UNLOCK (priv);

	/* callers will never have a uuid that is in server but not in cache */
	g_propagate_error (&error, EDC_ERROR (ObjectNotFound));

exit:
	e_data_cal_notify_object (cal, context, error, object);
	g_free (object);
}

static void
cal_backend_ews_get_object_list (ECalBackend *backend, const gchar *sexp, GList **objects, GError **error)
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

	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (cbsexp,
									    &occur_start,
									    &occur_end);
	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = l->next) {
		ECalComponent *comp = E_CAL_COMPONENT (l->data);

		if (e_cal_backend_get_kind (backend) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			if ((!search_needed) ||
			    (e_cal_backend_sexp_match_comp (cbsexp, comp, backend))) {
				*objects = g_list_append (*objects, e_cal_component_get_as_string (comp));
			}
		}
	}

	g_object_unref (cbsexp);
	g_slist_foreach (components, (GFunc) g_object_unref, NULL);
	g_slist_free (components);
}

static void
e_cal_backend_ews_get_object_list (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *sexp)
{
	GList *objects = NULL, *l;
	GError *error = NULL;

	cal_backend_ews_get_object_list (backend, sexp, &objects, &error);

	e_data_cal_notify_object_list (cal, context, error, objects);
	if (objects) {
		for (l = objects; l != NULL; l = l->next)
			g_free (l->data);
		g_list_free (objects);
	}
}

static void
ews_cal_delete_comp (ECalBackendEws *cbews, ECalComponent *comp, const gchar *item_id)
{
	ECalBackendEwsPrivate *priv = cbews->priv;
	gchar *comp_str;
	ECalComponentId *id;

	id = e_cal_component_get_id (comp);
	e_cal_backend_store_remove_component (priv->store, id->uid, id->rid);

	/* TODO test with recurrence handling */
	comp_str = e_cal_component_get_as_string (comp);
	e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbews), id, comp_str, NULL);

	PRIV_LOCK (priv);
	g_hash_table_remove (priv->item_id_hash, item_id);
	PRIV_UNLOCK (priv);

	e_cal_component_free_id (id);
	g_free (comp_str);
}

typedef struct {
	ECalBackendEws *cbews;
	EDataCal *cal;
	ECalComponent *comp;
	EServerMethodContext context;
	EwsId item_id;
	guint index;
} EwsRemoveData;

static void
ews_cal_remove_object_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EwsRemoveData *remove_data = user_data;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (!g_simple_async_result_propagate_error (simple, &error)) {
		/* FIXME: This is horrid. Will bite us when we start to delete
		   more than one item at a time... */
		if (remove_data->comp)
			ews_cal_delete_comp (remove_data->cbews, remove_data->comp, remove_data->item_id.id);
	} else {
		/*In case where item already removed, we do not want to fail*/
		if (error->code == EWS_CONNECTION_ERROR_ITEMNOTFOUND) {
			g_clear_error (&error);
			/*probably we are not in sync, let's sync with server*/
			ews_start_sync (remove_data->cbews);
		} else
			error->code = OtherError;
	}

	if (remove_data->context) {
		if (!error && remove_data->index != 0) ews_start_sync (remove_data->cbews);
		e_data_cal_notify_remove (remove_data->cal, remove_data->context, error);
	} else if (error) {
		g_warning ("Remove object error :  %s\n", error->message);
		g_clear_error (&error);
	}

	g_free (remove_data->item_id.id);
	g_free (remove_data->item_id.change_key);
	g_object_unref(remove_data->cbews);
	g_object_unref(remove_data->comp);
	g_object_unref(remove_data->cal);
	g_free(remove_data);
}

static guint
e_cal_rid_to_index (const char *rid, icalcomponent *comp, GError **error)
{
	guint index = 1;
	icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_RRULE_PROPERTY);
	struct icalrecurrencetype rule = icalproperty_get_rrule (prop);
	struct icaltimetype dtstart = icalcomponent_get_dtstart (comp);
	icalrecur_iterator* ritr = icalrecur_iterator_new (rule, dtstart);
	icaltimetype next = icalrecur_iterator_next (ritr),
		/*o_time = icaltime_from_string_with_zone (rid, dtstart.zone);*/
		o_time = icaltime_from_string (rid);

	o_time.zone = dtstart.zone;

	for (; !icaltime_is_null_time (next); next = icalrecur_iterator_next (ritr), index++) {
		if (icaltime_compare (o_time, next) == 0) break;
	}
	
	if (icaltime_is_null_time (next)) {
		g_propagate_error (error, EDC_ERROR_EX(OtherError,
		    "Invalid occurrence ID"));
	}
	
	return index;
}

static void
e_cal_backend_ews_remove_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context,
				 const gchar *uid, const gchar *rid, CalObjModType mod)
{
	EwsRemoveData *remove_data;
	ECalBackendEws *cbews = (ECalBackendEws *) backend;
	ECalBackendEwsPrivate *priv;
	ECalComponent *comp;
	GError *error = NULL;
	EwsId item_id;
	guint index = 0;

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);
	/*We have to run synce before rihth operations, in some cases change key may change */
	ews_start_sync (cbews);

	priv = cbews->priv;

	PRIV_LOCK (priv);

	comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	if (!comp) {
		comp = e_cal_backend_store_get_component (priv->store, uid, NULL);
		if (!comp) {
			g_warning ("EEE Cant find component with uid:%s & rid:%s\n", uid, rid);
			g_propagate_error (&error, EDC_ERROR(ObjectNotFound));
			goto exit;
		}

		index = e_cal_rid_to_index (rid, e_cal_component_get_icalcomponent (comp), &error);
		if (error) goto exit;

		ews_cal_component_get_item_id (comp, &item_id.id, &item_id.change_key);

		g_object_unref (comp);
		comp = NULL;
	} else {
		ews_cal_component_get_item_id (comp, &item_id.id, &item_id.change_key);
	}

	PRIV_UNLOCK (priv);

	if (!item_id.id) {
		g_propagate_error(&error, EDC_ERROR_EX(OtherError,
					       "Cannot determine EWS ItemId"));
		if (comp) g_object_unref (comp);
		goto exit;
	}

	remove_data = g_new0 (EwsRemoveData, 1);
	remove_data->cbews = g_object_ref(cbews);
	remove_data->comp = comp;
	remove_data->cal = g_object_ref(cal);
	remove_data->context = context;
	remove_data->index = index;
	remove_data->item_id.id = item_id.id;
	remove_data->item_id.change_key = item_id.change_key;

	e_ews_connection_delete_item_start (priv->cnc, EWS_PRIORITY_MEDIUM, &remove_data->item_id, index,
					     EWS_HARD_DELETE, EWS_SEND_TO_ALL_AND_SAVE_COPY, EWS_ALL_OCCURRENCES,
					     ews_cal_remove_object_cb, NULL,
					     remove_data);
	return;

exit:
	if (context)
		e_data_cal_notify_remove (cal, context, error);
	else if (error) {
		g_warning ("Remove object error :  %s\n", error->message);
		g_clear_error (&error);
	}
}

static icaltimezone * resolve_tzid (const gchar *tzid, gpointer user_data);
static void put_component_to_store (ECalBackendEws *cbews,ECalComponent *comp);

typedef struct {
	ECalBackendEws *cbews;
	EDataCal *cal;
	ECalComponent *comp;
	EServerMethodContext context;
} EwsCreateData;

static void add_attendees_list_to_message(ESoapMessage *msg, const gchar *listname, GSList *list) {
	GSList *item;

	e_soap_message_start_element(msg, listname, NULL, NULL);

	for (item = list ; item != NULL; item = item->next) {
		e_soap_message_start_element(msg, "Attendee", NULL, NULL);
		e_soap_message_start_element(msg, "Mailbox", NULL, NULL);

		e_ews_message_write_string_parameter(msg, "EmailAddress", NULL, item->data);

		e_soap_message_end_element(msg); /* "Mailbox" */
		e_soap_message_end_element(msg); /* "Attendee" */
	}

	e_soap_message_end_element(msg);
}

static void
convert_vevent_calcomp_to_xml(ESoapMessage *msg, gpointer user_data)
{
	icalcomponent *icalcomp = (icalcomponent*)user_data;
	ECalComponent *comp = e_cal_component_new();
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend;
	icalproperty *prop;
	gboolean has_alarms;

	e_cal_component_set_icalcomponent(comp, icalcomp);

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa564690.aspx */

	/* Prepare CalendarItem node in the SOAP message */
	e_soap_message_start_element(msg, "CalendarItem", NULL, NULL);

	/* subject */
	e_ews_message_write_string_parameter(msg, "Subject", NULL,  icalcomponent_get_summary(icalcomp));

	/* description */
	e_ews_message_write_string_parameter_with_attribute(msg, "Body", NULL, icalcomponent_get_description(icalcomp), "BodyType", "Text");

	/* set alarms */
	has_alarms = e_cal_component_has_alarms (comp);
	if (has_alarms)
		ews_set_alarm (msg, comp);
	else
		e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false");

	/* start time, end time and meeting time zone */
	dtstart = icalcomponent_get_dtstart (icalcomp);
	dtend = icalcomponent_get_dtend (icalcomp);

	ewscal_set_time (msg, "Start", &dtstart);
	ewscal_set_time (msg, "End", &dtend);
	/* We have to do the time zone(s) later, or the server rejects the request */

	/*freebusy*/
	prop = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
	if (!g_strcmp0 (icalproperty_get_value_as_string (prop), "TRANSPARENT"))
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus",NULL,"Free");
	else
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus",NULL,"Busy");

	/* location */
	e_ews_message_write_string_parameter(msg, "Location", NULL, icalcomponent_get_location(icalcomp));
	
	/* collect attendees */
	e_ews_collect_attendees(icalcomp, &required, &optional, &resource);

	if (required != NULL) {
		add_attendees_list_to_message(msg, "RequiredAttendees", required);
		g_slist_free(required);
	}
	if (optional != NULL) {
		add_attendees_list_to_message(msg, "OptionalAttendees", optional);
		g_slist_free(optional);
	}
	if (resource != NULL) {
		add_attendees_list_to_message(msg, "Resources", resource);
		g_slist_free(resource);
	}
	/* end of attendees */

	/* Recurrence */
	prop = icalcomponent_get_first_property(icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL) {
		ewscal_set_reccurence(msg, prop, &dtstart);
	}

	if (0 /* Exchange 2010 detected */ && dtstart.zone != dtend.zone) {
		/* We have to cast these because libical puts a const pointer into the
		   icaltimetype, but its basic read-only icaltimezone_foo() functions
		   take a non-const pointer! */
		ewscal_set_timezone (msg, "StartTimeZone", (icaltimezone *)dtstart.zone);
		ewscal_set_timezone (msg, "EndTimeZone", (icaltimezone *)dtstart.zone);
	} else
		ewscal_set_timezone (msg, "MeetingTimeZone", (icaltimezone *)dtstart.zone);

	// end of "CalendarItem"
	e_soap_message_end_element(msg);
}

static void
convert_vtodo_calcomp_to_xml(ESoapMessage *msg, gpointer user_data)
{
	icalcomponent *icalcomp = (icalcomponent*)user_data;
	icalproperty *prop;
	icaltimetype dt;
	int value;
	char buffer[16];

	e_soap_message_start_element(msg, "Task", NULL, NULL);

	e_ews_message_write_string_parameter(msg, "Subject", NULL, icalcomponent_get_summary(icalcomp));

	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, icalcomponent_get_description (icalcomp), "BodyType", "Text");

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DUE_PROPERTY);
	if (prop) {
		dt = icalproperty_get_due (prop);
		ewscal_set_time (msg, "DueDate", &dt);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = icalproperty_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_write_string_parameter(msg, "PercentComplete", NULL, buffer);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dt = icalproperty_get_dtstart (prop);
		ewscal_set_time (msg, "StartDate", &dt);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_STATUS_PROPERTY);
	if (prop) {
		switch (icalproperty_get_status (prop)) {
		case ICAL_STATUS_INPROCESS:
			e_ews_message_write_string_parameter(msg, "Status", NULL, "InProgress");
			break;
		case ICAL_STATUS_COMPLETED:
			e_ews_message_write_string_parameter(msg, "Status", NULL, "Completed");
			break;
		default:
			break;
		}
		ewscal_set_time (msg, "StartDate", &dt);
	}

	e_soap_message_end_element(msg); // "Task"
}

static void
convert_calcomp_to_xml(ESoapMessage *msg, gpointer user_data)
{
	icalcomponent *icalcomp = (icalcomponent*)user_data;

	switch (icalcomponent_isa (icalcomp)) {
	case ICAL_VEVENT_COMPONENT:
		convert_vevent_calcomp_to_xml (msg, user_data);
		break;
	case ICAL_VTODO_COMPONENT:
		convert_vtodo_calcomp_to_xml (msg, user_data);
		break;
	default:
		break;
	}
}

static void
e_cal_backend_ews_remove_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context,
				 const gchar *uid, const gchar *rid, CalObjModType mod);

typedef struct {
	ECalBackendEws *cbews;
	ECalComponent *comp;

	int cb_type; /* 0 - nothing, 1 - create, 2 - update */

	EDataCal *cal;
	EServerMethodContext context;

} EwsAttachmentsData;

static void
ews_create_attachments_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsAttachmentsData *create_data = user_data;
	ECalBackendEwsPrivate *priv = create_data->cbews->priv;
	gchar *change_key;
	GSList *ids, *i;
	GError *error = NULL;
	icalproperty *icalprop;
	icalcomponent *icalcomp;
	icalparameter *icalparam;
	const gchar *comp_uid;

	ids = e_ews_connection_create_attachments_finish (cnc, &change_key, res, &error);

	/* make sure there was no error */
	if (error != NULL) {
		g_warning ("Error while creating attachments: %s\n", error->message);
		g_clear_error (&error);
		return;
	}

	/* get exclusive access to the store */
	e_cal_backend_store_freeze_changes(priv->store);

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

	/* TODO: are we setting the correct attachment ids ? */
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

	e_cal_component_get_uid(create_data->comp, &comp_uid);
	if (create_data->cb_type == 1) {
		e_data_cal_notify_object_created (create_data->cal, create_data->context, error, comp_uid, e_cal_component_get_as_string(create_data->comp));
	} else if (create_data->cb_type == 2) {

	}

	g_slist_free (ids);

	g_object_unref (create_data->cbews);
	g_object_unref (create_data->comp);
	g_free (create_data);
}

static void
ews_delete_attachments_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsAttachmentsData *delete_data = user_data;
	ECalBackendEwsPrivate *priv = delete_data->cbews->priv;
	gchar *change_key;
	GSList *ids;
	GError *error = NULL;
	icalproperty *icalprop;
	icalcomponent *icalcomp;

	ids = e_ews_connection_delete_attachments_finish (cnc, res, &error);

	/* make sure there was no error */
	if (error != NULL) {
		g_warning ("Error while creating attachments: %s\n", error->message);
		g_clear_error (&error);
		return;
	}

	change_key = ids->data;

	/* get exclusive access to the store */
	e_cal_backend_store_freeze_changes(priv->store);

	/* Update change key. id remains the same, but change key changed.*/
	icalcomp = e_cal_component_get_icalcomponent (delete_data->comp);
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

	e_cal_component_commit_sequence (delete_data->comp);

	/* update changes and release access to the store */
	e_cal_backend_store_thaw_changes (priv->store);

	g_slist_free (ids);

	g_slist_free (ids);
	g_free (change_key);
	g_object_unref (delete_data->cbews);
	g_object_unref (delete_data->comp);
	g_free (delete_data);
}

static void
ews_create_object_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCreateData *create_data = user_data;
	ECalBackendEws *cbews = create_data->cbews;
	ECalBackendEwsPrivate *priv = cbews->priv;
	GError *error = NULL;
	GSList *ids = NULL, *attachments = NULL, *i;
	const gchar *comp_uid;
	const EwsId *item_id;
	icalproperty *icalprop;
	icalcomponent *icalcomp;
	guint n_attach;

	/* get a list of ids from server (single item) */
	e_ews_connection_create_items_finish(cnc, res, &ids, &error);

	/* make sure there was no error */
	if (error != NULL) {
		e_data_cal_notify_object_created(create_data->cal, create_data->context, error, NULL, NULL);
		return;
	}

	item_id = e_ews_item_get_id((EEwsItem *)ids->data);
	g_slist_free (ids);

	/* set item id */
	e_cal_component_set_uid(create_data->comp, item_id->id);

	/* attachments */
	n_attach = e_cal_component_get_num_attachments (create_data->comp);
	if (n_attach > 0) {
		EwsAttachmentsData *attach_data = g_new0(EwsAttachmentsData, 1);

		attach_data->cbews = g_object_ref (create_data->cbews);
		attach_data->comp = g_object_ref (create_data->comp);
		attach_data->cal = create_data->cal;
		attach_data->context = create_data->context;
		attach_data->cb_type = 1;

		e_cal_component_get_attachment_list (create_data->comp, &attachments);
		e_ews_connection_create_attachments_start (cnc, EWS_PRIORITY_MEDIUM,
							   item_id, attachments,
							   ews_create_attachments_cb, NULL, attach_data);

		for (i = attachments; i ; i = i->next) g_free (i->data);
		g_slist_free (attachments);
	}

	/* get exclusive access to the store */
	e_cal_backend_store_freeze_changes(priv->store);

	/* set a new ical property containing the change key we got from the exchange server for future use */
	icalprop = icalproperty_new_x (item_id->change_key);
	icalproperty_set_x_name (icalprop, "X-EVOLUTION-CHANGEKEY");
	icalcomp = e_cal_component_get_icalcomponent(create_data->comp);
	icalcomponent_add_property (icalcomp, icalprop);

	/* update component internal data */
	e_cal_component_commit_sequence(create_data->comp);
	put_component_to_store (create_data->cbews, create_data->comp);

	/* notify the backend and the application that a new object was created */
	e_cal_backend_notify_object_created (E_CAL_BACKEND(create_data->cbews), create_data->context);
	e_cal_component_get_uid(create_data->comp, &comp_uid);
	
	if (n_attach == 0)
		e_data_cal_notify_object_created (create_data->cal, create_data->context, error, comp_uid, e_cal_component_get_as_string(create_data->comp));

	/* place new component in our cache */
	PRIV_LOCK (priv);
	g_hash_table_insert (priv->item_id_hash, g_strdup(item_id->id), g_object_ref (create_data->comp));
	PRIV_UNLOCK (priv);

	/* update changes and release access to the store */
	e_cal_backend_store_thaw_changes (priv->store);

	/* Excluded occurrences */
	icalprop = icalcomponent_get_first_property(icalcomp, ICAL_RRULE_PROPERTY);
	if (icalprop != NULL) {
		icalprop = icalcomponent_get_first_property(icalcomp, ICAL_EXDATE_PROPERTY);
		for (; icalprop; icalprop = icalcomponent_get_next_property(icalcomp, ICAL_RRULE_PROPERTY)) {
			e_cal_backend_ews_remove_object (E_CAL_BACKEND (create_data->cbews), create_data->cal, NULL,
							 item_id->id, icalproperty_get_value_as_string (icalprop), CALOBJ_MOD_THIS);
		}
	}

	/* no need to keep reference to the object */
	g_object_unref(create_data->comp);

	/* free memory allocated for create_data & unref contained objects */
	g_object_unref(create_data->cbews);
	g_object_unref(create_data->cal);
	g_free(create_data);
}

static void
e_cal_backend_ews_create_object(ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	EwsCreateData *create_data;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	struct icaltimetype current;
	GError *error = NULL;
	GCancellable *cancellable = NULL;

	/* sanity check */
	e_data_cal_error_if_fail(E_IS_CAL_BACKEND_EWS(backend), InvalidArg);
	e_data_cal_error_if_fail(calobj != NULL && *calobj != '\0', InvalidArg);

	cbews = E_CAL_BACKEND_EWS(backend);
	priv = cbews->priv;

	kind = e_cal_backend_get_kind(E_CAL_BACKEND(backend));

	/* make sure we're not offline */
	if (priv->mode == CAL_MODE_LOCAL) {
		g_propagate_error(&error, EDC_ERROR(RepositoryOffline));
		goto exit;
	}

	g_warning ("start create a movible instance\n%s\n",calobj);
	/* parse ical data */
	icalcomp = icalparser_parse_string(calobj);

	/* make sure data was parsed properly */
	if (!icalcomp) {
		g_propagate_error(&error, EDC_ERROR(InvalidObject));
		goto exit;
	}

	/* make sure ical data we parse is actually an ical component */
	if (kind != icalcomponent_isa(icalcomp)) {
		icalcomponent_free(icalcomp);
		g_propagate_error(&error, EDC_ERROR(InvalidObject));
		goto exit;
	}

	/* prepare new calender component */
	comp = e_cal_component_new();
	e_cal_component_set_icalcomponent(comp, icalcomp);

	current = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
	e_cal_component_set_created(comp, &current);
	e_cal_component_set_last_modified(comp, &current);

	create_data = g_new0(EwsCreateData, 1);
	create_data->cbews = g_object_ref(cbews);
	create_data->comp = comp;
	create_data->cal = g_object_ref(cal);
	create_data->context = context;

	if (e_cal_component_has_attachments (comp))
		e_ews_connection_create_items_start(priv->cnc,
					     EWS_PRIORITY_MEDIUM,
					     "SaveOnly",
					     "SendToNone",
					     priv->folder_id,
					     convert_calcomp_to_xml,
					     icalcomp,
					     ews_create_object_cb,
					     cancellable,
					     create_data);
	else
		/* pass new calendar component data to the exchange server and expect response in the callback */
		e_ews_connection_create_items_start(priv->cnc,
					     EWS_PRIORITY_MEDIUM,NULL,
					     "SendToAllAndSaveCopy",
					     priv->folder_id,
					     convert_calcomp_to_xml,
					     icalcomp,
					     ews_create_object_cb,
					     cancellable,
					     create_data);
	return;

exit:
	e_data_cal_notify_object_created(cal, context, error, NULL, NULL);
}

typedef struct {
	ECalBackendEws *cbews;
	EDataCal *cal;
	ECalComponent *comp;
	ECalComponent *oldcomp;
	EServerMethodContext context;
	gchar *itemid;
	gchar *changekey;
} EwsModifyData;

static void
ews_cal_modify_object_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsModifyData *modify_data = user_data;
	ECalBackendEws *cbews = modify_data->cbews;
	ECalBackendEwsPrivate *priv = cbews->priv;
	GError *error = NULL;
	gchar *comp_str, *comp_str_old;
	GSList *ids = NULL;
	const EwsId *item_id;
	icalproperty *icalprop;
	icalcomponent *icalcomp;
	ECalComponentId *id;
	GSList *original_attachments = NULL, *modified_attachments = NULL, *added_attachments, *removed_attachments, *removed_attachments_ids = NULL, *i;
	const gchar *x_name;
	EwsAttachmentsData *attach_data;

	if (!e_ews_connection_update_items_finish (cnc, res, &ids, &error)) {
		/* The calendar UI doesn't *display* errors unless they have
		   the OtherError code */
		error->code = OtherError;
		return;
	}

	g_object_ref (modify_data->comp);
	g_object_ref (modify_data->oldcomp);

	e_cal_backend_store_freeze_changes(priv->store);

	item_id = e_ews_item_get_id((EEwsItem *)ids->data);

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
	id = e_cal_component_get_id (modify_data->oldcomp);
	e_cal_backend_store_remove_component (cbews->priv->store, id->uid, id->rid);
	put_component_to_store (cbews, modify_data->comp);

	comp_str = e_cal_component_get_as_string (modify_data->comp);
	comp_str_old = e_cal_component_get_as_string (modify_data->oldcomp);

	if (modify_data->context) {
		e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbews), comp_str_old, comp_str);
		e_data_cal_notify_object_modified (modify_data->cal, modify_data->context, error, comp_str_old, comp_str);
	}
	else if (error) {
		g_warning ("Modify object error :  %s\n", error->message);
		g_clear_error (&error);
	}

	PRIV_LOCK (priv);
	g_hash_table_replace (priv->item_id_hash, g_strdup(modify_data->itemid), g_object_ref (modify_data->comp));
	PRIV_UNLOCK (priv);

	e_cal_backend_store_thaw_changes (priv->store);

	/* Attachments */
	e_cal_component_get_attachment_list (modify_data->oldcomp, &original_attachments);
	e_cal_component_get_attachment_list (modify_data->comp, &modified_attachments);

	ewscal_get_attach_differences (original_attachments, modified_attachments, &removed_attachments, &added_attachments);
	g_slist_free (original_attachments);
	g_slist_free (modified_attachments);

	if (added_attachments) {
		attach_data = g_new0(EwsAttachmentsData, 1);

		attach_data->cbews = g_object_ref (modify_data->cbews);
		attach_data->comp = g_object_ref (modify_data->comp);

		e_ews_connection_create_attachments_start (cnc, EWS_PRIORITY_MEDIUM,
							   item_id, added_attachments,
							   ews_create_attachments_cb, NULL, attach_data);

		g_slist_free (added_attachments);
	}
	if (removed_attachments) {
		/* convert attachment uri to attachment id, should have used a hash table somehow */
		icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
		while (icalprop) {
			x_name = icalproperty_get_value_as_string (icalprop);

			for (i = removed_attachments; i; i = i->next)
				if (g_strcmp0 (i->data, x_name)) {
					removed_attachments_ids = g_slist_append (removed_attachments_ids, icalproperty_get_parameter_as_string_r (icalprop, "X-EWS-ATTACHMENTID"));
					break;
				}

			icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY);
		}

		attach_data = g_new0(EwsAttachmentsData, 1);

		attach_data->cbews = g_object_ref (modify_data->cbews);
		attach_data->comp = g_object_ref (modify_data->comp);

		e_ews_connection_delete_attachments_start (cnc, EWS_PRIORITY_MEDIUM,
							   removed_attachments_ids,
							   ews_delete_attachments_cb, NULL, attach_data);

		for (i = removed_attachments_ids; i; i = i->next) free (i->data);
		g_slist_free (removed_attachments_ids);
		g_slist_free (removed_attachments);
	}

	g_object_unref (modify_data->comp);
	g_object_unref (modify_data->oldcomp);

	icalproperty_free (icalprop);
	e_cal_component_free_id (id);
	g_free(comp_str);
	g_free(comp_str_old);
	g_free(modify_data->itemid);
	g_free(modify_data->changekey);
	g_object_unref(modify_data->comp);
	g_object_unref(modify_data->cbews);
	g_object_unref(modify_data->cal);
	g_free(modify_data);
}

static void
convert_vevent_property_to_updatexml (ESoapMessage *msg, const gchar *name, const gchar *value, const gchar * prefix, const gchar *attr_name, const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "CalendarItem");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_vevent_component_to_updatexml(ESoapMessage *msg, gpointer user_data)
{
	EwsModifyData *modify_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (modify_data->comp);
	icalcomponent *icalcomp_old = e_cal_component_get_icalcomponent (modify_data->oldcomp);
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend, dtstart_old, dtend_old;
	icalproperty *prop, *transp;
	const char *org_email_address = NULL, *value = NULL, *old_value = NULL;
	gboolean has_alarms, has_alarms_old;
	gint alarm = 0, alarm_old = 0;

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
					 modify_data->itemid, modify_data->changekey, 0);
	/* subject */
	value = icalcomponent_get_summary (icalcomp);
	old_value = icalcomponent_get_summary (icalcomp_old);
	if (g_ascii_strcasecmp (value, old_value))
		convert_vevent_property_to_updatexml (msg, "Subject", value, "item", NULL, NULL);

	/*description*/
	value = icalcomponent_get_description (icalcomp);
	old_value = icalcomponent_get_description (icalcomp_old);
	if (g_ascii_strcasecmp (value, old_value))
		convert_vevent_property_to_updatexml (msg, "Body", value, "item", "BodyType", "Text");

	/*update alarm items*/
	has_alarms = e_cal_component_has_alarms (modify_data->comp);
	if (has_alarms) {
		alarm = ews_get_alarm (modify_data->comp);
		has_alarms_old = e_cal_component_has_alarms (modify_data->oldcomp);
		if (has_alarms_old)
			alarm_old = ews_get_alarm (modify_data->oldcomp);
		if (!(alarm == alarm_old)) {
			char buf[20];
			snprintf (buf, 20, "%d", alarm);
			convert_vevent_property_to_updatexml (msg, "ReminderIsSet", "true", "item", NULL, NULL);
			convert_vevent_property_to_updatexml (msg, "ReminderMinutesBeforeStart", buf, "item", NULL, NULL);
		}
	}

	/*location*/
	value = icalcomponent_get_location (icalcomp);
	old_value = icalcomponent_get_location (icalcomp_old);
	if (g_ascii_strcasecmp (value, old_value))
		convert_vevent_property_to_updatexml (msg, "Location", value, "calendar", NULL, NULL);

	/*freebusy*/
	transp = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
	value = icalproperty_get_value_as_string (transp);
	transp = icalcomponent_get_first_property (icalcomp_old, ICAL_TRANSP_PROPERTY);
	old_value = icalproperty_get_value_as_string (transp);
	if (g_strcmp0 (value, old_value)) {
		if (!g_strcmp0 (value, "TRANSPARENT"))
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Free" , "calendar", NULL, NULL);
		else
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Busy" , "calendar", NULL, NULL);
	}

	org_email_address = e_ews_collect_orginizer (icalcomp);
	if (g_ascii_strcasecmp (org_email_address, modify_data->cbews->priv->user_email)) {
		e_ews_message_end_item_change (msg);
		return;
	}
	/* Update other properties allowed only for meeting organizers*/
	/*meeting dates*/
	dtstart = icalcomponent_get_dtstart (icalcomp);
	dtend = icalcomponent_get_dtend (icalcomp);
	dtstart_old = icalcomponent_get_dtstart (icalcomp_old);
	dtend_old = icalcomponent_get_dtend (icalcomp_old);
	if (g_strcmp0 (icaltime_as_ical_string (dtstart), icaltime_as_ical_string (dtstart_old)) ||
	    g_strcmp0 (icaltime_as_ical_string (dtend), icaltime_as_ical_string (dtend_old))) {
		e_ews_message_start_set_item_field (msg, "Start", "calendar","CalendarItem");
		ewscal_set_time (msg, "Start", &dtstart);
		e_ews_message_end_set_item_field (msg);

		e_ews_message_start_set_item_field (msg, "End", "calendar", "CalendarItem");
		ewscal_set_time (msg, "End", &dtend);
		e_ews_message_end_set_item_field (msg);
	}

	/*need to test it*/
	e_ews_collect_attendees(icalcomp, &required, &optional, &resource);
	if (required != NULL) {
		e_ews_message_start_set_item_field (msg, "RequiredAttendees", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "RequiredAttendees", required);
		g_slist_free(required);

		e_ews_message_end_set_item_field (msg);
	}
	if (optional != NULL) {
		e_ews_message_start_set_item_field (msg, "OptionalAttendees", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "OptionalAttendees", optional);
		g_slist_free(optional);

		e_ews_message_end_set_item_field (msg);
	}
	if (resource != NULL) {
		e_ews_message_start_set_item_field (msg, "Resources", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "Resources", resource);
		g_slist_free(resource);

		e_ews_message_end_set_item_field (msg);
	}

	/* Recurrence */
	value = NULL; old_value = NULL;
	prop = icalcomponent_get_first_property (icalcomp_old, ICAL_RRULE_PROPERTY);
	if (prop != NULL)
		old_value = icalproperty_get_value_as_string (prop);
	prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL)
		value = icalproperty_get_value_as_string (prop);

	if (g_strcmp0 (value, old_value) && prop != NULL)
		ewscal_set_reccurence(msg, prop, &dtstart);

	if (0 /* Exchange 2010 detected */ && dtstart.zone != dtend.zone) {
		if (dtstart.zone) {
			e_ews_message_start_set_item_field (msg, "StartTimeZone", "calendar", "CalendarItem");
			ewscal_set_timezone (msg, "StartTimeZone", (icaltimezone *)dtstart.zone);
			e_ews_message_end_set_item_field (msg);
		}
		if (dtend.zone) {
			e_ews_message_start_set_item_field (msg, "EndTimeZone", "calendar", "CalendarItem");
			ewscal_set_timezone (msg, "EndTimeZone", (icaltimezone *)dtend.zone);
			e_ews_message_end_set_item_field (msg);
		}
	} else {
		if (dtstart.zone) {
			e_ews_message_start_set_item_field (msg, "MeetingTimeZone", "calendar", "CalendarItem");
			ewscal_set_timezone (msg, "MeetingTimeZone", (icaltimezone *)dtstart.zone);
			e_ews_message_end_set_item_field (msg);
		}
	}

	e_ews_message_end_item_change (msg);
}

static void
convert_vtodo_property_to_updatexml (ESoapMessage *msg, const gchar *name, const gchar *value, const gchar * prefix, const gchar *attr_name, const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "Task");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_vtodo_component_to_updatexml (ESoapMessage *msg, gpointer user_data)
{
	EwsModifyData *modify_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (modify_data->comp);
	icalproperty *prop;
	icaltimetype dt;
	int value;
	char buffer[16];

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
					 modify_data->itemid, modify_data->changekey, 0);

	convert_vtodo_property_to_updatexml (msg, "Subject", icalcomponent_get_summary(icalcomp), "item", NULL, NULL);

	convert_vtodo_property_to_updatexml (msg, "Body", icalcomponent_get_description (icalcomp), "item", "BodyType", "Text");

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DUE_PROPERTY);
	if (prop) {
		dt = icalproperty_get_due (prop);
		e_ews_message_start_set_item_field (msg, "DueDate", "task", "Task");
		ewscal_set_time (msg, "DueDate", &dt);
		e_ews_message_end_set_item_field (msg);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = icalproperty_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_start_set_item_field (msg, "PercentComplete", "task", "Task");
		e_ews_message_write_string_parameter(msg, "PercentComplete", NULL, buffer);
		e_ews_message_end_set_item_field (msg);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dt = icalproperty_get_dtstart (prop);
		e_ews_message_start_set_item_field (msg, "StartDate", "task", "Task");
		ewscal_set_time (msg, "StartDate", &dt);
		e_ews_message_end_set_item_field (msg);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_STATUS_PROPERTY);
	if (prop) {
		switch (icalproperty_get_status (prop)) {
		case ICAL_STATUS_INPROCESS:
			convert_vtodo_property_to_updatexml (msg, "Status", "InProgress", "task", NULL, NULL);
			break;
		case ICAL_STATUS_COMPLETED:
			convert_vtodo_property_to_updatexml (msg, "Status", "Completed", "task", NULL, NULL);
			break;
		case ICAL_STATUS_NONE:
		case ICAL_STATUS_NEEDSACTION:
			convert_vtodo_property_to_updatexml (msg, "Status", "NotStarted", "task", NULL, NULL);
			break;
		default:
			break;
		}
	}

	e_ews_message_end_item_change (msg);
}

static void
convert_component_to_updatexml (ESoapMessage *msg, gpointer user_data)
{
	EwsModifyData *modify_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (modify_data->comp);

	switch (icalcomponent_isa (icalcomp)) {
	case ICAL_VEVENT_COMPONENT:
		convert_vevent_component_to_updatexml (msg, user_data);
		break;
	case ICAL_VTODO_COMPONENT:
		convert_vtodo_component_to_updatexml (msg, user_data);
		break;
	default:
		break;
	}
}

static void
e_cal_backend_ews_modify_object (ECalBackend *backend, EDataCal *cal, EServerMethodContext context,
				 const gchar *calobj, CalObjModType mod)
{
	EwsModifyData *modify_data;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	ECalComponent *comp, *oldcomp;
	icalcomponent *icalcomp;
	gchar *itemid = NULL, *changekey = NULL;
	struct icaltimetype current;
	GError *error = NULL;
	GCancellable *cancellable = NULL;

	e_data_cal_error_if_fail(E_IS_CAL_BACKEND_EWS(backend), InvalidArg);
	e_data_cal_error_if_fail(calobj != NULL && *calobj != '\0', InvalidArg);

	cbews = E_CAL_BACKEND_EWS(backend);
	priv = cbews->priv;
	kind = e_cal_backend_get_kind(E_CAL_BACKEND(backend));

	if (priv->mode == CAL_MODE_LOCAL) {
		g_propagate_error(&error, EDC_ERROR(RepositoryOffline));
		goto exit;
	}

	icalcomp = icalparser_parse_string(calobj);
	if (!icalcomp) {
		g_propagate_error(&error, EDC_ERROR(InvalidObject));
		goto exit;
	}
	if (kind != icalcomponent_isa(icalcomp)) {
		icalcomponent_free(icalcomp);
		g_propagate_error(&error, EDC_ERROR(InvalidObject));
		goto exit;
	}

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &current);

	ews_cal_component_get_item_id (comp, &itemid, &changekey);
	if (!itemid) {
		g_propagate_error(&error, EDC_ERROR_EX(OtherError,
					       "Cannot determine EWS ItemId"));
		g_object_unref (comp);
		goto exit;
	}

	PRIV_LOCK (priv);
	oldcomp = g_hash_table_lookup (priv->item_id_hash, itemid);
	if (!oldcomp) {
		g_propagate_error (&error, EDC_ERROR(ObjectNotFound));
		g_object_unref (comp);
		goto exit;
	}
	PRIV_UNLOCK (priv);

	modify_data = g_new0 (EwsModifyData, 1);
	modify_data->cbews = g_object_ref(cbews);
	modify_data->comp = comp;
	modify_data->oldcomp = oldcomp;
	modify_data->cal = g_object_ref(cal);
	modify_data->context = context;
	modify_data->itemid = itemid;
	modify_data->changekey = changekey;

	e_ews_connection_update_items_start (priv->cnc, EWS_PRIORITY_MEDIUM,
					     "AlwaysOverwrite", "SendAndSaveCopy",
					     "SendToChangedAndSaveCopy", priv->folder_id,
					     convert_component_to_updatexml, modify_data,
					     ews_cal_modify_object_cb, cancellable,
					     modify_data);
	return;

exit:
	if (context)
		e_data_cal_notify_object_modified (cal, context, error, NULL, NULL);
	else if (error) {
		g_warning ("Modify object error :  %s\n", error->message);
		g_clear_error (&error);
	}
}

typedef struct {
	const char *response_type;
	const char *item_id;
	const char *change_key;
} EwsAcceptData;

static gchar *
e_ews_get_icalcomponent_as_mime_content (icalcomponent *vevent)
{
	icalcomponent *vcal;
	char *vcal_str;

	vcal = icalcomponent_new (ICAL_VCALENDAR_COMPONENT);
	icalcomponent_add_property (vcal, icalproperty_new_version("2.0"));
	icalcomponent_add_property (vcal, icalproperty_new_method(ICAL_METHOD_REQUEST));
	icalcomponent_add_component (vcal, icalcomponent_new_clone (vevent));

	vcal_str = icalcomponent_as_ical_string_r ((icalcomponent *)vcal);

	icalcomponent_free (vcal);

	return vcal_str;
}

static void
prepare_create_item_with_mime_content_request(ESoapMessage *msg, gpointer user_data)
{
	gchar *mime_content = (gchar*) user_data;

	/* Prepare CalendarItem node in the SOAP message */
	e_soap_message_start_element(msg, "CalendarItem", NULL, NULL);

	e_ews_message_write_base64_parameter (msg,"MimeContent",NULL,mime_content);
	// end of "CalendarItem"
	e_soap_message_end_element(msg);
}

static void
e_ews_receive_objects_no_exchange_mail (ECalBackendEwsPrivate *priv, icalcomponent *subcomp, GSList *ids, GCancellable *cancellable, GError *error)
{
	gchar *mime_content = e_ews_get_icalcomponent_as_mime_content (subcomp);
	e_ews_connection_create_items (priv->cnc, EWS_PRIORITY_MEDIUM,
							       "SendAndSaveCopy", "SendToNone", NULL,
							       prepare_create_item_with_mime_content_request,
							       mime_content,
							       &ids,
							       cancellable,
							       &error);
	g_free (mime_content);
	/*we still have to send a mail with accept to meeting orginizaer*/
}

static const char*
e_ews_get_current_user_meeting_reponse (icalcomponent *icalcomp, const char *current_user_mail)
{
	icalproperty *attendee;
	const char *attendee_str = NULL, *attendee_mail = NULL;
	for (attendee = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
		attendee != NULL;
		attendee = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY)) {
		attendee_str = icalproperty_get_attendee (attendee);

		if (attendee_str != NULL){
			if (!strncasecmp (attendee_str, "MAILTO:", 7))
				attendee_mail = attendee_str + 7;
			else
				attendee_mail = attendee_str;
			if (g_strcmp0 (attendee_mail, current_user_mail) == 0)
				return icalproperty_get_parameter_as_string (attendee, "PARTSTAT");
		}
	}
	return NULL;
}

static void
prepare_accept_item_request (ESoapMessage *msg, gpointer user_data)
{
	EwsAcceptData *data = user_data;
	const char *response_type = data->response_type;

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa566464%28v=exchg.140%29.aspx
	 * Accept and Decline meeting have same method code (10032)
	 * The real status is reflected at Attendee property PARTSTAT
	 * need to find current user as attendee and make a desision what to do.
	 * Prepare AcceptItem node in the SOAP message */

	e_ews_get_icalcomponent_as_mime_content (NULL);
	
	if (!g_ascii_strcasecmp (response_type, "ACCEPTED"))
		e_soap_message_start_element (msg, "AcceptItem", NULL, NULL);
	else if (!g_ascii_strcasecmp (response_type, "DECLINED"))
		e_soap_message_start_element (msg, "DeclineItem", NULL, NULL);
	else
		e_soap_message_start_element (msg, "TentativelyAcceptItem", NULL, NULL);
	
	e_soap_message_start_element (msg, "ReferenceItemId", NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", data->item_id, NULL, NULL);
	e_soap_message_add_attribute (msg, "ChangeKey", data->change_key, NULL, NULL);
	e_soap_message_end_element (msg); // "ReferenceItemId"

	/* end of "AcceptItem" */
	e_soap_message_end_element (msg);
}

static void
prepare_set_free_busy_status (ESoapMessage *msg, gpointer user_data)
{
	EwsAcceptData *data = user_data;

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM, data->item_id, data->change_key, 0);

	e_ews_message_start_set_item_field (msg, "LegacyFreeBusyStatus", "calendar", "CalendarItem");

	e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus", NULL, "Free");

	e_ews_message_end_set_item_field (msg);

	e_ews_message_end_item_change (msg);
}

static void
e_cal_backend_ews_receive_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	icalcomponent *icalcomp, *subcomp;
	GError *error = NULL;
	icalproperty_method method;
	EwsAcceptData *accept_data;
	GCancellable *cancellable = NULL;

	cbews = E_CAL_BACKEND_EWS(backend);
	priv = cbews->priv;

	/* make sure we're not offline */
	if (priv->mode == CAL_MODE_LOCAL) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
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
		const char *response_type;
		gchar *item_id = NULL, *change_key = NULL;
		GSList *ids = NULL, *l;
		icalproperty *recurrence_id, *transp, *summary;
		char **split_subject;


		/* duplicate the ical component */
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp));

		/*getting a data for meeting request response*/
		response_type = e_ews_get_current_user_meeting_reponse (e_cal_component_get_icalcomponent (comp), priv->user_email);
		ews_cal_component_get_calendar_item_accept_id (comp, &item_id, &change_key);

		switch (method) {
			case ICAL_METHOD_REQUEST:
			case ICAL_METHOD_PUBLISH:
			case ICAL_METHOD_REPLY:
				accept_data = g_new0 (EwsAcceptData, 1);
				accept_data->response_type = response_type;
				accept_data->item_id = item_id;
				accept_data->change_key = change_key;
				e_ews_connection_create_items (priv->cnc, EWS_PRIORITY_MEDIUM,
							       "SendAndSaveCopy", NULL, NULL,
							       prepare_accept_item_request,
							       accept_data,
							       &ids,
							       cancellable,
							       &error);

				if (error && error->code == EWS_CONNECTION_ERROR_INVALIDIDMALFORMED) {
					g_clear_error (&error);
					e_ews_receive_objects_no_exchange_mail (priv, subcomp, ids, cancellable, error);
				}
				if (error)
					/* The calendar UI doesn't *display* errors unless they have
					 * the OtherError code */
					error->code = OtherError;
			else {
				transp = icalcomponent_get_first_property (subcomp, ICAL_TRANSP_PROPERTY);
				if (!g_strcmp0 (icalproperty_get_value_as_string (transp), "TRANSPARENT") &&
				    !g_strcmp0 (response_type, "ACCEPTED")) {
					/*user can accpet meeting but mark it as free in it's calendar
					 the folowing code is updating the exchange meeting status to free */
					for (l = ids; l != NULL; l = g_slist_next (l)) {
						EEwsItem *item = (EEwsItem *) l->data;
						if (item) {
							accept_data->item_id = e_ews_item_get_id (item)->id;
							accept_data->change_key = e_ews_item_get_id (item)->change_key;
							break;
						}
					}
					e_ews_connection_update_items (priv->cnc, 
								       EWS_PRIORITY_MEDIUM,
								       "AlwaysOverwrite",
								       NULL, "SendToNone",
								       NULL,
								       prepare_set_free_busy_status,
								       accept_data,
								       &ids,
								       cancellable,
								       &error);
					if (error)
						error->code = OtherError;
				}
			}
				g_free (item_id);
				g_free (change_key);
				g_free (accept_data);
				/*We have to run sync before any other operations */
				ews_start_sync (cbews);
				break;
			case ICAL_METHOD_CANCEL:
				recurrence_id = icalcomponent_get_first_property (subcomp, ICAL_RECURRENCEID_PROPERTY);
				e_cal_backend_ews_remove_object (backend, cal, NULL, item_id, icalproperty_get_value_as_string (recurrence_id), CALOBJ_MOD_ALL);
				break;
			case ICAL_METHOD_COUNTER:
				/*this is a new time proposal mail from one of the attendees
				 * if we decline the proposal, nothing have to be done
				 * if we accept it we will call to modify_object */
				if (!g_strcmp0 (response_type, "ACCEPTED")) {
					/*we have to edit the meeting subject to remove exchange header*/
					summary = icalcomponent_get_first_property (subcomp, ICAL_SUMMARY_PROPERTY);
					split_subject  = g_strsplit (icalproperty_get_value_as_string(summary), ":", -1);
					icalproperty_set_value_from_string (summary, split_subject[1] , "NO");
					g_strfreev (split_subject);

					e_cal_backend_ews_modify_object (backend, cal, NULL, icalcomponent_as_ical_string(subcomp), CALOBJ_MOD_ALL);
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
	e_data_cal_notify_objects_received (cal, context, error);
}

static const char *
e_cal_get_meeting_cancellation_comment (ECalComponent *comp)
{
	icalproperty *prop;
	prop = icalcomponent_get_first_property (e_cal_component_get_icalcomponent (comp),
		ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name, *x_val;
		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-RETRACT-COMMENT")) {
			return g_strdup (x_val);
			break;
		}

		prop = icalcomponent_get_next_property (e_cal_component_get_icalcomponent (comp),
			ICAL_X_PROPERTY);
	}
	return NULL;

}

static void
ewscal_send_cancellation_email (EEwsConnection *cnc, CamelAddress *from, CamelInternetAddress *recipient, const gchar *subject, const gchar *body, const gchar *calobj)
{
	CamelMimeMessage *message;
	GError *error = NULL;
	CamelMultipart *multi;
	CamelMimePart *text_part, *vcal_part;
	char *ical_str;
	icalcomponent *vcal;

	vcal = icalcomponent_new (ICAL_VCALENDAR_COMPONENT);
	icalcomponent_add_property (vcal, icalproperty_new_version("2.0"));
	icalcomponent_add_property (vcal, icalproperty_new_method(ICAL_METHOD_CANCEL));
	icalcomponent_add_component (vcal, icalcomponent_new_from_string (calobj));

	text_part = camel_mime_part_new ();
	camel_mime_part_set_content (text_part, body, strlen (body), "text/plain");

	vcal_part = camel_mime_part_new ();
	camel_content_type_set_param(CAMEL_DATA_WRAPPER (vcal_part)->mime_type, "charset", "utf-8");
	camel_content_type_set_param(CAMEL_DATA_WRAPPER (vcal_part)->mime_type, "method", "CANCEL");
	ical_str = icalcomponent_as_ical_string_r ((icalcomponent *)vcal);
	camel_mime_part_set_content (vcal_part, ical_str, strlen (ical_str), "text/calendar; method=CANCEL");
	free (ical_str);

	multi = camel_multipart_new ();
	camel_content_type_set_param(CAMEL_DATA_WRAPPER (multi)->mime_type, "type", "multipart/alternative");
	camel_multipart_add_part (multi, text_part);
	camel_multipart_set_boundary (multi, NULL);
	camel_multipart_add_part (multi, vcal_part);
	g_object_unref (text_part);
	g_object_unref (vcal_part);

	message = camel_mime_message_new ();
	camel_mime_message_set_subject (message, subject);
	camel_mime_message_set_from (message, CAMEL_INTERNET_ADDRESS (from));
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, recipient);

	camel_medium_set_content ((CamelMedium *)message, (CamelDataWrapper *)multi);
	g_object_unref (multi);

	camel_ews_utils_create_mime_message (cnc, "SendOnly", NULL, message, 0, from, NULL, NULL, NULL, &error);

	if (error) {
		g_warning ("Failed to send cancellation email\n");
		g_clear_error (&error);
	}

	g_object_unref (message);
	icalcomponent_free (vcal);
}

static void
e_cal_backend_ews_send_objects (ECalBackend *backend, EDataCal *cal, EServerMethodContext context, const gchar *calobj)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	icalcomponent *icalcomp, *subcomp = NULL;
	GError *error = NULL;
	gchar *subcalobj;

	/*we cannot support the send meeting cancleation until we will find solution for calendar:UID*/
	goto exit;

	cbews = E_CAL_BACKEND_EWS(backend);
	priv = cbews->priv;

	/* make sure we're not offline */
	if (priv->mode == CAL_MODE_LOCAL) {
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
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
		const char *new_body_content = NULL, *subject = NULL, *org_email = NULL;
		const gchar *org = NULL, *attendee = NULL;
		icalproperty *prop, *org_prop = NULL;
		CamelInternetAddress *org_addr = camel_internet_address_new ();

		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp));

		new_body_content = e_cal_get_meeting_cancellation_comment(comp);
		subject = icalproperty_get_value_as_string (icalcomponent_get_first_property (subcomp, ICAL_SUMMARY_PROPERTY));

		org_prop = icalcomponent_get_first_property (subcomp, ICAL_ORGANIZER_PROPERTY);
		org = icalproperty_get_organizer(org_prop);
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
			ewscal_send_cancellation_email (priv->cnc, CAMEL_ADDRESS(org_addr), attendee_addr, subject, new_body_content, subcalobj);
			g_object_unref (attendee_addr);
			free (subcalobj);
		}

		g_object_unref (org_addr);
		g_object_unref (comp);
		subcomp = icalcomponent_get_next_component (icalcomp, kind);
	}

	icalcomponent_free (icalcomp);

exit:
	e_data_cal_notify_objects_sent (cal,context,error,NULL,calobj);
}

/* TODO Do not replicate this in every backend */
static icaltimezone *
resolve_tzid (const gchar *tzid, gpointer user_data)
{
	icaltimezone *zone;

	zone = (!strcmp (tzid, "UTC"))
		? icaltimezone_get_utc_timezone ()
		: icaltimezone_get_builtin_timezone_from_tzid (tzid);

	if (!zone)
		zone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (user_data), tzid);

	return zone;
}

static void
put_component_to_store (ECalBackendEws *cbews,
			ECalComponent *comp)
{
	time_t time_start, time_end;
	ECalBackendEwsPrivate *priv;

	priv = cbews->priv;

	e_cal_util_get_component_occur_times (comp, &time_start, &time_end,
				   resolve_tzid, cbews, priv->default_zone,
				   e_cal_backend_get_kind (E_CAL_BACKEND (cbews)));

	e_cal_backend_store_put_component_with_time_range (priv->store, comp, time_start, time_end);
}

typedef struct {
	ECalComponent *comp;
	ECalBackendEws *cbews;
	gchar* itemid;
} EwsAttachmentData;

static void
ews_get_attachments_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsAttachmentData *att_data = user_data;
	GError *error = NULL;
	GSList *uris = NULL, *ids, *i;
	ECalComponentId *id;
	ECalBackendEws *cbews;
	gchar *comp_str, *itemid;
	ECalComponent *comp_att, *cache_comp = NULL;
	icalcomponent *icalcomp;
	icalproperty *icalprop;
	icalparameter *icalparam;

	ids = e_ews_connection_get_attachments_finish	(cnc, res, &uris, &error);

	if (error != NULL) {
		error->code = OtherError;
		return;
	}

	comp_att = att_data->comp;
	cbews = att_data->cbews;
	itemid = att_data->itemid;

	e_cal_component_set_attachment_list (comp_att, uris);

	icalcomp = e_cal_component_get_icalcomponent (comp_att);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	i = ids;
	for (; i && icalprop; i = i->next, icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
		icalparam = icalparameter_new_x (i->data);
		icalparameter_set_xname (icalparam, "X-EWS-ATTACHMENTID");
		icalproperty_add_parameter (icalprop, icalparam);
		g_free (i->data);
	}

	id = e_cal_component_get_id (comp_att);
	cache_comp = e_cal_backend_store_get_component (cbews->priv->store, id->uid, id->rid);
	e_cal_component_free_id (id);

	comp_str = e_cal_component_get_as_string (comp_att);
	put_component_to_store (cbews, comp_att);

	if (cache_comp) {
		gchar *cache_str;

		cache_str = e_cal_component_get_as_string (cache_comp);
		e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbews), cache_str, comp_str);

		g_free (cache_str);

		PRIV_LOCK (cbews->priv);
		g_hash_table_insert (cbews->priv->item_id_hash, g_strdup (itemid), g_object_ref (comp_att));
		PRIV_UNLOCK (cbews->priv);
	}

	g_slist_foreach (uris, (GFunc) g_free, NULL);
	g_slist_free (uris);
	g_free(comp_str);
	g_free(itemid);
	g_object_unref(att_data->comp);
	g_free(att_data);
}

static void
ews_get_attachments (ECalBackendEws *cbews, EEwsItem *item)
{
	gboolean has_attachment = FALSE;

	e_ews_item_has_attachments (item, & has_attachment);
	if (has_attachment) {
		const GSList *attachment_ids;
		const EwsId *item_id;
		EwsAttachmentData *att_data;

		attachment_ids = e_ews_item_get_attachments_ids (item);
		item_id = e_ews_item_get_id (item);
		att_data = g_new0 (EwsAttachmentData, 1);
		att_data->comp = g_hash_table_lookup (cbews->priv->item_id_hash, item_id->id);
		att_data->cbews = cbews;
		att_data->itemid = g_strdup (item_id->id);

		e_ews_connection_get_attachments_start (cbews->priv->cnc,
							EWS_PRIORITY_MEDIUM,
							attachment_ids,
							e_cal_backend_get_cache_dir(E_CAL_BACKEND(cbews)),
							TRUE,
							ews_get_attachments_ready_callback,
							NULL, NULL,
							NULL, att_data);
	}

}

static void
add_item_to_cache (ECalBackendEws *cbews, EEwsItem *item, gchar *uid)
{
	ECalBackendEwsPrivate *priv;
	icalcomponent_kind kind;
	icalcomponent *vtimezone, *icalcomp, *vcomp;
	const gchar *mime_content;

	kind = e_cal_backend_get_kind ((ECalBackend *) cbews);
	priv = cbews->priv;

	if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_TASK){
		icalproperty *icalprop;
		icaltimetype due_date, start_date, complete_date, created;
		icalproperty_status status  = ICAL_STATUS_NONE;
		icalproperty_class class = ICAL_CLASS_NONE;
		const char *ews_task_status, *sensitivity;
		EwsImportance item_importance;
		int priority = 5;
		gboolean has_this_date = FALSE;

		vcomp = icalcomponent_new (ICAL_VCALENDAR_COMPONENT);
		/*subject*/
		icalcomp = icalcomponent_new (ICAL_VTODO_COMPONENT);
		icalprop = icalproperty_new_summary (e_ews_item_get_subject (item));
		icalcomponent_add_property (icalcomp, icalprop);
		/*status*/
		ews_task_status = e_ews_item_get_status (item);
		if (!g_strcmp0(ews_task_status, "NotStarted") == 0) {
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
		icalprop  = icalproperty_new_percentcomplete (atoi (e_ews_item_get_percent_complete (item)));
		icalcomponent_add_property (icalcomp, icalprop);

		/*due date*/
		e_ews_item_task_has_due_date (item, &has_this_date);
		if (has_this_date) {
			due_date = icaltime_from_timet_with_zone (e_ews_item_get_due_date (item), 0, priv->default_zone);
			icalprop = icalproperty_new_due (due_date);
			icalcomponent_add_property (icalcomp, icalprop);
		}

		/*start date*/
		has_this_date = FALSE;
		e_ews_item_task_has_start_date (item, &has_this_date);
		if (has_this_date) {
			start_date = icaltime_from_timet_with_zone (e_ews_item_get_start_date (item), 0, priv->default_zone);
			icalprop = icalproperty_new_dtstart (start_date);
			icalcomponent_add_property (icalcomp, icalprop);
		}

		/*complete date*/
		has_this_date = FALSE;
		e_ews_item_task_has_complete_date (item, &has_this_date);
		if (has_this_date) {
			complete_date = icaltime_from_timet_with_zone (e_ews_item_get_complete_date (item), 0, priv->default_zone);
			icalprop = icalproperty_new_completed (complete_date);
			icalcomponent_add_property (icalcomp, icalprop);
		}

		/*date time created*/
		created = icaltime_from_timet_with_zone (e_ews_item_get_date_created (item), 0, priv->default_zone);
		icalprop = icalproperty_new_created (created);
		icalcomponent_add_property (icalcomp, icalprop);

		/*priority*/
		item_importance = e_ews_item_get_importance (item);
		if (item_importance == EWS_ITEM_HIGH)
			priority = 3;
		else if (item_importance == EWS_ITEM_LOW)
			priority = 7;
		icalprop = icalproperty_new_priority (priority);
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
		if (!(e_ews_item_get_delegator (item)== NULL)) {
			const char *task_owner = e_ews_item_get_delegator (item);
			GSList *mailboxes = NULL, *l;
			GError *error = NULL;
			gboolean includes_last_item;
			char *mailtoname;
			icalparameter *param;

			/*The task owner according to Exchange is current user, even that the task was assigned by
			 *someone else. I'm making the current user attendee and task delegator will be a task organizer */

			mailtoname = g_strdup_printf ("mailto:%s", priv->user_email);
			icalprop = icalproperty_new_attendee (mailtoname);
			g_free(mailtoname);

			param = icalparameter_new_cn (e_ews_item_get_owner (item));
			icalproperty_add_parameter (icalprop, param);
			icalcomponent_add_property (icalcomp, icalprop);

			/* get delegator mail box*/
			e_ews_connection_resolve_names	(priv->cnc, EWS_PRIORITY_MEDIUM, task_owner,
						 EWS_SEARCH_AD, NULL, FALSE, &mailboxes, NULL,
						 &includes_last_item, NULL, &error);

			for (l = mailboxes; l != NULL; l = g_slist_next (l)) {
				EwsMailbox *mb = l->data;

				mailtoname = g_strdup_printf ("mailto:%s", mb->email);
				icalprop = icalproperty_new_organizer (mailtoname);
				param = icalparameter_new_cn (mb->name);
				icalproperty_add_parameter (icalprop, param);
				icalcomponent_add_property (icalcomp, icalprop);

				g_free (mailtoname);
				g_free (mb->email);
				g_free (mb->name);
				g_free (mb);
			}
			g_slist_free (mailboxes);
		}


		icalcomponent_add_component (vcomp,icalcomp);
	} else {
		struct icaltimetype dt;
		icaltimezone *zone;
		mime_content = e_ews_item_get_mime_content (item);
		vcomp = icalparser_parse_string (mime_content);

		/* Add the timezone */
		vtimezone = icalcomponent_get_first_component (vcomp, ICAL_VTIMEZONE_COMPONENT);
		if (vtimezone) {
			zone = icaltimezone_new ();
			icaltimezone_set_component (zone, icalcomponent_new_clone (vtimezone));
			if (icaltimezone_get_tzid(zone))
				e_cal_backend_store_put_timezone (priv->store, zone);

			icaltimezone_free (zone, TRUE);
		}

		if ((zone = (icaltimezone *)e_cal_backend_store_get_timezone(priv->store, e_ews_item_get_tzid (item))) == NULL)
			zone = (icaltimezone *)icaltimezone_get_builtin_timezone(e_ews_item_get_tzid (item));

		if (zone) {
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
	/* Vevent or Vtodo */
	icalcomp = icalcomponent_get_first_component (vcomp, kind);
	if (icalcomp) {
		ECalComponent *comp, *cache_comp = NULL;
		icalproperty *icalprop, *freebusy;
		const EwsId *item_id;
		ECalComponentId *id;
		gchar *comp_str;
		const GSList *l = NULL;
		const char *org_email_address = e_ews_collect_orginizer(icalcomp);

		item_id = e_ews_item_get_id (item);

		/* Attendees */
		for (l = e_ews_item_get_attendees (item); l != NULL; l = g_slist_next (l)) {
			icalparameter *param;
			char *mailtoname;
			EwsAttendee *attendee = (EwsAttendee *)l->data;
			/*remove orginizer for attendeees list*/
			if (g_ascii_strcasecmp (org_email_address, attendee->mailbox->email)== 0)
				continue;

			mailtoname = g_strdup_printf("mailto:%s", attendee->mailbox->email);
			icalprop = icalproperty_new_attendee(mailtoname);
			g_free(mailtoname);

			param = icalparameter_new_cn(attendee->mailbox->name);
			icalproperty_add_parameter(icalprop, param);

			if (g_ascii_strcasecmp(attendee->attendeetype, "Required") == 0)
				param = icalparameter_new_role(ICAL_ROLE_REQPARTICIPANT);
			else
				param = icalparameter_new_role(ICAL_ROLE_OPTPARTICIPANT);
			icalproperty_add_parameter(icalprop, param);

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
			icalproperty_add_parameter(icalprop, param);

			icalcomponent_add_property(icalcomp, icalprop);
		}

		/* Free/Busy */
		freebusy = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
		if (!freebusy && (e_ews_item_get_item_type(item)!=E_EWS_ITEM_TYPE_TASK)) {
			/* Busy by default */
			freebusy = icalproperty_new_transp(ICAL_TRANSP_OPAQUE);
			icalcomponent_add_property (icalcomp, freebusy);
		}
		for (icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
				icalprop != NULL;
				icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY)) {

			if (g_strcmp0(icalproperty_get_x_name(icalprop), "X-MICROSOFT-CDO-BUSYSTATUS") == 0) {
				if (g_strcmp0(icalproperty_get_value_as_string(icalprop), "BUSY") == 0) {
					icalproperty_set_transp(freebusy, ICAL_TRANSP_OPAQUE);
				} else {
					icalproperty_set_transp(freebusy, ICAL_TRANSP_TRANSPARENT);
				}

				break;
			}
		}

		if (uid) {
			/* Exchange sets RRULE even on the children, which is broken */
			icalprop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
			if (icalprop)
				icalcomponent_remove_property (icalcomp, icalprop);
		}
		/* The server sets a UID here, but it bears no relation to the ItemID.
		   Override it to the ItemId instead. */
		icalcomponent_set_uid (icalcomp, uid?:item_id->id);

		icalprop = icalproperty_new_x (item_id->change_key);
		icalproperty_set_x_name (icalprop, "X-EVOLUTION-CHANGEKEY");
		icalcomponent_add_property (icalcomp, icalprop);

		comp = e_cal_component_new ();
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp));

		id = e_cal_component_get_id (comp);
		cache_comp = e_cal_backend_store_get_component (priv->store, id->uid, id->rid);
		e_cal_component_free_id (id);

		comp_str = e_cal_component_get_as_string (comp);
		put_component_to_store (cbews, comp);

		if (!cache_comp) {
			e_cal_backend_notify_object_created (E_CAL_BACKEND (cbews), comp_str);
		} else {
			gchar *cache_str;

			cache_str = e_cal_component_get_as_string (cache_comp);
			e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbews), cache_str, comp_str);

			g_free (cache_str);
		}

		g_free (comp_str);

		PRIV_LOCK (priv);
		g_hash_table_insert (priv->item_id_hash, g_strdup (item_id->id), g_object_ref (comp));
		PRIV_UNLOCK (priv);

		g_object_unref (comp);
	}
	icalcomponent_free (vcomp);
}

struct _ews_sync_data {
	ECalBackendEws *cbews;
	gchar *sync_state;
	gboolean sync_pending;
	gchar *master_uid;
};

static void
ews_cal_get_items_ready_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GSList *items = NULL, *l;
	struct _ews_sync_data *sync_data, *sub_sync_data;
	GError *error = NULL;

	sync_data = (struct _ews_sync_data *) user_data;
	cbews = sync_data->cbews;
	priv = cbews->priv;
	cnc = (EEwsConnection *) obj;

	e_ews_connection_get_items_finish	(cnc, res, &items, &error);
	if (error != NULL) {
		g_warning ("Unable to get items %s \n", error->message);

		PRIV_LOCK (priv);
		priv->refreshing = FALSE;
		PRIV_UNLOCK (priv);

		g_clear_error (&error);
		goto exit;
	}

	/* fetch modified occurrences */
	for (l = items; l != NULL; l = g_slist_next(l)) {
		const GSList *modified_occurrences = e_ews_item_get_modified_occurrences (l->data);

		if (modified_occurrences) {
			const EwsId *item_id = e_ews_item_get_id (l->data);

			sub_sync_data = g_new0 (struct _ews_sync_data, 1);
			sub_sync_data->cbews = sync_data->cbews;
			sub_sync_data->master_uid = g_strdup (item_id->id);

			e_ews_connection_get_items_start(g_object_ref(cnc), EWS_PRIORITY_MEDIUM,
					modified_occurrences,
					"IdOnly", "item:Attachments item:HasAttachments item:MimeContent calendar:ModifiedOccurrences calendar:RequiredAttendees calendar:OptionalAttendees",
					FALSE, NULL, ews_cal_get_items_ready_cb, NULL, NULL, NULL,
					(gpointer) sub_sync_data);

			g_object_unref(cnc);
		}
	}

	e_cal_backend_store_freeze_changes (priv->store);
	for (l = items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;

		if (item) {
			add_item_to_cache (cbews, item, sync_data->master_uid);
			ews_get_attachments (cbews, item);
			g_object_unref (item);
		}
	}
	e_cal_backend_store_thaw_changes (priv->store);


	if (sync_data->sync_state)
		e_cal_backend_store_put_key_value (priv->store, SYNC_KEY, sync_data->sync_state);
	if (sync_data->sync_pending)
		e_ews_connection_sync_folder_items_start
						(g_object_ref (priv->cnc), EWS_PRIORITY_MEDIUM,
						 sync_data->sync_state, priv->folder_id,
						 "IdOnly", NULL,
						 EWS_MAX_FETCH_COUNT,
						 ews_cal_sync_items_ready_cb,
						 NULL, cbews);

exit:
	g_free (sync_data->master_uid);
	g_free (sync_data->sync_state);
	g_free (sync_data);
	g_object_unref (cnc);
}

static void
ews_cal_sync_items_ready_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GSList *items_created = NULL, *items_updated = NULL;
	GSList *items_deleted = NULL, *l[2], *m, *cal_item_ids = NULL, *task_item_ids = NULL;
	gchar *sync_state = NULL;
	gboolean includes_last_item;
	GError *error = NULL;
	struct _ews_sync_data *sync_data = NULL;
	gint i;

	cnc = (EEwsConnection *) obj;
	cbews = (ECalBackendEws *) user_data;
	priv = cbews->priv;

	e_ews_connection_sync_folder_items_finish	(cnc, res, &sync_state, &includes_last_item,
							 &items_created, &items_updated,
							 &items_deleted, &error);

	PRIV_LOCK (priv);

	if (priv->opening_ctx) {
		/* Report success/failure for calendar open if pending,
		   translating an authentication failure into something that
		   will be recognised and handled appropriately */
		if (error && error->domain == EWS_CONNECTION_ERROR &&
		    error->code == EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED) {
			e_data_cal_notify_open(priv->opening_cal, priv->opening_ctx,
					       EDC_ERROR(AuthenticationFailed));
		} else {
			e_data_cal_notify_open(priv->opening_cal, priv->opening_ctx,
					       error?g_error_copy (error):NULL);
		}
		priv->opening_ctx = NULL;
		priv->opening_cal = NULL;
		if (error) {
			priv->cnc = NULL;
			g_object_unref (cnc);
		}

	}
	PRIV_UNLOCK (priv);

	if (error != NULL) {
		g_warning ("Unable to Sync changes %s \n", error->message);

		PRIV_LOCK (priv);
		priv->refreshing = FALSE;
		PRIV_UNLOCK (priv);

		g_clear_error (&error);
		g_object_unref (cnc);
		return;
	}

	l[0] = items_created;
	l[1] = items_updated;

	for (i = 0; i < 2; i++)	{
		for (;l[i] != NULL; l[i] = g_slist_next (l[i])) {
			EEwsItem *item = (EEwsItem *) l[i]->data;
			EEwsItemType type = e_ews_item_get_item_type (item);
			const EwsId *id;

			id = e_ews_item_get_id (item);
			if (type == E_EWS_ITEM_TYPE_CALENDAR_ITEM)
				cal_item_ids = g_slist_append (cal_item_ids, g_strdup (id->id));
			else if (type == E_EWS_ITEM_TYPE_TASK)
				task_item_ids = g_slist_append (task_item_ids, g_strdup (id->id));
			g_object_unref (item);
		}
	}

	e_cal_backend_store_freeze_changes (priv->store);
	for (m = items_deleted; m != NULL; m = g_slist_next (m)) {
		gchar *item_id = (gchar *) m->data;
		ECalComponent *comp;

		PRIV_LOCK (priv);
		comp = g_hash_table_lookup (priv->item_id_hash, item_id);
		PRIV_UNLOCK (priv);

		if (comp)
			ews_cal_delete_comp(cbews, comp, item_id);

		g_free (m->data);
	}
	e_cal_backend_store_thaw_changes (priv->store);

	if (!cal_item_ids && !task_item_ids && !includes_last_item) {
		e_cal_backend_store_put_key_value (priv->store, SYNC_KEY, sync_state);
		e_ews_connection_sync_folder_items_start
						(g_object_ref (priv->cnc), EWS_PRIORITY_MEDIUM,
						 sync_state, priv->folder_id,
						 "IdOnly", NULL,
						 EWS_MAX_FETCH_COUNT,
						 ews_cal_sync_items_ready_cb,
						 NULL, cbews);
		g_free (sync_state);
		goto exit;
	}

	if (cal_item_ids || task_item_ids) {
		sync_data = g_new0 (struct _ews_sync_data, 1);
		sync_data->cbews = cbews;
		sync_data->sync_state = sync_state;
		sync_data->sync_pending = !includes_last_item;
	}

	if (cal_item_ids)
		e_ews_connection_get_items_start (g_object_ref (cnc),
						  EWS_PRIORITY_MEDIUM,
						  cal_item_ids,
						  "IdOnly",
						  "item:Attachments item:HasAttachments item:MimeContent calendar:TimeZone calendar:ModifiedOccurrences calendar:RequiredAttendees calendar:OptionalAttendees",
						  FALSE, NULL,
						  ews_cal_get_items_ready_cb,
						  NULL, NULL, NULL,
						  (gpointer) sync_data);

	if (task_item_ids)
		e_ews_connection_get_items_start (g_object_ref (cnc), EWS_PRIORITY_MEDIUM,
						  task_item_ids,
						  "AllProperties",
						  NULL,
						  FALSE,
						  NULL,
						  ews_cal_get_items_ready_cb,
						  NULL, NULL, NULL,
						  (gpointer) sync_data);


exit:
	g_object_unref (cnc);
	if (cal_item_ids) {
		g_slist_foreach (cal_item_ids, (GFunc) g_free, NULL);
		g_slist_free (cal_item_ids);
	}
	if (task_item_ids) {
		g_slist_foreach (task_item_ids, (GFunc) g_free, NULL);
		g_slist_free (task_item_ids);
	}

	if (items_created)
		g_slist_free (items_created);
	if (items_updated)
		g_slist_free (items_updated);
	if (items_deleted)
		g_slist_free (items_deleted);
}

static gboolean
ews_start_sync	(gpointer data)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	const gchar *sync_state;

	cbews = (ECalBackendEws *) data;
	priv = cbews->priv;

	PRIV_LOCK (priv);
	priv->refreshing = TRUE;
	PRIV_UNLOCK (priv);

	sync_state = e_cal_backend_store_get_key_value (priv->store, SYNC_KEY);
	e_ews_connection_sync_folder_items_start
						(g_object_ref (priv->cnc), EWS_PRIORITY_MEDIUM,
						 sync_state, priv->folder_id,
						 "IdOnly", NULL,
						 EWS_MAX_FETCH_COUNT,
						 ews_cal_sync_items_ready_cb,
						 NULL, cbews);
	return TRUE;
}

static void
ews_cal_start_refreshing (ECalBackendEws *cbews)
{
	ECalBackendEwsPrivate *priv;

	priv = cbews->priv;

	PRIV_LOCK (priv);

	if	(!priv->refresh_timeout &&
 		 priv->mode == CAL_MODE_REMOTE &&
		 priv->cnc) {
			ews_start_sync (cbews);
			priv->refresh_timeout = g_timeout_add_seconds
							(REFRESH_INTERVAL,
							 (GSourceFunc) ews_start_sync,
							  cbews);
	}

	PRIV_UNLOCK (priv);
}

static void
e_cal_backend_ews_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendEws *cbews;
	GList *objects = NULL;
	GError *err = NULL;

	cbews = E_CAL_BACKEND_EWS (backend);

	ews_cal_start_refreshing (cbews);
	cal_backend_ews_get_object_list (backend, e_data_cal_view_get_text (query),
					 &objects, &err);
	if (err) {
		e_data_cal_view_notify_done (query, err);
		g_error_free (err);
		return;
	}

	/* notify listeners of all objects */
	if (objects) {
		e_data_cal_view_notify_objects_added (query, (const GList *) objects);

		/* free memory */
		g_list_foreach (objects, (GFunc) g_free, NULL);
		g_list_free (objects);
	}

	e_data_cal_view_notify_done (query, NULL);
}

static void
e_cal_backend_ews_refresh(ECalBackend *backend, EDataCal *cal, EServerMethodContext context) {
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GError *error = NULL;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

	PRIV_LOCK(priv);
	ews_start_sync(cbews);
	PRIV_UNLOCK(priv);

	e_data_cal_notify_refresh(cal, context, error);
}

typedef struct {
	ECalBackendEws *cbews;
	EDataCal *cal;
	EServerMethodContext context;
	GList *users;
	time_t start;
	time_t end;
	icaltimezone *timezone;
} EwsFreeBusyData;

static void
prepare_free_busy_request (ESoapMessage *msg, gpointer user_data)
{
	EwsFreeBusyData *free_busy_data = user_data;
	GList *addr;
	icaltimetype t_start, t_end;

	ewscal_set_availability_timezone (msg, free_busy_data->timezone);

	e_soap_message_start_element(msg, "MailboxDataArray", "messages", NULL);

	for (addr = free_busy_data->users; addr; addr = addr->next) {
		e_soap_message_start_element(msg, "MailboxData", NULL, NULL);

		e_soap_message_start_element(msg, "Email", NULL, NULL);
		e_ews_message_write_string_parameter (msg, "Address", NULL, addr->data);
		e_soap_message_end_element(msg); /* "Email" */

		e_ews_message_write_string_parameter (msg, "AttendeeType", NULL, "Required");
		e_ews_message_write_string_parameter (msg, "ExcludeConflicts", NULL, "false");

		e_soap_message_end_element(msg); /* "MailboxData" */
	}

	e_soap_message_end_element(msg); /* "MailboxDataArray" */

	e_soap_message_start_element(msg, "FreeBusyViewOptions", NULL, NULL);

	e_soap_message_start_element(msg, "TimeWindow", NULL, NULL);
	t_start = icaltime_from_timet_with_zone (free_busy_data->start, 0, free_busy_data->timezone);
	t_end = icaltime_from_timet_with_zone (free_busy_data->end, 0, free_busy_data->timezone);
	ewscal_set_time (msg, "StartTime", &t_start);
	ewscal_set_time (msg, "EndTime", &t_end);
	e_soap_message_end_element(msg); /* "TimeWindow" */

	e_ews_message_write_string_parameter (msg, "MergedFreeBusyIntervalInMinutes", NULL, "60");
	e_ews_message_write_string_parameter (msg, "RequestedView", NULL, "DetailedMerged");

	e_soap_message_end_element(msg); /* "FreeBusyViewOptions" */
}

static void
ews_cal_get_free_busy_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = (EEwsConnection *)obj;
	EwsFreeBusyData *free_busy_data = user_data;
	GSList *free_busy_sl = NULL, *i;
	GList *free_busy = NULL, *j;
	GError *error = NULL;

	if (!e_ews_connection_get_free_busy_finish (cnc, res, &free_busy_sl, &error)) {
		error->code = OtherError;
		goto done;
	}
	
	for (i = free_busy_sl, j = free_busy_data->users; i && j; i = i->next, j = j->next) {
		/* add attendee property */
		icalcomponent_add_property((icalcomponent *)i->data, icalproperty_new_attendee (j->data));

		free_busy = g_list_append (free_busy, icalcomponent_as_ical_string_r (i->data));
	}
	g_slist_free (free_busy_sl);

done:
	e_data_cal_notify_free_busy (free_busy_data->cal, free_busy_data->context, error, free_busy);

	g_object_unref (free_busy_data->cal);
	g_object_unref (free_busy_data->cbews);
	g_free (free_busy_data);
}

static void
e_cal_backend_ews_get_free_busy (ECalBackend *backend, EDataCal *cal,
				 EServerMethodContext context, GList *users,
				 time_t start, time_t end)
{
	ECalBackendEws *cbews = E_CAL_BACKEND_EWS (backend);
	ECalBackendEwsPrivate *priv = cbews->priv;
	GError *error = NULL;
	EwsFreeBusyData *free_busy_data;
	GCancellable *cancellable = NULL;

	/* make sure we're not offline */
	if (priv->mode == CAL_MODE_LOCAL)
	{
		g_propagate_error (&error, EDC_ERROR (RepositoryOffline));
		goto exit;
	}

	/* EWS can support only 100 identities, which is the maximum number of identities that the Web service method can request
	 see http://msdn.microsoft.com/en-us/library/aa564001%28v=EXCHG.140%29.aspx*/
	if (g_list_length (users) > 100)
	{
		g_propagate_error (&error, EDC_ERROR (SearchSizeLimitExceeded));
		goto exit;
	}

	free_busy_data = g_new0 (EwsFreeBusyData, 1);
	free_busy_data->cbews = g_object_ref (cbews);
	free_busy_data->cal = g_object_ref (cal);
	free_busy_data->context = context;
	free_busy_data->users = users;
	free_busy_data->start = start;
	free_busy_data->end = end;
	free_busy_data->timezone = priv->default_zone;

	e_ews_connection_get_free_busy_start (priv->cnc,
					      EWS_PRIORITY_MEDIUM,
					      prepare_free_busy_request,
					      free_busy_data,
					      ews_cal_get_free_busy_cb,
					      cancellable,
					      free_busy_data);

	return;

exit:
	e_data_cal_notify_free_busy (cal, context, error, NULL);
}

static void
e_cal_backend_ews_dispose (GObject *object)
{
	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
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
	g_static_rec_mutex_free (&priv->rec_mutex);

	/* TODO Cancel all the server requests */
	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

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

	if (priv->default_zone) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}

	if (priv->refresh_timeout) {
		g_source_remove (priv->refresh_timeout);
		priv->refresh_timeout = 0;
	}

	g_hash_table_destroy (priv->item_id_hash);

	g_free (priv);
	cbews->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Object initialization function for the file backend */
static void
e_cal_backend_ews_init (ECalBackendEws *cbews)
{
	ECalBackendEwsPrivate *priv;

	priv = g_new0 (ECalBackendEwsPrivate, 1);

	/* create the mutex for thread safety */
	g_static_rec_mutex_init (&priv->rec_mutex);
	priv->item_id_hash = g_hash_table_new_full
						(g_str_hash, g_str_equal,
						 (GDestroyNotify) g_free,
						 (GDestroyNotify) g_object_unref);

	cbews->priv = priv;
}

/* Class initialization function for the gw backend */
static void
e_cal_backend_ews_class_init (ECalBackendEwsClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;

	parent_class = g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_ews_dispose;
	object_class->finalize = e_cal_backend_ews_finalize;

	/* Property accessors */
	backend_class->is_read_only = e_cal_backend_ews_is_read_only;
	backend_class->get_cal_address = e_cal_backend_ews_get_cal_address;
	backend_class->get_static_capabilities = e_cal_backend_ews_get_static_capabilities;
	backend_class->set_default_zone = e_cal_backend_ews_set_default_zone;
	backend_class->is_loaded = e_cal_backend_ews_is_loaded;
	backend_class->internal_get_default_timezone = e_cal_backend_ews_internal_get_default_timezone;
	backend_class->get_mode = e_cal_backend_ews_get_mode;
	backend_class->set_mode = e_cal_backend_ews_set_mode;
	backend_class->get_ldap_attribute = e_cal_backend_ews_get_ldap_attribute;

	/* Many of these can be moved to Base class */
	backend_class->add_timezone = e_cal_backend_ews_add_timezone;
	backend_class->get_default_object = e_cal_backend_ews_get_default_object;
	backend_class->get_timezone = e_cal_backend_ews_get_timezone;

	backend_class->internal_get_timezone = e_cal_backend_ews_internal_get_timezone;

	backend_class->open = e_cal_backend_ews_open;
	backend_class->refresh = e_cal_backend_ews_refresh;
	backend_class->get_object = e_cal_backend_ews_get_object;
	backend_class->get_object_list = e_cal_backend_ews_get_object_list;
	backend_class->remove = e_cal_backend_ews_remove;

	backend_class->start_query = e_cal_backend_ews_start_query;

	backend_class->discard_alarm = e_cal_backend_ews_discard_alarm;

	backend_class->create_object = e_cal_backend_ews_create_object;
	backend_class->modify_object = e_cal_backend_ews_modify_object;

	backend_class->remove_object = e_cal_backend_ews_remove_object;

	backend_class->receive_objects = e_cal_backend_ews_receive_objects;
	backend_class->send_objects = e_cal_backend_ews_send_objects;
//	backend_class->get_attachment_list = e_cal_backend_ews_get_attachment_list;
	backend_class->get_free_busy = e_cal_backend_ews_get_free_busy;
//	backend_class->get_changes = e_cal_backend_ews_get_changes;
}
