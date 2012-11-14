/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-store.c : class for an ews store */

/*
 *  Authors:
 *  Chenthill Palanisamy <pchenthill@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-item-change.h"
#include "server/e-ews-message.h"

#include "camel-ews-folder.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define d(x) x
#define CURSOR_ITEM_LIMIT 100

#define CAMEL_EWS_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_STORE, CamelEwsStorePrivate))

#define FINFO_REFRESH_INTERVAL 60

struct _CamelEwsStorePrivate {
	time_t last_refresh_time;
	GMutex get_finfo_lock;
	EEwsConnection *connection;
	GMutex connection_lock;
};

static gboolean	ews_store_construct	(CamelService *service, CamelSession *session,
					 CamelProvider *provider, GError **error);

static void camel_ews_store_initable_init (GInitableIface *interface);
static void camel_ews_subscribable_init (CamelSubscribableInterface *interface);
static GInitableIface *parent_initable_interface;

G_DEFINE_TYPE_WITH_CODE (
	CamelEwsStore, camel_ews_store, CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE, camel_ews_store_initable_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_SUBSCRIBABLE, camel_ews_subscribable_init))

static void
ews_migrate_to_user_cache_dir (CamelService *service)
{
	const gchar *user_data_dir, *user_cache_dir;

	g_return_if_fail (service != NULL);
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	user_data_dir = camel_service_get_user_data_dir (service);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	g_return_if_fail (user_data_dir != NULL);
	g_return_if_fail (user_cache_dir != NULL);

	/* migrate only if the source directory exists and the destination doesn't */
	if (g_file_test (user_data_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR) &&
	    !g_file_test (user_cache_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		gchar *parent_dir;

		parent_dir = g_path_get_dirname (user_cache_dir);
		g_mkdir_with_parents (parent_dir, S_IRWXU);
		g_free (parent_dir);

		if (g_rename (user_data_dir, user_cache_dir) == -1) {
			g_debug ("%s: Failed to migrate '%s' to '%s': %s", G_STRFUNC, user_data_dir, user_cache_dir, g_strerror (errno));
		} else {
			gchar *old_summary_file = g_build_filename (user_cache_dir, "folder-tree-v2", NULL);

			if (old_summary_file && g_file_test (old_summary_file, G_FILE_TEST_EXISTS)) {
				gchar *new_summary_file = g_build_filename (user_cache_dir, "folder-tree", NULL);

				if (new_summary_file && g_rename (old_summary_file, new_summary_file) == -1)
					g_debug ("%s: Failed to migrate '%s' to '%s': %s", G_STRFUNC, old_summary_file, new_summary_file, g_strerror (errno));

				g_free (new_summary_file);
			}

			g_free (old_summary_file);
		}
	}
}

static gboolean
ews_store_initable_init (GInitable *initable,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	gboolean ret;

	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);
	session = camel_service_get_session (service);

	store->flags |= CAMEL_STORE_USE_CACHE_DIR;
	ews_migrate_to_user_cache_dir (service);

	/* Chain up to parent interface's init() method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	ret = ews_store_construct (service, session, NULL, error);

	/* Add transport here ? */

	return ret;
}

static void
camel_ews_store_initable_init (GInitableIface *interface)
{
	parent_initable_interface = g_type_interface_peek_parent (interface);

	interface->init = ews_store_initable_init;
}

static gboolean
ews_store_construct (CamelService *service,
                     CamelSession *session,
                     CamelProvider *provider,
                     GError **error)
{
	CamelEwsStore *ews_store;
	gchar *summary_file, *session_storage_path;

	ews_store = (CamelEwsStore *) service;

	/* Disable virtual trash and junk folders. Exchange has real
	 * folders for that */
	((CamelStore *) ews_store)->flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
	((CamelStore *) ews_store)->flags |= CAMEL_STORE_REAL_JUNK_FOLDER;

	/*storage path*/
	session_storage_path = g_strdup (camel_service_get_user_cache_dir (service));
	if (!session_storage_path) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Session has no storage path"));
		return FALSE;
	}
	ews_store->storage_path = session_storage_path;

	/* Note. update account-listener plugin if filename is changed here, as it would remove the summary
	 * by forming the path itself */
	g_mkdir_with_parents (ews_store->storage_path, 0700);
	summary_file = g_build_filename (ews_store->storage_path, "folder-tree", NULL);
	ews_store->summary = camel_ews_store_summary_new (summary_file);
	camel_ews_store_summary_load (ews_store->summary, NULL);

	g_free (summary_file);
	return TRUE;
}

