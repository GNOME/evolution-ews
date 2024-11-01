/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-ews-backend.h"

#include <glib/gi18n-lib.h>

#include "common/e-ews-connection-utils.h"
#include "common/e-source-ews-folder.h"
#include "../Microsoft365/common/camel-m365-settings.h"

#define EWS_HELPER_M365_RESOURCE_ID "helper-m365-calendar"

typedef struct _SyncFoldersClosure SyncFoldersClosure;

struct _EEwsBackendPrivate {
	gchar *deleted_items_folder_id;
	/* Folder ID -> ESource */
	GHashTable *folders;
	GMutex folders_lock;

	ESource *gal_source;
	gchar *oal_selected;

	gchar *sync_state;
	GMutex sync_state_lock;

	ENamedParameters *credentials;
	EEwsConnection *connection;
	GMutex connection_lock;

	gboolean need_update_folders;

	gulong source_changed_id;
};

struct _SyncFoldersClosure {
	EEwsBackend *backend;
	GSList *folders_created;
	GSList *folders_deleted;
	GSList *folders_updated;
	GHashTable *old_sources; /* gchar *folder_id ~> ESource * */
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EEwsBackend, e_ews_backend, E_TYPE_COLLECTION_BACKEND, 0,
	G_ADD_PRIVATE_DYNAMIC (EEwsBackend))

static void
sync_folders_closure_free (SyncFoldersClosure *closure)
{
	g_object_unref (closure->backend);

	/* List of EEwsFolder instances. */
	g_slist_free_full (closure->folders_created, g_object_unref);

	/* List of folder ID strings. */
	g_slist_free_full (closure->folders_deleted, g_free);

	/* List of EEwsFolder instances. */
	g_slist_free_full (closure->folders_updated, g_object_unref);

	g_hash_table_destroy (closure->old_sources);

	g_slice_free (SyncFoldersClosure, closure);
}

static void
ews_backend_folders_insert (EEwsBackend *backend,
                            gchar *folder_id, /* assumes ownership */
                            ESource *source)
{
	g_return_if_fail (folder_id != NULL);
	g_return_if_fail (E_IS_SOURCE (source));

	g_mutex_lock (&backend->priv->folders_lock);

	g_hash_table_insert (
		backend->priv->folders,
		folder_id,
		g_object_ref (source));

	g_mutex_unlock (&backend->priv->folders_lock);
}

static ESource *
ews_backend_folders_lookup (EEwsBackend *backend,
                            const gchar *folder_id)
{
	ESource *source;

	g_return_val_if_fail (folder_id != NULL, NULL);

	g_mutex_lock (&backend->priv->folders_lock);

	source = g_hash_table_lookup (backend->priv->folders, folder_id);

	if (source != NULL)
		g_object_ref (source);

	g_mutex_unlock (&backend->priv->folders_lock);

	return source;
}

static gboolean
ews_backend_folders_remove (EEwsBackend *backend,
                            const gchar *folder_id)
{
	gboolean removed;

	g_return_val_if_fail (folder_id != NULL, FALSE);

	g_mutex_lock (&backend->priv->folders_lock);

	removed = g_hash_table_remove (backend->priv->folders, folder_id);

	g_mutex_unlock (&backend->priv->folders_lock);

	return removed;
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
ews_backend_sync_authentication (EEwsBackend *ews_backend,
				 ESource *child_source)
{
	ESourceAuthentication *coll_authentication_extension, *child_authentication_extension;
	ESource *collection_source;

	g_return_if_fail (E_IS_EWS_BACKEND (ews_backend));
	g_return_if_fail (E_IS_SOURCE (child_source));

	collection_source = e_backend_get_source (E_BACKEND (ews_backend));

	coll_authentication_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	child_authentication_extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	e_source_authentication_set_host (child_authentication_extension,
		e_source_authentication_get_host (coll_authentication_extension));

	e_source_authentication_set_user (child_authentication_extension,
		e_source_authentication_get_user (coll_authentication_extension));

	e_source_authentication_set_method (child_authentication_extension,
		e_source_authentication_get_method (coll_authentication_extension));
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

	g_return_val_if_fail (fid != NULL, NULL);

	display_name = e_ews_folder_get_name (folder);

	/* Hide on-the-server hidden folders */
	if (e_ews_folder_get_is_hidden (folder))
		return NULL;

	collection_backend = E_COLLECTION_BACKEND (backend);
	source = e_collection_backend_new_child (collection_backend, fid->id);

	e_source_set_display_name (source, display_name);

	switch (e_ews_folder_get_folder_type (folder)) {
		case E_EWS_FOLDER_TYPE_CALENDAR:
			extension_name = E_SOURCE_EXTENSION_CALENDAR;
			break;
		case E_EWS_FOLDER_TYPE_TASKS:
			extension_name = E_SOURCE_EXTENSION_TASK_LIST;
			break;
		case E_EWS_FOLDER_TYPE_MEMOS:
			extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
			break;
		case E_EWS_FOLDER_TYPE_CONTACTS:
			extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
			break;
		default:
			g_object_unref (source);
			g_return_val_if_reached (NULL);
	}
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), "ews");
	ews_backend_sync_authentication (backend, source);

	if (e_ews_folder_get_folder_type (folder) != E_EWS_FOLDER_TYPE_CONTACTS &&
	    !e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER) &&
	    !e_source_has_extension (source, E_SOURCE_EXTENSION_ALARMS)) {
		/* a completely new ESource, do not notify with too old reminders */
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

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	extension = e_source_get_extension (source, extension_name);
	e_source_ews_folder_set_id (
		E_SOURCE_EWS_FOLDER (extension), fid->id);
	e_source_ews_folder_set_change_key (
		E_SOURCE_EWS_FOLDER (extension), fid->change_key);
	e_source_ews_folder_set_name (E_SOURCE_EWS_FOLDER (extension), e_ews_folder_get_name (folder));

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	extension = e_source_get_extension (source, extension_name);
	e_source_offline_set_stay_synchronized (
		E_SOURCE_OFFLINE (extension), TRUE);

	e_server_side_source_set_remote_deletable (
		E_SERVER_SIDE_SOURCE (source), TRUE);

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
ews_backend_new_memo_list (EEwsBackend *backend,
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
ews_backend_update_folder_name (ESource *source,
				EEwsFolder *folder)
{
	if (folder && source && e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER)) {
		ESourceEwsFolder *folder_extension;

		folder_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);

		/* The user did not change the folder name (old sources have stored NULL), but it changed on the server */
		if ((!e_source_ews_folder_get_name (folder_extension) ||
		    g_strcmp0 (e_source_ews_folder_get_name (folder_extension), e_source_get_display_name (source)) == 0) &&
		    g_strcmp0 (e_source_get_display_name (source), e_ews_folder_get_name (folder)) != 0) {
			e_source_set_display_name (source, e_ews_folder_get_name (folder));
		}

		e_source_ews_folder_set_name (folder_extension, e_ews_folder_get_name (folder));
	}
}

