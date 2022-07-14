/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libedata-cal/libedata-cal.h>
#include <libecal/libecal.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"
#include "common/e-m365-tz-utils.h"
#include "common/e-source-m365-folder.h"

#include "e-cal-backend-m365-utils.h"
#include "e-cal-backend-m365.h"

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)
#define ECC_ERROR_EX(_code, _msg) e_cal_client_error_create (_code, _msg)

#define LOCK(_cb) g_rec_mutex_lock (&_cb->priv->property_lock)
#define UNLOCK(_cb) g_rec_mutex_unlock (&_cb->priv->property_lock)

struct _ECalBackendM365Private {
	GRecMutex property_lock;
	EM365Connection *cnc;
	gchar *group_id;
	gchar *folder_id;
	gchar *attachments_dir;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendM365, e_cal_backend_m365, E_TYPE_CAL_META_BACKEND)

static void
ecb_m365_convert_error_to_client_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror ||
	    (*perror)->domain == E_CLIENT_ERROR ||
	    (*perror)->domain == E_CAL_CLIENT_ERROR)
		return;

	/*if ((*perror)->domain == M365_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case M365_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EC_ERROR_EX (E_CLIENT_ERROR_AUTHENTICATION_FAILED, (*perror)->message);
			break;
		case M365_CONNECTION_ERROR_FOLDERNOTFOUND:
		case M365_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case M365_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case M365_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR, (*perror)->message);
			break;
		case M365_CONNECTION_ERROR_EVENTNOTFOUND:
		case M365_CONNECTION_ERROR_ITEMNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND, (*perror)->message);
			break;
		case M365_CONNECTION_ERROR_UNAVAILABLE:
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND, (*perror)->message);
			break;
		}

		if (!error)
			error = EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, (*perror)->message);
	}*/

	if (error) {
		g_error_free (*perror);
		*perror = error;
	}
}

static const gchar *
ecb_m365_get_component_from_extra (const gchar *extra)
{
	gchar *enter;

	if (!extra)
		return NULL;

	enter = strchr (extra, '\n');

	if (!enter)
		return NULL;

	return enter + 1;
}

/* Modifies inout_extra, cannot be called multiple times with the same arguments */
static void
ecb_m365_split_extra (gchar *inout_extra,
		      const gchar **out_change_key,
		      const gchar **out_ical_comp)
{
	gchar *enter;

	if (!inout_extra)
		return;

	enter = (gchar *) ecb_m365_get_component_from_extra (inout_extra);
	g_return_if_fail (enter != NULL);

	enter[-1] = '\0';

	if (out_change_key)
		*out_change_key = inout_extra;

	if (out_ical_comp)
		*out_ical_comp = enter;
}

static gchar *
ecb_m365_join_to_extra (const gchar *change_key,
			const gchar *ical_comp)
{
	if (!change_key && !ical_comp)
		return NULL;

	return g_strconcat (change_key ? change_key : "", "\n", ical_comp, NULL);
}

static ICalComponent *
ecb_m365_json_to_ical (ECalBackendM365 *cbm365,
		       JsonObject *m365_object,
		       GCancellable *cancellable,
		       GError **error)
{
	g_return_val_if_fail (m365_object != NULL, NULL);

	return e_cal_backend_m365_utils_json_to_ical (cbm365->priv->cnc,
		cbm365->priv->group_id,
		cbm365->priv->folder_id,
		cbm365->priv->attachments_dir,
		E_TIMEZONE_CACHE (cbm365),
		e_cal_backend_get_kind (E_CAL_BACKEND (cbm365)),
		m365_object,
		cancellable,
		error);
}

static ECalMetaBackendInfo *
ecb_m365_json_to_ical_nfo (ECalBackendM365 *cbm365,
			   EM365Event *m365_event,
			   GCancellable *cancellable,
			   GError **error)
{
	ECalMetaBackendInfo *nfo;
	ICalComponent *icomp;

	icomp = ecb_m365_json_to_ical (cbm365, m365_event, cancellable, error);

	if (!icomp)
		return NULL;

	nfo = e_cal_meta_backend_info_new (i_cal_component_get_uid (icomp),
		e_m365_event_get_change_key (m365_event),
		NULL, NULL);

	if (nfo) {
		nfo->object = i_cal_component_as_ical_string (icomp);
		nfo->extra = ecb_m365_join_to_extra (e_m365_event_get_change_key (m365_event), nfo->object);
	}

	g_clear_object (&icomp);

	return nfo;
}