void
camel_ews_store_ensure_virtual_folders (CamelEwsStore *ews_store)
{
	gboolean needs_foreign = FALSE, has_foreign = FALSE;
	GSList *folders, *iter;
	GHashTable *children_count;
	GHashTableIter tab_iter;
	gpointer key, value;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	folders = camel_ews_store_summary_get_folders (ews_store->summary, NULL);
	if (!folders)
		return;

	children_count = g_hash_table_new (g_str_hash, g_str_equal);

	for (iter = folders; iter; iter = iter->next) {
		const gchar *fid = iter->data;
		GError *error = NULL;

		if (!fid)
			continue;

		if (g_str_has_prefix (fid, "ForeignMailbox::") &&
		    !g_hash_table_contains (children_count, fid))
			g_hash_table_insert (children_count, (gpointer) fid, GINT_TO_POINTER (0));

		if (!has_foreign && g_str_equal (fid, EWS_FOREIGN_FOLDER_ROOT_ID))
			has_foreign = TRUE;
		else if (camel_ews_store_summary_get_foreign (ews_store->summary, fid, &error) && !error) {
			gchar *pfid;

			needs_foreign = TRUE;

			pfid = camel_ews_store_summary_get_parent_folder_id (ews_store->summary, fid, NULL);
			if (pfid && g_str_has_prefix (pfid, "ForeignMailbox::")) {
				gint count = GPOINTER_TO_INT (g_hash_table_lookup (children_count, pfid));

				g_hash_table_insert (children_count, (gpointer) pfid, GINT_TO_POINTER (count + 1));
			}
		}

		g_clear_error (&error);
	}

	g_hash_table_iter_init (&tab_iter, children_count);
	while (g_hash_table_iter_next (&tab_iter, &key, &value)) {
		gint count = GPOINTER_TO_INT (value);

		if (!count) {
			CamelFolderInfo *fi;

			fi = camel_ews_utils_build_folder_info (ews_store, key);
			camel_ews_store_summary_remove_folder (ews_store->summary, key, NULL);

			camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
			camel_store_folder_deleted (CAMEL_STORE (ews_store), fi);
			camel_folder_info_free (fi);
		}
	}

	g_hash_table_destroy (children_count);

	if (needs_foreign && !has_foreign) {
		gchar *use_name = NULL;
		gchar *tmp_fid;
		gint counter = 0;

		tmp_fid = camel_ews_store_summary_get_folder_id_from_name (
			ews_store->summary, EWS_FOREIGN_FOLDER_ROOT_DISPLAY_NAME);
		while (tmp_fid) {
			counter++;

			g_free (tmp_fid);
			g_free (use_name);

			/* Translators: This composes a "Foreign Folders" folder name for case when
			 * user has such in his store already. The %s is replaced with "Foreign Folders",
			 * the %d with counter, thus it composes name like "Foreign Folders_1"
			*/
			use_name = g_strdup_printf (
				C_("ForeignFolders", "%s_%d"),
				EWS_FOREIGN_FOLDER_ROOT_DISPLAY_NAME, counter);

			tmp_fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, use_name);
		}

		camel_ews_store_summary_new_folder (
			ews_store->summary,
			EWS_FOREIGN_FOLDER_ROOT_ID, NULL, NULL,
			use_name ? use_name : EWS_FOREIGN_FOLDER_ROOT_DISPLAY_NAME,
			E_EWS_FOLDER_TYPE_MAILBOX,
			CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOSELECT,
			0, FALSE);

		g_free (use_name);
	} else if (has_foreign && !needs_foreign) {
		CamelFolderInfo *fi;

		fi = camel_ews_utils_build_folder_info (ews_store, EWS_FOREIGN_FOLDER_ROOT_ID);
		camel_ews_store_summary_remove_folder (ews_store->summary, EWS_FOREIGN_FOLDER_ROOT_ID, NULL);

		camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
		camel_store_folder_deleted (CAMEL_STORE (ews_store), fi);
		camel_folder_info_free (fi);
	}

	camel_ews_store_summary_rebuild_hashes (ews_store->summary);
	camel_ews_store_summary_save (ews_store->summary, NULL);

	g_slist_free_full (folders, g_free);
}

void
camel_ews_store_ensure_unique_path (CamelEwsStore *ews_store,
                                    gchar **ppath)
{
	gboolean done;
	guint counter = 0;
	gchar *base_path = NULL;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));
	g_return_if_fail (ews_store->summary != NULL);
	g_return_if_fail (ppath != NULL);
	g_return_if_fail (*ppath != NULL);

	done = FALSE;
	while (!done) {
		gchar *fid;

		done = TRUE;

		fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, *ppath);
		if (fid) {
			g_free (fid);

			done = FALSE;
			counter++;
			if (!counter) {
				g_debug ("%s: Counter overflow", G_STRFUNC);
				break;
			}

			if (!base_path)
				base_path = *ppath;
			else
				g_free (*ppath);

			*ppath = g_strdup_printf ("%s_%u", base_path, counter);
		}
	}

	g_free (base_path);
}

static void
ews_update_folder_hierarchy (CamelEwsStore *ews_store,
                             gchar *sync_state,
                             gboolean includes_last_folder,
                             GSList *folders_created,
                             GSList *folders_deleted,
                             GSList *folders_updated)
{
	ews_utils_sync_folders (ews_store, folders_created, folders_deleted, folders_updated);
	camel_ews_store_ensure_virtual_folders (ews_store);

	camel_ews_store_summary_store_string_val (ews_store->summary, "sync_state", sync_state);
	camel_ews_store_summary_save (ews_store->summary, NULL);

	g_slist_foreach (folders_created, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_updated, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_deleted, (GFunc) g_free, NULL);
	g_slist_free (folders_created);
	g_slist_free (folders_deleted);
	g_slist_free (folders_updated);
	g_free (sync_state);
}

static gboolean
ews_connect_sync (CamelService *service,
                  GCancellable *cancellable,
                  GError **error)
{
	EEwsConnection *connection;
	CamelEwsStore *ews_store;
	CamelSession *session;
	gboolean success;

	ews_store = CAMEL_EWS_STORE (service);
	session = camel_service_get_session (service);

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	connection = camel_ews_store_ref_connection (ews_store);
	if (connection != NULL) {
		g_object_unref (connection);
		return TRUE;
	}

	/* Try running an operation that requires authentication
	 * to make sure we have a valid password available. */
	success = camel_session_authenticate_sync (
		session, service, NULL, cancellable, error);

	if (success)
		camel_offline_store_set_online_sync (
			CAMEL_OFFLINE_STORE (ews_store),
			TRUE, cancellable, NULL);

	return success;
}