static void
ews_backend_sync_created_folders (EEwsBackend *backend,
				  GSList *list,
				  GHashTable *old_sources)
{
	ECollectionBackend *collection_backend;
	ESourceRegistryServer *server;
	GSList *link;

	collection_backend = E_COLLECTION_BACKEND (backend);
	server = e_collection_backend_ref_server (collection_backend);

	for (link = list; link != NULL; link = g_slist_next (link)) {
		EEwsFolder *folder = E_EWS_FOLDER (link->data);
		const EwsFolderId *fid, *parent_fid;
		ESource *source;

		fid = e_ews_folder_get_id (folder);
		if (!fid || !fid->id)
			continue;

		/* Skip those under 'Deleted Items' */
		parent_fid = e_ews_folder_get_parent_id (folder);
		if (parent_fid && parent_fid->id && g_strcmp0 (parent_fid->id, backend->priv->deleted_items_folder_id) == 0)
			continue;

		source = ews_backend_folders_lookup (backend, fid->id);

		/* If we already know about this folder, skip it. */
		if (source) {
			ews_backend_update_folder_name (source, folder);
			g_hash_table_remove (old_sources, fid->id);
			g_clear_object (&source);
			continue;
		}

		switch (e_ews_folder_get_folder_type (folder)) {
			case E_EWS_FOLDER_TYPE_CALENDAR:
				source = ews_backend_new_calendar (
					backend, folder);
				break;
			case E_EWS_FOLDER_TYPE_TASKS:
				source = ews_backend_new_task_list (
					backend, folder);
				break;
			case E_EWS_FOLDER_TYPE_MEMOS:
				source = ews_backend_new_memo_list (
					backend, folder);
				break;
			case E_EWS_FOLDER_TYPE_CONTACTS:
				source = ews_backend_new_address_book (
					backend, folder);
				break;
			default:
				break;
		}

		if (source != NULL) {
			e_source_registry_server_add_source (server, source);

			g_hash_table_remove (old_sources, fid->id);

			g_object_unref (source);
		}
	}

	g_object_unref (server);
}

static void
ews_backend_delete_folder (EEwsBackend *backend,
			   const gchar *folder_id,
			   GHashTable *old_sources)
{
	ESource *source = NULL;

	if (folder_id) {
		source = ews_backend_folders_lookup (backend, folder_id);
		g_hash_table_remove (old_sources, folder_id);
	}

	if (source) {
		/* This will trigger a "child-removed" signal and
		 * our handler will remove the hash table entry. */
		e_source_remove_sync (source, NULL, NULL);

		g_object_unref (source);
	}
}

static void
ews_backend_sync_deleted_folders (EEwsBackend *backend,
				  GSList *list,
				  GHashTable *old_sources)
{
	GSList *link;

	for (link = list; link != NULL; link = g_slist_next (link)) {
		const gchar *folder_id = link->data;

		ews_backend_delete_folder (backend, folder_id, old_sources);
	}
}

static void
ews_backend_sync_updated_folders (EEwsBackend *backend,
				  GSList *list,
				  GHashTable *old_sources)
{
	GSList *link;

	for (link = list; link != NULL; link = g_slist_next (link)) {
		EEwsFolder *folder = link->data;
		const EwsFolderId *id, *parent_id;

		if (!folder)
			continue;

		id = e_ews_folder_get_id (folder);
		parent_id = e_ews_folder_get_parent_id (folder);

		if (id && parent_id) {
			/* Deleted calendars are under 'Deleted Items', even they are not visible in the OWA */
			if (backend->priv->deleted_items_folder_id &&
			    g_strcmp0 (parent_id->id, backend->priv->deleted_items_folder_id) == 0) {
				ews_backend_delete_folder (backend, id->id, old_sources);
			} else {
				ESource *source;

				source = ews_backend_folders_lookup (backend, id->id);

				if (source) {
					ews_backend_update_folder_name (source, folder);
					g_object_unref (source);
				}
			}
		}
	}
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
	const gchar *oal_id = NULL;
	const gchar *uid;
	gchar *oal_selected;

	settings = ews_backend_get_settings (backend);
	collection_backend = E_COLLECTION_BACKEND (backend);
	source = e_backend_get_source (E_BACKEND (backend));
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
			/* skip ':' and any leading backslash in the display name */
			cp++;
			while (*cp && *cp == '\\')
				cp++;

			display_name = cp;
			oal_id = oal_selected;
		} else {
			g_free (oal_selected);
			oal_selected = NULL;

			camel_ews_settings_set_oal_selected (settings, NULL);
		}
	}

	if (oal_selected == NULL) {
		display_name = _("Global Address List");
		oal_id = "global-address-list";
	}

	g_free (backend->priv->oal_selected);
	backend->priv->oal_selected = oal_selected;  /* takes ownership */

	source = e_collection_backend_new_child (
		collection_backend, oal_id);
	ews_backend_sync_authentication (backend, source);

	e_source_set_display_name (source, display_name);

	/* do not re-setup previously saved ESource,
	 * that would rewrite user's choice */
	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER)) {
		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		backend_extension = e_source_get_extension (source, extension_name);
		e_source_backend_set_backend_name (backend_extension, "ews");

		extension_name = E_SOURCE_EXTENSION_AUTOCOMPLETE;
		autocomplete_extension = e_source_get_extension (source, extension_name);
		e_source_autocomplete_set_include_me (autocomplete_extension, TRUE);

		extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
		folder_extension = e_source_get_extension (source, extension_name);
		e_source_ews_folder_set_id (folder_extension, oal_id);
		e_source_ews_folder_set_name (folder_extension, display_name);

		extension_name = E_SOURCE_EXTENSION_OFFLINE;
		offline_extension = e_source_get_extension (source, extension_name);
		e_source_offline_set_stay_synchronized (offline_extension, TRUE);
	}

	server = e_collection_backend_ref_server (collection_backend);
	e_source_registry_server_add_source (server, source);
	g_object_unref (server);

	uid = e_source_get_uid (source);
	camel_ews_settings_set_gal_uid (settings, uid);

	g_object_unref (source);
}