static JsonBuilder *
ecb_m365_ical_to_json_locked (ECalBackendM365 *cbm365,
			      ICalComponent *new_comp,
			      ICalComponent *old_comp, /* nullable */
			      GCancellable *cancellable,
			      GError **error)
{
	g_return_val_if_fail (new_comp != NULL, NULL);

	return e_cal_backend_m365_utils_ical_to_json (cbm365->priv->cnc,
		cbm365->priv->group_id,
		cbm365->priv->folder_id,
		E_TIMEZONE_CACHE (cbm365),
		e_cal_backend_get_kind (E_CAL_BACKEND (cbm365)),
		new_comp,
		old_comp,
		cancellable,
		error);
}

static gboolean
ecb_m365_ical_to_json_2nd_go_locked (ECalBackendM365 *cbm365,
				     ICalComponent *new_comp,
				     ICalComponent *old_comp, /* nullable */
				     const gchar *m365_id,
				     GCancellable *cancellable,
				     GError **error)
{
	g_return_val_if_fail (new_comp != NULL, FALSE);
	g_return_val_if_fail (m365_id != NULL, FALSE);

	return e_cal_backend_m365_utils_ical_to_json_2nd_go (cbm365->priv->cnc,
		cbm365->priv->group_id,
		cbm365->priv->folder_id,
		E_TIMEZONE_CACHE (cbm365),
		e_cal_backend_get_kind (E_CAL_BACKEND (cbm365)),
		new_comp,
		old_comp,
		m365_id,
		cancellable,
		error);
}

static gboolean
ecb_m365_download_changes_locked (ECalBackendM365 *cbm365,
				  const GSList *ids,
				  GSList **out_info_objects,
				  GCancellable *cancellable,
				  GError **error)
{
	GSList *items = NULL, *link;

	if (!ids)
		return TRUE;

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbm365))) {
	case I_CAL_VEVENT_COMPONENT:
		if (!e_m365_connection_get_events_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id, cbm365->priv->folder_id, ids, NULL, NULL, &items, cancellable, error))
			return FALSE;
		break;
	case I_CAL_VTODO_COMPONENT:
		if (!e_m365_connection_get_tasks_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id, cbm365->priv->folder_id, ids, NULL, NULL, &items, cancellable, error))
			return FALSE;
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	for (link = items; link; link = g_slist_next (link)) {
		JsonObject *item = link->data;
		ECalMetaBackendInfo *nfo;

		if (!item)
			continue;

		nfo = ecb_m365_json_to_ical_nfo (cbm365, item, cancellable, error);

		if (nfo)
			*out_info_objects = g_slist_prepend (*out_info_objects, nfo);
	}

	g_slist_free_full (items, (GDestroyNotify) json_object_unref);

	return TRUE;
}