static gboolean
ews_disconnect_sync (CamelService *service,
                     gboolean clean,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelEwsStore *ews_store = (CamelEwsStore *) service;
	CamelServiceClass *service_class;

	g_mutex_lock (&ews_store->priv->connection_lock);

	/* TODO cancel all operations in the connection */
	if (ews_store->priv->connection != NULL) {
		CamelSettings *settings;

		/* FIXME This is somewhat broken, since the CamelSettings
		 *       instance returned here may not be the same instance
		 *       that we connected a signal handler to.  Need to keep
		 *       our own reference to that CamelSettings instance, or
		 *       better yet avoid connecting signal handlers to it in
		 *       the first place. */
		settings = camel_service_ref_settings (service);
		g_signal_handlers_disconnect_by_data (settings, service);
		g_object_unref (settings);

		e_ews_connection_set_password (
			ews_store->priv->connection, NULL);
		g_object_unref (ews_store->priv->connection);
		ews_store->priv->connection = NULL;
	}

	g_mutex_unlock (&ews_store->priv->connection_lock);

	service_class = CAMEL_SERVICE_CLASS (camel_ews_store_parent_class);
	return service_class->disconnect_sync (service, clean, cancellable, error);
}

typedef struct {
	const gchar *dist_folder_id;
	gint info_flags;
} SystemFolder;

static SystemFolder system_folder[] = {
	{"calendar", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_EVENTS},
	{"contacts", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_CONTACTS},
	{"deleteditems", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_TRASH},
	{"drafts", CAMEL_FOLDER_SYSTEM},
	{"inbox", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX},
	{"journal", CAMEL_FOLDER_SYSTEM | CAMEL_EWS_FOLDER_TYPE_JOURNAL},
	{"notes", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_MEMOS},
	{"outbox", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_OUTBOX},
	{"sentitems", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_SENT},
	{"tasks", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_TASKS},
	{"msgfolderroot", CAMEL_FOLDER_SYSTEM},
	{"root", CAMEL_FOLDER_SYSTEM},
	{"junkemail", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_JUNK},
	{"searchfolders", CAMEL_FOLDER_SYSTEM},
};

static void
ews_store_set_flags (CamelEwsStore *ews_store,
                     GSList *folders)
{
	GSList *temp = NULL;
	EEwsFolder *folder = NULL;
	const EwsFolderId *fid = NULL;
	gint n = 0;

	temp = folders;
	while (temp != NULL) {
		folder = (EEwsFolder *) temp->data;
		fid = e_ews_folder_get_id (folder);

		if (camel_ews_store_summary_has_folder (ews_store->summary, fid->id))
			camel_ews_store_summary_set_folder_flags (ews_store->summary, fid->id, system_folder[n].info_flags);

		temp = temp->next;
		n++;
	}
}

static void
ews_store_forget_all_folders (CamelEwsStore *ews_store)
{
	CamelStore *store;
	CamelSubscribable *subscribable;
	GSList *folders, *l;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	store = CAMEL_STORE (ews_store);
	subscribable = CAMEL_SUBSCRIBABLE (ews_store);
	folders = camel_ews_store_summary_get_folders (ews_store->summary, NULL);

	if (!folders)
		return;

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		CamelFolderInfo *fi;
		EEwsFolderType ftype;

		ftype = camel_ews_store_summary_get_folder_type (ews_store->summary, l->data, NULL);
		if (ftype != E_EWS_FOLDER_TYPE_MAILBOX)
			continue;

		fi = camel_ews_utils_build_folder_info (ews_store, l->data);
		camel_subscribable_folder_unsubscribed (subscribable, fi);
		camel_store_folder_deleted (store, fi);
		camel_folder_info_free (fi);
	}

	g_slist_free_full (folders, g_free);
}

