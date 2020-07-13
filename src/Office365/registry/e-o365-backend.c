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

#include <glib/gi18n-lib.h>

#include "common/e-o365-connection.h"
#include "common/e-source-o365-folder.h"
#include "common/camel-o365-settings.h"

#include "e-source-o365-deltas.h"

#include "e-o365-backend.h"

#define LOCK(_backend) g_mutex_lock (&_backend->priv->property_lock)
#define UNLOCK(_backend) g_mutex_unlock (&_backend->priv->property_lock)

struct _EO365BackendPrivate {
	GMutex property_lock;

	GHashTable *folder_sources; /* gchar *folder_id ~> ESource * */

	gboolean need_update_folders;

	gulong source_changed_id;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EO365Backend, e_o365_backend, E_TYPE_COLLECTION_BACKEND, 0,
	G_ADD_PRIVATE_DYNAMIC (EO365Backend))

static void o365_backend_populate (ECollectionBackend *backend);

static void
o365_backend_source_changed_cb (ESource *source,
				EO365Backend *backend)
{
	if (!e_source_get_enabled (source)) {
		backend->priv->need_update_folders = TRUE;
		return;
	}

	if (!backend->priv->need_update_folders)
		return;

	o365_backend_populate (E_COLLECTION_BACKEND (backend));
}

static void
o365_backend_populate (ECollectionBackend *backend)
{
	ESource *source;
	EO365Backend *o365_backend = E_O365_BACKEND (backend);

	source = e_backend_get_source (E_BACKEND (backend));

	o365_backend->priv->need_update_folders = TRUE;

	if (!o365_backend->priv->source_changed_id) {
		o365_backend->priv->source_changed_id = g_signal_connect (source, "changed",
			G_CALLBACK (o365_backend_source_changed_cb), o365_backend);
	}

	/* do not do anything, if account is disabled */
	if (!e_source_get_enabled (source))
		return;

	if (e_backend_get_online (E_BACKEND (backend)))
		e_backend_schedule_authenticate (E_BACKEND (backend), NULL);
}

static void
o365_backend_update_resource (EO365Backend *o365_backend,
			      const gchar *extension_name,
			      const gchar *id,
			      const gchar *display_name,
			      gboolean is_default,
			      const gchar *calendar_color)
{
	ESource *source;
	gboolean is_new;

	LOCK (o365_backend);
	source = g_hash_table_lookup (o365_backend->priv->folder_sources, id);
	if (source)
		g_object_ref (source);
	UNLOCK (o365_backend);

	is_new = !source;

	if (is_new)
		source = e_collection_backend_new_child (E_COLLECTION_BACKEND (o365_backend), id);

	if (source) {
		e_source_set_display_name (source, display_name);

		if (calendar_color && g_ascii_strcasecmp (calendar_color, "auto") != 0 && (
		    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_CALENDAR) == 0 ||
		    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_TASK_LIST) == 0 ||
		    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_MEMO_LIST) == 0)) {
			ESourceSelectable *selectable;

			selectable = e_source_get_extension (source, extension_name);
			e_source_selectable_set_color (selectable, calendar_color);
		}

		if (is_new) {
			ESourceRegistryServer *server;
			gpointer extension;

			extension = e_source_get_extension (source, extension_name);
			e_source_backend_set_backend_name (E_SOURCE_BACKEND (extension), "office365");

			/* Do not notify with too old reminders */
			if (g_strcmp0 (extension_name, E_SOURCE_EXTENSION_CALENDAR) == 0 ||
			    g_strcmp0 (extension_name, E_SOURCE_EXTENSION_TASK_LIST) == 0) {
				ESourceAlarms *alarms;
				gchar *today;
				GTimeVal today_tv;
				GDate dt;

				g_date_clear (&dt, 1);
				g_get_current_time (&today_tv);
				g_date_set_time_val (&dt, &today_tv);

				/* midnight UTC */
				today = g_strdup_printf ("%04d-%02d-%02dT00:00:00Z", g_date_get_year (&dt), g_date_get_month (&dt), g_date_get_day (&dt));

				alarms = e_source_get_extension (source, E_SOURCE_EXTENSION_ALARMS);
				e_source_alarms_set_last_notified (alarms, today);

				g_free (today);
			}

			extension = e_source_get_extension (source, E_SOURCE_EXTENSION_O365_FOLDER);
			e_source_o365_folder_set_id (extension, id);
			e_source_o365_folder_set_is_default (extension, is_default);

			server = e_collection_backend_ref_server (E_COLLECTION_BACKEND (o365_backend));

			e_source_registry_server_add_source (server, source);

			g_clear_object (&server);
		}
	}

	g_clear_object (&source);
}

