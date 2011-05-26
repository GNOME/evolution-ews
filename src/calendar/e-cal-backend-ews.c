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

	e_ews_message_start_set_item_field (msg, "ReminderIsSet","item");

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
	GSList *ids;
} EwsRemoveData;

static void
ews_cal_remove_object_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EwsRemoveData *remove_data = user_data;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (!g_simple_async_result_propagate_error(simple, &error)) {
		/* FIXME: This is horrid. Will bite us when we start to delete
		   more than one item at a time... */
		ews_cal_delete_comp (remove_data->cbews, remove_data->comp,
				     g_slist_nth_data(remove_data->ids, 0));
	} else {
		/* The calendar UI doesn't *display* errors unless they have
		   the OtherError code */
		error->code = OtherError;
	}

	e_data_cal_notify_remove (remove_data->cal, remove_data->context, error);

	g_slist_foreach (remove_data->ids, (GFunc) g_free, NULL);
	g_slist_free (remove_data->ids);
	g_object_unref(remove_data->cbews);
	g_object_unref(remove_data->comp);
	g_object_unref(remove_data->cal);
	g_free(remove_data);
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
	gchar *itemid = NULL;

	e_data_cal_error_if_fail (E_IS_CAL_BACKEND_EWS (cbews), InvalidArg);

	priv = cbews->priv;

	/* We don't handle recurring appointments yet */
	if (rid) {
		g_propagate_error(&error, EDC_ERROR_EX(OtherError,
		    "Removal of individual recurrences not yet supported"));
		goto exit;
	}

	PRIV_LOCK (priv);

	comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	if (!comp) {
		g_propagate_error (&error, EDC_ERROR(ObjectNotFound));
		goto exit;
	}

	PRIV_UNLOCK (priv);

	ews_cal_component_get_item_id (comp, &itemid, NULL);
	if (!itemid) {
		g_propagate_error(&error, EDC_ERROR_EX(OtherError,
					       "Cannot determine EWS ItemId"));
		g_object_unref (comp);
		goto exit;
	}

	remove_data = g_new0 (EwsRemoveData, 1);
	remove_data->cbews = g_object_ref(cbews);
	remove_data->comp = comp;
	remove_data->cal = g_object_ref(cal);
	remove_data->context = context;
	remove_data->ids = g_slist_append (NULL, (gpointer)itemid);

	e_ews_connection_delete_items_start (priv->cnc, EWS_PRIORITY_MEDIUM, remove_data->ids,
					     EWS_HARD_DELETE, EWS_SEND_TO_NONE, FALSE,
					     ews_cal_remove_object_cb, NULL,
					     remove_data);
	return;