static CamelAuthenticationResult
ews_authenticate_sync (CamelService *service,
                       const gchar *mechanism,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelAuthenticationResult result;
	CamelEwsStore *ews_store;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	EEwsConnection *connection;
	GSList *folders_created = NULL;
	GSList *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	GSList *folder_ids = NULL, *folders = NULL;
	gboolean includes_last_folder = FALSE;
	gboolean initial_setup = FALSE;
	const gchar *password;
	gchar *hosturl;
	gchar *old_sync_state = NULL, *new_sync_state = NULL;
	GError *local_error = NULL, *folder_err = NULL;
	gint n = 0;

	ews_store = CAMEL_EWS_STORE (service);

	password = camel_service_get_password (service);

	if (password == NULL) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Authentication password not available"));
		return CAMEL_AUTHENTICATION_ERROR;
	}

	settings = camel_service_ref_settings (service);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);

	connection = e_ews_connection_new (hosturl, ews_settings);
	e_ews_connection_set_password (connection, password);

	g_free (hosturl);

	g_object_unref (settings);

	/* XXX We need to run some operation that requires authentication
	 *     but does not change any server-side state, so we can check
	 *     the error status and determine if our password is valid.
	 *     David suggested e_ews_connection_sync_folder_hierarchy(),
	 *     since we have to do that eventually anyway. */

	/*use old sync_state from summary*/
	old_sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	if (!old_sync_state)
		initial_setup = TRUE;

	e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, old_sync_state,
		&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	g_free (old_sync_state);
	old_sync_state = NULL;

	if (!initial_setup && g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
		g_clear_error (&local_error);
		ews_store_forget_all_folders (ews_store);
		camel_ews_store_summary_store_string_val (ews_store->summary, "sync_state", "");
		camel_ews_store_summary_clear (ews_store->summary);

		initial_setup = TRUE;

		e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, NULL,
			&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
			cancellable, &local_error);
	}

	if (local_error == NULL) {
		g_mutex_lock (&ews_store->priv->connection_lock);
		if (ews_store->priv->connection != NULL)
			g_object_unref (ews_store->priv->connection);
		ews_store->priv->connection = g_object_ref (connection);
		g_mutex_unlock (&ews_store->priv->connection_lock);

		/* This consumes all allocated result data. */
		ews_update_folder_hierarchy (
			ews_store, new_sync_state, includes_last_folder,
			folders_created, folders_deleted, folders_updated);
	} else {
		g_mutex_lock (&ews_store->priv->connection_lock);
		if (ews_store->priv->connection != NULL) {
			g_object_unref (ews_store->priv->connection);
			ews_store->priv->connection = NULL;
		}
		g_mutex_unlock (&ews_store->priv->connection_lock);

		g_free (new_sync_state);

		/* Make sure we're not leaking anything. */
		g_warn_if_fail (folders_created == NULL);
		g_warn_if_fail (folders_updated == NULL);
		g_warn_if_fail (folders_deleted == NULL);
	}

	/*get folders using distinguished id by GetFolder operation and set system flags to folders, only for first time*/
	if (initial_setup && local_error == NULL) {
		while (n < G_N_ELEMENTS (system_folder)) {
			EwsFolderId *fid = NULL;

			fid = g_new0 (EwsFolderId, 1);
			fid->id = g_strdup (system_folder[n].dist_folder_id);
			fid->is_distinguished_id = TRUE;
			folder_ids = g_slist_append (folder_ids, fid);
			n++;
		}

		/* fetch system folders first using getfolder operation*/
		e_ews_connection_get_folder_sync (
			connection, EWS_PRIORITY_MEDIUM, "IdOnly",
			NULL, folder_ids, &folders,
			cancellable, &folder_err);

		if (g_slist_length (folders) && (g_slist_length (folders) != G_N_ELEMENTS (system_folder)))
			d (printf ("Error : not all folders are returned by getfolder operation"));
		else if (folder_err == NULL && folders != NULL)
			ews_store_set_flags (ews_store, folders);
		else if (folder_err) {
			/*report error and make sure we are not leaking anything*/
			g_warn_if_fail (folders == NULL);
		} else
			d (printf ("folders for respective distinguished ids don't exist"));

		g_slist_foreach (folders, (GFunc) g_object_unref, NULL);
		g_slist_foreach (folder_ids, (GFunc) e_ews_folder_id_free, NULL);
		g_slist_free (folders);
		g_slist_free (folder_ids);
		g_clear_error (&folder_err);
	}

	if (local_error == NULL) {
		result = CAMEL_AUTHENTICATION_ACCEPTED;
	} else if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED)) {
		g_clear_error (&local_error);
		result = CAMEL_AUTHENTICATION_REJECTED;
	} else {
		g_propagate_error (error, local_error);
		result = CAMEL_AUTHENTICATION_ERROR;
	}

	g_object_unref (connection);

	return result;
}

static  GList *
ews_store_query_auth_types_sync (CamelService *service,
                                 GCancellable *cancellable,
                                 GError **error)
{
	g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Query for authentication types is not supported"));

	return NULL;
}

static CamelFolderInfo * ews_create_folder_sync (CamelStore *store, const gchar *parent_name,const gchar *folder_name, GCancellable *cancellable, GError **error);

static CamelFolder *
ews_get_folder_sync (CamelStore *store,
                     const gchar *folder_name,
                     guint32 flags,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelEwsStore *ews_store;
	CamelFolder *folder = NULL;
	gchar *fid, *folder_dir;

	ews_store = (CamelEwsStore *) store;

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_name);

	/* We don't support CAMEL_STORE_FOLDER_EXCL. Nobody ever uses it */
	if (!fid && (flags & CAMEL_STORE_FOLDER_CREATE)) {
		CamelFolderInfo *fi;
		const gchar *parent, *top, *slash;
		gchar *copy = NULL;

		slash = strrchr (folder_name, '/');
		if (slash) {
			copy = g_strdup (folder_name);

			/* Split into parent path, and new name */
			copy[slash - folder_name] = 0;
			parent = copy;
			top = copy + (slash - folder_name) + 1;
		} else {
			parent = "";
			top = folder_name;
		}

		fi = ews_create_folder_sync (store, parent, top, cancellable, error);
		g_free (copy);

		if (!fi)
			return NULL;

		camel_folder_info_free (fi);
	} else if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_ERROR_GENERIC,
			_("No such folder: %s"), folder_name);
		return NULL;
	} else {
		/* We don't actually care what it is; only that it exists */
		g_free (fid);
	}

	folder_dir = g_build_filename (ews_store->storage_path, "folders", folder_name, NULL);
	folder = camel_ews_folder_new (store, folder_name, folder_dir, cancellable, error);

	g_free (folder_dir);

	return folder;
}

