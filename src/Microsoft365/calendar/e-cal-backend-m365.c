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

#include "e-ews-common-utils.h"
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

			e_binding_bind_property (
				backend, "proxy-resolver",
				cnc, "proxy-resolver",
				G_BINDING_SYNC_CREATE);

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
ecb_m365_gather_ids_cb (ECalCache *cal_cache,
			const gchar *uid,
			const gchar *rid,
			const gchar *revision,
			const gchar *object,
			const gchar *extra,
			guint32 custom_flags,
			EOfflineState offline_state,
			gpointer user_data)
{
	GHashTable *left_known_uids = user_data;

	if (uid && !g_hash_table_contains (left_known_uids, uid))
		g_hash_table_insert (left_known_uids, g_strdup (uid), NULL);

	return TRUE;
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
	GHashTable *left_known_ids;
	GHashTableIter iter;
	gpointer key;
	gboolean full_read;
	gboolean success = TRUE;
	gboolean (* list_items_func) (EM365Connection *cnc,
				      const gchar *user_override,
				      const gchar *group_id,
				      const gchar *calendar_id,
				      const gchar *prefer_outlook_timezone,
				      const gchar *select,
				      const gchar *filter,
				      GSList **out_items,
				      GCancellable *cancellable,
				      GError **error);
	const gchar *(* get_id_func) (JsonObject *item);
	const gchar *(* get_change_key_func) (JsonObject *item);
	const gchar *select_props = "id,changeKey";

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
		get_change_key_func = e_m365_task_get_last_modified_as_string;
		select_props = NULL;
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

	left_known_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	e_cal_cache_search_with_callback (cal_cache, "#t", ecb_m365_gather_ids_cb, left_known_ids, cancellable, NULL);

	LOCK (cbm365);

	full_read = !select_props || !e_cache_get_count (E_CACHE (cal_cache), E_CACHE_INCLUDE_DELETED, cancellable, NULL);

	success = list_items_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id, cbm365->priv->folder_id, NULL,
		full_read ? NULL : select_props, NULL, &items, cancellable, error);

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

			if (id)
				g_hash_table_remove (left_known_ids, id);

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

	g_hash_table_iter_init (&iter, left_known_ids);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		ECalMetaBackendInfo *nfo;
		const gchar *uid = key;

		nfo = e_cal_meta_backend_info_new (uid, NULL, NULL, NULL);
		*out_removed_objects = g_slist_prepend (*out_removed_objects, nfo);
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	g_hash_table_destroy (left_known_ids);
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
	ICalComponentKind kind;
	JsonObject *item = NULL;
	const gchar *(* get_change_key_func) (JsonObject *item);
	guint pass;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbm365));

	LOCK (cbm365);

	for (pass = 0; pass < 2; pass++) {
		gchar *tmp_uid = NULL;
		const gchar *use_uid = uid;

		if (pass == 1) {
			gchar *filter = g_strdup_printf ("iCalUid eq '%s'", uid);

			if (kind == I_CAL_VEVENT_COMPONENT) {
				GSList *events = NULL;

				if (e_m365_connection_list_events_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
					cbm365->priv->folder_id, NULL, "id", filter, &events, cancellable, NULL)) {
					if (events && !events->next) {
						EM365Event *event = events->data;
						tmp_uid = g_strdup (e_m365_event_get_id (event));
					}
					g_slist_free_full (events, (GDestroyNotify) json_object_unref);
				}
			} else if (kind == I_CAL_VTODO_COMPONENT) {
				GSList *events = NULL;

				if (e_m365_connection_list_tasks_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
					cbm365->priv->folder_id, NULL, "id", filter, &events, cancellable, NULL)) {
					if (events && !events->next) {
						EM365Event *event = events->data;
						tmp_uid = g_strdup (e_m365_event_get_id (event));
					}
					g_slist_free_full (events, (GDestroyNotify) json_object_unref);
				}
			} else {
				g_free (filter);
				break;
			}

			g_free (filter);

			if (tmp_uid)
				use_uid = tmp_uid;
			else
				break;
		}

		switch (kind) {
		case I_CAL_VEVENT_COMPONENT:
			success = e_m365_connection_get_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
				cbm365->priv->folder_id, use_uid, NULL, NULL, &item, cancellable, pass == 0 ? error : NULL);
			get_change_key_func = e_m365_event_get_change_key;
			break;
		case I_CAL_VTODO_COMPONENT:
			success = e_m365_connection_get_task_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
				cbm365->priv->folder_id, use_uid, NULL, NULL, &item, cancellable, pass == 0 ? error : NULL);
			get_change_key_func = e_m365_task_get_last_modified_as_string;
			break;
		default:
			success = FALSE;
			break;
		}

		g_free (tmp_uid);

		if (success || g_cancellable_is_cancelled (cancellable))
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

	g_clear_pointer (&item, json_object_unref);

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	return success;
}