exit:
	e_data_cal_notify_remove (cal, context, error);
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
convert_calcomp_to_xml(ESoapMessage *msg, gpointer user_data)
{
	icalcomponent *icalcomp = (icalcomponent*)user_data;
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend;
	icalproperty *prop;

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa564690.aspx */

	/* Prepare CalendarItem node in the SOAP message */
	e_soap_message_start_element(msg, "CalendarItem", NULL, NULL);

	/* subject */
	e_ews_message_write_string_parameter(msg, "Subject", NULL,  icalcomponent_get_summary(icalcomp));

	/* description */
	e_ews_message_write_string_parameter_with_attribute(msg, "Body", NULL, icalcomponent_get_description(icalcomp), "BodyType", "Text");

	/* start time, end time and meeting time zone */
	dtstart = icalcomponent_get_dtstart (icalcomp);
	dtend = icalcomponent_get_dtend (icalcomp);

	ewscal_set_time (msg, "Start", &dtstart);
	ewscal_set_time (msg, "End", &dtend);
	/* We have to do the time zone(s) later, or the server rejects the request */

	/* location */
	e_ews_message_write_string_parameter(msg, "Location", NULL, icalcomponent_get_location(icalcomp));

	/* Recurrence */
	prop = icalcomponent_get_first_property(icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL) {
		ewscal_set_reccurence(msg, prop, &dtstart);
	}
	
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

	/* TODO:attachments */

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
ews_create_object_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	EwsCreateData *create_data = user_data;
	ECalBackendEws *cbews = create_data->cbews;
	ECalBackendEwsPrivate *priv = cbews->priv;
	GError *error = NULL;
	GSList *ids = NULL;
	const gchar *comp_uid;
	const EwsId *item_id;
	icalproperty *icalprop;
	icalcomponent *icalcomp;

	/* get a list of ids from server (single item) */
	e_ews_connection_create_items_finish(cnc, res, &ids, &error);

	/* make sure there was no error */
	if (error != NULL) {
		e_data_cal_notify_object_created(create_data->cal, create_data->context, error, NULL, NULL);
		return;
	}

	/* get exclusive access to the store */
	e_cal_backend_store_freeze_changes(priv->store);

	/* set item id */
	item_id = e_ews_item_get_id((EEwsItem *)ids->data);
	e_cal_component_set_uid(create_data->comp, item_id->id);

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
	e_data_cal_notify_object_created (create_data->cal, create_data->context, error, comp_uid, e_cal_component_get_as_string(create_data->comp));

	/* place new component in our cache */
	PRIV_LOCK (priv);
	g_hash_table_insert (priv->item_id_hash, g_strdup(item_id->id), g_object_ref (create_data->comp));
	PRIV_UNLOCK (priv);

	/* update changes and release access to the store */
	e_cal_backend_store_thaw_changes (priv->store);

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

	if (!e_ews_connection_update_items_finish (cnc, res, &ids, &error)) {
		/* The calendar UI doesn't *display* errors unless they have
		   the OtherError code */
		error->code = OtherError;
		return;
	}

	e_cal_backend_store_freeze_changes(priv->store);

	item_id = e_ews_item_get_id((EEwsItem *)ids->data);

	/* Update change key. id remains the same, but change key changed.*/
	icalcomp = e_cal_component_get_icalcomponent (modify_data->comp);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		const gchar *x_name;
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

	e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbews), comp_str_old, comp_str);
	e_data_cal_notify_object_modified (modify_data->cal, modify_data->context, error, comp_str_old, comp_str);

	PRIV_LOCK (priv);
	g_hash_table_replace (priv->item_id_hash, g_strdup(modify_data->itemid), g_object_ref (modify_data->comp));
	PRIV_UNLOCK (priv);

	e_cal_backend_store_thaw_changes (priv->store);

	icalproperty_free (icalprop);
	e_cal_component_free_id (id);
	g_free(comp_str);
	g_free(comp_str_old);
	g_free(modify_data->itemid);
	g_free(modify_data->changekey);
	g_object_unref(modify_data->oldcomp);
	g_object_unref(modify_data->comp);
	g_object_unref(modify_data->cbews);
	g_object_unref(modify_data->cal);
	g_free(modify_data);
}

static void
convert_property_to_updatexml (ESoapMessage *msg, const gchar *name, const gchar *value, const gchar * prefix, const gchar *attr_name, const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix);
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static void
convert_component_to_updatexml(ESoapMessage *msg, gpointer user_data)
{
	EwsModifyData *modify_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (modify_data->comp);
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend;
	icalproperty *prop;

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
					 modify_data->itemid, modify_data->changekey, 0);

	convert_property_to_updatexml  (msg, "Subject", icalcomponent_get_summary(icalcomp), "item", NULL, NULL);

	convert_property_to_updatexml  (msg, "Body", icalcomponent_get_description(icalcomp), "item", "BodyType", "Text");

	dtstart = icalcomponent_get_dtstart (icalcomp);
	dtend = icalcomponent_get_dtend (icalcomp);

	e_ews_message_start_set_item_field (msg, "Start", "calendar");
	ewscal_set_time (msg, "Start", &dtstart);
	e_ews_message_end_set_item_field (msg);

	e_ews_message_start_set_item_field (msg, "End", "calendar");
	ewscal_set_time (msg, "End", &dtend);
	e_ews_message_end_set_item_field (msg);

	convert_property_to_updatexml  (msg, "Location", icalcomponent_get_location(icalcomp), "calendar", NULL, NULL);

	/* Recurrence */
	prop = icalcomponent_get_first_property(icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL) {
		ewscal_set_reccurence(msg, prop, &dtstart);
	}

	e_ews_collect_attendees(icalcomp, &required, &optional, &resource);
	if (required != NULL) {
		e_ews_message_start_set_item_field (msg, "RequiredAttendees", "calendar");

		add_attendees_list_to_message (msg, "RequiredAttendees", required);
		g_slist_free(required);

		e_ews_message_end_set_item_field (msg);
	}
	if (optional != NULL) {
		e_ews_message_start_set_item_field (msg, "OptionalAttendees", "calendar");

		add_attendees_list_to_message (msg, "OptionalAttendees", optional);
		g_slist_free(optional);

		e_ews_message_end_set_item_field (msg);
	}
	if (resource != NULL) {
		e_ews_message_start_set_item_field (msg, "Resources", "calendar");

		add_attendees_list_to_message (msg, "Resources", resource);
		g_slist_free(resource);

		e_ews_message_end_set_item_field (msg);
	}

	if (0 /* Exchange 2010 detected */ && dtstart.zone != dtend.zone) {
		if (dtstart.zone) {
			e_ews_message_start_set_item_field (msg, "StartTimeZone", "calendar");
			ewscal_set_timezone (msg, "StartTimeZone", (icaltimezone *)dtstart.zone);
			e_ews_message_end_set_item_field (msg);
		}
		if (dtend.zone) {
			e_ews_message_start_set_item_field (msg, "EndTimeZone", "calendar");
			ewscal_set_timezone (msg, "EndTimeZone", (icaltimezone *)dtend.zone);
			e_ews_message_end_set_item_field (msg);
		}
	} else {
		if (dtstart.zone) {
			e_ews_message_start_set_item_field (msg, "MeetingTimeZone", "calendar");
			ewscal_set_timezone (msg, "MeetingTimeZone", (icaltimezone *)dtstart.zone);
			e_ews_message_end_set_item_field (msg);
		}
	}

	e_ews_message_end_item_change (msg);
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
					     "SendToAllAndSaveCopy", priv->folder_id,
					     convert_component_to_updatexml, modify_data,
					     ews_cal_modify_object_cb, cancellable,
					     modify_data);
	return;