/* This source is used by the Calendar backend, when creating online meeting */
static void
ews_backend_maybe_add_m365_source (EEwsBackend *ews_backend)
{
	CamelEwsSettings *ews_settings;
	ECollectionBackend *collection_backend;
	ESource *collection_source;
	ESource *m365_source;
	ESourceAuthentication *auth_extension;
	ESourceAuthentication *collection_auth_extension;
	ESourceExtension *source_extension;
	ESourceRegistryServer *server;
	gchar *display_name;

	ews_settings = ews_backend_get_settings (ews_backend);

	if (camel_ews_settings_get_auth_mechanism (ews_settings) != EWS_AUTH_TYPE_OAUTH2)
		return;

	collection_source = e_backend_get_source (E_BACKEND (ews_backend));

	if (!collection_source)
		return;

	/* Make sure the ESourceCamel knows about it, even when no Microsoft365 mail account is created */
	e_source_camel_generate_subtype ("microsoft365", CAMEL_TYPE_M365_SETTINGS);

	collection_backend = E_COLLECTION_BACKEND (ews_backend);
	m365_source = e_collection_backend_new_child (collection_backend, EWS_HELPER_M365_RESOURCE_ID);

	display_name = g_strconcat (e_source_get_display_name (collection_source), " (Microsoft365)", NULL);

	source_extension = e_source_get_extension (m365_source, e_source_camel_get_extension_name ("microsoft365"));
	if (source_extension) {
		CamelSettings *settings = e_source_camel_get_settings (E_SOURCE_CAMEL (source_extension));

		if (settings) {
			g_object_set (settings,
				"host", "graph.microsoft.com",
				"auth-mechanism", "Microsoft365",
				NULL);
		}
	}

	collection_auth_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	e_source_set_display_name (m365_source, display_name);

	auth_extension = e_source_get_extension (m365_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	e_source_authentication_set_host (auth_extension, "graph.microsoft.com");
	e_source_authentication_set_method (auth_extension, "Microsoft365");
	e_source_authentication_set_user (auth_extension,
		e_source_authentication_get_user (collection_auth_extension));

	server = e_collection_backend_ref_server (collection_backend);
	e_source_registry_server_add_source (server, m365_source);

	g_object_unref (m365_source);
	g_object_unref (server);
	g_free (display_name);
}

static void ews_backend_populate (ECollectionBackend *backend);

static void
ews_backend_source_changed_cb (ESource *source,
                               EEwsBackend *backend)
{
	if (!e_collection_backend_get_part_enabled (E_COLLECTION_BACKEND (backend), E_COLLECTION_BACKEND_PART_ANY)) {
		backend->priv->need_update_folders = TRUE;
		return;
	}

	if (!backend->priv->need_update_folders)
		return;

	ews_backend_populate (E_COLLECTION_BACKEND (backend));
}

static void
add_remote_sources (EEwsBackend *backend,
		    GHashTable *old_sources)
{
	GHashTableIter iter;
	ESourceRegistryServer *registry;
	const gchar *extension_name;
	gpointer value;

	registry = e_collection_backend_ref_server (
		E_COLLECTION_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;

	g_hash_table_iter_init (&iter, old_sources);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		ESource *source = value;
		ESourceEwsFolder *extension;

		if (!e_source_has_extension (source, extension_name))
			continue;

		/* foreign or public folders are just added */
		extension = e_source_get_extension (source, extension_name);
		if (e_source_ews_folder_get_foreign (extension) ||
		    e_source_ews_folder_get_public (extension)) {
			e_server_side_source_set_writable (
				E_SERVER_SIDE_SOURCE (source), TRUE);
			e_server_side_source_set_remote_deletable (
				E_SERVER_SIDE_SOURCE (source), TRUE);
			e_source_registry_server_add_source (registry, source);
		} else {
			GError *error = NULL;

			if (!e_source_remove_sync (source, NULL, &error))
				g_warning ("%s: Failed to remove old EWS source '%s': %s", G_STRFUNC, e_source_get_uid (source),
					error ? error->message : "Unknown error");

			g_clear_error (&error);
		}
	}

	g_object_unref (registry);
}

static gboolean
ews_backend_sync_folders_idle_cb (gpointer user_data)
{
	SyncFoldersClosure *closure = user_data;

	ews_backend_sync_deleted_folders (closure->backend, closure->folders_deleted, closure->old_sources);
	ews_backend_sync_updated_folders (closure->backend, closure->folders_updated, closure->old_sources);
	ews_backend_sync_created_folders (closure->backend, closure->folders_created, closure->old_sources);

	add_remote_sources (closure->backend, closure->old_sources);

	return FALSE;
}

static void
ews_backend_dispose (GObject *object)
{
	EEwsBackend *ews_backend = E_EWS_BACKEND (object);
	ESource *source;

	source = e_backend_get_source (E_BACKEND (object));
	if (source && ews_backend->priv->source_changed_id) {
		g_signal_handler_disconnect (source, ews_backend->priv->source_changed_id);
		ews_backend->priv->source_changed_id = 0;
	}

	g_hash_table_remove_all (ews_backend->priv->folders);

	g_mutex_lock (&ews_backend->priv->connection_lock);
	g_clear_object (&ews_backend->priv->connection);
	g_mutex_unlock (&ews_backend->priv->connection_lock);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ews_backend_parent_class)->dispose (object);
}

