/*
 * e-ews-backend.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include "e-ews-backend.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include "server/e-ews-connection.h"
#include "server/e-source-ews-folder.h"

#define E_EWS_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_EWS_BACKEND, EEwsBackendPrivate))

typedef struct _SyncFoldersClosure SyncFoldersClosure;

struct _EEwsBackendPrivate {
	/* Folder ID -> ESource */
	GHashTable *folders;

	ESource *gal_source;
	gchar *oal_selected;

	gchar *sync_state;
	GMutex *sync_state_lock;
};

struct _SyncFoldersClosure {
	EEwsBackend *backend;
	GSList *folders_created;
	GSList *folders_deleted;
	GSList *folders_updated;
};

/* Forward Declarations */
static void	e_ews_backend_authenticator_init
				(ESourceAuthenticatorInterface *interface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EEwsBackend,
	e_ews_backend,
	E_TYPE_COLLECTION_BACKEND,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_ews_backend_authenticator_init))

static void
sync_folders_closure_free (SyncFoldersClosure *closure)
{
	g_object_unref (closure->backend);

	/* List of EEwsFolder instances. */
	g_slist_free_full (
		closure->folders_created,
		(GDestroyNotify) g_object_unref);

	/* List of folder ID strings. */
	g_slist_free_full (
		closure->folders_deleted,
		(GDestroyNotify) g_free);

	/* List of EEwsFolder instances. */
	g_slist_free_full (
		closure->folders_updated,
		(GDestroyNotify) g_object_unref);

	g_slice_free (SyncFoldersClosure, closure);
}

static CamelEwsSettings *
ews_backend_get_settings (EEwsBackend *backend)
{
	ESource *source;
	ESourceCamel *extension;
	CamelSettings *settings;
	const gchar *extension_name;

	source = e_backend_get_source (E_BACKEND (backend));
	extension_name = e_source_camel_get_extension_name ("ews");
	extension = e_source_get_extension (source, extension_name);
	settings = e_source_camel_get_settings (extension);

	return CAMEL_EWS_SETTINGS (settings);
}

static void
ews_backend_queue_auth_session (ECollectionBackend *backend)
{
	ESourceRegistryServer *server;
	EAuthenticationSession *session;
	ESource *source;

	server = e_collection_backend_ref_server (backend);
	source = e_backend_get_source (E_BACKEND (backend));

	session = e_authentication_session_new (
		server, E_SOURCE_AUTHENTICATOR (backend),
		e_source_get_uid (source));

	e_source_registry_server_queue_auth_session (server, session);

	g_object_unref (session);
	g_object_unref (server);
}

static ESource *
ews_backend_new_child (EEwsBackend *backend,
                       EEwsFolder *folder)
{
	ECollectionBackend *collection_backend;
	ESourceExtension *extension;
	ESource *source;
	const EwsFolderId *fid;
	const gchar *display_name;
	const gchar *extension_name;

	fid = e_ews_folder_get_id (folder);

	collection_backend = E_COLLECTION_BACKEND (backend);
	source = e_collection_backend_new_child (collection_backend, fid->id);

	display_name = e_ews_folder_get_name (folder);
	e_source_set_display_name (source, display_name);

	switch (e_ews_folder_get_folder_type (folder)) {
		case EWS_FOLDER_TYPE_CALENDAR:
			extension_name = E_SOURCE_EXTENSION_CALENDAR;
			break;
		case EWS_FOLDER_TYPE_TASKS:
			extension_name = E_SOURCE_EXTENSION_TASK_LIST;
			break;
		case EWS_FOLDER_TYPE_CONTACTS:
			extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
			break;
		default:
			g_object_unref (source);
			g_return_val_if_reached (NULL);
	}
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), "ews");

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	extension = e_source_get_extension (source, extension_name);
	e_source_ews_folder_set_id (
		E_SOURCE_EWS_FOLDER (extension), fid->id);
	e_source_ews_folder_set_change_key (
		E_SOURCE_EWS_FOLDER (extension), fid->change_key);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	extension = e_source_get_extension (source, extension_name);
	e_source_offline_set_stay_synchronized (
		E_SOURCE_OFFLINE (extension), TRUE);

	return source;
}

