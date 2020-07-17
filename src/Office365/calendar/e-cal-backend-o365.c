/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libedata-cal/libedata-cal.h>
#include <libecal/libecal.h>

#include "common/camel-o365-settings.h"
#include "common/e-o365-connection.h"
#include "common/e-source-o365-folder.h"

#include "e-cal-backend-o365.h"

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

#define LOCK(_cb) g_rec_mutex_lock (&_cb->priv->property_lock)
#define UNLOCK(_cb) g_rec_mutex_unlock (&_cb->priv->property_lock)

struct _ECalBackendO365Private {
	GRecMutex property_lock;
	EO365Connection *cnc;
	gchar *group_id;
	gchar *calendar_id;
	gchar *attachments_dir;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendO365, e_cal_backend_o365, E_TYPE_CAL_META_BACKEND)

static void
ecb_o365_convert_error_to_client_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror ||
	    (*perror)->domain == E_CLIENT_ERROR ||
	    (*perror)->domain == E_CAL_CLIENT_ERROR)
		return;

	/*if ((*perror)->domain == O365_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case O365_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EC_ERROR_EX (E_CLIENT_ERROR_AUTHENTICATION_FAILED, (*perror)->message);
			break;
		case O365_CONNECTION_ERROR_FOLDERNOTFOUND:
		case O365_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case O365_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case O365_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR, (*perror)->message);
			break;
		case O365_CONNECTION_ERROR_EVENTNOTFOUND:
		case O365_CONNECTION_ERROR_ITEMNOTFOUND:
			error = ECC_ERROR_EX (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND, (*perror)->message);
			break;
		case O365_CONNECTION_ERROR_UNAVAILABLE:
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

static gchar *
ecb_o365_join_to_extra (const gchar *change_key,
			const gchar *ical_comp)
{
	if (!change_key && !ical_comp)
		return NULL;

	return g_strconcat (change_key ? change_key : "", "\n", ical_comp, NULL);
}

/* Modifies inout_extra, cannot be called multiple times with the same arguments */
static void
ecb_o365_split_extra (gchar *inout_extra,
		      const gchar **out_change_key,
		      const gchar **out_ical_comp)
{
	gchar *enter;

	if (!inout_extra)
		return;

	enter = strchr (inout_extra, '\n');
	g_return_if_fail (enter != NULL);

	*enter = '\0';
	enter++;

	if (out_change_key)
		*out_change_key = inout_extra;

	if (out_ical_comp)
		*out_ical_comp = enter;
}

static ECalMetaBackendInfo *
ecb_o365_json_to_ical_nfo (ECalBackendO365 *cbo365,
			   EO365Event *event,
			   GCancellable *cancellable,
			   GError **error)
{
	return NULL;
}

static gboolean
ecb_o365_download_event_changes_locked (ECalBackendO365 *cbo365,
					const GSList *ids,
					GSList **out_info_objects,
					GCancellable *cancellable,
					GError **error)
{
	GSList *events = NULL, *link;

	if (!ids)
		return TRUE;

	if (!e_o365_connection_get_events_sync (cbo365->priv->cnc, NULL, cbo365->priv->group_id, cbo365->priv->calendar_id, ids, NULL, NULL, &events, cancellable, error))
		return FALSE;

	for (link = events; link; link = g_slist_next (link)) {
		EO365Event *event = link->data;
		ECalMetaBackendInfo *nfo;

		if (!event)
			continue;

		nfo = ecb_o365_json_to_ical_nfo (cbo365, event, cancellable, error);

		if (nfo)
			*out_info_objects = g_slist_prepend (*out_info_objects, nfo);
	}

	g_slist_free_full (events, (GDestroyNotify) json_object_unref);

	return TRUE;
}