exit:
	e_data_cal_notify_object_modified (cal, context, error, NULL, NULL);
}

typedef struct {
	ECalBackendEws *cbews;
	ECalComponent *comp;
} EwsAcceptData;

static void
prepare_accept_item_request (ESoapMessage *msg, gpointer user_data)
{
	EwsAcceptData *data = user_data;
	ECalComponent *comp = data->comp;
	ECalBackendEwsPrivate *priv = E_CAL_BACKEND_EWS(data->cbews)->priv;
	icalcomponent *icalcomp;
	icalproperty *attendee;
	gchar *uid = NULL, *change_key = NULL;
	const char *attendee_str = NULL, *response_type = NULL;

	/* gather needed data from icalcomponent */
	ews_cal_component_get_item_id (comp, &uid, &change_key);
	
	icalcomp = e_cal_component_get_icalcomponent (comp);

	for (attendee = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
		attendee != NULL;
		attendee = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY)) {
		attendee_str = icalproperty_get_attendee (attendee);
		if ((attendee_str != NULL) && !strncasecmp(attendee_str, "MAILTO:", 7))
			if (g_strcmp0(attendee_str + 7 , priv->user_email) == 0) {
				response_type = icalproperty_get_parameter_as_string (attendee, "PARTSTAT");
				break;
			}
	}

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa566464%28v=exchg.140%29.aspx
	 * Accept and Decline meeting have same method code (10032)
	 * The real status is reflected at Attendee property PARTSTAT
	 * need to find current user as attendee and make a desision what to do.
	 * Prepare AcceptItem node in the SOAP message */

	if (!g_ascii_strcasecmp (response_type, "ACCEPTED"))
		e_soap_message_start_element (msg, "AcceptItem", NULL, NULL);
	else if (!g_ascii_strcasecmp (response_type, "DECLINED"))
		e_soap_message_start_element (msg, "DeclineItem", NULL, NULL);
	else
		e_soap_message_start_element (msg, "TentativelyAcceptItem", NULL, NULL);
	
	e_soap_message_start_element (msg, "ReferenceItemId", NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", uid, NULL, NULL);
	e_soap_message_add_attribute (msg, "ChangeKey", change_key, NULL, NULL);
	e_soap_message_end_element (msg); // "ReferenceItemId"

	/* end of "AcceptItem" */
	e_soap_message_end_element (msg);

	g_free (uid);
	g_free (change_key);
}