static ICalComponent *
ecb_m365_dup_main_comp (ICalComponent *icomp,
			ICalComponentKind kind)
{
	ICalComponent *main_comp = NULL;

	if (i_cal_component_isa (icomp) == I_CAL_VCALENDAR_COMPONENT) {
		ICalComponent *subcomp;

		for (subcomp = i_cal_component_get_first_component (icomp, kind);
		     subcomp;
		     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icomp, kind)) {
			if (!e_cal_util_component_has_property (subcomp, I_CAL_RECURRENCEID_PROPERTY)) {
				main_comp = g_object_ref (subcomp);
				break;
			}
		}

		g_clear_object (&subcomp);
	} else {
		main_comp = g_object_ref (icomp);
	}

	return main_comp;
}

static gboolean
ecb_m365_save_recurrence_changes_locked_sync (ECalBackendM365 *cbm365,
					      const GSList *instances, /* ECalComponent * */
					      const gchar *m365_uid,
					      ICalComponent *comp_in_extra,
					      GCancellable *cancellable,
					      GError **error)
{
	gboolean (* get_instance_id_func) (EM365Connection *cnc,
					   const gchar *user_override,
					   const gchar *group_id,
					   const gchar *calendar_id,
					   const gchar *event_id,
					   ICalTime *instance_time,
					   gchar **out_instance_id,
					   GCancellable *cancellable,
					   GError **error);
	gboolean (* get_item_func) (EM365Connection *cnc,
				    const gchar *user_override,
				    const gchar *group_id,
				    const gchar *calendar_id,
				    const gchar *event_id,
				    const gchar *prefer_outlook_timezone,
				    const gchar *select,
				    JsonObject **out_item,
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
	gboolean (* delete_item_func) (EM365Connection *cnc,
				       const gchar *user_override,
				       const gchar *group_id,
				       const gchar *calendar_id,
				       const gchar *event_id,
				       GCancellable *cancellable,
				       GError **error);
	ICalComponentKind kind;
	GHashTable *known_exdates = NULL;
	GSList *link;
	gboolean success = TRUE;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbm365));

	switch (kind) {
	case I_CAL_VEVENT_COMPONENT:
		get_instance_id_func = e_m365_connection_get_event_instance_id_sync;
		get_item_func = e_m365_connection_get_event_sync;
		update_item_func = e_m365_connection_update_event_sync;
		delete_item_func = e_m365_connection_delete_event_sync;
		break;
	case I_CAL_VTODO_COMPONENT:
		get_instance_id_func = NULL;
		get_item_func = e_m365_connection_get_task_sync;
		update_item_func = e_m365_connection_update_task_sync;
		delete_item_func = e_m365_connection_delete_task_sync;
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	if (comp_in_extra) {
		ICalComponent *main_comp;
		ICalProperty *prop;

		main_comp = ecb_m365_dup_main_comp (comp_in_extra, kind);

		for (prop = main_comp ? i_cal_component_get_first_property (main_comp, I_CAL_EXDATE_PROPERTY) : NULL;
		     prop;
		     g_object_unref (prop), prop = i_cal_component_get_next_property (main_comp, I_CAL_EXDATE_PROPERTY)) {
			ICalTime *exdate = i_cal_property_get_exdate (prop);

			if (!exdate)
				continue;

			if (!known_exdates)
				known_exdates = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

			g_hash_table_add (known_exdates, i_cal_time_as_ical_string (exdate));

			g_clear_object (&exdate);
		}

		g_clear_object (&prop);
		g_clear_object (&main_comp);
	}

	for (link = (GSList *) instances; link && success; link = g_slist_next (link)) {
		ICalComponent *icomp = e_cal_component_get_icalcomponent (link->data);
		ICalProperty *prop;

		if (!icomp)
			continue;

		prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY);
		if (prop) {
			ICalTime *recurid;

			recurid = i_cal_property_get_recurrenceid (prop);
			if (recurid) {
				gchar *instance_id = NULL;
				GError *local_error = NULL;

				if (get_instance_id_func) {
					success = get_instance_id_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
						cbm365->priv->folder_id, m365_uid, recurid, &instance_id, cancellable, &local_error);
				} else {
					g_propagate_error (&local_error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
						_("Cannot store detached instances, because not supported in Microsoft 365")));
					success = FALSE;
				}

				if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
					/* this should not happen, but ignore the error if it does */
					g_clear_error (&local_error);
					success = TRUE;
				} else if (success && instance_id) {
					JsonObject *item = NULL;

					success = get_item_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
						cbm365->priv->folder_id, instance_id, NULL, NULL, &item, cancellable, &local_error);

					if (success && item) {
						ICalComponent *old_comp;

						old_comp = ecb_m365_json_to_ical (cbm365, item, cancellable, &local_error);
						if (old_comp) {
							JsonBuilder *builder;

							builder = ecb_m365_ical_to_json_locked (cbm365, icomp, old_comp, cancellable, &local_error);

							if (builder) {
								success = update_item_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
									cbm365->priv->folder_id, instance_id, builder, cancellable, &local_error);

								if (success && !local_error) {
									success = ecb_m365_ical_to_json_2nd_go_locked (cbm365, icomp, old_comp,
										instance_id, cancellable, &local_error);
								}

								g_clear_object (&builder);
							} else if (local_error) {
								success = FALSE;
							}

							g_clear_object (&old_comp);
						} else {
							success = FALSE;
						}
					}

					g_clear_pointer (&item, json_object_unref);
				}

				g_clear_object (&recurid);
				g_free (instance_id);

				if (local_error) {
					g_propagate_error (error, local_error);
					success = FALSE;
				}
			}

			g_clear_object (&prop);
		} else {
			/* master object, handle EXDATE-s, which are for removed and detached instances */
			for (prop = i_cal_component_get_first_property (icomp, I_CAL_EXDATE_PROPERTY);
			     prop && success;
			     g_object_unref (prop), prop = i_cal_component_get_next_property (icomp, I_CAL_EXDATE_PROPERTY)) {
				ICalTime *exdate = i_cal_property_get_exdate (prop);
				gchar *exdate_str;

				if (!exdate)
					continue;

				exdate_str = i_cal_time_as_ical_string (exdate);
				if (exdate_str && (!known_exdates || !g_hash_table_contains (known_exdates, exdate_str))) {
					/* new EXDATE */
					gboolean is_detached_instance = FALSE;
					GSList *link2;

					for (link2 = (GSList *) instances; link2 && !is_detached_instance; link2 = g_slist_next (link2)) {
						ICalComponent *subcomp = e_cal_component_get_icalcomponent (link2->data);
						ICalProperty *subprop;

						if (!subcomp || subcomp == icomp)
							continue;

						subprop = i_cal_component_get_first_property (subcomp, I_CAL_RECURRENCEID_PROPERTY);
						if (subprop) {
							ICalTime *subrecurid = i_cal_property_get_recurrenceid (subprop);
							is_detached_instance = subrecurid && i_cal_time_compare_date_only (exdate, subrecurid) == 0;
							g_clear_object (&subrecurid);
						}

						g_clear_object (&subprop);
					}

					if (!is_detached_instance) {
						gchar *instance_id = NULL;
						GError *local_error = NULL;

						if (get_instance_id_func) {
							success = get_instance_id_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
								cbm365->priv->folder_id, m365_uid, exdate, &instance_id, cancellable, &local_error);
						} else {
							g_propagate_error (&local_error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
								_("Cannot store detached instances, because not supported in Microsoft 365")));
							success = FALSE;
						}

						if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
							/* removing already removed component => success */
							g_clear_error (&local_error);
							success = TRUE;
						} else if (success && instance_id) {
							success = delete_item_func (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
								cbm365->priv->folder_id, instance_id, cancellable, &local_error);

							if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
								/* removing already removed component => success */
								g_clear_error (&local_error);
								success = TRUE;
							}
						}

						g_free (instance_id);

						if (local_error) {
							g_propagate_error (error, local_error);
							success = FALSE;
						}
					}
				}

				g_clear_object (&exdate);
				g_free (exdate_str);
			}

			g_clear_object (&prop);
		}
	}

	g_clear_pointer (&known_exdates, g_hash_table_destroy);

	return success;
}