static void
ecb_m365_maybe_disconnect_sync (ECalBackendM365 *cbm365,
				GError **in_perror,
				GCancellable *cancellable)
{
	g_return_if_fail (E_IS_CAL_BACKEND_M365 (cbm365));

	if (in_perror && g_error_matches (*in_perror, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		e_cal_meta_backend_disconnect_sync (E_CAL_META_BACKEND (cbm365), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (cbm365), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static gboolean
ecb_m365_unset_connection_sync (ECalBackendM365 *cbm365,
				gboolean is_disconnect,
				GCancellable *cancellable,
				GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (cbm365), FALSE);

	LOCK (cbm365);

	if (cbm365->priv->cnc) {
		if (is_disconnect)
			success = e_m365_connection_disconnect_sync (cbm365->priv->cnc, cancellable, error);
	}

	g_clear_object (&cbm365->priv->cnc);
	g_clear_pointer (&cbm365->priv->group_id, g_free);
	g_clear_pointer (&cbm365->priv->folder_id, g_free);

	UNLOCK (cbm365);

	return success;
}

static gboolean
ecb_m365_connect_sync (ECalMetaBackend *meta_backend,
		       const ENamedParameters *credentials,
		       ESourceAuthenticationResult *out_auth_result,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
		       GCancellable *cancellable,
		       GError **error)
{
	ECalBackendM365 *cbm365;
	EM365FolderKind folder_kind;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend))) {
	case I_CAL_VEVENT_COMPONENT:
		folder_kind = E_M365_FOLDER_KIND_CALENDAR;
		break;
	case I_CAL_VTODO_COMPONENT:
		folder_kind = E_M365_FOLDER_KIND_TASKS;
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	if (cbm365->priv->cnc) {
		UNLOCK (cbm365);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	} else {
		EBackend *backend;
		ESourceRegistry *registry;
		ESource *source;
		EM365Connection *cnc = NULL;
		ESourceM365Folder *m365_folder_extension;
		CamelM365Settings *m365_settings;
		gchar *group_id;
		gchar *folder_id;

		backend = E_BACKEND (cbm365);
		source = e_backend_get_source (backend);
		registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbm365));
		m365_settings = camel_m365_settings_get_from_backend (backend, registry);
		g_warn_if_fail (m365_settings != NULL);

		m365_folder_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER);
		group_id = e_source_m365_folder_dup_group_id (m365_folder_extension);
		folder_id = e_source_m365_folder_dup_id (m365_folder_extension);

		if (folder_id) {
			cnc = e_m365_connection_new_for_backend (backend, registry, source, m365_settings);

			*out_auth_result = e_m365_connection_authenticate_sync (cnc, NULL, folder_kind, group_id, folder_id,
				out_certificate_pem, out_certificate_errors, cancellable, error);

			if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
				cbm365->priv->cnc = g_object_ref (cnc);

				g_warn_if_fail (cbm365->priv->group_id == NULL);
				g_warn_if_fail (cbm365->priv->folder_id == NULL);

				g_free (cbm365->priv->group_id);
				cbm365->priv->group_id = group_id;

				g_free (cbm365->priv->folder_id);
				cbm365->priv->folder_id = folder_id;

				group_id = NULL;
				folder_id = NULL;
				success = TRUE;

				e_cal_backend_set_writable (E_CAL_BACKEND (cbm365), TRUE);
			}
		} else {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Folder ID is not set")));
		}

		g_clear_object (&cnc);
		g_free (group_id);
		g_free (folder_id);
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);

	return success;
}

static gboolean
ecb_m365_disconnect_sync (ECalMetaBackend *meta_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);

	return ecb_m365_unset_connection_sync (E_CAL_BACKEND_M365 (meta_backend), TRUE, cancellable, error);
}

static gboolean
ecb_m365_get_changes_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendM365 *cbm365;
	ECalCache *cal_cache;
	GSList *items = NULL, *link;
	gboolean full_read;
	gboolean success = TRUE;
	gboolean (* list_items_func) (EM365Connection *cnc,
				      const gchar *user_override,
				      const gchar *group_id,
				      const gchar *calendar_id,
				      const gchar *prefer_outlook_timezone,
				      const gchar *select,
				      GSList **out_items,
				      GCancellable *cancellable,
				      GError **error);
	const gchar *(* get_id_func) (JsonObject *item);
	const gchar *(* get_change_key_func) (JsonObject *item);

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend))) {
	case I_CAL_VEVENT_COMPONENT:
		list_items_func = e_m365_connection_list_events_sync;
		get_id_func = e_m365_event_get_id;
		get_change_key_func = e_m365_event_get_change_key;
		break;
	case I_CAL_VTODO_COMPONENT:
		list_items_func = e_m365_connection_list_tasks_sync;
		get_id_func = e_m365_task_get_id;
		get_change_key_func = e_m365_task_get_change_key;
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	LOCK (cbm365);

	full_read = !e_cache_get_count (E_CACHE (cal_cache), E_CACHE_INCLUDE_DELETED, cancellable, NULL);

	success = list_items_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id, cbm365->priv->folder_id, NULL,
		full_read ? NULL : "id,changeKey", &items, cancellable, error);

	if (success) {
		GSList *new_ids = NULL; /* const gchar *, borrowed from 'items' objects */
		GSList *changed_ids = NULL; /* const gchar *, borrowed from 'items' objects */

		for (link = items; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
			JsonObject *item = link->data;
			const gchar *id, *change_key;
			gchar *extra = NULL;

			if (!item)
				continue;

			id = get_id_func (item);
			change_key = get_change_key_func (item);

			if (e_cal_cache_get_component_extra (cal_cache, id, NULL, &extra, cancellable, NULL)) {
				const gchar *saved_change_key = NULL;

				ecb_m365_split_extra (extra, &saved_change_key, NULL);

				if (g_strcmp0 (saved_change_key, change_key) == 0) {
					g_free (extra);
					continue;
				} else if (full_read) {
					ECalMetaBackendInfo *nfo;

					nfo = ecb_m365_json_to_ical_nfo (cbm365, item, cancellable, NULL);

					if (nfo)
						*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
				} else {
					changed_ids = g_slist_prepend (changed_ids, (gpointer) id);
				}

				g_free (extra);
			} else if (full_read) {
				ECalMetaBackendInfo *nfo;

				nfo = ecb_m365_json_to_ical_nfo (cbm365, item, cancellable, NULL);

				if (nfo)
					*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
			} else {
				new_ids = g_slist_prepend (new_ids, (gpointer) id);
			}
		}

		if (new_ids) {
			new_ids = g_slist_reverse (new_ids);
			success = ecb_m365_download_changes_locked (cbm365, new_ids, out_created_objects, cancellable, error);
		}

		if (success && changed_ids) {
			changed_ids = g_slist_reverse (changed_ids);
			success = ecb_m365_download_changes_locked (cbm365, changed_ids, out_modified_objects, cancellable, error);
		}

		g_slist_free (new_ids);
		g_slist_free (changed_ids);
	}

	g_slist_free_full (items, (GDestroyNotify) json_object_unref);

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	g_clear_object (&cal_cache);

	return success;
}