static void
o365_backend_remove_resource (EO365Backend *o365_backend,
			      const gchar *extension_name,
			      const gchar *id) /* NULL to remove the "is-default" resource for the extension_name */
{
	ESource *existing_source;

	LOCK (o365_backend);

	if (id) {
		existing_source = g_hash_table_lookup (o365_backend->priv->folder_sources, id);
	} else {
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, o365_backend->priv->folder_sources);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			ESource *source = value;

			if (value && e_source_has_extension (source, extension_name) &&
			    e_source_o365_folder_get_is_default (e_source_get_extension (source, E_SOURCE_EXTENSION_O365_FOLDER))) {
				existing_source = source;
				break;
			}
		}
	}

	if (existing_source)
		g_object_ref (existing_source);

	UNLOCK (o365_backend);

	if (existing_source)
		e_source_remove_sync (existing_source, NULL, NULL);

	g_clear_object (&existing_source);
}

static void
o365_backend_forget_folders (EO365Backend *o365_backend,
			     const gchar *extension_name)
{
	GHashTableIter iter;
	GSList *ids = NULL, *link;
	gpointer value;

	LOCK (o365_backend);

	g_hash_table_iter_init (&iter, o365_backend->priv->folder_sources);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		ESource *source = value;

		if (source && e_source_has_extension (source, extension_name))
			ids = g_slist_prepend (ids, e_source_o365_folder_dup_id (e_source_get_extension (source, E_SOURCE_EXTENSION_O365_FOLDER)));
	}

	UNLOCK (o365_backend);

	for (link = ids; link; link = g_slist_next (link)) {
		const gchar *id = link->data;

		if (id)
			o365_backend_remove_resource (o365_backend, extension_name, id);
	}

	g_slist_free_full (ids, g_free);
}

static gboolean
o365_backend_got_contact_folders_delta_cb (EO365Connection *cnc,
					   const GSList *results, /* JsonObject * - the returned objects from the server */
					   gpointer user_data,
					   GCancellable *cancellable,
					   GError **error)
{
	EO365Backend *o365_backend = user_data;
	GSList *link;

	g_return_val_if_fail (E_IS_O365_BACKEND (o365_backend), FALSE);

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		JsonObject *object = link->data;
		const gchar *id = e_o365_folder_get_id (object);

		if (!id)
			continue;

		if (e_o365_delta_is_removed_object (object)) {
			o365_backend_remove_resource (o365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK, id);
		} else {
			o365_backend_update_resource (o365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK,
			      id, e_o365_folder_get_display_name (object),
			      FALSE, NULL);
		}
	}

	return TRUE;
}

static void
o365_backend_sync_folders_thread (GTask *task,
				  gpointer source_object,
				  gpointer task_data,
				  GCancellable *cancellable)
{
	EO365Backend *o365_backend = source_object;
	EO365Connection *cnc = task_data;
	ESourceO365Deltas *o365_deltas;
	EO365Folder *user_contacts = NULL;
	gchar *old_delta_link, *new_delta_link;
	gboolean success;
	GError *error = NULL;

	g_return_if_fail (E_IS_O365_BACKEND (o365_backend));
	g_return_if_fail (E_IS_O365_CONNECTION (cnc));

	o365_deltas = e_source_get_extension (e_backend_get_source (E_BACKEND (o365_backend)), E_SOURCE_EXTENSION_O365_DELTAS);

	if (e_o365_connection_get_contacts_folder_sync (cnc, NULL, &user_contacts, cancellable, &error)) {
		const gchar *id, *display_name;

		id = e_o365_folder_get_id (user_contacts);
		display_name = e_o365_folder_get_display_name (user_contacts);

		g_warn_if_fail (id != NULL);
		g_warn_if_fail (display_name != NULL);

		o365_backend_update_resource (o365_backend,
			E_SOURCE_EXTENSION_ADDRESS_BOOK,
			id, display_name, TRUE, NULL);

		json_object_unref (user_contacts);
	} else if (g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_NOT_FOUND) ||
		   g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		o365_backend_remove_resource (o365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK, NULL);
	}

	g_clear_error (&error);

	new_delta_link = NULL;
	old_delta_link = e_source_o365_deltas_dup_contacts_link (o365_deltas);

	success = e_o365_connection_get_folders_delta_sync (cnc, NULL, E_O365_FOLDER_KIND_CONTACTS, NULL, old_delta_link, 0,
		o365_backend_got_contact_folders_delta_cb, o365_backend, &new_delta_link, cancellable, &error);

	if (old_delta_link && *old_delta_link && e_o365_connection_util_delta_token_failed (error)) {
		g_clear_pointer (&old_delta_link, g_free);
		g_clear_error (&error);

		o365_backend_forget_folders (o365_backend, E_SOURCE_EXTENSION_ADDRESS_BOOK);

		success = e_o365_connection_get_folders_delta_sync (cnc, NULL, E_O365_FOLDER_KIND_CONTACTS, NULL, NULL, 0,
			o365_backend_got_contact_folders_delta_cb, o365_backend, &new_delta_link, cancellable, &error);
	}

	if (success)
		e_source_o365_deltas_set_contacts_link (o365_deltas, new_delta_link);

	g_clear_pointer (&old_delta_link, g_free);
	g_clear_pointer (&new_delta_link, g_free);
	g_clear_error (&error);
}