static ESource *
ews_backend_new_calendar (EEwsBackend *backend,
                          EEwsFolder *folder)
{
	/* No extra configuration to do. */
	return ews_backend_new_child (backend, folder);
}

static ESource *
ews_backend_new_task_list (EEwsBackend *backend,
                           EEwsFolder *folder)
{
	/* No extra configuration to do. */
	return ews_backend_new_child (backend, folder);
}

static ESource *
ews_backend_new_address_book (EEwsBackend *backend,
                              EEwsFolder *folder)
{
	/* No extra configuration to do. */
	return ews_backend_new_child (backend, folder);
}

static void
ews_backend_sync_created_folders (EEwsBackend *backend,
                                  GSList *list)
{
	ECollectionBackend *collection_backend;
	ESourceRegistryServer *server;
	GSList *link;

	collection_backend = E_COLLECTION_BACKEND (backend);
	server = e_collection_backend_ref_server (collection_backend);

	for (link = list; link != NULL; link = g_slist_next (link)) {
		EEwsFolder *folder = E_EWS_FOLDER (link->data);
		const EwsFolderId *fid;
		ESource *source = NULL;

		/* If we already know about this folder, skip it. */
		fid = e_ews_folder_get_id (folder);
		if (fid->id == NULL)
			continue;  /* not a valid ID anyway */
		if (g_hash_table_contains (backend->priv->folders, fid->id))
			continue;

		switch (e_ews_folder_get_folder_type (folder)) {
			case EWS_FOLDER_TYPE_CALENDAR:
				source = ews_backend_new_calendar (
					backend, folder);
				break;
			case EWS_FOLDER_TYPE_TASKS:
				source = ews_backend_new_task_list (
					backend, folder);
				break;
			case EWS_FOLDER_TYPE_CONTACTS:
				source = ews_backend_new_address_book (
					backend, folder);
				break;
			default:
				break;
		}

		if (source != NULL) {
			e_source_registry_server_add_source (server, source);
			g_object_unref (source);
		}
	}

	g_object_unref (server);
}

static void
ews_backend_sync_deleted_folders (EEwsBackend *backend,
                                  GSList *list)
{
	ECollectionBackend *collection_backend;
	ESourceRegistryServer *server;
	GSList *link;

	collection_backend = E_COLLECTION_BACKEND (backend);
	server = e_collection_backend_ref_server (collection_backend);

	for (link = list; link != NULL; link = g_slist_next (link)) {
		const gchar *folder_id = link->data;
		ESource *source = NULL;

		if (folder_id != NULL)
			source = g_hash_table_lookup (
				backend->priv->folders, folder_id);

		if (source == NULL)
			continue;

		/* This will trigger a "child-removed" signal and
		 * our handler will remove the hash table entry. */
		e_source_registry_server_remove_source (server, source);
	}

	g_object_unref (server);
}