static void
ews_backend_finalize (GObject *object)
{
	EEwsBackend *ews_backend = E_EWS_BACKEND (object);

	g_free (ews_backend->priv->deleted_items_folder_id);
	g_hash_table_destroy (ews_backend->priv->folders);
	g_mutex_clear (&ews_backend->priv->folders_lock);
	g_free (ews_backend->priv->oal_selected);
	g_free (ews_backend->priv->sync_state);
	g_mutex_clear (&ews_backend->priv->sync_state_lock);
	g_mutex_clear (&ews_backend->priv->connection_lock);
	e_named_parameters_free (ews_backend->priv->credentials);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ews_backend_parent_class)->finalize (object);
}

static void
ews_backend_constructed (GObject *object)
{
	EBackend *backend;
	ESource *source;
	ESourceAuthentication *auth_extension;
	CamelEwsSettings *settings;
	const gchar *extension_name;
	gchar *host = NULL;
	guint16 port = 0;
	gboolean changed = FALSE;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_ews_backend_parent_class)->constructed (object);

	backend = E_BACKEND (object);
	source = e_backend_get_source (backend);

	/* XXX Wondering if we ought to delay this until after folders
	 *     are initially populated, just to remove the possibility
	 *     of weird races with clients trying to create folders. */
	e_server_side_source_set_remote_creatable (
		E_SERVER_SIDE_SOURCE (source), TRUE);

	/* Setup the Authentication extension so
	 * Camel can determine host reachability. */
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	if (e_backend_get_destination_address (backend, &host, &port)) {
		e_source_authentication_set_host (auth_extension, host);
		e_source_authentication_set_port (auth_extension, port);
	}

	g_free (host);

	settings = ews_backend_get_settings (E_EWS_BACKEND (backend));

	/* NTLM is a fallback, for any unknown value, but ESoupSession requires it
	   explicitly set, to use it for authentication, thus make sure it is set */
	if (camel_ews_settings_get_auth_mechanism (settings) == EWS_AUTH_TYPE_NTLM &&
	    g_strcmp0 (e_source_authentication_get_method (auth_extension), "NTLM") != 0) {
		e_source_authentication_set_method (auth_extension, "NTLM");
		changed = TRUE;
	}

	/* Reset the connectable, it steals data from Authentication extension,
	   where is written incorrect address */
	e_backend_set_connectable (backend, NULL);

	/* Eventually unset temporary SSL trust, but only once, when the process started.
	   It might bee too often anywhere lease (like in the authenticate callback) */
	if (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
		ESourceWebdav *webdav_extension;

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		e_source_webdav_unset_temporary_ssl_trust (webdav_extension);
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
		ESourceCollection *collection_extension;

		collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
		e_source_collection_set_allow_sources_rename (collection_extension, TRUE);
	}

	/* Make sure the auth method change is saved, maybe with temporary SSL trust unset too */
	if (changed)
		e_source_write (source, NULL, NULL, NULL);
}

static void
ews_backend_claim_old_resources (ECollectionBackend *backend)
{
	ESourceRegistryServer *registry;
	GList *old_resources, *iter;

	g_return_if_fail (E_IS_COLLECTION_BACKEND (backend));

	registry = e_collection_backend_ref_server (backend);
	old_resources = e_collection_backend_claim_all_resources (backend);

	for (iter = old_resources; iter; iter = g_list_next (iter)) {
		ESource *source = iter->data;

		e_source_registry_server_add_source (registry, source);
	}

	g_list_free_full (old_resources, g_object_unref);
	g_clear_object (&registry);
}

static void
ews_backend_folders_synced_cb (GObject *source,
			       GAsyncResult *result,
			       gpointer user_data)
{
	EEwsBackend *ews_backend;

	g_return_if_fail (E_IS_EWS_BACKEND (source));

	ews_backend = E_EWS_BACKEND (source);

	if (!e_ews_backend_sync_folders_finish (ews_backend, result, NULL))
		ews_backend_claim_old_resources (E_COLLECTION_BACKEND (ews_backend));

	e_collection_backend_thaw_populate (E_COLLECTION_BACKEND (ews_backend));
}

static void
ews_backend_populate (ECollectionBackend *collection_backend)
{
	ESource *source;
	EEwsBackend *ews_backend = E_EWS_BACKEND (collection_backend);
	EBackend *backend = E_BACKEND (ews_backend);

	source = e_backend_get_source (backend);

	ews_backend->priv->need_update_folders = TRUE;

	if (!ews_backend->priv->source_changed_id) {
		ews_backend->priv->source_changed_id = g_signal_connect (
			source, "changed",
			G_CALLBACK (ews_backend_source_changed_cb), ews_backend);
	}

	/* only claim old sources, when the account is disabled, thus
	   those sources can be auto-enabled when the account is enabled */
	if (!e_collection_backend_get_part_enabled (collection_backend, E_COLLECTION_BACKEND_PART_ANY)) {
		ews_backend_claim_old_resources (collection_backend);
		return;
	}

	if (!e_collection_backend_freeze_populate (collection_backend)) {
		e_collection_backend_thaw_populate (collection_backend);
		return;
	}

	ews_backend_claim_old_resources (collection_backend);
	ews_backend_add_gal_source (ews_backend);
	ews_backend_maybe_add_m365_source (ews_backend);

	if (e_backend_get_online (backend)) {
		CamelEwsSettings *ews_settings;

		ews_settings = ews_backend_get_settings (ews_backend);

		if (e_ews_connection_utils_get_without_password (ews_settings)) {
			e_backend_schedule_authenticate (backend, NULL);
		} else {
			e_backend_credentials_required_sync (backend,
				E_SOURCE_CREDENTIALS_REASON_REQUIRED, NULL, 0, NULL,
				NULL, NULL);
		}
	}

	e_collection_backend_thaw_populate (collection_backend);
}