static void
ecb_o365_maybe_disconnect_sync (ECalBackendO365 *cbo365,
				GError **in_perror,
				GCancellable *cancellable)
{
	g_return_if_fail (E_IS_CAL_BACKEND_O365 (cbo365));

	if (in_perror && g_error_matches (*in_perror, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		e_cal_meta_backend_disconnect_sync (E_CAL_META_BACKEND (cbo365), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (cbo365), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static gboolean
ecb_o365_unset_connection_sync (ECalBackendO365 *cbo365,
				gboolean is_disconnect,
				GCancellable *cancellable,
				GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (cbo365), FALSE);

	LOCK (cbo365);

	if (cbo365->priv->cnc) {
		if (is_disconnect)
			success = e_o365_connection_disconnect_sync (cbo365->priv->cnc, cancellable, error);
	}

	g_clear_object (&cbo365->priv->cnc);
	g_clear_pointer (&cbo365->priv->group_id, g_free);
	g_clear_pointer (&cbo365->priv->calendar_id, g_free);

	UNLOCK (cbo365);

	return success;
}

static gboolean
ecb_o365_connect_sync (ECalMetaBackend *meta_backend,
		       const ENamedParameters *credentials,
		       ESourceAuthenticationResult *out_auth_result,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
		       GCancellable *cancellable,
		       GError **error)
{
	ECalBackendO365 *cbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	cbo365 = E_CAL_BACKEND_O365 (meta_backend);

	LOCK (cbo365);

	if (cbo365->priv->cnc) {
		UNLOCK (cbo365);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	} else {
		EBackend *backend;
		ESourceRegistry *registry;
		ESource *source;
		EO365Connection *cnc;
		ESourceO365Folder *o365_folder_extension;
		CamelO365Settings *o365_settings;
		gchar *group_id;
		gchar *calendar_id;

		backend = E_BACKEND (cbo365);
		source = e_backend_get_source (backend);
		registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbo365));
		o365_settings = camel_o365_settings_get_from_backend (backend, registry);
		g_warn_if_fail (o365_settings != NULL);

		o365_folder_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_O365_FOLDER);
		group_id = e_source_o365_folder_dup_group_id (o365_folder_extension);
		calendar_id = e_source_o365_folder_dup_id (o365_folder_extension);

		if (calendar_id) {
			cnc = e_o365_connection_new_for_backend (backend, registry, source, o365_settings);

			*out_auth_result = e_o365_connection_authenticate_sync (cnc, NULL, E_O365_FOLDER_KIND_CALENDAR, group_id, calendar_id,
				out_certificate_pem, out_certificate_errors, cancellable, error);

			if (*out_auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
				cbo365->priv->cnc = g_object_ref (cnc);

				g_warn_if_fail (cbo365->priv->group_id == NULL);
				g_warn_if_fail (cbo365->priv->calendar_id == NULL);

				g_free (cbo365->priv->group_id);
				cbo365->priv->group_id = group_id;

				g_free (cbo365->priv->calendar_id);
				cbo365->priv->calendar_id = calendar_id;

				group_id = NULL;
				calendar_id = NULL;
				success = TRUE;

				e_cal_backend_set_writable (E_CAL_BACKEND (cbo365), TRUE);
			}
		} else {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
			g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Folder ID is not set")));
		}

		g_clear_object (&cnc);
		g_free (group_id);
		g_free (calendar_id);
	}

	UNLOCK (cbo365);

	ecb_o365_convert_error_to_client_error (error);

	return success;
}

static gboolean
ecb_o365_disconnect_sync (ECalMetaBackend *meta_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (meta_backend), FALSE);

	return ecb_o365_unset_connection_sync (E_CAL_BACKEND_O365 (meta_backend), TRUE, cancellable, error);
}