static gboolean
ecb_m365_organizer_is_user (ECalBackendM365 *cbm365,
			    ICalComponent *icomp)
{
	ICalProperty *prop;
	const gchar *email;
	gboolean is_organizer = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (cbm365), FALSE);
	g_return_val_if_fail (I_CAL_IS_COMPONENT (icomp), FALSE);

	prop = i_cal_component_get_first_property (icomp, I_CAL_ORGANIZER_PROPERTY);
	if (!prop)
		return FALSE;

	email = i_cal_property_get_organizer (prop);
	if (email && *email) {
		CamelM365Settings *m365_settings;
		gchar *user_email;

		m365_settings = camel_m365_settings_get_from_backend (E_BACKEND (cbm365), e_cal_backend_get_registry (E_CAL_BACKEND (cbm365)));

		user_email = camel_m365_settings_dup_email (m365_settings);

		email = e_cal_util_strip_mailto (email);

		is_organizer = user_email && g_ascii_strcasecmp (email, user_email) == 0;

		g_free (user_email);

		if (!is_organizer) {
			GHashTable *aliases;

			aliases = e_ews_common_utils_dup_mail_addresses (e_cal_backend_get_registry (E_CAL_BACKEND (cbm365)),
				e_backend_get_source (E_BACKEND (cbm365)), NULL);

			if (aliases) {
				is_organizer = g_hash_table_contains (aliases, email);

				g_hash_table_unref (aliases);
			}
		}
	}

	g_object_unref (prop);

	return is_organizer;
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
	ICalComponent *new_comp, *old_comp = NULL, *comp_in_extra = NULL;
	JsonBuilder *builder;
	gboolean success = FALSE, bad_instances = FALSE;
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

	new_comp = e_cal_component_get_icalcomponent (instances->data);

	if (instances->next) {
		gboolean has_master = FALSE;
		const gchar *uid = NULL;
		GSList *link;

		for (link = (GSList *) instances; link; link = g_slist_next (link)) {
			ECalComponent *comp = link->data;
			ICalComponent *icomp = e_cal_component_get_icalcomponent (comp);

			if (!uid)
				uid = i_cal_component_get_uid (icomp);
			else
				bad_instances = g_strcmp0 (uid, i_cal_component_get_uid (icomp)) != 0;

			if (bad_instances)
				break;

			if (!e_cal_util_component_has_property (icomp, I_CAL_RECURRENCEID_PROPERTY)) {
				bad_instances = has_master;
				has_master = TRUE;
				new_comp = icomp;
			}
		}
	}

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend))) {
	case I_CAL_VEVENT_COMPONENT:
		if (bad_instances) {
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
		if (bad_instances) {
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
				_("Can store only simple tasks into Microsoft 365 task folder")));

			return FALSE;
		}

		create_item_func = e_m365_connection_create_task_sync;
		update_item_func = e_m365_connection_update_task_sync;
		get_id_func = e_m365_task_get_id;
		get_change_key_func = e_m365_task_get_last_modified_as_string;
		break;
	default:
		g_warn_if_reached ();
		return FALSE;
	}

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	if (extra && *extra) {
		const gchar *comp_str;

		comp_str = ecb_m365_get_component_from_extra (extra);

		if (comp_str)
			comp_in_extra = i_cal_component_new_from_string (comp_str);

		if (comp_in_extra)
			old_comp = ecb_m365_dup_main_comp (comp_in_extra, e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend)));
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
		} else if (e_cal_util_component_has_organizer (new_comp) &&
			   e_cal_util_component_has_attendee (new_comp) &&
			   !ecb_m365_organizer_is_user (cbm365, new_comp)) {
			success = FALSE;
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_PERMISSION_DENIED, _("Cannot create meetings organized by other users in a Microsoft 365 calendar.")));
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

	if (*out_new_uid && success && (
	    e_cal_util_component_has_recurrences (new_comp) ||
	    e_cal_util_component_is_instance (new_comp))) {
		success = ecb_m365_save_recurrence_changes_locked_sync (cbm365, instances, *out_new_uid, comp_in_extra, cancellable, error);
	}

	UNLOCK (cbm365);

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);

	g_clear_object (&old_comp);
	g_clear_object (&comp_in_extra);

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
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_M365 (meta_backend), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbm365 = E_CAL_BACKEND_M365 (meta_backend);

	LOCK (cbm365);

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbm365))) {
	case I_CAL_VEVENT_COMPONENT:
		success = e_m365_connection_delete_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, uid, cancellable, &local_error);
		break;
	case I_CAL_VTODO_COMPONENT:
		success = e_m365_connection_delete_task_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, uid, cancellable, &local_error);
		break;
	default:
		g_warn_if_reached ();
		success = FALSE;
		break;
	}

	UNLOCK (cbm365);

	if (g_error_matches (local_error, E_M365_ERROR, E_M365_ERROR_ITEM_NOT_FOUND)) {
		g_clear_error (&local_error);
		success = TRUE;
	} else if (local_error) {
		g_propagate_error (error, local_error);
	}

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