static gchar *
ews_backend_dup_resource_id (ECollectionBackend *backend,
                             ESource *child_source)
{
	ESourceEwsFolder *extension;
	const gchar *extension_name;

	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *auth_extension;

		auth_extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION);

		if (g_strcmp0 (e_source_authentication_get_method (auth_extension), "Microsoft365") == 0)
			return g_strdup (EWS_HELPER_M365_RESOURCE_ID);
	}

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	extension = e_source_get_extension (child_source, extension_name);

	return e_source_ews_folder_dup_id (extension);
}

static void
ews_backend_child_added (ECollectionBackend *backend,
                         ESource *child_source)
{
	ESource *collection_source;
	const gchar *extension_name;

	collection_source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceAuthentication *auth_child_extension;

		auth_child_extension = e_source_get_extension (child_source, extension_name);

		if (g_strcmp0 (e_source_authentication_get_method (auth_child_extension), "Microsoft365") != 0) {
			ESourceAuthentication *auth_collection_extension;

			auth_collection_extension = e_source_get_extension (collection_source, extension_name);

			e_binding_bind_property (
				auth_collection_extension, "host",
				auth_child_extension, "host",
				G_BINDING_SYNC_CREATE);

			e_binding_bind_property (
				auth_collection_extension, "user",
				auth_child_extension, "user",
				G_BINDING_SYNC_CREATE);

			e_binding_bind_property (
				auth_collection_extension, "method",
				auth_child_extension, "method",
				G_BINDING_SYNC_CREATE);
		}
	}

	/* We track EWS folders in a hash table by folder ID. */
	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceEwsFolder *extension;
		gchar *folder_id;

		extension = e_source_get_extension (
			child_source, extension_name);
		folder_id = e_source_ews_folder_dup_id (extension);
		if (folder_id != NULL) {
			ews_backend_folders_insert (
				E_EWS_BACKEND (backend),
				folder_id, child_source);
		}
	}

	/* Chain up to parent's child_added() method. */
	E_COLLECTION_BACKEND_CLASS (e_ews_backend_parent_class)->
		child_added (backend, child_source);
}

static void
ews_backend_child_removed (ECollectionBackend *backend,
                           ESource *child_source)
{
	const gchar *extension_name;

	/* We track EWS folders in a hash table by folder ID. */
	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceEwsFolder *extension;
		const gchar *folder_id;

		extension = e_source_get_extension (
			child_source, extension_name);
		folder_id = e_source_ews_folder_get_id (extension);
		if (folder_id != NULL)
			ews_backend_folders_remove (
				E_EWS_BACKEND (backend), folder_id);
	}

	/* Chain up to parent's child_removed() method. */
	E_COLLECTION_BACKEND_CLASS (e_ews_backend_parent_class)->
		child_removed (backend, child_source);
}

static gboolean
ews_backend_create_resource_sync (ECollectionBackend *backend,
                                  ESource *source,
                                  GCancellable *cancellable,
                                  GError **error)
{
	EEwsConnection *connection = NULL;
	EwsFolderId *out_folder_id = NULL;
	EEwsFolderType folder_type = E_EWS_FOLDER_TYPE_UNKNOWN;
	const gchar *extension_name;
	const gchar *parent_folder_id = NULL;
	gchar *folder_name;
	gboolean success = FALSE;

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	if (e_source_has_extension (source, extension_name)) {
		ESourceEwsFolder *extension;

		/* foreign and public folders are just added */
		extension = e_source_get_extension (source, extension_name);
		if (e_source_ews_folder_get_foreign (extension) ||
		    e_source_ews_folder_get_public (extension))
			success = TRUE;
	}

	if (!success) {
		connection = e_ews_backend_ref_connection_sync (E_EWS_BACKEND (backend), NULL, NULL, NULL, cancellable, error);
		if (connection == NULL)
			return FALSE;

		extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_EWS_FOLDER_TYPE_CONTACTS;
			parent_folder_id = "contacts";
		}

		extension_name = E_SOURCE_EXTENSION_CALENDAR;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_EWS_FOLDER_TYPE_CALENDAR;
			parent_folder_id = "calendar";
		}

		extension_name = E_SOURCE_EXTENSION_TASK_LIST;
		if (e_source_has_extension (source, extension_name)) {
			folder_type = E_EWS_FOLDER_TYPE_TASKS;
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

		success = e_ews_connection_create_folder_sync (
			connection, EWS_PRIORITY_MEDIUM,
			parent_folder_id, TRUE,
			folder_name, folder_type,
			&out_folder_id, cancellable, error);

		/* Sanity check */
		g_warn_if_fail (
			(success && out_folder_id != NULL) ||
			(!success && out_folder_id == NULL));

		if (out_folder_id != NULL) {
			ESourceEwsFolder *extension;

			extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
			extension = e_source_get_extension (source, extension_name);
			e_source_ews_folder_set_id (
				extension, out_folder_id->id);
			e_source_ews_folder_set_change_key (
				extension, out_folder_id->change_key);
			e_source_ews_folder_set_name (extension, folder_name);

			e_ews_folder_id_free (out_folder_id);
		}

		g_free (folder_name);
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
}