static CamelFolderInfo *
folder_info_from_store_summary (CamelEwsStore *store,
                                const gchar *top,
                                guint32 flags,
                                GError **error)
{
	CamelEwsStoreSummary *ews_summary;
	GSList *folders, *l;
	GPtrArray *folder_infos;
	CamelFolderInfo *root_fi = NULL;

	ews_summary = store->summary;
	folders = camel_ews_store_summary_get_folders (ews_summary, top);

	if (!folders)
		return NULL;

	folder_infos = g_ptr_array_new ();

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		CamelFolderInfo *fi;
		EEwsFolderType ftype;

		ftype = camel_ews_store_summary_get_folder_type (ews_summary, l->data, NULL);
		if (ftype != E_EWS_FOLDER_TYPE_MAILBOX)
			continue;

		fi = camel_ews_utils_build_folder_info (store, l->data);
		g_ptr_array_add (folder_infos, fi);
	}

	root_fi = camel_folder_info_build (folder_infos, top, '/', TRUE);

	g_ptr_array_free (folder_infos, TRUE);
	g_slist_foreach (folders, (GFunc) g_free, NULL);
	g_slist_free (folders);

	return root_fi;
}

static void
ews_folder_hierarchy_ready_cb (GObject *obj,
                               GAsyncResult *res,
                               gpointer user_data)
{
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	CamelEwsStore *ews_store = (CamelEwsStore *) user_data;
	CamelEwsStorePrivate *priv = ews_store->priv;
	EEwsConnection *cnc = (EEwsConnection *) obj;
	gchar *sync_state = NULL;
	gboolean includes_last_folder;
	GError *error = NULL;

	e_ews_connection_sync_folder_hierarchy_finish (
		cnc, res, &sync_state, &includes_last_folder,
		&folders_created, &folders_updated,
		&folders_deleted, &error);

	if (error != NULL) {
		g_warning ("Unable to fetch the folder hierarchy: %s :%d \n", error->message, error->code);

		camel_ews_store_maybe_disconnect (ews_store, error);

		g_mutex_lock (&priv->get_finfo_lock);
		ews_store->priv->last_refresh_time -= FINFO_REFRESH_INTERVAL;
		g_mutex_unlock (&priv->get_finfo_lock);
		goto exit;
	}
	g_mutex_lock (&priv->get_finfo_lock);
	ews_update_folder_hierarchy (
		ews_store, sync_state, includes_last_folder,
		folders_created, folders_deleted, folders_updated);

	ews_store->priv->last_refresh_time = time (NULL);
	g_mutex_unlock (&priv->get_finfo_lock);

exit:
	g_object_unref (ews_store);
	g_clear_error (&error);
}

static gboolean
ews_refresh_finfo (CamelEwsStore *ews_store)
{
	EEwsConnection *connection;
	gchar *sync_state;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store)))
		return FALSE;

	if (!camel_service_connect_sync ((CamelService *) ews_store, NULL, NULL))
		return FALSE;

	sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);

	connection = camel_ews_store_ref_connection (ews_store);

	e_ews_connection_sync_folder_hierarchy (
		connection, EWS_PRIORITY_MEDIUM,
		sync_state, NULL, ews_folder_hierarchy_ready_cb,
		g_object_ref (ews_store));

	g_object_unref (connection);

	g_free (sync_state);

	return TRUE;
}

static CamelFolderInfo *
ews_get_folder_info_sync (CamelStore *store,
                          const gchar *top,
                          guint32 flags,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	CamelFolderInfo *fi = NULL;
	EEwsConnection *connection;
	gchar *old_sync_state, *new_sync_state = NULL;
	gboolean initial_setup = FALSE;
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder;
	gboolean success;
	GError *local_error = NULL;

	if ((flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) != 0) {
		g_set_error_literal (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot list folders available for subscription of Exchange Web Services account, "
			"use 'Subscribe to folder of other user' context menu option above the account node "
			"in the folder tree instead."));
		return NULL;
	}

	ews_store = (CamelEwsStore *) store;
	priv = ews_store->priv;

	g_mutex_lock (&priv->get_finfo_lock);
	if (!(camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))
	      && camel_service_connect_sync ((CamelService *) store, cancellable, error))) {
		camel_ews_store_ensure_virtual_folders (ews_store);
		g_mutex_unlock (&priv->get_finfo_lock);
		goto offline;
	}

	old_sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	if (!old_sync_state)
		initial_setup = TRUE;

	if (!initial_setup && (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) != 0) {
		time_t now = time (NULL);

		g_free (old_sync_state);
		if (now - priv->last_refresh_time > FINFO_REFRESH_INTERVAL && ews_refresh_finfo (ews_store))
			ews_store->priv->last_refresh_time = time (NULL);

		g_mutex_unlock (&priv->get_finfo_lock);
		goto offline;
	}

	connection = camel_ews_store_ref_connection (ews_store);

	success = e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, old_sync_state,
		&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	g_free (old_sync_state);
	old_sync_state = NULL;

	if (!initial_setup && g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
		g_clear_error (&local_error);
		ews_store_forget_all_folders (ews_store);
		camel_ews_store_summary_store_string_val (ews_store->summary, "sync_state", "");
		camel_ews_store_summary_clear (ews_store->summary);

		initial_setup = TRUE;

		success = e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, NULL,
			&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
			cancellable, &local_error);
	}

	g_object_unref (connection);

	if (!success) {
		if (local_error)
			g_warning (
				"Unable to fetch the folder hierarchy: %s :%d \n",
				local_error->message, local_error->code);
		else
			g_warning ("Unable to fetch the folder hierarchy.\n");

		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);

		g_mutex_unlock (&priv->get_finfo_lock);
		return NULL;
	}
	ews_update_folder_hierarchy (
		ews_store, new_sync_state, includes_last_folder,
		folders_created, folders_deleted, folders_updated);
	g_mutex_unlock (&priv->get_finfo_lock);

offline:
	fi = folder_info_from_store_summary ( (CamelEwsStore *) store, top, flags, error);
	return fi;
}