static gboolean
ecb_m365_load_component_sync (ECalMetaBackend *meta_backend,
			      const gchar *uid,
			      const gchar *extra,
			      ICalComponent **out_component,
			      gchar **out_extra,
			      GCancellable *cancellable,
			      GError **error)
{
	ECalBackendM365 *cbm365;
	JsonObject *item = NULL;
	const gchar *(* get_change_key_func) (JsonObject *item);
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbm365))) {
	case I_CAL_VEVENT_COMPONENT:
		success = e_m365_connection_get_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, uid, NULL, NULL, &item, cancellable, error);
		get_change_key_func = e_m365_event_get_change_key;
		break;
	case I_CAL_VTODO_COMPONENT:
		success = e_m365_connection_get_task_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, uid, NULL, NULL, &item, cancellable, error);
		get_change_key_func = e_m365_task_get_change_key;
		break;
	default:
		success = FALSE;
		break;
	}

	if (success) {
		*out_component = ecb_m365_json_to_ical (cbm365, item, cancellable, error);

		if (*out_component) {
			gchar *ical_str;

			ical_str = i_cal_component_as_ical_string (*out_component);

			*out_extra = ecb_m365_join_to_extra (get_change_key_func (item), ical_str);

			g_free (ical_str);
		} else {
			success = FALSE;
		}
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	return success;
}