static gboolean
ecb_m365_get_rsvp (ICalProperty *attendee,
		   EM365ResponseType *out_response,
		   gboolean *out_rsvp_requested)
{
	ICalParameter *param;

	if (!attendee)
		return FALSE;

	param = i_cal_property_get_first_parameter (attendee, I_CAL_PARTSTAT_PARAMETER);
	if (param) {
		switch (i_cal_parameter_get_partstat (param)) {
		case I_CAL_PARTSTAT_NEEDSACTION:
			*out_response = E_M365_RESPONSE_NOT_RESPONDED;
			break;
		case I_CAL_PARTSTAT_ACCEPTED:
			*out_response = E_M365_RESPONSE_ACCEPTED;
			break;
		case I_CAL_PARTSTAT_DECLINED:
			*out_response = E_M365_RESPONSE_DECLINED;
			break;
		case I_CAL_PARTSTAT_TENTATIVE:
			*out_response = E_M365_RESPONSE_TENTATIVELY_ACCEPTED;
			break;
		default:
			g_clear_object (&param);
			return FALSE;
		}
		g_clear_object (&param);
	}

	param = i_cal_property_get_first_parameter (attendee, I_CAL_RSVP_PARAMETER);
	if (param) {
		*out_rsvp_requested = i_cal_parameter_get_rsvp (param) == I_CAL_RSVP_TRUE;
		g_clear_object (&param);
	}

	return TRUE;
}