static CamelFolderInfo *
ews_create_folder_sync (CamelStore *store,
                        const gchar *parent_name,
                        const gchar *folder_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	gchar *fid = NULL;
	gchar *full_name;
	EwsFolderId *folder_id;
	EEwsConnection *connection;
	CamelFolderInfo *fi = NULL;
	gboolean success;
	GError *local_error = NULL;

	if (parent_name && *parent_name)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary, full_name);
	if (fid) {
		g_free (fid);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create folder '%s', folder already exists"),
			full_name);
		g_free (full_name);
		return NULL;
	}

	g_free (full_name);

	/* Get Parent folder ID */
	if (parent_name && parent_name[0]) {
		fid = camel_ews_store_summary_get_folder_id_from_name (
			ews_summary, parent_name);
		if (!fid) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Parent folder %s does not exist"),
				parent_name);
			return NULL;
		}

		if (g_str_equal (fid, EWS_FOREIGN_FOLDER_ROOT_ID)) {
			g_free (fid);

			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot create folder under '%s', it is used for folders of other users only"),
				parent_name);
			return NULL;
		}
	}

	if (!camel_ews_store_connected (ews_store, cancellable, error)) {
		if (fid) g_free (fid);
		return NULL;
	}

	connection = camel_ews_store_ref_connection (ews_store);

	success = e_ews_connection_create_folder_sync (
		connection,
		EWS_PRIORITY_MEDIUM, fid,
		FALSE, folder_name, E_EWS_FOLDER_TYPE_MAILBOX,
		&folder_id, cancellable, &local_error);

	g_object_unref (connection);

	if (!success) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		g_free (fid);
		return NULL;
	}

	/* Translate & store returned folder id */
	if (fid)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	camel_ews_store_summary_new_folder (
		ews_summary, folder_id->id,
		fid, folder_id->change_key,
		folder_name,
		E_EWS_FOLDER_TYPE_MAILBOX,
		0, 0, FALSE);
	fi = camel_ews_utils_build_folder_info (ews_store, folder_id->id);
	e_ews_folder_id_free (folder_id);

	camel_store_folder_created (store, fi);
	camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);

	g_free (full_name);
	g_free (fid);
	return fi;
}

static gboolean
ews_delete_folder_sync (CamelStore *store,
                        const gchar *folder_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	gchar *fid;
	CamelFolderInfo *fi = NULL;
	gboolean success;
	GError *local_error = NULL;

	fid = camel_ews_store_summary_get_folder_id_from_name (
		ews_summary, folder_name);
	if (!fid) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Folder does not exist"));
		return FALSE;
	}

	if (g_str_equal (fid, EWS_FOREIGN_FOLDER_ROOT_ID)) {
		g_free (fid);

		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot remove folder '%s', it is used for folders of other users only"),
			folder_name);
		return FALSE;
	}

	if (!camel_ews_store_connected (ews_store, cancellable, error)) {
		g_free (fid);
		return FALSE;
	}

	if (camel_ews_store_summary_get_foreign (ews_store->summary, fid, NULL)) {
		/* do not delete foreign folders,
		 * just remove them from local store */
		success = TRUE;
	} else {
		EEwsConnection *connection;

		connection = camel_ews_store_ref_connection (ews_store);

		success = e_ews_connection_delete_folder_sync (
			connection,
			EWS_PRIORITY_MEDIUM,
			fid, FALSE, "HardDelete",
			cancellable, &local_error);

		g_object_unref (connection);
	}

	if (!success) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		g_free (fid);
		return FALSE;
	}

	fi = camel_ews_utils_build_folder_info (ews_store, fid);
	camel_ews_store_summary_remove_folder (ews_summary, fid, error);

	camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
	camel_store_folder_deleted (store, fi);
	camel_folder_info_free (fi);

	g_free (fid);

	camel_ews_store_ensure_virtual_folders (ews_store);
	camel_ews_store_summary_save (ews_store->summary, NULL);

	return TRUE;
}

struct _rename_cb_data {
	const gchar *display_name;
	const gchar *change_key;
	const gchar *folder_id;
};