static gboolean
ecb_m365_save_component_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendM365 *cbm365;
	ICalComponent *new_comp, *old_comp = NULL;
	JsonBuilder *builder;
	gboolean success = FALSE;
	gboolean (* create_item_func) (EM365Connection *cnc,
				       const gchar *user_override,
				       const gchar *group_id,
				       const gchar *folder_id,
				       JsonBuilder *item,
				       JsonObject **out_created_item,
				       GCancellable *cancellable,
				       GError **error);
	gboolean (* update_item_func) (EM365Connection *cnc,
				       const gchar *user_override,
				       const gchar *group_id,
				       const gchar *folder_id,
				       const gchar *item_id,
				       JsonBuilder *item,
				       GCancellable *cancellable,
				       GError **error);
	const gchar *(* get_id_func) (JsonObject *item);
	const gchar *(* get_change_key_func) (JsonObject *item);


	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (instances != NULL, FALSE);

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend))) {
	case I_CAL_VEVENT_COMPONENT:
		if (instances->next) {
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
				_("Can store only simple events into Microsoft 365 calendar")));

			return FALSE;
		}

		create_item_func = e_m365_connection_create_event_sync;
		update_item_func = e_m365_connection_update_event_sync;
		get_id_func = e_m365_event_get_id;
		get_change_key_func = e_m365_event_get_change_key;
		break;
	case I_CAL_VTODO_COMPONENT:
		if (instances->next) {
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
				_("Can store only simple tasks into Microsoft 365 task folder")));

			return FALSE;
		}

		create_item_func = e_m365_connection_create_task_sync;
		update_item_func = e_m365_connection_update_task_sync;
		get_id_func = e_m365_task_get_id;
		get_change_key_func = e_m365_task_get_change_key;
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	new_comp = e_cal_component_get_icalcomponent (instances->data);

	if (extra && *extra) {
		const gchar *comp_str;

		comp_str = ecb_m365_get_component_from_extra (extra);

		if (comp_str)
			old_comp = i_cal_component_new_from_string (comp_str);
	}

	builder = ecb_m365_ical_to_json_locked (cbm365, new_comp, old_comp, cancellable, error);

	if (builder) {
		if (overwrite_existing) {
			const gchar *uid = i_cal_component_get_uid (new_comp);

			success = update_item_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
				cbm365->priv->folder_id, uid, builder, cancellable, error);

			if (success)
				success = ecb_m365_ical_to_json_2nd_go_locked (cbm365, new_comp, old_comp, uid, cancellable, error);

			if (success) {
				/* To re-read it from the server */
				*out_new_uid = g_strdup (uid);
			}
		} else {
			JsonObject *created_item = NULL;

			success = create_item_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
				cbm365->priv->folder_id, builder, &created_item, cancellable, error);

			if (success && created_item) {
				const gchar *m365_id = get_id_func (created_item);

				success = ecb_m365_ical_to_json_2nd_go_locked (cbm365, new_comp, old_comp, m365_id, cancellable, error);
			}

			if (success && created_item) {
				ICalComponent *icomp;

				*out_new_uid = g_strdup (get_id_func (created_item));

				icomp = ecb_m365_json_to_ical (cbm365, created_item, cancellable, error);

				if (icomp) {
					gchar *ical_str;

					ical_str = i_cal_component_as_ical_string (icomp);

					*out_new_extra = ecb_m365_join_to_extra (get_change_key_func (created_item), ical_str);

					g_clear_object (&icomp);
					g_free (ical_str);
				} else {
					success = FALSE;
				}
			}

			if (created_item)
				json_object_unref (created_item);
		}

		g_clear_object (&builder);
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	g_clear_object (&old_comp);

	return success;
}

static gboolean
ecb_m365_remove_component_sync (ECalMetaBackend *meta_backend,
				EConflictResolution conflict_resolution,
				const gchar *uid,
				const gchar *extra,
				const gchar *object,
				guint32 opflags,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendM365 *cbm365;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbm365))) {
	case I_CAL_VEVENT_COMPONENT:
		success = e_m365_connection_delete_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, uid, cancellable, error);
		break;
	case I_CAL_VTODO_COMPONENT:
		success = e_m365_connection_delete_task_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, uid, cancellable, error);
		break;
	default:
		g_warn_if_reached ();
		success = FALSE;
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	return success;
}

static void
ecb_m365_discard_alarm_sync (ECalBackendSync *cal_backend_sync,
			     EDataCal *cal,
			     GCancellable *cancellable,
			     const gchar *uid,
			     const gchar *rid,
			     const gchar *auid,
			     guint32 opflags,
			     GError **error)
{
	ECalBackendM365 *cbm365;

	g_return_if_fail (E_IS_CAL_BACKEND_M365 (cal_backend_sync));
	g_return_if_fail (uid != NULL);

	if (e_cal_backend_get_kind (E_CAL_BACKEND (cal_backend_sync)) != I_CAL_VEVENT_COMPONENT) {
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_NOT_SUPPORTED));
		return;
	}

	cbm365 = E_CAL_BACKEND_M365 (cal_backend_sync);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbm365), cancellable, error))
		return;

	LOCK (cbm365);

	e_m365_connection_dismiss_reminder_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
		cbm365->priv->folder_id, uid, cancellable, error);

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);
}