static void
o365_backend_sync_folders (EO365Backend *o365_backend,
			   EO365Connection *cnc,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer user_data)
{
	GTask *task;

	o365_backend->priv->need_update_folders = FALSE;

	task = g_task_new (o365_backend, cancellable, callback, user_data);

	g_task_set_check_cancellable (task, TRUE);
	g_task_set_task_data (task, g_object_ref (cnc), g_object_unref);
	g_task_run_in_thread (task, o365_backend_sync_folders_thread);

	g_object_unref (task);
}

static gchar *
o365_backend_dup_resource_id (ECollectionBackend *backend,
			      ESource *child_source)
{
	ESourceO365Folder *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_O365_FOLDER;
	extension = e_source_get_extension (child_source, extension_name);

	return e_source_o365_folder_dup_id (extension);
}

static void
o365_backend_child_added (ECollectionBackend *backend,
			  ESource *child_source)
{
	ESource *collection_source;

	collection_source = e_backend_get_source (E_BACKEND (backend));

	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION) && (
	    e_source_has_extension (child_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT) ||
	    e_source_has_extension (child_source, E_SOURCE_EXTENSION_MAIL_IDENTITY) ||
	    e_source_has_extension (child_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT))) {
		ESourceAuthentication *auth_child_extension;
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
		auth_child_extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION);

		e_binding_bind_property (
			collection_extension, "identity",
			auth_child_extension, "user",
			G_BINDING_SYNC_CREATE);
	}

	/* We track O365 folders in a hash table by folder ID. */
	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_O365_FOLDER)) {
		ESourceO365Folder *extension;
		gchar *folder_id;

		extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_O365_FOLDER);
		folder_id = e_source_o365_folder_dup_id (extension);

		if (folder_id) {
			EO365Backend *o365_backend = E_O365_BACKEND (backend);

			LOCK (o365_backend);
			g_hash_table_insert (o365_backend->priv->folder_sources, folder_id, g_object_ref (child_source));
			UNLOCK (o365_backend);
		}
	}

	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_CLASS (e_o365_backend_parent_class)->child_added (backend, child_source);
}

static void
o365_backend_child_removed (ECollectionBackend *backend,
			    ESource *child_source)
{
	/* We track O365 folders in a hash table by folder ID. */
	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_O365_FOLDER)) {
		ESourceO365Folder *extension;
		const gchar *folder_id;

		extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_O365_FOLDER);
		folder_id = e_source_o365_folder_get_id (extension);

		if (folder_id) {
			EO365Backend *o365_backend = E_O365_BACKEND (backend);

			LOCK (o365_backend);
			g_hash_table_remove (o365_backend->priv->folder_sources, folder_id);
			UNLOCK (o365_backend);
		}
	}

	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_CLASS (e_o365_backend_parent_class)->child_removed (backend, child_source);
}