static void
ews_backend_add_gal_source (EEwsBackend *backend)
{
	ECollectionBackend *collection_backend;
	ESourceAutocomplete *autocomplete_extension;
	ESourceBackend *backend_extension;
	ESourceEwsFolder *folder_extension;
	ESourceOffline *offline_extension;
	ESourceRegistryServer *server;
	ESource *source = NULL;
	CamelEwsSettings *settings;
	const gchar *display_name;
	const gchar *extension_name;
	const gchar *gal_uid;
	const gchar *oal_id;
	const gchar *uid;
	gchar *oal_selected;

	settings = ews_backend_get_settings (backend);
	collection_backend = E_COLLECTION_BACKEND (backend);

	gal_uid = camel_ews_settings_get_gal_uid (settings);

	if (gal_uid != NULL) {
		server = e_collection_backend_ref_server (collection_backend);
		source = e_source_registry_server_ref_source (server, gal_uid);
		g_object_unref (server);

		if (source != NULL) {
			g_object_unref (source);
			return;
		}
	}

	oal_selected = camel_ews_settings_dup_oal_selected (settings);

	/* This is supposed to be in the form: ID ':' NAME */
	if (oal_selected != NULL) {
		gchar *cp = strrchr (oal_selected, ':');
		if (cp != NULL) {
			*cp++ = '\0';
			display_name = cp;
			oal_id = oal_selected;
		} else {
			g_free (oal_selected);
			oal_selected = NULL;
		}
	}

	if (oal_selected == NULL) {
		display_name = _("Global Address List");
		oal_id = NULL;
	}

	g_free (backend->priv->oal_selected);
	backend->priv->oal_selected = oal_selected;  /* takes ownership */

	if (oal_id != NULL)
		source = e_collection_backend_new_child (
			collection_backend, oal_id);
	else
		source = e_collection_backend_new_child (
			collection_backend, "Global Address List");

	e_source_set_display_name (source, display_name);

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	backend_extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (backend_extension, "ews");

	extension_name = E_SOURCE_EXTENSION_AUTOCOMPLETE;
	autocomplete_extension = e_source_get_extension (source, extension_name);
	e_source_autocomplete_set_include_me (autocomplete_extension, TRUE);

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	folder_extension = e_source_get_extension (source, extension_name);
	e_source_ews_folder_set_id (folder_extension, oal_id);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	offline_extension = e_source_get_extension (source, extension_name);
	e_source_offline_set_stay_synchronized (offline_extension, TRUE);

	server = e_collection_backend_ref_server (collection_backend);
	e_source_registry_server_add_source (server, source);
	g_object_unref (server);

	uid = e_source_get_uid (source);
	camel_ews_settings_set_gal_uid (settings, uid);

	g_object_unref (source);
}

static void
ews_backend_source_changed_cb (ESource *source,
                               EEwsBackend *backend)
{
	CamelEwsSettings *settings;
	const gchar *oal_selected;
	const gchar *gal_uid;

	settings = ews_backend_get_settings (backend);
	gal_uid = camel_ews_settings_get_gal_uid (settings);
	oal_selected = camel_ews_settings_get_oal_selected (settings);

	if (g_strcmp0 (oal_selected, backend->priv->oal_selected) == 0)
		return;

	/* Remove the old Global Address List source if present. */
	if (gal_uid != NULL) {
		ECollectionBackend *collection_backend;
		ESourceRegistryServer *server;

		collection_backend = E_COLLECTION_BACKEND (backend);
		server = e_collection_backend_ref_server (collection_backend);
		source = e_source_registry_server_ref_source (server, gal_uid);

		if (source != NULL) {
			e_source_registry_server_remove_source (server, source);
			g_object_unref (source);
		}

		camel_ews_settings_set_gal_uid (settings, NULL);
	}

	ews_backend_add_gal_source (backend);
}

static gboolean
ews_backend_sync_folders_idle_cb (gpointer user_data)
{
	SyncFoldersClosure *closure = user_data;

	/* FIXME Handle updated folders. */

	ews_backend_sync_deleted_folders (
		closure->backend, closure->folders_deleted);
	ews_backend_sync_created_folders (
		closure->backend, closure->folders_created);

	return FALSE;
}

static void
ews_backend_dispose (GObject *object)
{
	EEwsBackendPrivate *priv;

	priv = E_EWS_BACKEND_GET_PRIVATE (object);

	g_hash_table_remove_all (priv->folders);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ews_backend_parent_class)->dispose (object);
}

static void
ews_backend_finalize (GObject *object)
{
	EEwsBackendPrivate *priv;

	priv = E_EWS_BACKEND_GET_PRIVATE (object);

	g_hash_table_destroy (priv->folders);

	g_free (priv->oal_selected);

	g_free (priv->sync_state);
	g_mutex_free (priv->sync_state_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ews_backend_parent_class)->finalize (object);
}