static gboolean
ecb_o365_get_changes_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendO365 *cbo365;
	ECalCache *cal_cache;
	EO365Calendar *o365_calendar = NULL;
	gboolean changed = FALSE;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	cbo365 = E_CAL_BACKEND_O365 (meta_backend);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	LOCK (cbo365);

	if (e_o365_connection_get_calendar_folder_sync (cbo365->priv->cnc, NULL, cbo365->priv->group_id, cbo365->priv->calendar_id, "id,changeKey",
		&o365_calendar, cancellable, error) && o365_calendar) {
		changed = g_strcmp0 (last_sync_tag, e_o365_calendar_get_change_key (o365_calendar)) != 0;

		if (changed)
			*out_new_sync_tag = g_strdup (e_o365_calendar_get_change_key (o365_calendar));

		json_object_unref (o365_calendar);
	} else {
		success = FALSE;
	}

	if (changed) {
		GSList *events = NULL, *link;
		gboolean full_read;

		full_read = !e_cache_get_count (E_CACHE (cal_cache), E_CACHE_INCLUDE_DELETED, cancellable, NULL);

		success = e_o365_connection_list_events_sync (cbo365->priv->cnc, NULL, cbo365->priv->group_id, cbo365->priv->calendar_id, NULL,
			full_read ? NULL : "id,changeKey", &events, cancellable, error);

		if (success) {
			GSList *new_ids = NULL; /* const gchar *, borrowed from 'events' objects */
			GSList *changed_ids = NULL; /* const gchar *, borrowed from 'events' objects */

			for (link = events; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
				EO365Event *event = link->data;
				const gchar *id, *change_key;
				gchar *extra = NULL;

				if (!event)
					continue;

				id = e_o365_event_get_id (event);
				change_key = e_o365_event_get_change_key (event);

				if (e_cal_cache_get_component_extra (cal_cache, id, NULL, &extra, cancellable, NULL)) {
					const gchar *saved_change_key = NULL;

					ecb_o365_split_extra (extra, &saved_change_key, NULL);

					if (g_strcmp0 (saved_change_key, change_key) == 0) {
						g_free (extra);
						continue;
					} else if (full_read) {
						ECalMetaBackendInfo *nfo;

						nfo = ecb_o365_json_to_ical_nfo (cbo365, event, cancellable, NULL);

						if (nfo)
							*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
					} else {
						changed_ids = g_slist_prepend (changed_ids, (gpointer) id);
					}

					g_free (extra);
				} else if (full_read) {
					ECalMetaBackendInfo *nfo;

					nfo = ecb_o365_json_to_ical_nfo (cbo365, event, cancellable, NULL);

					if (nfo)
						*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
				} else {
					new_ids = g_slist_prepend (new_ids, (gpointer) id);
				}
			}

			if (new_ids) {
				new_ids = g_slist_reverse (new_ids);
				success = ecb_o365_download_event_changes_locked (cbo365, new_ids, out_created_objects, cancellable, error);
			}

			if (success && changed_ids) {
				changed_ids = g_slist_reverse (changed_ids);
				success = ecb_o365_download_event_changes_locked (cbo365, changed_ids, out_modified_objects, cancellable, error);
			}

			g_slist_free (new_ids);
			g_slist_free (changed_ids);
		}

		g_slist_free_full (events, (GDestroyNotify) json_object_unref);
	}

	UNLOCK (cbo365);

	ecb_o365_convert_error_to_client_error (error);
	ecb_o365_maybe_disconnect_sync (cbo365, error, cancellable);

	g_clear_object (&cal_cache);

	return success;
}

static gboolean
ecb_o365_load_component_sync (ECalMetaBackend *meta_backend,
			      const gchar *uid,
			      const gchar *extra,
			      ICalComponent **out_component,
			      gchar **out_extra,
			      GCancellable *cancellable,
			      GError **error)
{
	ECalBackendO365 *cbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	cbo365 = E_CAL_BACKEND_O365 (meta_backend);

	LOCK (cbo365);

	UNLOCK (cbo365);

	ecb_o365_convert_error_to_client_error (error);
	ecb_o365_maybe_disconnect_sync (cbo365, error, cancellable);

	return success;
}

static gboolean
ecb_o365_save_component_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendO365 *cbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (meta_backend), FALSE);

	cbo365 = E_CAL_BACKEND_O365 (meta_backend);

	LOCK (cbo365);

	UNLOCK (cbo365);

	ecb_o365_convert_error_to_client_error (error);
	ecb_o365_maybe_disconnect_sync (cbo365, error, cancellable);

	return success;
}