static gboolean
e_cal_backend_send_accept_item (ECalBackend *backend, icalcomponent *icalcomp, GError **error)
{
	EwsAcceptData *accept_data;
	ECalBackendEwsPrivate *priv = E_CAL_BACKEND_EWS(backend)->priv;
	GCancellable *cancellable = NULL;
	GSList *ids = NULL;
	ECalComponent *comp;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	if (!comp) {
		g_propagate_error (error, EDC_ERROR(InvalidObject));
		return FALSE;
	}
	accept_data = g_new0 (EwsAcceptData, 1);
	accept_data->cbews = g_object_ref(backend);
	accept_data->comp = comp;

	return e_ews_connection_create_items (priv->cnc,
					      EWS_PRIORITY_MEDIUM,
					      "SendAndSaveCopy",NULL,NULL,
					      prepare_accept_item_request,
					      accept_data,
					      &ids,
					      cancellable,
					      error);
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

	cbews = E_CAL_BACKEND_EWS(backend);
	priv = cbews->priv;

	/* make sure we're not offline */
	if (priv->mode == CAL_MODE_LOCAL) {
		g_propagate_error(&error, EDC_ERROR(RepositoryOffline));
		goto exit;
	}

	icalcomp = icalparser_parse_string (calobj);

	/* make sure data was parsed properly */
	if (!icalcomp) {
		g_propagate_error (&error, EDC_ERROR(InvalidObject));
		goto exit;
	}

	/* make sure ical data we parse is actually an vcal component */
	if (icalcomponent_isa(icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);
		g_propagate_error(&error, EDC_ERROR(InvalidObject));
		goto exit;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND(backend));
	method = icalcomponent_get_method (icalcomp);
	subcomp = icalcomponent_get_first_component (icalcomp, kind);

	while (subcomp) {
		ECalComponent *comp = e_cal_component_new();
		gboolean result;

		/* duplicate the ical component */
		e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone(subcomp));

		switch (method) {
			case ICAL_METHOD_REQUEST:
				result = e_cal_backend_send_accept_item (backend, subcomp, &error);
				if (!result && error)
					error->code = OtherError;
				break;
			case ICAL_METHOD_CANCEL:
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
	GSList *uris = NULL;
	ECalComponentId *id;
	ECalBackendEws *cbews;
	gchar *comp_str, *itemid;
	ECalComponent *comp_att, *cache_comp = NULL;

	e_ews_connection_get_attachments_finish	(cnc, res, &uris, &error);

	if (error != NULL) {
		error->code = OtherError;
		return;
	}

	comp_att = att_data->comp;
	cbews = att_data->cbews;
	itemid = att_data->itemid;

	e_cal_component_set_attachment_list (comp_att, uris);

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
	mime_content = e_ews_item_get_mime_content (item);
	vcomp = icalparser_parse_string (mime_content);

	/* Add the timezone */
	vtimezone = icalcomponent_get_first_component (vcomp, ICAL_VTIMEZONE_COMPONENT);
	if (vtimezone) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, icalcomponent_new_clone (vtimezone));
		e_cal_backend_store_put_timezone (priv->store, zone);

		icaltimezone_free (zone, TRUE);
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

		item_id = e_ews_item_get_id (item);

		/* Attendees */
		for (l = e_ews_item_get_attendees (item); l != NULL; l = g_slist_next (l)) {
			icalparameter *param;
			char *mailtoname;
			EwsAttendee *attendee = (EwsAttendee *)l->data;

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
		if (!freebusy) {
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
	GSList *items_deleted = NULL, *l[2], *m, *cal_item_ids = NULL;
	gchar *sync_state = NULL;
	gboolean includes_last_item;
	GError *error = NULL;
	struct _ews_sync_data *sync_data;
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

	if (!cal_item_ids && !includes_last_item) {
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

	if (!cal_item_ids)
		goto exit;

	sync_data = g_new0 (struct _ews_sync_data, 1);
	sync_data->cbews = cbews;
	sync_data->sync_state = sync_state;
	sync_data->sync_pending = !includes_last_item;

	e_ews_connection_get_items_start	(g_object_ref (cnc), EWS_PRIORITY_MEDIUM,
						 cal_item_ids,
						 "IdOnly", "item:Attachments item:HasAttachments item:MimeContent calendar:ModifiedOccurrences calendar:RequiredAttendees calendar:OptionalAttendees",
						 FALSE, NULL, ews_cal_get_items_ready_cb, NULL, NULL, NULL,
						 (gpointer) sync_data);

exit:
	g_object_unref (cnc);
	if (cal_item_ids) {
		g_slist_foreach (cal_item_ids, (GFunc) g_free, NULL);
		g_slist_free (cal_item_ids);
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

static void
e_cal_backend_ews_get_free_busy (ECalBackend *backend, EDataCal *cal,
				 EServerMethodContext context, GList *users,
				 time_t start, time_t end)
{
	/* Implement me */
	e_data_cal_notify_free_busy (cal, context, NULL, NULL);
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
//	backend_class->send_objects = e_cal_backend_ews_send_objects;
//	backend_class->get_attachment_list = e_cal_backend_ews_get_attachment_list;
	backend_class->get_free_busy = e_cal_backend_ews_get_free_busy;
//	backend_class->get_changes = e_cal_backend_ews_get_changes;
}