static EM365ResponseType
ecb_m365_get_current_user_meeting_reponse (ECalBackendM365 *cbm365,
					   ICalComponent *icomp,
					   const gchar *current_user_mail,
					   GHashTable *aliases,
					   gboolean *out_rsvp_requested)
{
	ICalProperty *attendee;
	const gchar *attendee_str = NULL, *attendee_mail = NULL;
	gint attendees_count = 0;
	EM365ResponseType response = E_M365_RESPONSE_NOT_RESPONDED;
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
				return E_M365_RESPONSE_ORGANIZER;
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
				found = ecb_m365_get_rsvp (attendee, &response, out_rsvp_requested);
				if (found) {
					if (response == E_M365_RESPONSE_NOT_RESPONDED) {
						found = FALSE;
					} else {
						/* stop early when have a response, in case
						   the user is in the attendees multiple times */
						g_clear_object (&attendee);
						break;
					}
				}
			}
		}
	}

	/* this should not happen, but if the user's configured email does not match the one
	   used in the invitation, like when the invitation comes to a mailing list... */
	if (!found && attendees_count == 1) {
		attendee = i_cal_component_get_first_property (icomp, I_CAL_ATTENDEE_PROPERTY);
		found = ecb_m365_get_rsvp (attendee, &response, out_rsvp_requested);
	} else if (!found) {
		attendee = e_ews_common_utils_find_attendee (E_CAL_BACKEND (cbm365), icomp, aliases);

		if (attendee) {
			found = ecb_m365_get_rsvp (attendee, &response, out_rsvp_requested);
			g_clear_object (&attendee);
		}
	}

	return response;
}