static void
ews_backend_populate (ECollectionBackend *backend)
{
	ESource *source;

	/* We test authentication passwords by attempting to synchronize
	 * the folder hierarchy.  Since we want to synchronize the folder
	 * hierarchy immediately on startup, schedule an authentication
	 * session first thing. */
	ews_backend_queue_auth_session (backend);

	ews_backend_add_gal_source (E_EWS_BACKEND (backend));

	source = e_backend_get_source (E_BACKEND (backend));

	g_signal_connect (
		source, "changed",
		G_CALLBACK (ews_backend_source_changed_cb), backend);
}

static gchar *
ews_backend_dup_resource_id (ECollectionBackend *backend,
                             ESource *child_source)
{
	ESourceEwsFolder *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	extension = e_source_get_extension (child_source, extension_name);

	return e_source_ews_folder_dup_id (extension);
}

static void
ews_backend_child_added (ECollectionBackend *backend,
                         ESource *child_source)
{
	EEwsBackendPrivate *priv;
	ESource *collection_source;
	const gchar *extension_name;
	gboolean is_mail = FALSE;

	priv = E_EWS_BACKEND_GET_PRIVATE (backend);

	collection_source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	/* Synchronize mail-related display names with the collection. */
	if (is_mail)
		g_object_bind_property (
			collection_source, "display-name",
			child_source, "display-name",
			G_BINDING_SYNC_CREATE);

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

		g_object_bind_property (
			collection_extension, "identity",
			auth_child_extension, "user",
			G_BINDING_SYNC_CREATE);
	}

	/* We track EWS folders in a hash table by folder ID. */
	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceEwsFolder *extension;
		gchar *folder_id;

		extension = e_source_get_extension (
			child_source, extension_name);
		folder_id = e_source_ews_folder_dup_id (extension);
		if (folder_id != NULL)
			g_hash_table_insert (
				priv->folders, folder_id,
				g_object_ref (child_source));
	}

	/* Chain up to parent's child_added() method. */
	E_COLLECTION_BACKEND_CLASS (e_ews_backend_parent_class)->
		child_added (backend, child_source);
}

static void
ews_backend_child_removed (ECollectionBackend *backend,
                           ESource *child_source)
{
	EEwsBackendPrivate *priv;
	const gchar *extension_name;

	priv = E_EWS_BACKEND_GET_PRIVATE (backend);

	/* We track EWS folders in a hash table by folder ID. */
	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceEwsFolder *extension;
		const gchar *folder_id;

		extension = e_source_get_extension (
			child_source, extension_name);
		folder_id = e_source_ews_folder_get_id (extension);
		if (folder_id != NULL)
			g_hash_table_remove (priv->folders, folder_id);
	}

	/* Chain up to parent's child_removed() method. */
	E_COLLECTION_BACKEND_CLASS (e_ews_backend_parent_class)->
		child_removed (backend, child_source);
}

