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
#include "e-cal-backend-ews.h"
#include "e-cal-backend-ews-utils.h"
#include "e-ews-connection.h"

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

static void
e_cal_backend_ews_discard_alarm (ECalBackend *backend, EDataCal *cal, const gchar *uid, const gchar *auid, GError **perror)
{
        /* None of the other backends seem to do anything here. Should we be
	   clearing the <ReminderIsSet> property so that other clients also
	   clear the alarm, so that cancelling it on *one* of your computers
	   will stop it from annoying you on *all* of them? */
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

static gchar *
ews_cal_component_get_item_id (ECalComponent *comp)
{
	icalproperty *prop;

	prop = icalcomponent_get_first_property (e_cal_component_get_icalcomponent (comp),
						 ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name, *x_val;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);
		if (!strcmp (x_name, "X-EVOLUTION-ITEMID")) {
			return g_strdup (x_val);
		}

		prop = icalcomponent_get_next_property (e_cal_component_get_icalcomponent (comp),
							ICAL_X_PROPERTY);
	}
	return NULL;
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
		gchar *item_id = ews_cal_component_get_item_id (comp);

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

	if (priv->mode == CAL_MODE_LOCAL) {
		PRIV_UNLOCK (priv);
		goto exit;
	}
	
	if (!priv->cnc) {
		ESource *esource;
		const gchar *host_url;

		esource = e_cal_backend_get_source (E_CAL_BACKEND (cbews));
		
		priv->folder_id = e_source_get_duped_property (esource, "folder-id");
		priv->user_email = e_source_get_duped_property (esource, "email");
		
		host_url = e_source_get_property (esource, "hosturl");
		priv->cnc = e_ews_connection_new (host_url, username, password, &error);
	}
	
	PRIV_UNLOCK (priv);

exit:	
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

static void
add_item_to_cache (ECalBackendEws *cbews, EEwsItem *item)
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
		icalproperty *icalprop;
		const EwsId *item_id;
		ECalComponentId *id;
		gchar *comp_str;

		item_id = e_ews_item_get_id (item);
		icalprop = icalproperty_new_x (item_id->id);
		icalproperty_set_x_name (icalprop, "X-EVOLUTION-ITEMID");
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
		
		PRIV_LOCK (priv);
		g_hash_table_insert (priv->item_id_hash, g_strdup (item_id->id), g_object_ref (comp));
		PRIV_UNLOCK (priv);

		g_object_unref (comp);
	}
}

struct _ews_sync_data {
	ECalBackendEws *cbews;
	gchar *sync_state;
	gboolean sync_pending;
};

static void
ews_cal_get_items_ready_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc;
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;
	GSList *items = NULL, *l;
	struct _ews_sync_data *sync_data;
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

	e_cal_backend_store_freeze_changes (priv->store);
	for (l = items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;

		add_item_to_cache (cbews, item)	;
		g_object_unref (item);
	}
	e_cal_backend_store_thaw_changes (priv->store);
	/* TODO fetch attachments */	

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

		if (comp) {
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
						 "IdOnly", "item:Attachments item:HasAttachments item:MimeContent",
						 FALSE, ews_cal_get_items_ready_cb, NULL, 
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
	ECalBackendEwsPrivate *priv;
	GList *objects = NULL;
	GError *err = NULL;

	cbews = E_CAL_BACKEND_EWS (backend);
	priv = cbews->priv;

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
e_cal_backend_ews_dispose (GObject *object)
{
	ECalBackendEws *cbews;
	ECalBackendEwsPrivate *priv;

	cbews = E_CAL_BACKEND_EWS (object);
	priv = cbews->priv;

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

	/* Many of these can be moved to Base class */
	backend_class->add_timezone = e_cal_backend_ews_add_timezone;
	backend_class->get_default_object = e_cal_backend_ews_get_default_object;
	backend_class->get_timezone = e_cal_backend_ews_get_timezone;

	backend_class->internal_get_timezone = e_cal_backend_ews_internal_get_timezone;
	
	backend_class->open = e_cal_backend_ews_open;
	backend_class->get_object = e_cal_backend_ews_get_object;
	backend_class->get_object_list = e_cal_backend_ews_get_object_list;
	backend_class->remove = e_cal_backend_ews_remove;
	
	backend_class->start_query = e_cal_backend_ews_start_query;

	backend_class->discard_alarm = e_cal_backend_ews_discard_alarm;

/*	backend_class->create_object = e_cal_backend_ews_create_object;
	backend_class->modify_object = e_cal_backend_ews_modify_object;
	backend_class->remove_object = e_cal_backend_ews_remove_object;
	backend_class->receive_objects = e_cal_backend_ews_receive_objects;
	backend_class->send_objects = e_cal_backend_ews_send_objects;
	backend_class->get_attachment_list = e_cal_backend_ews_get_attachment_list;
	backend_class->get_freebusy = e_cal_backend_ews_get_free_busy;
	backend_class->get_changes = e_cal_backend_ews_get_changes;
*/
}