static gboolean
ews_backend_delete_resource_sync (ECollectionBackend *backend,
                                  ESource *source,
                                  GCancellable *cancellable,
                                  GError **error)
{
	EEwsConnection *connection;
	ESourceEwsFolder *extension;
	const gchar *extension_name;
	gboolean success = FALSE;

	connection = e_ews_backend_ref_connection_sync (E_EWS_BACKEND (backend), NULL, NULL, NULL, cancellable, error);
	if (connection == NULL)
		return FALSE;

	extension_name = E_SOURCE_EXTENSION_EWS_FOLDER;
	if (!e_source_has_extension (source, extension_name)) {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_INVALID_ARGUMENT,
			_("Data source “%s” does not represent "
			"an Exchange Web Services folder"),
			e_source_get_display_name (source));
		goto exit;
	}
	extension = e_source_get_extension (source, extension_name);

	if (e_source_ews_folder_get_foreign (extension) ||
	    e_source_ews_folder_get_public (extension)) {
		/* do not delete foreign or public folders,
		 * just remove them from local store */
		success = TRUE;
	} else {
		gchar *folder_id;

		folder_id = e_source_ews_folder_dup_id (extension);

		success = e_ews_connection_delete_folder_sync (
			connection, EWS_PRIORITY_MEDIUM, folder_id,
			FALSE, "HardDelete", cancellable, error);

		g_free (folder_id);
	}

	if (success)
		success = e_source_remove_sync (source, cancellable, error);

exit:
	g_object_unref (connection);

	return success;
}

static gboolean
ews_backend_get_destination_address (EBackend *backend,
				     gchar **host,
				     guint16 *port)
{
	CamelEwsSettings *ews_settings;
	GUri *uri;
	gchar *host_url;
	gboolean result = FALSE;

	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	ews_settings = ews_backend_get_settings (E_EWS_BACKEND (backend));
	g_return_val_if_fail (ews_settings != NULL, FALSE);

	host_url = camel_ews_settings_dup_hosturl (ews_settings);
	if (!host_url) {
		ESource *source;
		ESourceAuthentication *auth_extension;

		source = e_backend_get_source (backend);
		auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

		*host = e_source_authentication_dup_host (auth_extension);
		*port = e_source_authentication_get_port (auth_extension);

		return *host && **host;
	}

	uri = g_uri_parse (host_url, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
	if (uri) {
		*host = g_strdup (g_uri_get_host (uri));
		*port = g_uri_get_port (uri) > 0 ? g_uri_get_port (uri) : 0;

		result = *host && **host;
		if (!result) {
			g_free (*host);
			*host = NULL;
		}

		g_uri_unref (uri);
	}

	g_free (host_url);

	return result;
}

static ESourceAuthenticationResult
ews_backend_authenticate_sync (EBackend *backend,
			       const ENamedParameters *credentials,
			       gchar **out_certificate_pem,
			       GTlsCertificateFlags *out_certificate_errors,
			       GCancellable *cancellable,
			       GError **error)
{
	EEwsBackend *ews_backend;
	EEwsConnection *connection;
	CamelEwsSettings *ews_settings;
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ERROR;
	gboolean in_sync_folders = FALSE;

	g_return_val_if_fail (E_IS_EWS_BACKEND (backend), E_SOURCE_AUTHENTICATION_ERROR);

	ews_backend = E_EWS_BACKEND (backend);
	ews_settings = ews_backend_get_settings (ews_backend);
	g_return_val_if_fail (ews_settings != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	e_collection_backend_freeze_populate (E_COLLECTION_BACKEND (backend));

	g_mutex_lock (&ews_backend->priv->connection_lock);
	g_clear_object (&ews_backend->priv->connection);
	e_named_parameters_free (ews_backend->priv->credentials);
	ews_backend->priv->credentials = e_named_parameters_new_clone (credentials);
	g_mutex_unlock (&ews_backend->priv->connection_lock);

	connection = e_ews_backend_ref_connection_sync (ews_backend, &result, out_certificate_pem, out_certificate_errors, cancellable, error);
	g_clear_object (&connection);

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		e_collection_backend_authenticate_children (E_COLLECTION_BACKEND (backend), credentials);

		e_ews_backend_sync_folders (ews_backend, NULL, ews_backend_folders_synced_cb, NULL);

		in_sync_folders = TRUE;
	} else if (e_ews_connection_utils_get_without_password (ews_settings) &&
		   result == E_SOURCE_AUTHENTICATION_REJECTED &&
		   !e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_PASSWORD)) {
		e_ews_connection_utils_force_off_ntlm_auth_check ();
		result = E_SOURCE_AUTHENTICATION_REQUIRED;
	}

	if (!in_sync_folders)
		e_collection_backend_thaw_populate (E_COLLECTION_BACKEND (backend));

	return result;
}

static void
e_ews_backend_class_init (EEwsBackendClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECollectionBackendClass *collection_backend_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = ews_backend_dispose;
	object_class->finalize = ews_backend_finalize;
	object_class->constructed = ews_backend_constructed;

	collection_backend_class = E_COLLECTION_BACKEND_CLASS (class);
	collection_backend_class->populate = ews_backend_populate;
	collection_backend_class->dup_resource_id = ews_backend_dup_resource_id;
	collection_backend_class->child_added = ews_backend_child_added;
	collection_backend_class->child_removed = ews_backend_child_removed;
	collection_backend_class->create_resource_sync = ews_backend_create_resource_sync;
	collection_backend_class->delete_resource_sync = ews_backend_delete_resource_sync;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->get_destination_address = ews_backend_get_destination_address;
	backend_class->authenticate_sync = ews_backend_authenticate_sync;

	/* This generates an ESourceCamel subtype for CamelEwsSettings. */
	e_source_camel_generate_subtype ("ews", CAMEL_TYPE_EWS_SETTINGS);
}

static void
e_ews_backend_class_finalize (EEwsBackendClass *class)
{
}