static ESourceAuthenticationResult
ews_backend_try_password_sync (ESourceAuthenticator *authenticator,
                               const GString *password,
                               GCancellable *cancellable,
                               GError **error)
{
	EEwsBackend *backend;
	EEwsConnection *connection;
	ESource *source;
	ESourceCollection *collection_extension;
	ESourceAuthenticationResult result;
	CamelEwsSettings *settings;
	GSList *folders_created = NULL;
	GSList *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder = FALSE;
	const gchar *extension_name;
	gchar *sync_state;
	gchar *hosturl;
	gchar *user;
	GError *local_error = NULL;

	/* This tests the password by updating the folder hierarchy. */

	backend = E_EWS_BACKEND (authenticator);
	source = e_backend_get_source (E_BACKEND (backend));

	settings = ews_backend_get_settings (backend);
	hosturl = camel_ews_settings_dup_hosturl (settings);

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	collection_extension = e_source_get_extension (source, extension_name);
	user = e_source_collection_dup_identity (collection_extension);

	connection = e_ews_connection_new (
		hosturl, user, password->str,
		camel_network_settings_get_auth_mechanism (CAMEL_NETWORK_SETTINGS (settings)),
		camel_ews_settings_get_timeout (settings),
		NULL, NULL, error);

	g_free (hosturl);
	g_free (user);

	if (connection == NULL)
		return E_SOURCE_AUTHENTICATION_ERROR;

	g_mutex_lock (backend->priv->sync_state_lock);
	sync_state = g_strdup (backend->priv->sync_state);
	g_mutex_unlock (backend->priv->sync_state_lock);

	/* XXX I think this leaks the old sync_state value when
	 *     it replaces it with the new sync_state value. */
	e_ews_connection_sync_folder_hierarchy_sync (
		connection, EWS_PRIORITY_MEDIUM,
		&sync_state, &includes_last_folder,
		&folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	g_object_unref (connection);

	if (local_error == NULL) {
		SyncFoldersClosure *closure;

		/* We can now report the password was accepted.
		 * Because a password dialog may be stuck in a busy
		 * state, process the synchronization results from an
		 * idle callback so we don't delay the authentication
		 * session any longer than necessary. */

		/* This takes ownership of the folder lists. */
		closure = g_slice_new0 (SyncFoldersClosure);
		closure->backend = g_object_ref (backend);
		closure->folders_created = folders_created;
		closure->folders_deleted = folders_deleted;
		closure->folders_updated = folders_updated;

		g_idle_add_full (
			G_PRIORITY_DEFAULT_IDLE,
			ews_backend_sync_folders_idle_cb, closure,
			(GDestroyNotify) sync_folders_closure_free);

		g_mutex_lock (backend->priv->sync_state_lock);
		g_free (backend->priv->sync_state);
		backend->priv->sync_state = sync_state;
		g_mutex_unlock (backend->priv->sync_state_lock);

		result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	} else {
		gboolean auth_failed;

		/* Make sure we're not leaking anything. */
		g_warn_if_fail (folders_created == NULL);
		g_warn_if_fail (folders_updated == NULL);
		g_warn_if_fail (folders_deleted == NULL);

		auth_failed = g_error_matches (
			local_error, EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED);

		if (auth_failed) {
			g_clear_error (&local_error);
			result = E_SOURCE_AUTHENTICATION_REJECTED;
		} else {
			g_propagate_error (error, local_error);
			result = E_SOURCE_AUTHENTICATION_ERROR;
		}

		g_free (sync_state);
	}

	return result;
}

static void
e_ews_backend_class_init (EEwsBackendClass *class)
{
	GObjectClass *object_class;
	ECollectionBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EEwsBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = ews_backend_dispose;
	object_class->finalize = ews_backend_finalize;

	backend_class = E_COLLECTION_BACKEND_CLASS (class);
	backend_class->populate = ews_backend_populate;
	backend_class->dup_resource_id = ews_backend_dup_resource_id;
	backend_class->child_added = ews_backend_child_added;
	backend_class->child_removed = ews_backend_child_removed;

	/* This generates an ESourceCamel subtype for CamelEwsSettings. */
	e_source_camel_generate_subtype ("ews", CAMEL_TYPE_EWS_SETTINGS);
}

static void
e_ews_backend_class_finalize (EEwsBackendClass *class)
{
}

static void
e_ews_backend_authenticator_init (ESourceAuthenticatorInterface *interface)
{
	interface->try_password_sync = ews_backend_try_password_sync;
}

static void
e_ews_backend_init (EEwsBackend *backend)
{
	backend->priv = E_EWS_BACKEND_GET_PRIVATE (backend);

	backend->priv->folders = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	backend->priv->sync_state_lock = g_mutex_new ();
}

void
e_ews_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_backend_register_type (type_module);
}