static gboolean
ecb_m365_comp_exists (ECalBackendM365 *cbm365,
		      ICalComponent *subcomp,
		      gchar **out_real_id,
		      GCancellable *cancellable)
{
	const gchar *uid = i_cal_component_get_uid (subcomp);
	gboolean exists = FALSE;
	ECalCache *cal_cache;

	if (!uid)
		return FALSE;

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbm365));
	if (cal_cache) {
		exists = e_cal_cache_contains (cal_cache, uid, NULL, E_CACHE_EXCLUDE_DELETED);
		g_clear_object (&cal_cache);
	}

	if (!exists) {
		EM365Event *event = NULL;

		LOCK (cbm365);
		exists = e_m365_connection_get_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, uid, NULL, "id", &event, cancellable, NULL);
		UNLOCK (cbm365);

		g_clear_pointer (&event, json_object_unref);
	}

	if (!exists) {
		/* search for the event by the iCalUid property */
		gchar *filter = g_strdup_printf ("iCalUid eq '%s'", uid);
		GSList *events = NULL;

		LOCK (cbm365);

		if (e_m365_connection_list_events_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
			cbm365->priv->folder_id, NULL, "id", filter, &events, cancellable, NULL)) {
			if (events && !events->next) {
				EM365Event *event = events->data;
				const gchar *tmp_id = e_m365_event_get_id (event);

				if (tmp_id && *tmp_id) {
					exists = TRUE;

					if (out_real_id)
						*out_real_id = g_strdup (tmp_id);
				}
			}

			g_slist_free_full (events, (GDestroyNotify) json_object_unref);
		}

		UNLOCK (cbm365);

		g_free (filter);
	}

	return exists;
}