static void
e_ews_backend_init (EEwsBackend *backend)
{
	backend->priv = e_ews_backend_get_instance_private (backend);

	backend->priv->folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	g_mutex_init (&backend->priv->folders_lock);
	g_mutex_init (&backend->priv->sync_state_lock);
	g_mutex_init (&backend->priv->connection_lock);
}

void
e_ews_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_backend_register_type (type_module);
}

static void
ews_backend_ref_connection_thread (GSimpleAsyncResult *simple,
                                   GObject *object,
                                   GCancellable *cancellable)
{
	EEwsConnection *connection;
	GError *error = NULL;

	connection = e_ews_backend_ref_connection_sync (E_EWS_BACKEND (object), NULL, NULL, NULL, cancellable, &error);

	/* Sanity check. */
	g_return_if_fail (
		((connection != NULL) && (error == NULL)) ||
		((connection == NULL) && (error != NULL)));

	if (connection != NULL)
		g_simple_async_result_set_op_res_gpointer (
			simple, connection, g_object_unref);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

EEwsConnection *
e_ews_backend_ref_connection_sync (EEwsBackend *backend,
				   ESourceAuthenticationResult *result,
				   gchar **out_certificate_pem,
				   GTlsCertificateFlags *out_certificate_errors,
                                   GCancellable *cancellable,
                                   GError **error)
{
	EEwsConnection *connection = NULL;
	ESourceAuthenticationResult local_result;
	ESource *source;
	CamelEwsSettings *settings;
	gchar *hosturl;
	gboolean success;

	g_return_val_if_fail (E_IS_EWS_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->connection_lock);
	if (backend->priv->connection != NULL)
		connection = g_object_ref (backend->priv->connection);
	g_mutex_unlock (&backend->priv->connection_lock);

	/* If we already have an authenticated
	 * connection object, just return that. */
	if (connection != NULL || !backend->priv->credentials)
		return connection;

	source = e_backend_get_source (E_BACKEND (backend));
	settings = ews_backend_get_settings (backend);
	hosturl = camel_ews_settings_dup_hosturl (settings);
	if (!hosturl) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Host URL not set");

		if (result)
			*result = E_SOURCE_AUTHENTICATION_ERROR;

		return NULL;
	}

	connection = e_ews_connection_new_full (source, hosturl, settings, FALSE);
	g_free (hosturl);

	e_binding_bind_property (
		backend, "proxy-resolver",
		connection, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	local_result = e_ews_connection_try_credentials_sync (connection, backend->priv->credentials, NULL,
		out_certificate_pem, out_certificate_errors, cancellable, error);
	if (result)
		*result = local_result;

	success = local_result == E_SOURCE_AUTHENTICATION_ACCEPTED;

	if (success) {
		g_mutex_lock (&backend->priv->connection_lock);
		if (backend->priv->connection != NULL)
			g_object_unref (backend->priv->connection);
		backend->priv->connection = g_object_ref (connection);
		g_mutex_unlock (&backend->priv->connection_lock);
	} else {
		g_object_unref (connection);
		connection = NULL;
	}

	return connection;
}

void
e_ews_backend_ref_connection (EEwsBackend *backend,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_EWS_BACKEND (backend));

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback,
		user_data, e_ews_backend_ref_connection);

	g_simple_async_result_run_in_thread (
		simple, ews_backend_ref_connection_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

EEwsConnection *
e_ews_backend_ref_connection_finish (EEwsBackend *backend,
                                     GAsyncResult *result,
                                     GError **error)
{
	GSimpleAsyncResult *simple;
	EEwsConnection *connection;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_ews_backend_ref_connection), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	connection = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (E_IS_EWS_CONNECTION (connection), NULL);

	return g_object_ref (connection);
}

static void
ews_backend_sync_folders_thread (GSimpleAsyncResult *simple,
                                 GObject *object,
                                 GCancellable *cancellable)
{
	GError *error = NULL;

	e_ews_backend_sync_folders_sync (
		E_EWS_BACKEND (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
ews_backend_delete_each_source_cb (gpointer data,
				   gpointer user_data)
{
	ESource *source = data;
	ECollectionBackend *backend = user_data;

	e_collection_backend_delete_resource_sync (backend, source, NULL, NULL);
}

static void
ews_backend_forget_all_sources (EEwsBackend *backend)
{
	GList *sources;

	g_return_if_fail (E_IS_EWS_BACKEND (backend));

	sources = e_collection_backend_list_calendar_sources (E_COLLECTION_BACKEND (backend));
	g_list_foreach (sources, ews_backend_delete_each_source_cb, backend);
	g_list_free_full (sources, g_object_unref);

	sources = e_collection_backend_list_contacts_sources (E_COLLECTION_BACKEND (backend));
	g_list_foreach (sources, ews_backend_delete_each_source_cb, backend);
	g_list_free_full (sources, g_object_unref);

	sources = e_collection_backend_list_mail_sources (E_COLLECTION_BACKEND (backend));
	g_list_foreach (sources, ews_backend_delete_each_source_cb, backend);
	g_list_free_full (sources, g_object_unref);
}

static void
ews_backend_fill_known_sources (EEwsBackend *backend,
				GHashTable *known_sources) /* gchar *folder_id ~> ESource * */
{
	ECollectionBackend *collection_backend;
	CamelEwsSettings *settings;
	GList *sources, *link;
	gchar *gal_source_uid;

	g_return_if_fail (E_IS_EWS_BACKEND (backend));
	g_return_if_fail (known_sources != NULL);

	settings = ews_backend_get_settings (backend);
	gal_source_uid = camel_ews_settings_dup_gal_uid (settings);

	collection_backend = E_COLLECTION_BACKEND (backend);

	sources = e_collection_backend_list_calendar_sources (collection_backend);
	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER)) {
			ESourceEwsFolder *extension;
			gchar *folder_id;

			extension = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);
			folder_id = e_source_ews_folder_dup_id (extension);
			if (folder_id)
				g_hash_table_insert (known_sources, folder_id, g_object_ref (source));
		}
	}
	g_list_free_full (sources, g_object_unref);

	sources = e_collection_backend_list_contacts_sources (collection_backend);
	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;

		/* Do not include GAL in known sources, there is always at least one */
		if ((!gal_source_uid || g_strcmp0 (gal_source_uid, e_source_get_uid (source)) != 0) &&
		    e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER)) {
			ESourceEwsFolder *extension;
			gchar *folder_id;

			extension = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);
			folder_id = e_source_ews_folder_dup_id (extension);
			if (folder_id)
				g_hash_table_insert (known_sources, folder_id, g_object_ref (source));
		}
	}
	g_list_free_full (sources, g_object_unref);

	g_free (gal_source_uid);
}