static void
rename_folder_cb (ESoapMessage *msg,
                  gpointer user_data)
{
	struct _rename_cb_data *rename_data = user_data;

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_FOLDER,
		rename_data->folder_id, rename_data->change_key, 0);
	e_soap_message_start_element (msg, "SetFolderField", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (
		msg, "FieldURI", NULL, NULL,
		"FieldURI", "folder:DisplayName");

	e_soap_message_start_element (msg, "Folder", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", NULL, rename_data->display_name);
	e_soap_message_end_element (msg); /* Folder */

	e_soap_message_end_element (msg); /* SetFolderField */

	e_ews_message_end_item_change (msg);
}

static gboolean
ews_rename_folder_sync (CamelStore *store,
                        const gchar *old_name,
                        const gchar *new_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	EEwsConnection *connection;
	const gchar *old_slash, *new_slash;
	gchar *fid;
	gchar *changekey;
	gboolean res = FALSE;
	GError *local_error = NULL;

	if (!strcmp (old_name, new_name))
		return TRUE;

	if (!camel_ews_store_connected (ews_store, cancellable, error)) {
		return FALSE;
	}

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary, old_name);
	if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Folder %s does not exist"), old_name);
		return FALSE;
	}

	changekey = camel_ews_store_summary_get_change_key (ews_summary, fid, error);
	if (!changekey) {
		g_free (fid);
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No change key record for folder %s"), fid);
		return FALSE;
	}

	connection = camel_ews_store_ref_connection (ews_store);

	old_slash = g_strrstr (old_name, "/");
	new_slash = g_strrstr (new_name, "/");

	if (old_slash)
		old_slash++;
	else
		old_slash = old_name;

	if (new_slash)
		new_slash++;
	else
		new_slash = new_name;

	if (strcmp (old_slash, new_slash)) {
		gint parent_len = old_slash - old_name;
		struct _rename_cb_data *rename_data;

		/* Folder basename changed (i.e. UpdateFolder needed).
		 * Therefore, we can only do it if the folder hasn't also
		 * been moved from one parent folder to another.
 *
		 * Strictly speaking, we could probably handle this, even
		 * if there are name collisions. We could UpdateFolder to
		 * a new temporary name that doesn't exist in either the
		 * old or new parent folders, then MoveFolder, then issue
		 * another UpdateFolder to the name we actually wanted.
		 * But since the Evolution UI doesn't seem to let us
		 * make both changes at the same time anyway, we'll just
		 * bail out for now; we can deal with it later if we need
		 * to.
		*/
		if (new_slash - new_name != parent_len ||
		    strncmp (old_name, new_name, parent_len)) {
			g_set_error (
				error, CAMEL_STORE_ERROR,
				CAMEL_STORE_ERROR_INVALID,
				_("Cannot both rename and move a folder at the same time"));
			g_free (changekey);
			goto out;
		}

		rename_data = g_new0 (struct _rename_cb_data, 1);
		rename_data->display_name = new_slash;
		rename_data->folder_id = fid;
		rename_data->change_key = changekey;

		res = e_ews_connection_update_folder_sync (
			connection, EWS_PRIORITY_MEDIUM,
			rename_folder_cb, rename_data,
			cancellable, &local_error);

		if (!res) {
			g_free (rename_data);
			goto out;
		}
		g_free (rename_data);
		camel_ews_store_summary_set_folder_name (ews_summary, fid, new_slash);
	} else {
		gchar *pfid = NULL;
		gchar *parent_name;

		/* If we are not moving to the root folder, work out the ItemId of
		 * the new parent folder */
		if (new_slash != new_name) {
			parent_name = g_strndup (new_name, new_slash - new_name - 1);
			pfid = camel_ews_store_summary_get_folder_id_from_name (
				ews_summary, parent_name);
			g_free (parent_name);
			if (!pfid) {
				g_set_error (
					error, CAMEL_STORE_ERROR,
					CAMEL_STORE_ERROR_NO_FOLDER,
					_("Cannot find folder ID for parent folder %s"),
					parent_name);
				goto out;
			}
		}

		res = e_ews_connection_move_folder_sync (
			connection, EWS_PRIORITY_MEDIUM,
			pfid, fid, cancellable, &local_error);

		if (!res) {
			g_free (pfid);
			goto out;
		}
		camel_ews_store_summary_set_parent_folder_id (ews_summary, fid, pfid);
		g_free (pfid);
	}

	res = TRUE;
 out:
	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
	}

	g_object_unref (connection);

	g_free (changekey);
	g_free (fid);
	return res;
}

gchar *
ews_get_name (CamelService *service,
              gboolean brief)
{
	CamelSettings *settings;
	gchar *name;
	gchar *host;
	gchar *user;

	settings = camel_service_ref_settings (service);

	user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (settings));
	host = camel_ews_utils_get_host_name (settings);

	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf (
			_("Exchange server %s"), host);
	else
		name = g_strdup_printf (
			_("Exchange service for %s on %s"), user, host);

	g_free (host);
	g_free (user);

	return name;
}

EEwsConnection *
camel_ews_store_ref_connection (CamelEwsStore *ews_store)
{
	EEwsConnection *connection = NULL;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (ews_store), NULL);

	g_mutex_lock (&ews_store->priv->connection_lock);

	if (ews_store->priv->connection != NULL)
		connection = g_object_ref (ews_store->priv->connection);

	g_mutex_unlock (&ews_store->priv->connection_lock);

	return connection;
}

static CamelFolder *
ews_get_trash_folder_sync (CamelStore *store,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelEwsStore *ews_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (store), NULL);

	ews_store = CAMEL_EWS_STORE (store);
	folder_id = camel_ews_store_summary_get_folder_id_from_folder_type (
		ews_store->summary, CAMEL_FOLDER_TYPE_TRASH);

	if (folder_id == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Could not locate Trash folder"));
		return NULL;
	}

	folder_name = camel_ews_store_summary_get_folder_full_name (
		ews_store->summary, folder_id, NULL);

	folder = ews_get_folder_sync (
		store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);

	return folder;
}

static CamelFolder *
ews_get_junk_folder_sync (CamelStore *store,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelEwsStore *ews_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (store), NULL);

	ews_store = CAMEL_EWS_STORE (store);
	folder_id = camel_ews_store_summary_get_folder_id_from_folder_type (
		ews_store->summary, CAMEL_FOLDER_TYPE_JUNK);

	if (folder_id == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Could not locate Junk folder"));
		return NULL;
	}

	folder_name = camel_ews_store_summary_get_folder_full_name (
		ews_store->summary, folder_id, NULL);

	folder = ews_get_folder_sync (
		store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);

	return folder;
}

static gboolean
ews_can_refresh_folder (CamelStore *store,
                        CamelFolderInfo *info,
                        GError **error)
{
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	gboolean check_all;

	/* Skip unselectable folders from automatic refresh */
	if (info && (info->flags & CAMEL_FOLDER_NOSELECT) != 0)
		return FALSE;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	check_all = camel_ews_settings_get_check_all (ews_settings);

	g_object_unref (settings);

	if (check_all)
		return TRUE;

	/* Delegate decision to parent class */
	return CAMEL_STORE_CLASS (camel_ews_store_parent_class)->
		can_refresh_folder (store, info, error);
}