static void
ecb_m365_receive_objects_sync (ECalBackendSync *sync_backend,
			       EDataCal *cal,
			       GCancellable *cancellable,
			       const gchar *calobj,
			       guint32 opflags,
			       GError **error)
{
	ECalBackendM365 *cbm365;
	ECalBackend *cal_backend;
	CamelM365Settings *m365_settings;
	ICalComponent *icomp, *subcomp;
	ICalComponentKind kind;
	GHashTable *aliases;
	gchar *user_email;
	gboolean success = TRUE, do_refresh = FALSE;

	g_return_if_fail (E_IS_CAL_BACKEND_M365 (sync_backend));

	cbm365 = E_CAL_BACKEND_M365 (sync_backend);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbm365), cancellable, error))
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

	cal_backend = E_CAL_BACKEND (cbm365);
	kind = e_cal_backend_get_kind (cal_backend);

	m365_settings = camel_m365_settings_get_from_backend (E_BACKEND (cal_backend), e_cal_backend_get_registry (cal_backend));
	user_email = camel_m365_settings_dup_email (m365_settings);
	aliases = e_ews_common_utils_dup_mail_addresses (e_cal_backend_get_registry (E_CAL_BACKEND (cbm365)),
		e_backend_get_source (E_BACKEND (cbm365)), &user_email);

	switch (i_cal_component_get_method (icomp)) {
	case I_CAL_METHOD_REQUEST:
	case I_CAL_METHOD_PUBLISH:
	case I_CAL_METHOD_REPLY: {
		GHashTable *split_by_uid = g_hash_table_new (g_str_hash, g_str_equal);
		GHashTableIter iter;
		gpointer value;

		for (subcomp = i_cal_component_get_first_component (icomp, kind);
		     subcomp;
		     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icomp, kind)) {
			const gchar *uid = i_cal_component_get_uid (subcomp);
			if (uid && *uid) {
				GSList *comps;
				comps = g_hash_table_lookup (split_by_uid, uid);
				comps = g_slist_prepend (comps, g_object_ref (subcomp));

				g_hash_table_insert (split_by_uid, (gpointer) uid, comps);
			}
		}

		g_hash_table_iter_init (&iter, split_by_uid);
		while (success && g_hash_table_iter_next (&iter, NULL, &value)) {
			GSList *comps = value;
			gchar *real_id = NULL;

			subcomp = comps->data;

			if (ecb_m365_comp_exists (cbm365, subcomp, &real_id, cancellable)) {
				EM365ResponseType response_type;
				gboolean rsvp_requested = FALSE;

				/* getting a data for meeting request response */
				response_type = ecb_m365_get_current_user_meeting_reponse (cbm365, subcomp, user_email, aliases, &rsvp_requested);
				rsvp_requested = rsvp_requested && !(opflags & E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE);

				if (response_type == E_M365_RESPONSE_ACCEPTED ||
				    response_type == E_M365_RESPONSE_DECLINED ||
				    response_type == E_M365_RESPONSE_TENTATIVELY_ACCEPTED) {
					const gchar *comment = i_cal_component_get_comment (subcomp);
					const gchar *uid = i_cal_component_get_uid (subcomp);
					GError *local_error = NULL;

					if (comment && !*comment)
						comment = NULL;

					LOCK (cbm365);

					success = e_m365_connection_response_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
						cbm365->priv->folder_id, real_id ? real_id : uid, response_type, comment, rsvp_requested, cancellable, &local_error);

					if (!success && g_error_matches (local_error, E_M365_ERROR, E_M365_ERROR_ID_MALFORMED)) {
						/* search for the event by the iCalUid property */
						gchar *filter = g_strdup_printf ("iCalUid eq '%s'", uid);
						GSList *events = NULL;

						g_clear_error (&local_error);

						if (e_m365_connection_list_events_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
							cbm365->priv->folder_id, NULL, "id", filter, &events, cancellable, NULL)) {
							if (events && !events->next) {
								EM365Event *event = events->data;
								const gchar *tmp_id = e_m365_event_get_id (event);
								if (tmp_id && *tmp_id) {
									g_clear_error (&local_error);
									success = e_m365_connection_response_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
										cbm365->priv->folder_id, tmp_id, response_type, comment, rsvp_requested, cancellable, &local_error);
								}
							}
							g_slist_free_full (events, (GDestroyNotify) json_object_unref);
						}
						g_free (filter);

						if (!success && !local_error)
							local_error = ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
					}

					if (local_error)
						g_propagate_error (error, local_error);

					UNLOCK (cbm365);

					do_refresh = TRUE;
				} else if (e_cal_util_component_has_organizer (subcomp) &&
					   e_cal_util_component_has_attendee (subcomp)) {
					g_set_error (error, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_UNKNOWN_USER, _("Cannot find user “%s” between attendees"), user_email ? user_email : "NULL");
					success = FALSE;
				}

				g_free (real_id);
			} else {
				EDataCal *data_cal;
				GSList *calobjs = NULL, *link, *new_uids = NULL, *new_components = NULL;
				GError *local_error = NULL;

				if (e_cal_util_component_has_organizer (subcomp) &&
				    e_cal_util_component_has_attendee (subcomp) &&
				    !ecb_m365_organizer_is_user (cbm365, subcomp)) {
					g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_PERMISSION_DENIED, _("Cannot create meetings organized by other users in a Microsoft 365 calendar.")));
					success = FALSE;
					break;
				}

				for (link = comps; link; link = g_slist_next (link)) {
					subcomp = link->data;
					calobjs = g_slist_prepend (calobjs, i_cal_component_as_ical_string (subcomp));
				}

				data_cal = e_cal_backend_ref_data_cal (E_CAL_BACKEND (cbm365));

				e_cal_backend_sync_create_objects (E_CAL_BACKEND_SYNC (cbm365), data_cal, cancellable, calobjs,
					opflags | E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE, &new_uids, &new_components, &local_error);

				success = !local_error;

				if (local_error)
					g_propagate_error (error, local_error);

				g_slist_free_full (calobjs, g_free);
				g_slist_free_full (new_uids, g_free);
				g_slist_free_full (new_components, g_object_unref);
				g_clear_object (&data_cal);
			}
		}
		g_hash_table_iter_init (&iter, split_by_uid);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			GSList *comps = value;
			g_slist_free_full (comps, g_object_unref);
		}
		g_hash_table_destroy (split_by_uid);
		} break;
	case I_CAL_METHOD_COUNTER:
		/*
		 * this is a new time proposal mail from one of the attendees
		 * if we decline the proposal, nothing have to be done
		 * if we accept it we will call to modify_object
		 */
		success = FALSE;
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_NOT_SUPPORTED));
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
		e_cal_meta_backend_schedule_refresh (E_CAL_META_BACKEND (cbm365));

	ecb_m365_convert_error_to_client_error (error);
	ecb_m365_maybe_disconnect_sync (cbm365, error, cancellable);
}