static void
ecb_m365_get_free_busy_sync (ECalBackendSync *cal_backend_sync,
			     EDataCal *cal,
			     GCancellable *cancellable,
			     const GSList *users,
			     time_t start,
			     time_t end,
			     GSList **out_freebusyobjs,
			     GError **error)
{
	ECalBackendM365 *cbm365;
	GSList *infos = NULL;
	gboolean success;

	g_return_if_fail (E_IS_CAL_BACKEND_M365 (cal_backend_sync));
	g_return_if_fail (users != NULL);
	g_return_if_fail (out_freebusyobjs != NULL);

	if (e_cal_backend_get_kind (E_CAL_BACKEND (cal_backend_sync)) != I_CAL_VEVENT_COMPONENT) {
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_NOT_SUPPORTED));
		return;
	}

	cbm365 = E_CAL_BACKEND_M365 (cal_backend_sync);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbm365), cancellable, error))
		return;

	LOCK (cbm365);

	success = e_m365_connection_get_schedule_sync (cbm365->priv->cnc, NULL, 30, start, end, users, &infos, cancellable, error);

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	if (success) {
		ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();
		GSList *link;

		*out_freebusyobjs = NULL;

		for (link = infos; link; link = g_slist_next (link)) {
			EM365ScheduleInformation *schinfo = link->data;
			ICalComponent *vfb = NULL;
			JsonArray *array;
			guint ii, sz;

			if (!schinfo || !e_m365_schedule_information_get_schedule_id (schinfo))
				continue;

			array = e_m365_schedule_information_get_schedule_items (schinfo);
			sz = array ? json_array_get_length (array) : 0;

			for (ii = 0; ii < sz; ii++) {
				EM365ScheduleItem *schitem = json_array_get_object_element (array, ii);
				EM365DateTimeWithZone *dt;
				ICalProperty *prop;
				ICalPeriod *ipt;
				ICalTime *itt;
				const gchar *tmp;

				if (!schitem || !e_m365_schedule_item_get_start (schitem) || !e_m365_schedule_item_get_end (schitem))
					continue;

				ipt = i_cal_period_new_null_period ();

				dt = e_m365_schedule_item_get_start (schitem);
				itt = i_cal_time_new_from_timet_with_zone (e_m365_date_time_get_date_time (dt), 0, utc_zone);
				i_cal_period_set_start (ipt, itt);
				g_clear_object (&itt);

				dt = e_m365_schedule_item_get_end (schitem);
				itt = i_cal_time_new_from_timet_with_zone (e_m365_date_time_get_date_time (dt), 0, utc_zone);
				i_cal_period_set_end (ipt, itt);
				g_clear_object (&itt);

				prop = i_cal_property_new_freebusy (ipt);
				g_clear_object (&ipt);

				switch (e_m365_schedule_item_get_status (schitem)) {
				case E_M365_FREE_BUSY_STATUS_FREE:
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "FREE");
					break;
				case E_M365_FREE_BUSY_STATUS_TENTATIVE:
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "BUSY-TENTATIVE");
					break;
				case E_M365_FREE_BUSY_STATUS_BUSY:
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "BUSY");
					break;
				case E_M365_FREE_BUSY_STATUS_OOF:
				case E_M365_FREE_BUSY_STATUS_WORKING_ELSEWHERE:
					i_cal_property_set_parameter_from_string (prop, "FBTYPE", "BUSY-UNAVAILABLE");
					break;
				default:
					break;
				}

				tmp = e_m365_schedule_item_get_subject (schitem);

				if (tmp && *tmp)
					i_cal_property_set_parameter_from_string (prop, "X-SUMMARY", tmp);

				tmp = e_m365_schedule_item_get_location (schitem);

				if (tmp && *tmp)
					i_cal_property_set_parameter_from_string (prop, "X-LOCATION", tmp);

				if (!vfb)
					vfb = i_cal_component_new_vfreebusy ();

				i_cal_component_take_property (vfb, prop);
			}

			if (vfb) {
				gchar *mailto;

				mailto = g_strconcat ("mailto:", e_m365_schedule_information_get_schedule_id (schinfo), NULL);
				i_cal_component_take_property (vfb, i_cal_property_new_attendee (mailto));
				g_free (mailto);

				*out_freebusyobjs = g_slist_prepend (*out_freebusyobjs, i_cal_component_as_ical_string (vfb));

				g_clear_object (&vfb);
			}
		}

		*out_freebusyobjs = g_slist_reverse (*out_freebusyobjs);
	}

	g_slist_free_full (infos, (GDestroyNotify) json_object_unref);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);
}