gboolean
e_ews_backend_sync_folders_sync (EEwsBackend *backend,
                                 GCancellable *cancellable,
                                 GError **error)
{
	EEwsConnection *connection;
	GSList *folders_created = NULL;
	GSList *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder = FALSE;
	gchar *old_sync_state, *new_sync_state = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_EWS_BACKEND (backend), FALSE);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		SyncFoldersClosure *closure;

		/* This takes ownership of the folder lists. */
		closure = g_slice_new0 (SyncFoldersClosure);
		closure->backend = g_object_ref (backend);
		closure->old_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

		/* Process the results from an idle callback. */
		g_idle_add_full (
			G_PRIORITY_DEFAULT_IDLE,
			ews_backend_sync_folders_idle_cb, closure,
			(GDestroyNotify) sync_folders_closure_free);

		return TRUE;
	}

	connection = e_ews_backend_ref_connection_sync (backend, NULL, NULL, NULL, cancellable, error);

	if (connection == NULL) {
		backend->priv->need_update_folders = TRUE;
		return FALSE;
	}

	backend->priv->need_update_folders = FALSE;

	g_mutex_lock (&backend->priv->sync_state_lock);
	old_sync_state = g_strdup (backend->priv->sync_state);

	if (!backend->priv->deleted_items_folder_id) {
		EwsFolderId fid;
		GSList in_lst, *folders = NULL;

		memset (&fid, 0, sizeof (EwsFolderId));
		fid.id = (gchar *) "deleteditems";
		fid.is_distinguished_id = TRUE;

		memset (&in_lst, 0, sizeof (GSList));
		in_lst.data = &fid;

		g_mutex_unlock (&backend->priv->sync_state_lock);

		if (e_ews_connection_get_folder_sync (connection, EWS_PRIORITY_MEDIUM, "IdOnly", NULL, &in_lst, &folders, cancellable, NULL)) {
			EEwsFolder *deleteditems = folders ? folders->data : NULL;

			g_mutex_lock (&backend->priv->sync_state_lock);

			if (deleteditems) {
				const EwsFolderId *id = e_ews_folder_get_id (deleteditems);

				if (id && id->id && *id->id) {
					/* In case multiple threads fight on the authenticate at the same
					   time and one might eventually set the value before the lock
					   was re-acquired. */
					g_free (backend->priv->deleted_items_folder_id);
					backend->priv->deleted_items_folder_id = g_strdup (id->id);
				}
			}

			g_slist_free_full (folders, g_object_unref);
		} else {
			g_mutex_lock (&backend->priv->sync_state_lock);
		}
	}

	g_mutex_unlock (&backend->priv->sync_state_lock);

	success = e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, old_sync_state,
		&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	if (old_sync_state && g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
		g_clear_error (&local_error);

		g_mutex_lock (&backend->priv->sync_state_lock);
		g_free (backend->priv->sync_state);
		backend->priv->sync_state = NULL;
		g_mutex_unlock (&backend->priv->sync_state_lock);

		ews_backend_forget_all_sources (backend);

		success = e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, NULL,
			&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
			cancellable, &local_error);
	} else if (local_error) {
		g_propagate_error (error, local_error);
		local_error = NULL;
	}

	g_free (old_sync_state);
	old_sync_state = NULL;

	if (success) {
		SyncFoldersClosure *closure;
		gboolean had_sync_state;

		/* This takes ownership of the folder lists. */
		closure = g_slice_new0 (SyncFoldersClosure);
		closure->backend = g_object_ref (backend);
		closure->old_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		closure->folders_created = folders_created;
		closure->folders_deleted = folders_deleted;
		closure->folders_updated = folders_updated;

		g_mutex_lock (&backend->priv->sync_state_lock);
		had_sync_state = backend->priv->sync_state && *(backend->priv->sync_state);

		g_free (backend->priv->sync_state);
		backend->priv->sync_state = g_strdup (new_sync_state);
		g_mutex_unlock (&backend->priv->sync_state_lock);

		if (!had_sync_state)
			ews_backend_fill_known_sources (backend, closure->old_sources);

		/* Process the results from an idle callback. */
		g_idle_add_full (
			G_PRIORITY_DEFAULT_IDLE,
			ews_backend_sync_folders_idle_cb, closure,
			(GDestroyNotify) sync_folders_closure_free);

	} else {
		/* Make sure we're not leaking anything. */
		g_warn_if_fail (folders_created == NULL);
		g_warn_if_fail (folders_updated == NULL);
		g_warn_if_fail (folders_deleted == NULL);

		backend->priv->need_update_folders = TRUE;
	}

	g_free (new_sync_state);

	g_object_unref (connection);

	return success;
}

void
e_ews_backend_sync_folders (EEwsBackend *backend,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (E_IS_EWS_BACKEND (backend));

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback,
		user_data, e_ews_backend_sync_folders);

	g_simple_async_result_run_in_thread (
		simple, ews_backend_sync_folders_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

gboolean
e_ews_backend_sync_folders_finish (EEwsBackend *backend,
                                   GAsyncResult *result,
                                   GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		e_ews_backend_sync_folders), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