static gboolean
ews_store_folder_is_subscribed (CamelSubscribable *subscribable,
                                const gchar *folder_name)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (subscribable);
	gchar *fid;
	gboolean truth = FALSE;
	GError *error = NULL;

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_name);
	if (!fid)
		return FALSE;

	if (camel_ews_store_summary_get_foreign (ews_store->summary, fid, &error) && !error) {
		truth = TRUE;
	}

	g_clear_error (&error);
	g_free (fid);

	return truth;
}

static gboolean
ews_store_subscribe_folder_sync (CamelSubscribable *subscribable,
                                 const gchar *folder_name,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (subscribable);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Cannot subscribe EWS folders in offline mode"));
		return FALSE;
	}

	/* it does nothing currently */

	return TRUE;
}

static gboolean
ews_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                   const gchar *folder_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (subscribable);
	gboolean res = TRUE;
	gchar *fid;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Cannot unsubscribe EWS folders in offline mode"));
		return FALSE;
	}

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_name);
	if (!fid) {
		/* no such folder in the cache, might be unsubscribed already */
		return TRUE;
	}

	if (!camel_ews_store_summary_get_foreign (ews_store->summary, fid, NULL)) {
		/* nothing to do for regular folders */
		res = TRUE;
	} else {
		CamelFolderInfo *fi;

		fi = camel_ews_utils_build_folder_info (ews_store, fid);
		camel_ews_store_summary_remove_folder (ews_store->summary, fid, error);

		camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
		camel_store_folder_deleted (CAMEL_STORE (ews_store), fi);
		camel_folder_info_free (fi);

		camel_ews_store_ensure_virtual_folders (ews_store);
		camel_ews_store_summary_save (ews_store->summary, NULL);
	}

	g_free (fid);

	return res;
}

gboolean
camel_ews_store_connected (CamelEwsStore *ews_store,
                           GCancellable *cancellable,
                           GError **error)
{

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (!camel_service_connect_sync ((CamelService *) ews_store, cancellable, error))
		return FALSE;

	return TRUE;
}

void
camel_ews_store_maybe_disconnect (CamelEwsStore *store,
                                  const GError *error)
{
	CamelService *service;

	g_return_if_fail (store != NULL);

	if (!error)
		return;

	service = CAMEL_SERVICE (store);

	if (camel_service_get_connection_status (service) != CAMEL_SERVICE_CONNECTED)
		return;

	if (g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NORESPONSE) ||
	    g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED))
		camel_service_disconnect_sync (service, FALSE, NULL, NULL);
}

static void
ews_store_dispose (GObject *object)
{
	CamelEwsStore *ews_store;

	ews_store = CAMEL_EWS_STORE (object);

	if (ews_store->summary != NULL) {
		camel_ews_store_summary_save (ews_store->summary, NULL);
		g_object_unref (ews_store->summary);
		ews_store->summary = NULL;
	}

	if (ews_store->priv->connection != NULL) {
		g_object_unref (ews_store->priv->connection);
		ews_store->priv->connection = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_ews_store_parent_class)->dispose (object);
}

static void
ews_store_finalize (GObject *object)
{
	CamelEwsStore *ews_store;

	ews_store = CAMEL_EWS_STORE (object);

	g_free (ews_store->storage_path);
	g_mutex_clear (&ews_store->priv->get_finfo_lock);
	g_mutex_clear (&ews_store->priv->connection_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_store_parent_class)->finalize (object);
}

static void
camel_ews_store_class_init (CamelEwsStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	g_type_class_add_private (class, sizeof (CamelEwsStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = ews_store_dispose;
	object_class->finalize = ews_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_EWS_SETTINGS;
	service_class->query_auth_types_sync = ews_store_query_auth_types_sync;
	service_class->get_name = ews_get_name;
	service_class->connect_sync = ews_connect_sync;
	service_class->disconnect_sync = ews_disconnect_sync;
	service_class->authenticate_sync = ews_authenticate_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder_sync = ews_get_folder_sync;
	store_class->create_folder_sync = ews_create_folder_sync;
	store_class->delete_folder_sync = ews_delete_folder_sync;
	store_class->rename_folder_sync = ews_rename_folder_sync;
	store_class->get_folder_info_sync = ews_get_folder_info_sync;
	store_class->free_folder_info = camel_store_free_folder_info_full;

	store_class->get_trash_folder_sync = ews_get_trash_folder_sync;
	store_class->get_junk_folder_sync = ews_get_junk_folder_sync;
	store_class->can_refresh_folder = ews_can_refresh_folder;
}

static void
camel_ews_subscribable_init (CamelSubscribableInterface *interface)
{
	interface->folder_is_subscribed = ews_store_folder_is_subscribed;
	interface->subscribe_folder_sync = ews_store_subscribe_folder_sync;
	interface->unsubscribe_folder_sync = ews_store_unsubscribe_folder_sync;
}

static void
camel_ews_store_init (CamelEwsStore *ews_store)
{
	ews_store->priv =
		CAMEL_EWS_STORE_GET_PRIVATE (ews_store);

	ews_store->priv->last_refresh_time = time (NULL) - (FINFO_REFRESH_INTERVAL + 10);
	g_mutex_init (&ews_store->priv->get_finfo_lock);
	g_mutex_init (&ews_store->priv->connection_lock);
}