static gchar *
ecb_m365_get_backend_property (ECalBackend *cal_backend,
			      const gchar *prop_name)
{
	ECalBackendM365 *cbm365;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	cbm365 = E_CAL_BACKEND_M365 (cal_backend);

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
			E_CAL_STATIC_CAPABILITY_NO_ALARM_AFTER_START,
			E_CAL_STATIC_CAPABILITY_NO_MEMO_START_DATE,
			E_CAL_STATIC_CAPABILITY_ALL_DAY_EVENT_AS_TIME,
			E_CAL_STATIC_CAPABILITY_TASK_DATE_ONLY,
			E_CAL_STATIC_CAPABILITY_TASK_NO_ALARM,
			E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR,
			E_CAL_STATIC_CAPABILITY_TASK_HANDLE_RECUR,
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cbm365)),
			NULL);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		/* return email address of the person who opened the calendar */
		CamelM365Settings *m365_settings;

		m365_settings = camel_m365_settings_get_from_backend (E_BACKEND (cal_backend), e_cal_backend_get_registry (cal_backend));

		return camel_m365_settings_dup_email (m365_settings);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* Microsoft 365 does not support email based alarms */
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_m365_parent_class)->impl_get_backend_property (cal_backend, prop_name);
}

static gboolean
ecb_m365_get_destination_address (EBackend *backend,
				  gchar **host,
				  guint16 *port)
{
	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	/* Sanity checking */
	if (!e_cal_backend_get_registry (E_CAL_BACKEND (backend)) ||
	    !e_backend_get_source (backend))
		return FALSE;

	*host = g_strdup ("graph.microsoft.com");
	*port = 443;

	return TRUE;
}

static gchar *
ecb_m365_dup_component_revision (ECalCache *cal_cache,
				 ICalComponent *icomp,
				 gpointer user_data)
{
	g_return_val_if_fail (icomp != NULL, NULL);

	return e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
}

static void
ecb_m365_constructed (GObject *object)
{
	ECalBackendM365 *cbm365 = E_CAL_BACKEND_M365 (object);
	ECalCache *cal_cache;
	gchar *cache_dirname;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_m365_parent_class)->constructed (object);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (E_BACKEND (object), NULL);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbm365));
	g_return_if_fail (cal_cache != NULL);

	cache_dirname = g_path_get_dirname (e_cache_get_filename (E_CACHE (cal_cache)));
	g_signal_connect (cal_cache, "dup-component-revision", G_CALLBACK (ecb_m365_dup_component_revision), NULL);

	g_clear_object (&cal_cache);

	cbm365->priv->attachments_dir = g_build_filename (cache_dirname, "attachments", NULL);
	g_mkdir_with_parents (cbm365->priv->attachments_dir, 0777);

	g_free (cache_dirname);

	e_m365_tz_utils_ref_windows_zones ();
}

static void
ecb_m365_dispose (GObject *object)
{
	ECalBackendM365 *cbm365 = E_CAL_BACKEND_M365 (object);

	ecb_m365_unset_connection_sync (cbm365, FALSE, NULL, NULL);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_m365_parent_class)->dispose (object);
}

static void
ecb_m365_finalize (GObject *object)
{
	ECalBackendM365 *cbm365 = E_CAL_BACKEND_M365 (object);

	g_free (cbm365->priv->attachments_dir);

	g_rec_mutex_clear (&cbm365->priv->property_lock);

	e_m365_tz_utils_unref_windows_zones ();

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_m365_parent_class)->finalize (object);
}

static void
e_cal_backend_m365_init (ECalBackendM365 *cbm365)
{
	cbm365->priv = e_cal_backend_m365_get_instance_private (cbm365);

	g_rec_mutex_init (&cbm365->priv->property_lock);
}

static void
e_cal_backend_m365_class_init (ECalBackendM365Class *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *cal_backend_sync_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_m365_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_m365_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_m365_get_changes_sync;
	cal_meta_backend_class->load_component_sync = ecb_m365_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_m365_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_m365_remove_component_sync;

	cal_backend_sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);
	cal_backend_sync_class->discard_alarm_sync = ecb_m365_discard_alarm_sync;
	cal_backend_sync_class->get_free_busy_sync = ecb_m365_get_free_busy_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_m365_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ecb_m365_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ecb_m365_constructed;
	object_class->dispose = ecb_m365_dispose;
	object_class->finalize = ecb_m365_finalize;
}