static gboolean
o365_backend_create_resource_sync (ECollectionBackend *backend,
				   ESource *source,
				   GCancellable *cancellable,
				   GError **error)
{
#if 0
	EO365Connection *connection = NULL;
	O365FolderId *out_folder_id = NULL;
	EO365FolderType folder_type = E_O365_FOLDER_TYPE_UNKNOWN;
	const gchar *extension_name;
	const gchar *parent_folder_id = NULL;
	gchar *folder_name;
	gboolean success = FALSE;

	extension_name = E_SOURCE_EXTENSION_O365_FOLDER;
	if (e_source_has_extension (source, extension_name)) {
		ESourceO365Folder *extension;

		/* foreign and public folders are just added */
		extension = e_source_get_extension (source, extension_name);
		if (e_source_o365_folder_get_foreign (extension) ||
		    e_source_o365_folder_get_public (extension))
			success = TRUE;
	}

	if (!success) {
		connection = e_o365_backend_ref_connection_sync (E_O365_BACKEND (backend), NULL, NULL, NULL, cancellable, error);
		if (connection == NULL)
			return FALSE;

		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_O365_FOLDER_TYPE_CONTACTS;
			parent_folder_id = "contacts";
		}

		extension_name = E_SOURCE_EXTENSION_CALENDAR;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_O365_FOLDER_TYPE_CALENDAR;
			parent_folder_id = "calendar";
		}

		extension_name = E_SOURCE_EXTENSION_TASK_LIST;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_O365_FOLDER_TYPE_TASKS;
			parent_folder_id = "tasks";
		}

		/* FIXME No support for memo lists. */

		if (parent_folder_id == NULL) {
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_INVALID_ARGUMENT,
				_("Could not determine a suitable folder "
				"class for a new folder named “%s”"),
				e_source_get_display_name (source));
			goto exit;
		}

		folder_name = e_source_dup_display_name (source);

		success = e_o365_connection_create_folder_sync (
			connection, O365_PRIORITY_MEDIUM,
			parent_folder_id, TRUE,
			folder_name, folder_type,
			&out_folder_id, cancellable, error);

		g_free (folder_name);

		/* Sanity check */
		g_warn_if_fail (
			(success && out_folder_id != NULL) ||
			(!success && out_folder_id == NULL));

		if (out_folder_id != NULL) {
			ESourceO365Folder *extension;
			const gchar *extension_name;

			extension_name = E_SOURCE_EXTENSION_O365_FOLDER;
			extension = e_source_get_extension (source, extension_name);
			e_source_o365_folder_set_id (
				extension, out_folder_id->id);
			e_source_o365_folder_set_change_key (
				extension, out_folder_id->change_key);

			e_o365_folder_id_free (out_folder_id);
		}
	}

	if (success) {
		ESourceRegistryServer *server;
		ESource *parent_source;
		const gchar *cache_dir;
		const gchar *parent_uid;

		/* Configure the source as a collection member. */
		parent_source = e_backend_get_source (E_BACKEND (backend));
		parent_uid = e_source_get_uid (parent_source);
		e_source_set_parent (source, parent_uid);

		/* Changes should be written back to the cache directory. */
		cache_dir = e_collection_backend_get_cache_dir (backend);
		e_server_side_source_set_write_directory (
			E_SERVER_SIDE_SOURCE (source), cache_dir);

		/* Set permissions for clients. */
		e_server_side_source_set_writable (E_SERVER_SIDE_SOURCE (source), TRUE);
		e_server_side_source_set_remote_deletable (E_SERVER_SIDE_SOURCE (source), TRUE);

		server = e_collection_backend_ref_server (backend);
		e_source_registry_server_add_source (server, source);
		g_object_unref (server);
	}

 exit:
	if (connection)
		g_object_unref (connection);

	return success;
#endif
	return FALSE;
}

static gboolean
o365_backend_delete_resource_sync (ECollectionBackend *backend,
				   ESource *source,
				   GCancellable *cancellable,
				   GError **error)
{
#if 0
	EO365Connection *connection;
	ESourceO365Folder *extension;
	const gchar *extension_name;
	gboolean success = FALSE;

	connection = e_o365_backend_ref_connection_sync (E_O365_BACKEND (backend), NULL, NULL, NULL, cancellable, error);
	if (connection == NULL)
		return FALSE;

	extension_name = E_SOURCE_EXTENSION_O365_FOLDER;
	if (!e_source_has_extension (source, extension_name)) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_INVALID_ARGUMENT,
			_("Data source “%s” does not represent an Office365 folder"),
			e_source_get_display_name (source));
		goto exit;
	}
	extension = e_source_get_extension (source, extension_name);

	if (e_source_o365_folder_get_foreign (extension) ||
	    e_source_o365_folder_get_public (extension)) {
		/* do not delete foreign or public folders,
		 * just remove them from local store */
		success = TRUE;
	} else {
		gchar *folder_id;

		folder_id = e_source_o365_folder_dup_id (extension);

		success = e_o365_connection_delete_folder_sync (
			connection, O365_PRIORITY_MEDIUM, folder_id,
			FALSE, "HardDelete", cancellable, error);

		g_free (folder_id);
	}

	if (success)
		success = e_source_remove_sync (source, cancellable, error);

 exit:
	g_object_unref (connection);

	return success;