static void
ecb_m365_send_objects_sync (ECalBackendSync *sync_backend,
			    EDataCal *cal,
			    GCancellable *cancellable,
			    const gchar *calobj,
			    guint32 opflags,
			    GSList **users,
			    gchar **modified_calobj,
			    GError **error)
{
	ECalBackendM365 *cbm365;
	ICalComponentKind kind;
	ICalComponent *icomp, *subcomp = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_M365 (sync_backend));

	cbm365 = E_CAL_BACKEND_M365 (sync_backend);

	if (!e_cal_meta_backend_ensure_connected_sync (E_CAL_META_BACKEND (cbm365), cancellable, error))
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

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbm365));

	if (i_cal_component_isa (icomp) == I_CAL_VCALENDAR_COMPONENT) {
		subcomp = i_cal_component_get_first_component (icomp, kind);
	} else if (i_cal_component_isa (icomp) == I_CAL_VEVENT_COMPONENT) {
		subcomp = g_object_ref (icomp);
	}

	if (subcomp) {
		ICalProperty *prop;
		const gchar *uid = i_cal_component_get_uid (subcomp);
		const gchar *comment = i_cal_component_get_comment (subcomp);
		gchar *instance_id = NULL;
		gboolean success = TRUE;

		LOCK (cbm365);

		prop = i_cal_component_get_first_property (subcomp, I_CAL_RECURRENCEID_PROPERTY);
		if (prop) {
			ICalTime *recurid;

			recurid = i_cal_property_get_recurrenceid (prop);
			if (recurid) {
				success = e_m365_connection_get_event_instance_id_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
					cbm365->priv->folder_id, uid, recurid, &instance_id, cancellable, error);

				if (success && instance_id)
					uid = instance_id;

				g_clear_object (&recurid);
			}
		}

		if (success) {
			e_m365_connection_cancel_event_sync (cbm365->priv->cnc, NULL, cbm365->priv->group_id,
				cbm365->priv->folder_id, uid, comment, cancellable, error);
		}

		UNLOCK (cbm365);

		g_clear_object (&prop);
		g_free (instance_id);
	}

	g_clear_object (&subcomp);
	g_object_unref (icomp);

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
	ESourceRegistry *registry;
	CamelM365Settings *m365_settings;
	gchar *user_email;
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

	/* ensure the user email is set */
	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbm365));
	m365_settings = camel_m365_settings_get_from_backend (E_BACKEND (cbm365), registry);
	user_email = camel_m365_settings_dup_email (m365_settings);

	if (!user_email || !*user_email) {
		GHashTable *addresses;

		g_clear_pointer (&user_email, g_free);

		addresses = e_ews_common_utils_dup_mail_addresses (registry,
			e_backend_get_source (E_BACKEND (cbm365)), &user_email);
		g_clear_pointer (&addresses, g_hash_table_unref);

		if (user_email && *user_email)
			camel_m365_settings_set_email (m365_settings, user_email);
	}

	g_free (user_email);
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
	cal_backend_sync_class->receive_objects_sync = ecb_m365_receive_objects_sync;
	cal_backend_sync_class->send_objects_sync = ecb_m365_send_objects_sync;
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
