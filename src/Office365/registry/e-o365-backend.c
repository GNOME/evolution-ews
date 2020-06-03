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

#include "common/e-source-o365-folder.h"
#include "common/camel-o365-settings.h"

#include "e-o365-backend.h"

struct _EO365BackendPrivate {
	GMutex property_lock;

	gboolean need_update_folders;

	gulong source_changed_id;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EO365Backend, e_o365_backend, E_TYPE_COLLECTION_BACKEND, 0,
	G_ADD_PRIVATE_DYNAMIC (EO365Backend))

static void o365_backend_populate (ECollectionBackend *backend);

#if 0
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
#endif

static void
o365_backend_populate (ECollectionBackend *backend)
{
#if 0
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

	o365_backend_add_gal_source (o365_backend);
	o365_backend_claim_old_resources (backend);

	if (e_backend_get_online (E_BACKEND (backend))) {
		CamelO365Settings *o365_settings;

		o365_settings = camel_o365_settings_get_from_backend (o365_backend, NULL);

		if (e_o365_connection_utils_get_without_password (o365_settings)) {
			e_backend_schedule_authenticate (E_BACKEND (backend), NULL);
		} else {
			e_backend_credentials_required_sync (E_BACKEND (backend),
				E_SOURCE_CREDENTIALS_REASON_REQUIRED, NULL, 0, NULL,
				NULL, NULL);
		}
	}
#endif
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
#if 0
	ESource *collection_source;
	const gchar *extension_name;
	gboolean is_mail = FALSE;

	collection_source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	/* Synchronize mail-related user with the collection identity. */
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	if (is_mail && e_source_has_extension (child_source, extension_name)) {
		ESourceAuthentication *auth_child_extension;
		ESourceCollection *collection_extension;

		extension_name = E_SOURCE_EXTENSION_COLLECTION;
		collection_extension = e_source_get_extension (
			collection_source, extension_name);

		extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
		auth_child_extension = e_source_get_extension (
			child_source, extension_name);

		e_binding_bind_property (
			collection_extension, "identity",
			auth_child_extension, "user",
			G_BINDING_SYNC_CREATE);
	}

	/* We track O365 folders in a hash table by folder ID. */
	extension_name = E_SOURCE_EXTENSION_O365_FOLDER;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceO365Folder *extension;
		gchar *folder_id;

		extension = e_source_get_extension (
			child_source, extension_name);
		folder_id = e_source_o365_folder_dup_id (extension);
		if (folder_id != NULL) {
			o365_backend_folders_insert (
				E_O365_BACKEND (backend),
				folder_id, child_source);
			g_free (folder_id);
		}
	}
#endif
	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_CLASS (e_o365_backend_parent_class)->child_added (backend, child_source);
}

static void
o365_backend_child_removed (ECollectionBackend *backend,
			    ESource *child_source)
{
#if 0
	const gchar *extension_name;

	/* We track O365 folders in a hash table by folder ID. */
	extension_name = E_SOURCE_EXTENSION_O365_FOLDER;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceO365Folder *extension;
		const gchar *folder_id;

		extension = e_source_get_extension (
			child_source, extension_name);
		folder_id = e_source_o365_folder_get_id (extension);
		if (folder_id != NULL)
			o365_backend_folders_remove (
				E_O365_BACKEND (backend), folder_id);
	}
#endif

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
	/*EO365Backend *o365_backend;
	EO365Connection *connection;*/
	CamelO365Settings *o365_settings;
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ERROR;

	g_return_val_if_fail (E_IS_O365_BACKEND (backend), E_SOURCE_AUTHENTICATION_ERROR);

	o365_settings = camel_o365_settings_get_from_backend (backend, NULL);
	g_return_val_if_fail (o365_settings != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	/*o365_backend = E_O365_BACKEND (backend);
	g_mutex_lock (&o365_backend->priv->connection_lock);
	g_clear_object (&o365_backend->priv->connection);
	e_named_parameters_free (o365_backend->priv->credentials);
	o365_backend->priv->credentials = e_named_parameters_new_clone (credentials);
	g_mutex_unlock (&o365_backend->priv->connection_lock);

	connection = e_o365_backend_ref_connection_sync (o365_backend, &result, out_certificate_pem, out_certificate_errors, cancellable, error);
	g_clear_object (&connection);

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		e_collection_backend_authenticate_children (E_COLLECTION_BACKEND (backend), credentials);

		e_o365_backend_sync_folders (o365_backend, NULL, o365_backend_folders_synced_cb, NULL);
	} else if (e_o365_connection_utils_get_without_password (o365_settings) &&
		   result == E_SOURCE_AUTHENTICATION_REJECTED &&
		   !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD)) {
		e_o365_connection_utils_force_off_ntlm_auth_check ();
		result = E_SOURCE_AUTHENTICATION_REQUIRED;
	}*/

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