#endif
	return FALSE;
}

static gboolean
o365_backend_get_destination_address (EBackend *backend,
				      gchar **host,
				      guint16 *port)
{
	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	*host = g_strdup ("graph.microsoft.com");
	*port = 443;

	return TRUE;
}

static ESourceAuthenticationResult
o365_backend_authenticate_sync (EBackend *backend,
				const ENamedParameters *credentials,
				gchar **out_certificate_pem,
				GTlsCertificateFlags *out_certificate_errors,
				GCancellable *cancellable,
				GError **error)
{
	CamelO365Settings *o365_settings;
	EO365Connection *cnc;
	ESourceAuthenticationResult result;

	g_return_val_if_fail (E_IS_O365_BACKEND (backend), E_SOURCE_AUTHENTICATION_ERROR);

	o365_settings = camel_o365_settings_get_from_backend (backend, NULL);
	g_return_val_if_fail (o365_settings != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	cnc = e_o365_connection_new (e_backend_get_source (backend), o365_settings);

	result = e_o365_connection_authenticate_sync (cnc, NULL, E_O365_FOLDER_KIND_UNKNOWN, NULL, out_certificate_pem, out_certificate_errors, cancellable, error);

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		e_collection_backend_authenticate_children (E_COLLECTION_BACKEND (backend), credentials);
		o365_backend_sync_folders (E_O365_BACKEND (backend), cnc, NULL, NULL, NULL);
	} else if (result == E_SOURCE_AUTHENTICATION_REJECTED &&
		   !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD)) {
		result = E_SOURCE_AUTHENTICATION_REQUIRED;
	}

	g_clear_object (&cnc);

	return result;
}

static void
o365_backend_constructed (GObject *object)
{
	EBackend *backend;
	ESource *source;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_backend_parent_class)->constructed (object);

	backend = E_BACKEND (object);
	source = e_backend_get_source (backend);

	e_server_side_source_set_remote_creatable (E_SERVER_SIDE_SOURCE (source), TRUE);

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (backend, NULL);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
		e_source_collection_set_allow_sources_rename (collection_extension, TRUE);
	}
}

static void
o365_backend_dispose (GObject *object)
{
	EO365Backend *o365_backend = E_O365_BACKEND (object);
	ESource *source;

	source = e_backend_get_source (E_BACKEND (object));
	if (source && o365_backend->priv->source_changed_id) {
		g_signal_handler_disconnect (source, o365_backend->priv->source_changed_id);
		o365_backend->priv->source_changed_id = 0;
	}

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_backend_parent_class)->dispose (object);
}

static void
o365_backend_finalize (GObject *object)
{
	EO365Backend *o365_backend = E_O365_BACKEND (object);

	g_hash_table_destroy (o365_backend->priv->folder_sources);
	g_mutex_clear (&o365_backend->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_backend_parent_class)->finalize (object);
}

static void
e_o365_backend_class_init (EO365BackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECollectionBackendClass *collection_backend_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = o365_backend_dispose;
	object_class->finalize = o365_backend_finalize;
	object_class->constructed = o365_backend_constructed;

	collection_backend_class = E_COLLECTION_BACKEND_CLASS (class);
	collection_backend_class->populate = o365_backend_populate;
	collection_backend_class->dup_resource_id = o365_backend_dup_resource_id;
	collection_backend_class->child_added = o365_backend_child_added;
	collection_backend_class->child_removed = o365_backend_child_removed;
	collection_backend_class->create_resource_sync = o365_backend_create_resource_sync;
	collection_backend_class->delete_resource_sync = o365_backend_delete_resource_sync;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->get_destination_address = o365_backend_get_destination_address;
	backend_class->authenticate_sync = o365_backend_authenticate_sync;

	/* This generates an ESourceCamel subtype for CamelO365Settings. */
	e_source_camel_generate_subtype ("office365", CAMEL_TYPE_O365_SETTINGS);
}

static void
e_o365_backend_class_finalize (EO365BackendClass *class)
{
}

static void
e_o365_backend_init (EO365Backend *backend)
{
	backend->priv = e_o365_backend_get_instance_private (backend);
	backend->priv->folder_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	g_mutex_init (&backend->priv->property_lock);
}

void
e_o365_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_o365_backend_register_type (type_module);
}