static gboolean
ecb_o365_remove_component_sync (ECalMetaBackend *meta_backend,
				EConflictResolution conflict_resolution,
				const gchar *uid,
				const gchar *extra,
				const gchar *object,
				guint32 opflags,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendO365 *cbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbo365 = E_CAL_BACKEND_O365 (meta_backend);

	LOCK (cbo365);

	UNLOCK (cbo365);

	ecb_o365_convert_error_to_client_error (error);
	ecb_o365_maybe_disconnect_sync (cbo365, error, cancellable);

	return success;
}

static gchar *
ecb_o365_get_backend_property (ECalBackend *cal_backend,
			      const gchar *prop_name)
{
	ECalBackendO365 *cbo365;

	g_return_val_if_fail (E_IS_CAL_BACKEND_O365 (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	cbo365 = E_CAL_BACKEND_O365 (cal_backend);

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
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cbo365)),
			NULL);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		/* return email address of the person who opened the calendar */
		CamelO365Settings *o365_settings;

		o365_settings = camel_o365_settings_get_from_backend (E_BACKEND (cal_backend), e_cal_backend_get_registry (cal_backend));

		return camel_o365_settings_dup_email (o365_settings);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* Microsoft 365 does not support email based alarms */
		return NULL;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_o365_parent_class)->impl_get_backend_property (cal_backend, prop_name);
}

static gboolean
ecb_o365_get_destination_address (EBackend *backend,
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
ecb_o365_dup_component_revision (ECalCache *cal_cache,
				 ICalComponent *icomp,
				 gpointer user_data)
{
	g_return_val_if_fail (icomp != NULL, NULL);

	return e_cal_util_component_dup_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
}

static void
ecb_o365_constructed (GObject *object)
{
	ECalBackendO365 *cbo365 = E_CAL_BACKEND_O365 (object);
	ECalCache *cal_cache;
	gchar *cache_dirname;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_o365_parent_class)->constructed (object);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (E_BACKEND (object), NULL);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbo365));
	g_return_if_fail (cal_cache != NULL);

	cache_dirname = g_path_get_dirname (e_cache_get_filename (E_CACHE (cal_cache)));
	g_signal_connect (cal_cache, "dup-component-revision", G_CALLBACK (ecb_o365_dup_component_revision), NULL);

	g_clear_object (&cal_cache);

	cbo365->priv->attachments_dir = g_build_filename (cache_dirname, "attachments", NULL);
	g_mkdir_with_parents (cbo365->priv->attachments_dir, 0777);

	g_free (cache_dirname);
}

static void
ecb_o365_dispose (GObject *object)
{
	ECalBackendO365 *cbo365 = E_CAL_BACKEND_O365 (object);

	ecb_o365_unset_connection_sync (cbo365, FALSE, NULL, NULL);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_o365_parent_class)->dispose (object);
}

static void
ecb_o365_finalize (GObject *object)
{
	ECalBackendO365 *cbo365 = E_CAL_BACKEND_O365 (object);

	g_free (cbo365->priv->attachments_dir);

	g_rec_mutex_clear (&cbo365->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_o365_parent_class)->finalize (object);
}

static void
e_cal_backend_o365_init (ECalBackendO365 *cbo365)
{
	cbo365->priv = e_cal_backend_o365_get_instance_private (cbo365);

	g_rec_mutex_init (&cbo365->priv->property_lock);
}

static void
e_cal_backend_o365_class_init (ECalBackendO365Class *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_o365_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_o365_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_o365_get_changes_sync;
	cal_meta_backend_class->load_component_sync = ecb_o365_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_o365_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_o365_remove_component_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_o365_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ecb_o365_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ecb_o365_constructed;
	object_class->dispose = ecb_o365_dispose;
	object_class->finalize = ecb_o365_finalize;
}
