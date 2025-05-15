/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Chenthill Palanisamy <pchenthill@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "common/camel-ews-settings.h"
#include "common/e-ews-item-change.h"
#include "common/e-ews-request.h"
#include "common/e-ews-oof-settings.h"

#include "camel-ews-folder.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"
#include "camel-ews-enumtypes.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define d(x) x
#define CURSOR_ITEM_LIMIT 100

#define FINFO_REFRESH_INTERVAL 60

#define UPDATE_LOCK(x) (g_rec_mutex_lock(&(x)->priv->update_lock))
#define UPDATE_UNLOCK(x) (g_rec_mutex_unlock(&(x)->priv->update_lock))

struct _CamelEwsStorePrivate {
	time_t last_refresh_time;
	GMutex get_finfo_lock;
	EEwsConnection *connection;
	GMutex connection_lock;
	gboolean has_ooo_set;
	CamelEwsStoreOooAlertState ooo_alert_state;
	gint password_expires_in_days;

	gboolean listen_notifications;
	guint subscription_key;

	/* The subscription ID is not tight to the actual connection, it survives
	   disconnects, thus remember it and pass it back to the new connection,
	   thus it can eventually unsubscribe from it. */
	gchar *last_subscription_id;

	guint update_folder_id;
	guint update_folder_list_id;
	GCancellable *updates_cancellable;
	GSList *update_folder_names;
	GRecMutex update_lock;

	GSList *public_folders; /* EEwsFolder * objects */
};

static gboolean	ews_store_construct	(CamelService *service, CamelSession *session,
					 CamelProvider *provider, GError **error);

static void camel_ews_store_initable_init (GInitableIface *iface);
static void camel_ews_subscribable_init (CamelSubscribableInterface *iface);
static GInitableIface *parent_initable_interface;

static CamelFolderInfo *folder_info_from_store_summary (CamelEwsStore *store,
							const gchar *top,
							guint32 flags,
							GCancellable *cancellable,
							GError **error);

enum {
	PROP_0,
	PROP_HAS_OOO_SET,
	PROP_OOO_ALERT_STATE,
	PROP_CONNECTABLE,
	PROP_HOST_REACHABLE
};

G_DEFINE_TYPE_WITH_CODE (CamelEwsStore, camel_ews_store, CAMEL_TYPE_OFFLINE_STORE,
	G_ADD_PRIVATE (CamelEwsStore)
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, camel_ews_store_initable_init)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_NETWORK_SERVICE, NULL)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_SUBSCRIBABLE, camel_ews_subscribable_init))

static void
ews_store_set_property (GObject *object,
                        guint property_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_HAS_OOO_SET:
			camel_ews_store_set_has_ooo_set (
				CAMEL_EWS_STORE (object),
				g_value_get_boolean (value));
			return;
		case PROP_OOO_ALERT_STATE:
			camel_ews_store_set_ooo_alert_state (
				CAMEL_EWS_STORE (object),
				g_value_get_enum (value));
			return;
		case PROP_CONNECTABLE:
			camel_network_service_set_connectable (
				CAMEL_NETWORK_SERVICE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_store_get_property (GObject *object,
                        guint property_id,
                        GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_HAS_OOO_SET:
			g_value_set_boolean (
				value,
				camel_ews_store_get_has_ooo_set (
				CAMEL_EWS_STORE (object)));
			return;
		case PROP_OOO_ALERT_STATE:
			g_value_set_enum (
				value,
				camel_ews_store_get_ooo_alert_state (
				CAMEL_EWS_STORE (object)));
			return;
		case PROP_CONNECTABLE:
			g_value_take_object (
				value,
				camel_network_service_ref_connectable (
					CAMEL_NETWORK_SERVICE (object)));
			return;
		case PROP_HOST_REACHABLE:
			g_value_set_boolean (
				value,
				camel_network_service_get_host_reachable (
					CAMEL_NETWORK_SERVICE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

void
camel_ews_store_set_has_ooo_set (CamelEwsStore *ews_store,
                                 gboolean has_ooo_set)
{
	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	if ((ews_store->priv->has_ooo_set ? 1 : 0) == (has_ooo_set ? 1 : 0))
		return;

	ews_store->priv->has_ooo_set = has_ooo_set;
	g_object_notify (G_OBJECT (ews_store), "has-ooo-set");
}

gboolean
camel_ews_store_get_has_ooo_set (const CamelEwsStore *ews_store)
{
	g_return_val_if_fail (CAMEL_IS_EWS_STORE (ews_store), FALSE);

	return ews_store->priv->has_ooo_set;
}

void
camel_ews_store_set_ooo_alert_state (CamelEwsStore *ews_store,
                                     CamelEwsStoreOooAlertState state)
{
	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	if (ews_store->priv->ooo_alert_state == state)
		return;

	ews_store->priv->ooo_alert_state = state;
	g_object_notify (G_OBJECT (ews_store), "ooo-alert-state");
}

CamelEwsStoreOooAlertState
camel_ews_store_get_ooo_alert_state (const CamelEwsStore *ews_store)
{
	g_return_val_if_fail (
			CAMEL_IS_EWS_STORE (ews_store),
			CAMEL_EWS_STORE_OOO_ALERT_STATE_UNKNOWN);

	return ews_store->priv->ooo_alert_state;
}

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

	camel_store_set_flags (store, camel_store_get_flags (store) | CAMEL_STORE_USE_CACHE_DIR | CAMEL_STORE_SUPPORTS_INITIAL_SETUP);
	ews_migrate_to_user_cache_dir (service);

	camel_store_set_flags (store, camel_store_get_flags (store) | CAMEL_STORE_CAN_DELETE_FOLDERS_AT_ONCE);

	/* Chain up to parent interface's init() method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	session = camel_service_ref_session (service);

	ret = ews_store_construct (service, session, NULL, error);

	g_object_unref (session);

	/* Add transport here ? */

	return ret;
}

static void
camel_ews_store_initable_init (GInitableIface *iface)
{
	parent_initable_interface = g_type_interface_peek_parent (iface);

	iface->init = ews_store_initable_init;
}

static gboolean
ews_store_construct (CamelService *service,
                     CamelSession *session,
                     CamelProvider *provider,
                     GError **error)
{
	CamelEwsStore *ews_store;
	gchar *summary_file, *session_storage_path;
	guint32 store_flags;

	ews_store = (CamelEwsStore *) service;

	store_flags = camel_store_get_flags (CAMEL_STORE (ews_store));

	/* Disable virtual trash and junk folders. Exchange has real
	 * folders for that */
	store_flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
	store_flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	camel_store_set_flags (CAMEL_STORE (ews_store), store_flags);

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

/* returns NULL when it's safe to use the default "Public Folders" name; otherwise g_free() it */
static gchar *
ews_store_get_public_folders_name (CamelEwsStore *ews_store)
{
	gchar *use_name = NULL;
	gchar *tmp_fid;
	gint counter = 0;

	tmp_fid = camel_ews_store_summary_get_folder_id_from_name (
		ews_store->summary, EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME);
	while (tmp_fid) {
		counter++;

		g_free (tmp_fid);
		g_free (use_name);

		use_name = g_strdup_printf (
			/* Translators: This composes a "Public Folders" folder name for case when
			 * user has such in his store already. The %s is replaced with "Public Folders",
			 * the %d with counter, thus it composes name like "Public Folders_1"
			 */
			C_("PublicFolders", "%s_%d"),
			EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME, counter);

		tmp_fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, use_name);
	}

	return use_name;
}

static gboolean
ews_store_show_public_folders (CamelEwsStore *ews_store)
{
	CamelSettings *settings;
	gboolean show;

	settings = camel_service_ref_settings (CAMEL_SERVICE (ews_store));

	show = settings && camel_ews_settings_get_show_public_folders (CAMEL_EWS_SETTINGS (settings));

	g_clear_object (&settings);

	return show;
}

void
camel_ews_store_ensure_virtual_folders (CamelEwsStore *ews_store)
{
	gboolean needs_foreign = FALSE, has_foreign = FALSE;
	gboolean needs_public = FALSE, has_public = FALSE;
	gboolean show_public_folders;
	CamelFolderInfo *fi;
	GSList *folders, *iter;
	GHashTable *children_count;
	GHashTableIter tab_iter;
	gpointer key, value;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	folders = camel_ews_store_summary_get_folders (ews_store->summary, NULL, FALSE);
	if (!folders)
		return;

	show_public_folders = ews_store_show_public_folders (ews_store);
	children_count = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (iter = folders; iter; iter = iter->next) {
		const gchar *fid = iter->data;
		GError *error = NULL;

		if (!fid)
			continue;

		if (g_str_has_prefix (fid, "ForeignMailbox::") &&
		    !g_hash_table_contains (children_count, fid))
			g_hash_table_insert (children_count, g_strdup (fid), GINT_TO_POINTER (0));

		if (g_str_equal (fid, EWS_PUBLIC_FOLDER_ROOT_ID) &&
		    !g_hash_table_contains (children_count, fid))
			g_hash_table_insert (children_count, g_strdup (fid), GINT_TO_POINTER (0));

		if (!has_foreign && g_str_equal (fid, EWS_FOREIGN_FOLDER_ROOT_ID))
			has_foreign = TRUE;
		else if (camel_ews_store_summary_get_foreign (ews_store->summary, fid, &error) && !error) {
			gchar *pfid;

			needs_foreign = TRUE;

			pfid = camel_ews_store_summary_get_parent_folder_id (ews_store->summary, fid, NULL);
			if (pfid && g_str_has_prefix (pfid, "ForeignMailbox::")) {
				gint count = GPOINTER_TO_INT (g_hash_table_lookup (children_count, pfid));

				g_hash_table_insert (children_count, pfid, GINT_TO_POINTER (count + 1));
			} else {
				g_free (pfid);
			}
		}

		g_clear_error (&error);

		if (!has_public && g_str_equal (fid, EWS_PUBLIC_FOLDER_ROOT_ID))
			has_public = TRUE;
		else if (camel_ews_store_summary_get_public (ews_store->summary, fid, &error) && !error) {
			EEwsFolderType ftype;
			gchar *pfid;

			ftype = camel_ews_store_summary_get_folder_type (ews_store->summary, fid, &error);
			if (ftype == E_EWS_FOLDER_TYPE_MAILBOX && !error) {
				guint64 fflags;

				fflags = camel_ews_store_summary_get_folder_flags (ews_store->summary, fid, &error);
				if (show_public_folders || ((fflags & CAMEL_FOLDER_SUBSCRIBED) != 0 && !error)) {
					needs_public = TRUE;

					pfid = camel_ews_store_summary_get_parent_folder_id (ews_store->summary, fid, NULL);
					if (pfid && g_str_equal (pfid, EWS_PUBLIC_FOLDER_ROOT_ID)) {
						gint count = GPOINTER_TO_INT (g_hash_table_lookup (children_count, pfid));

						g_hash_table_insert (children_count, pfid, GINT_TO_POINTER (count + 1));
					} else {
						g_free (pfid);
					}
				}
			}
		}

		g_clear_error (&error);
	}

	needs_public = needs_public || show_public_folders;

	g_hash_table_iter_init (&tab_iter, children_count);
	while (g_hash_table_iter_next (&tab_iter, &key, &value)) {
		gint count = GPOINTER_TO_INT (value);

		if (!count) {
			if (needs_public && g_str_equal (key, EWS_PUBLIC_FOLDER_ROOT_ID))
				continue;

			if (has_foreign && g_str_equal (key, EWS_FOREIGN_FOLDER_ROOT_ID))
				has_foreign = FALSE;

			if (has_public && g_str_equal (key, EWS_PUBLIC_FOLDER_ROOT_ID))
				has_public = FALSE;

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

			use_name = g_strdup_printf (
				/* Translators: This composes a "Foreign Folders" folder name for case when
				 * user has such in his store already. The %s is replaced with "Foreign Folders",
				 * the %d with counter, thus it composes name like "Foreign Folders_1"
				 */
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
			0, FALSE, FALSE);

		g_free (use_name);

		fi = camel_ews_utils_build_folder_info (ews_store, EWS_FOREIGN_FOLDER_ROOT_ID);
		camel_store_folder_created (CAMEL_STORE (ews_store), fi);
		camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
		camel_folder_info_free (fi);
	} else if (has_foreign && !needs_foreign) {
		fi = camel_ews_utils_build_folder_info (ews_store, EWS_FOREIGN_FOLDER_ROOT_ID);
		camel_ews_store_summary_remove_folder (ews_store->summary, EWS_FOREIGN_FOLDER_ROOT_ID, NULL);

		camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
		camel_store_folder_deleted (CAMEL_STORE (ews_store), fi);
		camel_folder_info_free (fi);
	}

	if (needs_public && !has_public) {
		gchar *use_name;

		use_name = ews_store_get_public_folders_name (ews_store);

		camel_ews_store_summary_new_folder (
			ews_store->summary,
			EWS_PUBLIC_FOLDER_ROOT_ID, NULL, NULL,
			use_name ? use_name : EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME,
			E_EWS_FOLDER_TYPE_MAILBOX,
			CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOSELECT,
			0, FALSE, FALSE);

		g_free (use_name);

		fi = camel_ews_utils_build_folder_info (ews_store, EWS_PUBLIC_FOLDER_ROOT_ID);
		camel_store_folder_created (CAMEL_STORE (ews_store), fi);
		camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
		camel_folder_info_free (fi);
	} else if (has_public && !needs_public) {
		fi = camel_ews_utils_build_folder_info (ews_store, EWS_PUBLIC_FOLDER_ROOT_ID);
		camel_ews_store_summary_remove_folder (ews_store->summary, EWS_PUBLIC_FOLDER_ROOT_ID, NULL);

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
                             GSList *folders_updated,
			     GSList **created_folder_ids)
{
	ews_utils_sync_folders (ews_store, folders_created, folders_deleted, folders_updated, created_folder_ids);
	camel_ews_store_ensure_virtual_folders (ews_store);

	camel_ews_store_summary_store_string_val (ews_store->summary, "sync_state", sync_state);
	camel_ews_store_summary_save (ews_store->summary, NULL);

	g_slist_free_full (folders_created, g_object_unref);
	g_slist_free_full (folders_updated, g_object_unref);
	g_slist_free_full (folders_deleted, g_free);
	g_free (sync_state);
}

static void
ews_update_has_ooo_set (CamelSession *session,
			GCancellable *cancellable,
			gpointer user_data,
			GError **error)
{
	CamelEwsStore *ews_store = user_data;
	EEwsOofSettings *oof_settings;
	EEwsOofState oof_state;
	EEwsConnection *cnc;
	GError *local_error = NULL;

	cnc = camel_ews_store_ref_connection (ews_store);
	if (!cnc)
		return;

	camel_operation_push_message (cancellable, _("Checking “Out of Office” settings"));

	oof_settings = e_ews_oof_settings_new_sync (cnc, cancellable, &local_error);

	g_clear_object (&cnc);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		camel_operation_pop_message (cancellable);
		return;
	}

	oof_state = e_ews_oof_settings_get_state (oof_settings);
	switch (oof_state) {
		case E_EWS_OOF_STATE_ENABLED:
			camel_ews_store_set_has_ooo_set (ews_store, TRUE);
			break;
		case E_EWS_OOF_STATE_DISABLED:
		case E_EWS_OOF_STATE_SCHEDULED:
			camel_ews_store_set_has_ooo_set (ews_store, FALSE);
			break;
		default:
			break;
	}

	camel_operation_pop_message (cancellable);
	g_clear_object (&oof_settings);
}

static void
ews_exchange_server_categories_cb (CamelSession *session,
				   GCancellable *cancellable,
				   gpointer user_data,
				   GError **error)
{
	CamelEwsStore *ews_store = user_data;
	EEwsConnection *cnc;
	EwsFolderId fid = { 0 };
	gchar *properties = NULL;
	GError *local_error = NULL;

	cnc = camel_ews_store_ref_connection (ews_store);
	if (!cnc)
		return;

	fid.id = (gchar *) "calendar";
	fid.is_distinguished_id = TRUE;

	if (e_ews_connection_get_user_configuration_sync (cnc, G_PRIORITY_DEFAULT, &fid, "CategoryList",
		E_EWS_USER_CONFIGURATION_PROPERTIES_XMLDATA, &properties, cancellable, &local_error) && properties) {
		guchar *data;
		gsize data_len = 0;

		data = g_base64_decode (properties, &data_len);

		if (data && data_len > 0)
			camel_ews_utils_merge_category_list (ews_store, data, data_len);

		g_free (data);
	}

	g_clear_error (&local_error);
	g_clear_object (&cnc);
	g_free (properties);
}

struct ScheduleUpdateData
{
	GCancellable *cancellable;
	CamelEwsStore *ews_store;
	guint expected_id;
};

static void
free_schedule_update_data (gpointer ptr)
{
	struct ScheduleUpdateData *sud = ptr;

	if (sud == NULL)
		return;

	g_clear_object (&sud->cancellable);
	g_clear_object (&sud->ews_store);

	g_slice_free (struct ScheduleUpdateData, sud);
}

static GHashTable * /* gchar *id ~> gchar *change_key */
ews_store_get_existing_folders_in_path (CamelEwsStore *ews_store,
					const gchar *in_path)
{
	GHashTable *folders;
	GSList *stored_folders, *link;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (ews_store), NULL);
	g_return_val_if_fail (in_path != NULL, NULL);

	folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	stored_folders = camel_ews_store_summary_get_folders (ews_store->summary, in_path, TRUE);

	for (link = stored_folders; link; link = g_slist_next (link)) {
		g_hash_table_insert (folders, link->data,
			camel_ews_store_summary_get_change_key (ews_store->summary, link->data, NULL));
	}

	/* The ::data is used in 'folders', thus do not free it here */
	g_slist_free (stored_folders);

	return folders;
}

static gboolean
ews_store_sync_public_folders (CamelEwsStore *ews_store,
			       EEwsConnection *connection,
			       const gchar *top,
			       GSList **pfolders_created,
			       GSList **pfolders_updated,
			       GSList **pfolders_deleted,
			       GCancellable *cancellable,
			       GError **error)
{
	gboolean includes_last_folder = TRUE;
	GHashTable *existing_folders;
	GSList *folders = NULL;
	gchar *fid_str;
	EwsFolderId *folder_id;
	GError *local_error = NULL;

	g_return_val_if_fail (pfolders_created != NULL, FALSE);
	g_return_val_if_fail (pfolders_updated != NULL, FALSE);
	g_return_val_if_fail (pfolders_deleted != NULL, FALSE);

	/* Read only explicit 'Public Folders' hierarchy, and not recursively,
	   due to possibly deep/large Public Folders hierarchy */
	if (!top || !*top || !g_str_has_prefix (top, EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME))
		return TRUE;

	existing_folders = ews_store_get_existing_folders_in_path (ews_store, top);
	fid_str = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, top);

	/* This should never be removed in this function */
	g_hash_table_remove (existing_folders, EWS_PUBLIC_FOLDER_ROOT_ID);

	if (!fid_str || g_strcmp0 (fid_str, EWS_PUBLIC_FOLDER_ROOT_ID) == 0)
		folder_id = e_ews_folder_id_new ("publicfoldersroot", NULL, TRUE);
	else
		folder_id = e_ews_folder_id_new (fid_str, NULL, FALSE);

	while (e_ews_connection_find_folder_sync (connection, EWS_PRIORITY_MEDIUM, folder_id, &includes_last_folder, &folders,
		cancellable, &local_error) && !local_error &&
		!g_cancellable_is_cancelled (cancellable)) {
		GSList *fiter;

		if (!folders)
			break;

		for (fiter = folders; fiter != NULL; fiter = fiter->next) {
			EEwsFolder *folder = fiter->data;
			const EwsFolderId *fid;
			const gchar *existing_change_key;

			if (!folder || e_ews_folder_is_error (folder))
				continue;

			fid = e_ews_folder_get_id (folder);

			if (!fid)
				continue;

			if (!e_ews_folder_get_parent_id (folder)) {
				if (!folder_id->is_distinguished_id) {
					e_ews_folder_set_parent_id (folder,
						e_ews_folder_id_new (folder_id->id, folder_id->change_key, folder_id->is_distinguished_id));
				} else {
					e_ews_folder_set_parent_id (folder, e_ews_folder_id_new (EWS_PUBLIC_FOLDER_ROOT_ID, NULL, FALSE));
				}
			}

			e_ews_folder_set_public (folder, TRUE);

			existing_change_key = g_hash_table_lookup (existing_folders, fid->id);

			if (!existing_change_key) {
				*pfolders_created = g_slist_prepend (*pfolders_created, g_object_ref (folder));
			} else {
				/* Always claim the folder as updated, thus the summary updates also folder
				   flags with information about child folders */
				*pfolders_updated = g_slist_prepend (*pfolders_updated, g_object_ref (folder));
			}

			g_hash_table_remove (existing_folders, fid->id);
		}

		g_slist_free_full (folders, g_object_unref);

		if (includes_last_folder)
			break;
	}

	e_ews_folder_id_free (folder_id);
	g_free (fid_str);

	if (!local_error && g_hash_table_size (existing_folders) > 0) {
		GHashTableIter iter;
		gpointer key;

		g_hash_table_iter_init (&iter, existing_folders);

		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			const gchar *id = key;
			gchar *full_name;
			GSList *removed_folders;

			full_name = camel_ews_store_summary_get_folder_full_name (ews_store->summary, id, NULL);

			if (!full_name) {
				*pfolders_deleted = g_slist_prepend (*pfolders_deleted, g_strdup (id));
				continue;
			}

			removed_folders = camel_ews_store_summary_get_folders (ews_store->summary, full_name, FALSE);

			if (removed_folders)
				*pfolders_deleted = g_slist_concat (*pfolders_deleted, removed_folders);

			g_free (full_name);
		}
	}

	g_hash_table_destroy (existing_folders);

	if (local_error) {
		if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_FOLDERNOTFOUND) &&
		    !g_str_equal (top, EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME)) {
			GSList *removed_folders;

			removed_folders = camel_ews_store_summary_get_folders (ews_store->summary, top, FALSE);

			if (removed_folders)
				*pfolders_deleted = g_slist_concat (*pfolders_deleted, removed_folders);
		}

		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);

		return FALSE;
	}

	return TRUE;
}

static gpointer
camel_ews_folder_list_update_thread (gpointer user_data)
{
	struct ScheduleUpdateData *sud = user_data;
	CamelEwsStore *ews_store = sud->ews_store;
	EEwsConnection *cnc = NULL;
	GSList *created = NULL;
	GSList *updated = NULL;
	GSList *deleted = NULL;
	gchar *old_sync_state = NULL;
	gchar *new_sync_state;
	gboolean includes_last;
	GError *local_error = NULL;

	if (g_cancellable_is_cancelled (sud->cancellable))
		goto exit;

	cnc = camel_ews_store_ref_connection (ews_store);
	if (!cnc)
		goto exit;

	old_sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	if (!e_ews_connection_sync_folder_hierarchy_sync (
			cnc,
			EWS_PRIORITY_LOW,
			old_sync_state,
			&new_sync_state,
			&includes_last,
			&created,
			&updated,
			&deleted,
			sud->cancellable,
			&local_error))
		goto exit;

	if (g_cancellable_is_cancelled (sud->cancellable)) {
		g_slist_free_full (created, g_object_unref);
		g_slist_free_full (updated, g_object_unref);
		g_slist_free_full (deleted, g_free);
		g_free (new_sync_state);

		goto exit;
	}

	if (ews_store_show_public_folders (ews_store))
		ews_store_sync_public_folders (ews_store, cnc, EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME, &created, &updated, &deleted, sud->cancellable, NULL);

	if (created != NULL || updated != NULL || deleted != NULL) {
		ews_update_folder_hierarchy (
				ews_store,
				new_sync_state, /* freed in the function */
				includes_last,
				created, /* freed in the function */
				deleted, /* freed in the function */
				updated, /* freed in the function */
				NULL);
	} else {
		g_slist_free_full (created, g_object_unref);
		g_slist_free_full (updated, g_object_unref);
		g_slist_free_full (deleted, g_free);
		g_free (new_sync_state);
	}

 exit:
	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_clear_error (&local_error);

		g_mutex_lock (&ews_store->priv->get_finfo_lock);
		ews_store->priv->last_refresh_time -= FINFO_REFRESH_INTERVAL;
		g_mutex_unlock (&ews_store->priv->get_finfo_lock);
	} else {
		g_mutex_lock (&ews_store->priv->get_finfo_lock);
		ews_store->priv->last_refresh_time = time (NULL);
		g_mutex_unlock (&ews_store->priv->get_finfo_lock);
	}

	g_free (old_sync_state);
	g_clear_object (&cnc);
	free_schedule_update_data (sud);
	return NULL;
}

static gpointer
camel_ews_folder_update_thread (gpointer user_data)
{
	struct ScheduleUpdateData *sud = user_data;
	CamelEwsStore *ews_store = sud->ews_store;
	GSList *update_folder_names, *l;

	g_return_val_if_fail (sud != NULL, NULL);

	UPDATE_LOCK (ews_store);
	update_folder_names = ews_store->priv->update_folder_names;
	ews_store->priv->update_folder_names = NULL;
	UPDATE_UNLOCK (ews_store);

	for (l = update_folder_names; l != NULL && !g_cancellable_is_cancelled (sud->cancellable); l = l->next) {
		const gchar *folder_name = l->data;
		CamelFolder *folder;
		GError *error = NULL;

		folder = camel_store_get_folder_sync (CAMEL_STORE (ews_store), folder_name, 0, sud->cancellable, NULL);
		if (folder == NULL)
			continue;

		camel_folder_refresh_info_sync (folder, sud->cancellable, &error);
		g_object_unref (folder);

		if (error != NULL) {
			g_warning ("%s: %s\n", G_STRFUNC, error->message);
			g_clear_error (&error);
			break;
		}
	}

	g_slist_free_full (update_folder_names, g_free);
	update_folder_names = NULL;
	free_schedule_update_data (sud);

	return NULL;
}

static void
run_update_thread (CamelEwsStore *ews_store,
		   gboolean folder_list,
		   GCancellable *cancellable)
{
	GThread *thread;
	struct ScheduleUpdateData *sud;

	g_return_if_fail (ews_store != NULL);
	g_return_if_fail (cancellable != NULL);

	sud = g_slice_new0 (struct ScheduleUpdateData);
	sud->ews_store = g_object_ref (ews_store);
	sud->cancellable = g_object_ref (cancellable);

	thread = g_thread_new (
		NULL,
		folder_list ? camel_ews_folder_list_update_thread : camel_ews_folder_update_thread,
		sud);
	g_thread_unref (thread);
}

static gboolean
folder_update_cb (gpointer user_data)
{
	struct ScheduleUpdateData *sud = user_data;

	g_return_val_if_fail (sud != NULL, FALSE);

	if (g_cancellable_is_cancelled (sud->cancellable))
		return FALSE;

	g_return_val_if_fail (sud->ews_store != NULL, FALSE);
	g_return_val_if_fail (sud->ews_store->priv != NULL, FALSE);

	UPDATE_LOCK (sud->ews_store);
	if (sud->expected_id != sud->ews_store->priv->update_folder_id)
		goto exit;

	sud->ews_store->priv->update_folder_id = 0;

	if (!g_cancellable_is_cancelled (sud->cancellable))
		run_update_thread (sud->ews_store, FALSE, sud->cancellable);

exit:
	UPDATE_UNLOCK (sud->ews_store);
	return FALSE;
}

static void
get_folder_names_to_update (gpointer key,
			    gpointer value,
			    gpointer user_data)
{
	CamelEwsStore *ews_store = user_data;
	const gchar *folder_id = key;
	gchar *folder_name;

	folder_name = camel_ews_store_summary_get_folder_full_name (ews_store->summary, folder_id, NULL);
	if (folder_name != NULL)
		ews_store->priv->update_folder_names = g_slist_prepend (ews_store->priv->update_folder_names, folder_name);
}

static void
schedule_folder_update (CamelEwsStore *ews_store,
			GHashTable *folder_ids)
{
	struct ScheduleUpdateData *sud;
	CamelSettings *settings;

	g_return_if_fail (ews_store != NULL);
	g_return_if_fail (ews_store->priv != NULL);

	UPDATE_LOCK (ews_store);
	g_hash_table_foreach (folder_ids, get_folder_names_to_update, ews_store);

	if (ews_store->priv->update_folder_names == NULL)
		goto exit;

	sud = g_slice_new0 (struct ScheduleUpdateData);
	sud->ews_store = g_object_ref (ews_store);
	sud->cancellable = g_object_ref (ews_store->priv->updates_cancellable);

	if (ews_store->priv->update_folder_id > 0)
		g_source_remove (ews_store->priv->update_folder_id);

	settings = camel_service_ref_settings (CAMEL_SERVICE (ews_store));

	ews_store->priv->update_folder_id = e_named_timeout_add_seconds_full (
								G_PRIORITY_LOW,
								1,
								folder_update_cb,
								sud,
								free_schedule_update_data);
	sud->expected_id = ews_store->priv->update_folder_id;

	g_object_unref (settings);

exit:
	UPDATE_UNLOCK (ews_store);
}

static gboolean
folder_list_update_cb (gpointer user_data)
{
	struct ScheduleUpdateData *sud = user_data;

	g_return_val_if_fail (sud != NULL, FALSE);

	if (g_cancellable_is_cancelled (sud->cancellable))
		return FALSE;

	g_return_val_if_fail (sud->ews_store != NULL, FALSE);
	g_return_val_if_fail (sud->ews_store->priv != NULL, FALSE);

	UPDATE_LOCK (sud->ews_store);
	if (sud->expected_id != sud->ews_store->priv->update_folder_list_id)
		goto exit;

	sud->ews_store->priv->update_folder_list_id = 0;

	if (!g_cancellable_is_cancelled (sud->cancellable))
		run_update_thread (sud->ews_store, TRUE, sud->cancellable);

exit:
	UPDATE_UNLOCK (sud->ews_store);

	return FALSE;
}

static void
schedule_folder_list_update (CamelEwsStore *ews_store)
{
	struct ScheduleUpdateData *sud;
	CamelSettings *settings;

	g_return_if_fail (ews_store != NULL);
	g_return_if_fail (ews_store->priv != NULL);

	UPDATE_LOCK (ews_store);
	if (!ews_store->priv->updates_cancellable)
		goto exit;

	sud = g_slice_new0 (struct ScheduleUpdateData);
	sud->ews_store = g_object_ref (ews_store);
	sud->cancellable = g_object_ref (ews_store->priv->updates_cancellable);

	if (ews_store->priv->update_folder_list_id > 0)
		g_source_remove (ews_store->priv->update_folder_list_id);

	settings = camel_service_ref_settings (CAMEL_SERVICE (ews_store));

	ews_store->priv->update_folder_list_id = e_named_timeout_add_seconds_full (
								G_PRIORITY_LOW,
								1,
								folder_list_update_cb,
								sud,
								free_schedule_update_data);
	sud->expected_id = ews_store->priv->update_folder_list_id;

	g_object_unref (settings);

exit:
	UPDATE_UNLOCK (ews_store);
}

static void
camel_ews_store_server_notification_cb (CamelEwsStore *ews_store,
					GSList *events,
					EEwsConnection *cnc)
{
	GSList *l;
	gboolean update_folder = FALSE;
	gboolean update_folder_list = FALSE;
	GHashTable *folder_ids;

	g_return_if_fail (ews_store != NULL);
	g_return_if_fail (ews_store->priv != NULL);

	folder_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (l = events; l != NULL; l = l->next) {
		EEwsNotificationEvent *event = l->data;

		switch (event->type) {
			case E_EWS_NOTIFICATION_EVENT_CREATED:
			case E_EWS_NOTIFICATION_EVENT_DELETED:
			case E_EWS_NOTIFICATION_EVENT_MODIFIED:
				UPDATE_LOCK (ews_store);
				if (event->is_item) {
					update_folder = TRUE;
					if (!g_hash_table_lookup (folder_ids, event->folder_id))
						g_hash_table_insert (
							folder_ids, g_strdup (event->folder_id), GINT_TO_POINTER (1));
				} else {
					update_folder_list = TRUE;
				}
				UPDATE_UNLOCK (ews_store);
				break;
			case E_EWS_NOTIFICATION_EVENT_MOVED:
			case E_EWS_NOTIFICATION_EVENT_COPIED:
				UPDATE_LOCK (ews_store);
				if (event->is_item) {
					update_folder = TRUE;
					if (!g_hash_table_lookup (folder_ids, event->old_folder_id))
						g_hash_table_insert (
							folder_ids, g_strdup (event->old_folder_id), GINT_TO_POINTER (1));

					if (!g_hash_table_lookup (folder_ids, event->folder_id))
						g_hash_table_insert (
							folder_ids, g_strdup (event->folder_id), GINT_TO_POINTER (1));
				} else {
					update_folder_list = TRUE;
				}
				UPDATE_UNLOCK (ews_store);
				break;
			default:
				break;
		}
	}

	if (update_folder)
		schedule_folder_update (ews_store, folder_ids);
	if (update_folder_list)
		schedule_folder_list_update (ews_store);

	g_hash_table_destroy (folder_ids);
}

struct HandleNotificationsData {
	CamelEwsStore *ews_store;
	GSList *folders;
};

static void
handle_notifications_data_free (struct HandleNotificationsData *hnd)
{
	if (hnd == NULL)
		return;

	if (hnd->ews_store)
		g_object_unref (hnd->ews_store);

	g_slist_free_full (hnd->folders, g_free);
	g_slice_free (struct HandleNotificationsData, hnd);
}

static gpointer
start_notifications_thread (gpointer data)
{
	struct HandleNotificationsData *hnd = data;
	CamelEwsStore *ews_store = hnd->ews_store;
	EEwsConnection *cnc;

	cnc = camel_ews_store_ref_connection (ews_store);
	if (!cnc)
		goto exit;

	if (ews_store->priv->listen_notifications) {
		if (ews_store->priv->subscription_key != 0)
			goto exit;

		e_ews_connection_enable_notifications_sync (
			cnc,
			hnd->folders,
			&ews_store->priv->subscription_key);
	} else {
		if (ews_store->priv->subscription_key == 0)
			goto exit;

		e_ews_connection_disable_notifications_sync (
			cnc,
			ews_store->priv->subscription_key);

		ews_store->priv->subscription_key = 0;
	}

exit:
	handle_notifications_data_free (hnd);
	g_clear_object (&cnc);

	return NULL;
}

static void
folder_ids_populate (CamelFolderInfo *folder_info,
		     gpointer data)
{
	struct HandleNotificationsData *hnd = data;

	while (folder_info != NULL) {
		gchar *id;

		id = camel_ews_store_summary_get_folder_id_from_name (hnd->ews_store->summary, folder_info->full_name);
		if (id && !g_str_has_prefix (id, "ForeignMailbox::") &&
		    !g_str_equal (id, EWS_PUBLIC_FOLDER_ROOT_ID) &&
		    !g_str_equal (id, EWS_FOREIGN_FOLDER_ROOT_ID) &&
		    !camel_ews_store_summary_get_foreign (hnd->ews_store->summary, id, NULL) &&
		    !camel_ews_store_summary_get_public (hnd->ews_store->summary, id, NULL))
			hnd->folders = g_slist_prepend (hnd->folders, id);
		else
			g_free (id);

		if (folder_info->child != NULL)
			folder_ids_populate (folder_info->child, hnd);

		folder_info = folder_info->next;
	}
}

static void
camel_ews_store_handle_notifications (CamelEwsStore *ews_store,
				      CamelEwsSettings *ews_settings)
{
	GThread *thread;
	EEwsConnection *cnc;
	struct HandleNotificationsData *hnd;

	cnc = camel_ews_store_ref_connection (ews_store);

	if (!cnc)
		return;

	if (!e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2010_SP1)) {
		g_clear_object (&cnc);
		return;
	}

	hnd = g_slice_new0 (struct HandleNotificationsData);
	hnd->ews_store = g_object_ref (ews_store);

	if (!camel_ews_settings_get_check_all (ews_settings)) {
		gchar *inbox;

		inbox = camel_ews_store_summary_get_folder_id_from_folder_type (
			ews_store->summary, CAMEL_FOLDER_TYPE_INBOX);
		hnd->folders = g_slist_prepend (hnd->folders, inbox);
	} else {
		CamelFolderInfo *fi;

		fi = folder_info_from_store_summary (
			ews_store,
			NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE,
			NULL,
			NULL);

		folder_ids_populate (fi, hnd);

		camel_folder_info_free (fi);
	}

	g_clear_object (&cnc);

	thread = g_thread_new (NULL, start_notifications_thread, hnd);
	g_thread_unref (thread);
}

static void
camel_ews_store_listen_notifications_cb (CamelEwsStore *ews_store,
					 GParamSpec *spec,
					 CamelEwsSettings *ews_settings)
{
	if (ews_store->priv->listen_notifications == camel_ews_settings_get_listen_notifications (ews_settings))
		return;

	ews_store->priv->listen_notifications = !ews_store->priv->listen_notifications;

	camel_ews_store_handle_notifications (ews_store, ews_settings);
}

static void
camel_ews_store_check_all_cb (CamelEwsStore *ews_store,
			      GParamSpec *spec,
			      CamelEwsSettings *ews_settings)
{
	if (!ews_store->priv->listen_notifications)
		return;

	camel_ews_store_handle_notifications (ews_store, ews_settings);
}

static void
ews_camel_subscription_id_changed_cb (EEwsConnection *cnc,
				      const gchar *subscription_id,
				      gpointer user_data)
{
	CamelEwsStore *ews_store = user_data;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	g_mutex_lock (&ews_store->priv->connection_lock);

	if (g_strcmp0 (ews_store->priv->last_subscription_id, subscription_id) != 0) {
		g_free (ews_store->priv->last_subscription_id);
		ews_store->priv->last_subscription_id = g_strdup (subscription_id);
	}

	g_mutex_unlock (&ews_store->priv->connection_lock);
}

static gboolean
ews_connect_sync (CamelService *service,
                  GCancellable *cancellable,
                  GError **error)
{
	EEwsConnection *connection;
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	CamelEwsSettings *ews_settings;
	CamelSession *session;
	CamelSettings *settings;
	const gchar *auth_mech;
	gboolean success;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_ews_store_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	ews_store = CAMEL_EWS_STORE (service);
	priv = ews_store->priv;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	connection = camel_ews_store_ref_connection (ews_store);
	if (connection != NULL) {
		g_object_unref (connection);
		return TRUE;
	}

	session = camel_service_ref_session (service);
	settings = camel_service_ref_settings (service);
	ews_settings = CAMEL_EWS_SETTINGS (settings);

	/* Try running an operation that requires authentication
	 * to make sure we have valid credentials available. */
	auth_mech = camel_ews_settings_get_auth_mechanism_string (ews_settings);
	success = camel_session_authenticate_sync (session, service,
			   auth_mech ? auth_mech : "NTLM", cancellable, error);

	priv->listen_notifications = FALSE;

	if (success) {
		CamelEwsStoreOooAlertState state;

		state = camel_ews_store_get_ooo_alert_state (ews_store);
		if (state == CAMEL_EWS_STORE_OOO_ALERT_STATE_UNKNOWN)
			camel_session_submit_job (
				session, _("Checking “Out of Office” settings"),
				ews_update_has_ooo_set,
				g_object_ref (ews_store),
				g_object_unref);

		camel_session_submit_job (
			session, _("Look up Exchange server categories"),
			ews_exchange_server_categories_cb,
			g_object_ref (ews_store),
			g_object_unref);

		if (!priv->updates_cancellable)
			priv->updates_cancellable = g_cancellable_new ();

		if (camel_ews_settings_get_listen_notifications (ews_settings))
			camel_ews_store_listen_notifications_cb (ews_store, NULL, ews_settings);

		camel_offline_store_set_online_sync (
			CAMEL_OFFLINE_STORE (ews_store),
			TRUE, cancellable, NULL);

		connection = camel_ews_store_ref_connection (ews_store);
		if (connection) {
			g_signal_connect_swapped (
				connection,
				"server-notification",
				G_CALLBACK (camel_ews_store_server_notification_cb),
				ews_store);

			e_ews_connection_set_last_subscription_id (connection, ews_store->priv->last_subscription_id);

			g_signal_connect_object (connection, "subscription-id-changed",
				G_CALLBACK (ews_camel_subscription_id_changed_cb), ews_store, 0);

			g_clear_object (&connection);
		}
	}

	g_signal_connect_swapped (
		ews_settings,
		"notify::listen-notifications",
		G_CALLBACK (camel_ews_store_listen_notifications_cb),
		ews_store);

	g_signal_connect_swapped (
		ews_settings,
		"notify::check-all",
		G_CALLBACK (camel_ews_store_check_all_cb),
		ews_store);

	g_object_unref (session);
	g_object_unref (settings);

	return success;
}

static void
camel_ews_store_password_will_expire_cb (EEwsConnection *connection,
					 gint in_days,
					 const gchar *service_url,
					 gpointer user_data)
{
	CamelEwsStore *ews_store = user_data;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	if (ews_store->priv->password_expires_in_days < 0 ||
	    ews_store->priv->password_expires_in_days > in_days) {
		CamelService *service;
		CamelSession *session;

		ews_store->priv->password_expires_in_days = in_days;

		service = CAMEL_SERVICE (ews_store);
		session = camel_service_ref_session (service);

		if (session) {
			gchar *msg;

			if (service_url) {
				msg = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
					/* Translators: The "%s" is a service URL, provided by the server */
					"Password will expire in %d day. Open “%s” to change it.",
					"Password will expire in %d days. Open “%s” to change it.",
					in_days),
					in_days, service_url);
			} else {
				msg = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
					"Password will expire in one day.",
					"Password will expire in %d days.",
					in_days),
					in_days);
			}

			camel_session_user_alert (session, service, CAMEL_SESSION_ALERT_WARNING, msg);

			g_object_unref (session);
			g_free (msg);
		}
	}
}

static void
stop_pending_updates (CamelEwsStore *ews_store)
{
	CamelEwsStorePrivate *priv;

	g_return_if_fail (ews_store != NULL);
	g_return_if_fail (ews_store->priv != NULL);

	priv = ews_store->priv;

	UPDATE_LOCK (ews_store);
	if (priv->updates_cancellable) {
		g_cancellable_cancel (priv->updates_cancellable);
		g_object_unref (priv->updates_cancellable);
		priv->updates_cancellable = NULL;
	}

	g_slist_free_full (priv->update_folder_names, g_free);
	priv->update_folder_names = NULL;
	UPDATE_UNLOCK (ews_store);
}

static void
ews_store_unset_connection_locked (CamelEwsStore *ews_store,
				   gboolean is_disconnect)
{
	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	/* TODO cancel all operations in the connection */
	if (ews_store->priv->connection != NULL) {
		CamelSettings *settings;

		/* FIXME This is somewhat broken, since the CamelSettings
		 *       instance returned here may not be the same instance
		 *       that we connected a signal handler to.  Need to keep
		 *       our own reference to that CamelSettings instance, or
		 *       better yet avoid connecting signal handlers to it in
		 *       the first place. */
		settings = camel_service_ref_settings (CAMEL_SERVICE (ews_store));
		g_signal_handlers_disconnect_by_data (settings, ews_store);
		g_signal_handlers_disconnect_by_func (
			ews_store->priv->connection, camel_ews_store_server_notification_cb, ews_store);

		g_object_unref (settings);

		if (ews_store->priv->listen_notifications) {
			stop_pending_updates (ews_store);

			if (ews_store->priv->subscription_key != 0) {
				e_ews_connection_disable_notifications_sync (
					ews_store->priv->connection,
					ews_store->priv->subscription_key);

				ews_store->priv->subscription_key = 0;
			}

			ews_store->priv->listen_notifications = FALSE;
		}

		if (is_disconnect) {
			e_ews_connection_set_password (ews_store->priv->connection, NULL);
			e_ews_connection_set_disconnected_flag (ews_store->priv->connection, TRUE);
		}

		g_signal_handlers_disconnect_by_func (ews_store->priv->connection,
			G_CALLBACK (camel_ews_store_password_will_expire_cb), ews_store);
		g_object_unref (ews_store->priv->connection);
		ews_store->priv->connection = NULL;
	}
}

static gboolean
ews_disconnect_sync (CamelService *service,
                     gboolean clean,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (service);
	CamelServiceClass *service_class;

	g_mutex_lock (&ews_store->priv->connection_lock);
	ews_store_unset_connection_locked (ews_store, TRUE);
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
	{"drafts", CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_DRAFTS},
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

		if (folder && !e_ews_folder_is_error (folder)) {
			fid = e_ews_folder_get_id (folder);

			if (camel_ews_store_summary_has_folder (ews_store->summary, fid->id)) {
				camel_ews_store_summary_set_folder_flags (ews_store->summary, fid->id, system_folder[n].info_flags |
					(camel_ews_store_summary_get_folder_flags (ews_store->summary, fid->id, NULL) & (CAMEL_FOLDER_CHILDREN | CAMEL_FOLDER_NOCHILDREN)));
			}
		}

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
	folders = camel_ews_store_summary_get_folders (ews_store->summary, NULL, FALSE);

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

struct EwsUpdateForeignSubfoldersData
{
	CamelEwsStore *ews_store;
	gchar *folder_id;
};

static void
ews_update_foreign_subfolders_data_free (gpointer data)
{
	struct EwsUpdateForeignSubfoldersData *euf = data;

	if (euf) {
		g_object_unref (euf->ews_store);
		g_free (euf->folder_id);
		g_slice_free (struct EwsUpdateForeignSubfoldersData, euf);
	}
}

static void
ews_store_update_foreign_subfolders (CamelSession *session,
				     GCancellable *cancellable,
				     gpointer user_data,
				     GError **error)
{
	struct EwsUpdateForeignSubfoldersData *euf = user_data;
	CamelEwsStore *ews_store;
	EEwsConnection *conn;
	GSList *tocheck = NULL, *remote_folders = NULL, *local_folders = NULL;
	const gchar *fid;
	GError *local_error = NULL;

	g_return_if_fail (euf != NULL);

	ews_store = euf->ews_store;
	fid = euf->folder_id;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store)))
		return;

	conn = camel_ews_store_ref_connection (ews_store);
	g_return_if_fail (conn != NULL);

	camel_operation_push_message (cancellable, _("Updating foreign folder structure"));

	/* read remote folder structure at the server */
	while (fid && !g_cancellable_is_cancelled (cancellable) && !local_error) {
		gboolean includes_last_item = FALSE;
		EwsFolderId *folder_id = e_ews_folder_id_new (fid, NULL, FALSE);

		while (!includes_last_item && !g_cancellable_is_cancelled (cancellable) && !local_error) {
			GSList *folders = NULL, *ff;

			if (!e_ews_connection_find_folder_sync (conn, EWS_PRIORITY_MEDIUM, folder_id,
				&includes_last_item, &folders, cancellable, &local_error))
				break;

			for (ff = folders; ff != NULL; ff = ff->next) {
				EEwsFolder *folder = ff->data;

				e_ews_folder_set_parent_id (folder, e_ews_folder_id_new (fid, NULL, FALSE));

				remote_folders = g_slist_prepend (remote_folders, folder);

				if (e_ews_folder_get_child_count (folder) > 0 && e_ews_folder_get_id (folder))
					tocheck = g_slist_prepend (tocheck, e_ews_folder_get_id (folder)->id);
			}

			g_slist_free (folders);
		}

		e_ews_folder_id_free (folder_id);

		if (tocheck) {
			fid = g_slist_last (tocheck)->data;
			tocheck = g_slist_remove (tocheck, fid);
		} else {
			fid = NULL;
		}
	}

	/* get local folder structure */
	if (!local_error && !g_cancellable_is_cancelled (cancellable)) {
		gchar *full_name = camel_ews_store_summary_get_folder_full_name (ews_store->summary, euf->folder_id, NULL);
		if (full_name) {
			local_folders = camel_ews_store_summary_get_folders (ews_store->summary, full_name, FALSE);
		}
		g_free (full_name);
	}

	/* merge local and remote folder structures */
	if (!local_error && !g_cancellable_is_cancelled (cancellable)) {
		GHashTable *locals = g_hash_table_new (g_str_hash, g_str_equal);
		GSList *ii;

		remote_folders = g_slist_reverse (remote_folders);

		for (ii = local_folders; ii; ii = ii->next) {
			g_hash_table_insert (locals, ii->data, ii->data);
		}

		for (ii = remote_folders; ii; ii = ii->next) {
			EEwsFolder *folder = ii->data;
			const EwsFolderId *folder_id = e_ews_folder_get_id (folder);
			const EwsFolderId *parent_fid = e_ews_folder_get_parent_id (folder);

			if (e_ews_folder_get_folder_type (folder) == E_EWS_FOLDER_TYPE_MAILBOX &&
			    folder_id && folder_id->id) {
				if (!g_hash_table_remove (locals, folder_id->id)) {
					CamelFolderInfo *fi;

					/* it's a new folder, add it */
					camel_ews_store_summary_new_folder (
						ews_store->summary,
						folder_id->id, parent_fid ? parent_fid->id : euf->folder_id, folder_id->change_key,
						e_ews_folder_get_escaped_name (folder), E_EWS_FOLDER_TYPE_MAILBOX,
						CAMEL_FOLDER_SUBSCRIBED, e_ews_folder_get_total_count (folder), TRUE, FALSE);

					fi = camel_ews_utils_build_folder_info (ews_store, folder_id->id);
					camel_store_folder_created (CAMEL_STORE (ews_store), fi);
					camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
					camel_folder_info_free (fi);
				}
			}
		}

		/* to not remove the parent */
		g_hash_table_remove (locals, euf->folder_id);

		/* and now the locals contains only folders which were removed */
		if (g_hash_table_size (locals) > 0) {
			CamelSubscribable *subscribable = CAMEL_SUBSCRIBABLE (ews_store);
			CamelStore *store = CAMEL_STORE (ews_store);
			GHashTableIter iter;
			gpointer key, value;

			g_hash_table_iter_init (&iter, locals);
			while (g_hash_table_iter_next (&iter, &key, &value)) {
				CamelFolderInfo *fi;

				fi = camel_ews_utils_build_folder_info (ews_store, key);
				camel_subscribable_folder_unsubscribed (subscribable, fi);
				camel_store_folder_deleted (store, fi);
				camel_folder_info_free (fi);
			}
		}

		g_hash_table_destroy (locals);

		camel_ews_store_summary_save (ews_store->summary, &local_error);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	camel_operation_pop_message (cancellable);

	g_slist_free_full (remote_folders, g_object_unref);
	g_slist_free_full (local_folders, g_free);
	g_slist_free (tocheck);
	g_object_unref (conn);
}

void
camel_ews_store_update_foreign_subfolders (CamelEwsStore *ews_store,
					   const gchar *fid)
{
	struct EwsUpdateForeignSubfoldersData *euf;
	CamelSession *session;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));
	g_return_if_fail (fid != NULL);

	session = camel_service_ref_session (CAMEL_SERVICE (ews_store));
	g_return_if_fail (session != NULL);

	euf = g_slice_new0 (struct EwsUpdateForeignSubfoldersData);
	euf->ews_store = g_object_ref (ews_store);
	euf->folder_id = g_strdup (fid);

	camel_session_submit_job (
		session, _("Updating foreign folders"), ews_store_update_foreign_subfolders,
		euf, ews_update_foreign_subfolders_data_free);

	g_object_unref (session);
}

static gboolean
ews_initial_setup_with_connection_sync (CamelStore *store,
					GHashTable *save_setup,
					EEwsConnection *connection,
					GCancellable *cancellable,
					GError **error)
{
	CamelEwsStore *ews_store;
	GSList *folders = NULL, *folder_ids = NULL;
	gint nn;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (store), FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	ews_store = CAMEL_EWS_STORE (store);
	if (connection) {
		g_object_ref (connection);
	} else {
		if (!camel_ews_store_connected (ews_store, cancellable, error))
			return FALSE;

		connection = camel_ews_store_ref_connection (ews_store);
		g_return_val_if_fail (connection != NULL, FALSE);
	}

	for (nn = 0; nn < G_N_ELEMENTS (system_folder); nn++) {
		EwsFolderId *fid = NULL;

		fid = g_new0 (EwsFolderId, 1);
		fid->id = g_strdup (system_folder[nn].dist_folder_id);
		fid->is_distinguished_id = TRUE;

		folder_ids = g_slist_append (folder_ids, fid);
	}

	/* fetch system folders first using getfolder operation */
	if (!e_ews_connection_get_folder_sync (
		connection, EWS_PRIORITY_MEDIUM, "IdOnly",
		NULL, folder_ids, &folders,
		cancellable, &local_error)) {
		g_clear_object (&connection);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (folders && (g_slist_length (folders) != G_N_ELEMENTS (system_folder)))
		d (printf ("Error : not all folders are returned by getfolder operation"));
	else if (!local_error && folders)
		ews_store_set_flags (ews_store, folders);
	else if (local_error) {
		/* report error and make sure we are not leaking anything */
		g_warn_if_fail (folders == NULL);
	} else
		d (printf ("folders for respective distinguished ids don't exist"));

	if (save_setup) {
		gchar *folder_id;

		folder_id = camel_ews_store_summary_get_folder_id_from_folder_type (ews_store->summary, CAMEL_FOLDER_TYPE_SENT);
		if (folder_id) {
			gchar *fullname;

			fullname = camel_ews_store_summary_get_folder_full_name (ews_store->summary, folder_id, NULL);
			if (fullname && *fullname) {
				g_hash_table_insert (save_setup,
					g_strdup (CAMEL_STORE_SETUP_SENT_FOLDER),
					g_strdup (fullname));
			}

			g_free (fullname);
			g_free (folder_id);
		}

		if (g_slist_length (folders) == G_N_ELEMENTS (system_folder)) {
			gint ii;

			for (ii = 0; ii < G_N_ELEMENTS (system_folder); ii++) {
				if (g_str_equal ("drafts", system_folder[ii].dist_folder_id)) {
					break;
				}
			}

			if (ii < G_N_ELEMENTS (system_folder)) {
				EEwsFolder *drafts = g_slist_nth (folders, ii)->data;
				if (drafts && !e_ews_folder_is_error (drafts)) {
					const EwsFolderId *fid = e_ews_folder_get_id (drafts);

					if (fid && fid->id) {
						gchar *fullname;

						fullname = camel_ews_store_summary_get_folder_full_name (ews_store->summary, fid->id, NULL);
						if (fullname && *fullname) {
							g_hash_table_insert (save_setup,
								g_strdup (CAMEL_STORE_SETUP_DRAFTS_FOLDER),
								g_strdup (fullname));
						}

						g_free (fullname);
					}
				}
			}
		}
	}

	g_slist_free_full (folders, g_object_unref);
	g_slist_free_full (folder_ids, (GDestroyNotify) e_ews_folder_id_free);
	g_clear_object (&connection);
	g_clear_error (&local_error);

	return TRUE;
}

static gboolean
ews_initial_setup_sync (CamelStore *store,
			GHashTable *save_setup,
			GCancellable *cancellable,
			GError **error)
{
	return ews_initial_setup_with_connection_sync (store, save_setup, NULL, cancellable, error);
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
	ESource *source;
	GSList *folders_created = NULL;
	GSList *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	GSList *folder_ids = NULL;
	GSList *created_folder_ids = NULL;
	gboolean includes_last_folder = FALSE;
	gboolean initial_setup = FALSE;
	const gchar *password;
	gchar *hosturl;
	gchar *old_sync_state = NULL, *new_sync_state = NULL;
	gchar *certificate_pem = NULL;
	GTlsCertificateFlags certificate_errors = 0;
	GError *local_error = NULL;

	ews_store = CAMEL_EWS_STORE (service);

	password = camel_service_get_password (service);

	settings = camel_service_ref_settings (service);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);
	source = camel_ews_utils_ref_corresponding_source (service, cancellable);

	connection = e_ews_connection_new (source, hosturl, ews_settings);
	e_ews_connection_set_password (connection, password);

	g_clear_object (&source);
	g_free (hosturl);

	g_object_unref (settings);

	e_binding_bind_property (
		service, "proxy-resolver",
		connection, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	/* XXX We need to run some operation that requires authentication
	 *     but does not change any server-side state, so we can check
	 *     the error status and determine if our password is valid.
	 *     David suggested e_ews_connection_sync_folder_hierarchy(),
	 *     since we have to do that eventually anyway. */

	/*use old sync_state from summary*/
	old_sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	if (!old_sync_state) {
		initial_setup = TRUE;
	} else {
		gchar *folder_id;

		folder_id = camel_ews_store_summary_get_folder_id_from_folder_type (ews_store->summary, CAMEL_FOLDER_TYPE_INBOX);
		if (!folder_id || !*folder_id)
			initial_setup = TRUE;

		g_free (folder_id);

		if (!initial_setup) {
			folder_id = camel_ews_store_summary_get_folder_id_from_folder_type (ews_store->summary, CAMEL_FOLDER_TYPE_DRAFTS);
			if (!folder_id || !*folder_id)
				initial_setup = TRUE;

			g_free (folder_id);
		}
	}

	e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, old_sync_state,
		&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	g_free (old_sync_state);
	old_sync_state = NULL;

	if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_UNAVAILABLE)) {
		local_error->domain = CAMEL_SERVICE_ERROR;
		local_error->code = CAMEL_SERVICE_ERROR_UNAVAILABLE;
	}

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
		GSList *foreign_fids, *ff;

		g_mutex_lock (&ews_store->priv->connection_lock);
		ews_store_unset_connection_locked (ews_store, FALSE);
		ews_store->priv->connection = g_object_ref (connection);
		g_signal_connect (ews_store->priv->connection, "password-will-expire",
			G_CALLBACK (camel_ews_store_password_will_expire_cb), ews_store);
		g_mutex_unlock (&ews_store->priv->connection_lock);

		/* This consumes all allocated result data. */
		ews_update_folder_hierarchy (
			ews_store, new_sync_state, includes_last_folder,
			folders_created, folders_deleted, folders_updated, &created_folder_ids);

		/* Also update folder structures of foreign folders,
		   those which are subscribed with subfolders */
		foreign_fids = camel_ews_store_summary_get_foreign_folders (ews_store->summary, NULL);
		for (ff = foreign_fids; ff != NULL; ff = ff->next) {
			const gchar *fid = ff->data;

			if (camel_ews_store_summary_get_foreign_subfolders (ews_store->summary, fid, NULL)) {
				camel_ews_store_update_foreign_subfolders (ews_store, fid);
			}
		}

		g_slist_free_full (foreign_fids, g_free);
	} else {
		g_mutex_lock (&ews_store->priv->connection_lock);
		ews_store_unset_connection_locked (ews_store, FALSE);
		g_mutex_unlock (&ews_store->priv->connection_lock);

		g_free (new_sync_state);

		/* Make sure we're not leaking anything. */
		g_warn_if_fail (folders_created == NULL);
		g_warn_if_fail (folders_updated == NULL);
		g_warn_if_fail (folders_deleted == NULL);
	}

	/*get folders using distinguished id by GetFolder operation and set system flags to folders, only for first time*/
	if (!local_error && initial_setup && connection) {
		ews_initial_setup_with_connection_sync (CAMEL_STORE (ews_store), NULL, connection, cancellable, NULL);
	}

	/* postpone notification of new folders to time when also folder flags are known,
	   thus the view in evolution sows Inbox with an Inbox icon. */
	for (folder_ids = created_folder_ids; folder_ids; folder_ids = folder_ids->next) {
		CamelFolderInfo *fi;

		fi = camel_ews_utils_build_folder_info (ews_store, folder_ids->data);
		camel_store_folder_created (CAMEL_STORE (ews_store), fi);
		camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
		camel_folder_info_free (fi);
	}

	g_slist_free_full (created_folder_ids, g_free);

	if (g_error_matches (local_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE) &&
	    e_ews_connection_get_ssl_error_details (connection, &certificate_pem, &certificate_errors)) {
		source = e_ews_connection_get_source (connection);

		if (source) {
			e_source_emit_credentials_required (source, E_SOURCE_CREDENTIALS_REASON_SSL_FAILED,
				certificate_pem, certificate_errors, local_error);
		}

		g_free (certificate_pem);
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

static GList *
ews_store_query_auth_types_sync (CamelService *service,
                                 GCancellable *cancellable,
                                 GError **error)
{
	EEwsConnection *connection;
	ESource *source;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	GList *auth_types = NULL;
	GSList *auth_methods = NULL, *aiter;
	gchar *hosturl;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (service), NULL);

	settings = camel_service_ref_settings (service);
	ews_settings = CAMEL_EWS_SETTINGS (settings);
	hosturl = camel_ews_settings_dup_hosturl (ews_settings);
	source = camel_ews_utils_ref_corresponding_source (service, cancellable);

	connection = e_ews_connection_new_full (source, hosturl, ews_settings, FALSE);

	g_clear_object (&source);
	g_free (hosturl);
	g_object_unref (settings);

	e_binding_bind_property (
		service, "proxy-resolver",
		connection, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	if (e_ews_connection_query_auth_methods_sync (connection, G_PRIORITY_DEFAULT, &auth_methods, cancellable, error)) {
		CamelProvider *provider;
		CamelServiceAuthType *authtype;

		provider = camel_service_get_provider (service);
		g_return_val_if_fail (provider != NULL, NULL);

		for (aiter = auth_methods; aiter; aiter = aiter->next) {
			GList *siter;
			const gchar *auth = aiter->data;

			if (!auth)
				continue;

			if (g_ascii_strcasecmp (auth, "NTLM") == 0)
				auth = ""; 
			else if (g_ascii_strcasecmp (auth, "Basic") == 0)
				auth = "PLAIN";
			else if (g_ascii_strcasecmp (auth, "Negotiate") == 0)
				auth = "GSSAPI";
			else if (e_oauth2_services_is_supported () &&
				 g_ascii_strcasecmp (auth, "Bearer") == 0) {
				/* Use Camel name for OAuth2. It's up to the caller to decide whether
				   it can be used or not. */
				authtype = camel_sasl_authtype ("XOAUTH2");
				if (authtype)
					auth_types = g_list_prepend (auth_types, authtype);

				continue;
			}

			for (siter = provider->authtypes; siter; siter = siter->next) {
				authtype = siter->data;

				if (g_ascii_strcasecmp (authtype->authproto, auth) == 0)
					auth_types = g_list_prepend (auth_types, authtype);
			}
		}

		g_slist_free_full (auth_methods, g_free);
	}

	g_object_unref (connection);

	return g_list_reverse (auth_types);
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

	if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return NULL;
	} else {
		/* We don't actually care what it is; only that it exists */
		g_free (fid);
	}

	folder_dir = g_build_filename (ews_store->storage_path, "folders", folder_name, NULL);
	folder = camel_ews_folder_new (store, folder_name, folder_dir, cancellable, error);

	g_free (folder_dir);

	if ((flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0)
		camel_folder_prepare_content_refresh (folder);

	return folder;
}

static gchar *
get_public_folder_full_name (EEwsFolder *folder,
			     GHashTable *folders_by_id)
{
	const EwsFolderId *parent_fid;
	GString *full_name;

	g_return_val_if_fail (folder != NULL, NULL);
	g_return_val_if_fail (folders_by_id != NULL, NULL);

	full_name = g_string_new (e_ews_folder_get_escaped_name (folder));
	while (folder) {
		parent_fid = e_ews_folder_get_parent_id (folder);
		if (!parent_fid || !parent_fid->id)
			break;

		folder = g_hash_table_lookup (folders_by_id, parent_fid->id);
		if (folder) {
			g_string_prepend_c (full_name, '/');
			g_string_prepend (full_name, e_ews_folder_get_escaped_name (folder));
		}
	}

	g_string_prepend_c (full_name, '/');
	g_string_prepend (full_name, EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME);

	return g_string_free (full_name, FALSE);
}

static gboolean
ews_store_has_as_parent_id (CamelEwsStoreSummary *ews_summary,
			    const gchar *fid,
			    const gchar *mailroot_fid)
{
	gchar *parent = NULL;
	gboolean found = FALSE;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE_SUMMARY (ews_summary), FALSE);
	g_return_val_if_fail (fid != NULL, FALSE);
	g_return_val_if_fail (mailroot_fid != NULL, FALSE);

	while (found = g_strcmp0 (fid, mailroot_fid) == 0, fid && !found) {
		gchar *tmp = parent;

		parent = camel_ews_store_summary_get_parent_folder_id (ews_summary, fid, NULL);
		fid = parent;

		g_free (tmp);
	}

	g_free (parent);

	return found;
}

static gboolean
ews_store_folder_is_direct_subfolder (CamelEwsStore *ews_store,
				      const gchar *fid,
				      const gchar *path)
{
	gchar *fid_path;
	gboolean res;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (ews_store), FALSE);
	g_return_val_if_fail (fid != NULL, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	fid_path = camel_ews_store_summary_get_folder_full_name (ews_store->summary, fid, NULL);

	if (!fid_path)
		return FALSE;

	res = g_str_has_prefix (fid_path, path);

	if (res) {
		gint path_len;

		path_len = strlen (path);

		res = fid_path[path_len] == '/' && !strchr (fid_path + path_len + 1, '/');
	}

	g_free (fid_path);

	return res;
}

static CamelFolderInfo *
folder_info_from_store_summary (CamelEwsStore *store,
                                const gchar *top,
                                guint32 flags,
				GCancellable *cancellable,
                                GError **error)
{
	CamelEwsStoreSummary *ews_summary;
	GPtrArray *folder_infos = NULL;
	CamelFolderInfo *root_fi = NULL, *fi;

	/* search in public folders */
	if ((flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) != 0) {
		GHashTable *folders_by_id;
		GSList *fiter;
		GList *esources = NULL;
		gchar *hosturl = NULL, *username = NULL;

		g_mutex_lock (&store->priv->get_finfo_lock);

		if (!store->priv->public_folders) {
			g_mutex_unlock (&store->priv->get_finfo_lock);
			return NULL;
		}

		folder_infos = g_ptr_array_new ();
		folders_by_id = g_hash_table_new (g_str_hash, g_str_equal);

		for (fiter = store->priv->public_folders; fiter != NULL; fiter = g_slist_next (fiter)) {
			EEwsFolder *folder = fiter->data;
			const EwsFolderId *fid;

			if (!folder)
				continue;

			fid = e_ews_folder_get_id (folder);
			if (!fid || !fid->id)
				continue;

			g_hash_table_insert (folders_by_id, fid->id, folder);
		}

		fi = camel_folder_info_new ();
		fi->full_name = g_strdup (EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME);
		fi->display_name = g_strdup (fi->full_name);
		fi->flags = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_NOSELECT;
		fi->unread = -1;
		fi->total = -1;

		g_ptr_array_add (folder_infos, fi);

		for (fiter = store->priv->public_folders; fiter != NULL; fiter = g_slist_next (fiter)) {
			EEwsFolder *folder = fiter->data;
			const EwsFolderId *fid;

			if (!folder)
				continue;

			fid = e_ews_folder_get_id (folder);
			if (!fid || !fid->id)
				continue;

			fi = camel_folder_info_new ();
			fi->full_name = get_public_folder_full_name (folder, folders_by_id);
			fi->display_name = g_strdup (e_ews_folder_get_name (folder));
			fi->flags = 0;
			fi->unread = e_ews_folder_get_unread_count (folder);
			fi->total = e_ews_folder_get_total_count (folder);

			switch (e_ews_folder_get_folder_type (folder)) {
			case E_EWS_FOLDER_TYPE_CALENDAR:
				fi->flags |= CAMEL_FOLDER_TYPE_EVENTS;
				break;
			case E_EWS_FOLDER_TYPE_CONTACTS:
				fi->flags |= CAMEL_FOLDER_TYPE_CONTACTS;
				break;
			case E_EWS_FOLDER_TYPE_TASKS:
				fi->flags |= CAMEL_FOLDER_TYPE_TASKS;
				break;
			case E_EWS_FOLDER_TYPE_MEMOS:
				fi->flags |= CAMEL_FOLDER_TYPE_MEMOS;
				break;
			default:
				break;
			}

			if (camel_ews_store_summary_has_folder (store->summary, fid->id)) {
				guint64 fflags = camel_ews_store_summary_get_folder_flags (store->summary, fid->id, NULL);

				if ((fflags & CAMEL_FOLDER_SUBSCRIBED) != 0)
					fi->flags |= CAMEL_FOLDER_SUBSCRIBED;
			}

			if (!(fi->flags & CAMEL_FOLDER_SUBSCRIBED) &&
			    e_ews_folder_get_folder_type (folder) != E_EWS_FOLDER_TYPE_MAILBOX) {
				if (!hosturl && !username && !esources) {
					CamelSettings *settings;
					CamelEwsSettings *ews_settings;

					settings = camel_service_ref_settings (CAMEL_SERVICE (store));
					ews_settings = CAMEL_EWS_SETTINGS (settings);

					hosturl = camel_ews_settings_dup_hosturl (ews_settings);
					username = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (ews_settings));
					esources = e_ews_folder_utils_get_esources (hosturl, username, cancellable, NULL);

					g_object_unref (settings);
				}

				if (e_ews_folder_utils_is_subscribed_as_esource (esources, hosturl, username, fid->id))
					fi->flags |= CAMEL_FOLDER_SUBSCRIBED;
			}

			g_ptr_array_add (folder_infos, fi);
		}

		g_list_free_full (esources, g_object_unref);
		g_hash_table_destroy (folders_by_id);
		g_free (hosturl);
		g_free (username);
		g_mutex_unlock (&store->priv->get_finfo_lock);

	/* search in regular/subscribed folders */
	} else {
		GSList *folders, *fiter;
		gchar *mailroot_fid = NULL, *inbox_fid;
		gboolean show_public_folders, is_in_public_folders_root;
		gboolean is_online;

		ews_summary = store->summary;
		folders = camel_ews_store_summary_get_folders (ews_summary, top, FALSE);
		if (!folders)
			return NULL;

		show_public_folders = ews_store_show_public_folders (store);
		is_in_public_folders_root = top && g_str_has_prefix (top, EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME);
		is_online = camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store));
		inbox_fid = camel_ews_store_summary_get_folder_id_from_folder_type (ews_summary, CAMEL_FOLDER_TYPE_INBOX);
		if (inbox_fid) {
			mailroot_fid = camel_ews_store_summary_get_parent_folder_id (ews_summary, inbox_fid, NULL);
		}
		g_free (inbox_fid);
		inbox_fid = NULL;

		folder_infos = g_ptr_array_new ();

		for (fiter = folders; fiter != NULL; fiter = g_slist_next (fiter)) {
			EEwsFolderType ftype;
			const gchar *fid = fiter->data;

			ftype = camel_ews_store_summary_get_folder_type (ews_summary, fid, NULL);
			if (ftype != E_EWS_FOLDER_TYPE_MAILBOX)
				continue;

			if (camel_ews_store_summary_get_public (ews_summary, fid, NULL) ||
			    (show_public_folders && g_strcmp0 (fid, EWS_PUBLIC_FOLDER_ROOT_ID) == 0)) {
				if (!show_public_folders || (!is_in_public_folders_root && g_strcmp0 (fid, EWS_PUBLIC_FOLDER_ROOT_ID) != 0)) {
					guint64 fflags;

					fflags = camel_ews_store_summary_get_folder_flags (ews_summary, fid, NULL);
					if (!(fflags & CAMEL_FOLDER_SUBSCRIBED))
						continue;
				}
			} else if (!camel_ews_store_summary_get_foreign (ews_summary, fid, NULL) &&
				mailroot_fid && !ews_store_has_as_parent_id (ews_summary, fid, mailroot_fid)) {
				/* Skip mail folders out of the msgfolderroot hierarchy */
				continue;
			}

			if (is_in_public_folders_root && is_online && show_public_folders &&
			    g_strcmp0 (fid, EWS_PUBLIC_FOLDER_ROOT_ID) != 0 && !ews_store_folder_is_direct_subfolder (store, fid, top))
				continue;

			fi = camel_ews_utils_build_folder_info (store, fid);
			g_ptr_array_add (folder_infos, fi);
		}

		g_slist_free_full (folders, g_free);
		g_free (mailroot_fid);
	}

	root_fi = camel_folder_info_build (folder_infos, top, '/', TRUE);
	g_ptr_array_free (folder_infos, TRUE);

	return root_fi;
}

static gboolean
ews_refresh_finfo (CamelEwsStore *ews_store)
{
	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store)))
		return FALSE;

	if (!ews_store->priv->updates_cancellable)
		ews_store->priv->updates_cancellable = g_cancellable_new ();

	run_update_thread (ews_store, TRUE, ews_store->priv->updates_cancellable);

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
	gboolean includes_last_folder = TRUE;
	gboolean success;
	GError *local_error = NULL;

	ews_store = (CamelEwsStore *) store;
	priv = ews_store->priv;

	if ((flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0 &&
	    camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
		CamelSession *session;

		session = camel_service_ref_session (CAMEL_SERVICE (ews_store));
		if (session) {
			ews_exchange_server_categories_cb (session, cancellable, ews_store, NULL);
			g_object_unref (session);
		}
	}

	if ((flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST) != 0) {
		GSList *folders = NULL, *to_check = NULL;
		EwsFolderId *folder_id;

		if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
			g_set_error_literal (
				error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Cannot list EWS public folders in offline mode"));
			return NULL;
		}

		g_mutex_lock (&priv->get_finfo_lock);

		g_slist_free_full (priv->public_folders, g_object_unref);
		priv->public_folders = NULL;

		connection = camel_ews_store_ref_connection (ews_store);
		folder_id = e_ews_folder_id_new ("publicfoldersroot", NULL, TRUE);
		to_check = g_slist_append (to_check, folder_id);

		while (!local_error && !g_cancellable_is_cancelled (cancellable) && to_check) {
			folder_id = to_check->data;
			to_check = g_slist_remove (to_check, folder_id);

			while (e_ews_connection_find_folder_sync (connection, EWS_PRIORITY_MEDIUM, folder_id, &includes_last_folder, &folders,
				cancellable, &local_error) && !local_error &&
				!g_cancellable_is_cancelled (cancellable)) {
				GSList *fiter;

				if (!folders)
					break;

				for (fiter = folders; fiter != NULL; fiter = fiter->next) {
					EEwsFolder *folder = fiter->data;

					if (e_ews_folder_get_child_count (folder) > 0) {
						const EwsFolderId *fid = e_ews_folder_get_id (folder);

						if (fid)
							to_check = g_slist_prepend (to_check,
								e_ews_folder_id_new (fid->id, fid->change_key, fid->is_distinguished_id));
					}

					if (!e_ews_folder_get_parent_id (folder)) {
						if (!folder_id->is_distinguished_id) {
							e_ews_folder_set_parent_id (folder,
								e_ews_folder_id_new (folder_id->id, folder_id->change_key, folder_id->is_distinguished_id));
						} else {
							e_ews_folder_set_parent_id (folder, e_ews_folder_id_new (EWS_PUBLIC_FOLDER_ROOT_ID, NULL, FALSE));
						}
					}
				}

				priv->public_folders = g_slist_concat (priv->public_folders, folders);
				folders = NULL;

				if (includes_last_folder)
					break;
			}

			e_ews_folder_id_free (folder_id);
		}

		g_mutex_unlock (&priv->get_finfo_lock);

		g_object_unref (connection);
		g_slist_free_full (to_check, (GDestroyNotify) e_ews_folder_id_free);

		camel_ews_store_ensure_virtual_folders (ews_store);

		if (local_error) {
			camel_ews_store_maybe_disconnect (ews_store, local_error);
			g_propagate_error (error, local_error);

			return NULL;
		}

		if (!priv->public_folders) {
			g_set_error_literal (
				error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Cannot find any EWS public folders"));
			return NULL;
		}

		goto offline;
	}

	g_mutex_lock (&priv->get_finfo_lock);
	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		camel_ews_store_ensure_virtual_folders (ews_store);
		g_mutex_unlock (&priv->get_finfo_lock);
		goto offline;
	}

	if (!camel_service_connect_sync ((CamelService *) store, cancellable, error)) {
		camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (store), FALSE, NULL, NULL);
		camel_ews_store_ensure_virtual_folders (ews_store);
		g_mutex_unlock (&priv->get_finfo_lock);
		return NULL;
	}

	old_sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	if (!old_sync_state)
		initial_setup = TRUE;

	if (!initial_setup && (flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0) {
		time_t now = time (NULL);

		g_free (old_sync_state);
		if (now - priv->last_refresh_time > FINFO_REFRESH_INTERVAL && ews_refresh_finfo (ews_store))
			ews_store->priv->last_refresh_time = time (NULL);

		g_mutex_unlock (&priv->get_finfo_lock);
		goto offline;
	}

	connection = camel_ews_store_ref_connection (ews_store);
	includes_last_folder = TRUE;

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

		success = e_ews_connection_sync_folder_hierarchy_sync (connection, EWS_PRIORITY_MEDIUM, NULL,
			&new_sync_state, &includes_last_folder, &folders_created, &folders_updated, &folders_deleted,
			cancellable, &local_error);
	}

	if (success && ews_store_show_public_folders (ews_store))
		ews_store_sync_public_folders (ews_store, connection, top, &folders_created, &folders_updated, &folders_deleted, cancellable, NULL);

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
		folders_created, folders_deleted, folders_updated, NULL);
	g_mutex_unlock (&priv->get_finfo_lock);

offline:
	fi = folder_info_from_store_summary (ews_store, top, flags, cancellable, error);
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
	gboolean success, is_foreign = FALSE, is_public = FALSE;
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
			_("Cannot create folder “%s”, folder already exists"),
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
				_("Cannot create folder under “%s”, it is used for folders of other users only"),
				parent_name);
			return NULL;
		}

		if (g_str_equal (fid, EWS_PUBLIC_FOLDER_ROOT_ID)) {
			g_free (fid);

			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot create folder under “%s”, it is used for public folders only"),
				parent_name);
			return NULL;
		}

		is_foreign = camel_ews_store_summary_get_foreign (ews_summary, fid, NULL);
		is_public = camel_ews_store_summary_get_public (ews_summary, fid, NULL);
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
		0, 0, is_foreign, is_public);
	fi = camel_ews_utils_build_folder_info (ews_store, folder_id->id);
	e_ews_folder_id_free (folder_id);

	camel_store_folder_created (store, fi);
	camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);

	g_free (full_name);
	g_free (fid);
	return fi;
}

static void
ews_update_store_move_recursive (CamelEwsStore *ews_store,
				 CamelFolderInfo *folder_info)
{
	while (folder_info != NULL) {
		if (folder_info->child != NULL)
			ews_update_store_move_recursive (ews_store, folder_info->child);

		camel_store_folder_created (CAMEL_STORE (ews_store), folder_info);
		camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), folder_info);

		folder_info = folder_info->next;
	}
}

static gboolean
ews_delete_folder_sync (CamelStore *store,
                        const gchar *folder_name,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	gchar *fid;
	gchar *trash_fid;
	gchar *trash_name;
	CamelFolderInfo *folder_info;
	CamelFolderInfo *to_update;
	gboolean success;
	gboolean is_under_trash_folder;
	GError *local_error = NULL;

	folder_info = camel_store_get_folder_info_sync (
		store, folder_name,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE |
		CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
		cancellable, &local_error);

	if (folder_info == NULL) {
		g_propagate_error (error, local_error);

		return FALSE;
	}

	to_update = folder_info;

	fid = camel_ews_store_summary_get_folder_id_from_name (
		ews_store->summary, folder_name);

	if (!fid) {
		camel_folder_info_free (folder_info);

		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Folder does not exist"));
		return FALSE;
	}

	if (g_str_equal (fid, EWS_FOREIGN_FOLDER_ROOT_ID)) {
		g_free (fid);
		camel_folder_info_free (folder_info);

		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot remove folder “%s”, it is used for folders of other users only"),
			folder_name);
		return FALSE;
	}

	if (g_str_equal (fid, EWS_PUBLIC_FOLDER_ROOT_ID)) {
		g_free (fid);
		camel_folder_info_free (folder_info);

		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot remove folder “%s”, it is used for public folders only"),
			folder_name);
		return FALSE;
	}

	trash_fid = camel_ews_store_summary_get_folder_id_from_folder_type (ews_store->summary, CAMEL_FOLDER_TYPE_TRASH);
	trash_name = camel_ews_store_summary_get_folder_full_name (ews_store->summary, trash_fid, &local_error);
	if (!trash_name) {
		g_free (trash_fid);
		g_free (fid);
		camel_folder_info_free (folder_info);

		g_propagate_error (error, local_error);
		return FALSE;
	}

	is_under_trash_folder = g_str_has_prefix (folder_name, trash_name);

	g_free (trash_name);

	if (!camel_ews_store_connected (ews_store, cancellable, &local_error)) {
		g_free (trash_fid);
		g_free (fid);
		camel_folder_info_free (folder_info);

		g_propagate_error (error, local_error);

		return FALSE;
	}

	if (camel_ews_store_summary_get_foreign (ews_store->summary, fid, NULL) ||
	    camel_ews_store_summary_get_public (ews_store->summary, fid, NULL)) {
		/* do not delete foreign or public folders,
		 * only remove them from local store */
		success = TRUE;
	} else {
		EEwsConnection *connection;

		connection = camel_ews_store_ref_connection (ews_store);

		success = e_ews_connection_delete_folder_sync (
			connection,
			EWS_PRIORITY_MEDIUM,
			fid, FALSE, is_under_trash_folder ? "HardDelete" : "MoveToDeletedItems",
			cancellable, &local_error);

		g_object_unref (connection);
	}

	if (!success) {
		g_free (trash_fid);
		g_free (fid);
		camel_folder_info_free (folder_info);

		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);

		return FALSE;
	}

	if (is_under_trash_folder) {
		success = camel_ews_utils_delete_folders_from_summary_recursive (ews_store, to_update, FALSE, error);

		if (!success) {
			g_free (trash_fid);
			g_free (fid);
			camel_folder_info_free (folder_info);

			g_propagate_error (error, local_error);
			return FALSE;
		}
	} else {
		camel_ews_store_summary_set_parent_folder_id (ews_store->summary, fid, trash_fid);
	}

	camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), folder_info);
	camel_store_folder_deleted (CAMEL_STORE (ews_store), folder_info);

	camel_folder_info_free (folder_info);

	if (!is_under_trash_folder) {
		camel_ews_store_summary_rebuild_hashes (ews_store->summary);

		folder_info = camel_ews_utils_build_folder_info (ews_store, fid);

		to_update = folder_info;
		ews_update_store_move_recursive (ews_store, to_update);
		camel_folder_info_free (folder_info);
	}

	camel_ews_store_ensure_virtual_folders (ews_store);
	camel_ews_store_summary_save (ews_store->summary, NULL);

	g_free (trash_fid);
	g_free (fid);

	return TRUE;
}

struct _rename_cb_data {
	const gchar *display_name;
	const gchar *change_key;
	const gchar *folder_id;
};

static gboolean
rename_folder_cb (ESoapRequest *request,
                  gpointer user_data,
		  GError **error)
{
	struct _rename_cb_data *rename_data = user_data;

	e_ews_request_start_item_change (
		request, E_EWS_ITEMCHANGE_TYPE_FOLDER,
		rename_data->folder_id, rename_data->change_key, 0);
	e_soap_request_start_element (request, "SetFolderField", NULL, NULL);
	e_ews_request_write_string_parameter_with_attribute (
		request, "FieldURI", NULL, NULL,
		"FieldURI", "folder:DisplayName");

	e_soap_request_start_element (request, "Folder", NULL, NULL);
	e_ews_request_write_string_parameter (request, "DisplayName", NULL, rename_data->display_name);
	e_soap_request_end_element (request); /* Folder */

	e_soap_request_end_element (request); /* SetFolderField */

	e_ews_request_end_item_change (request);

	return TRUE;
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
	gchar *fid, *parent_fid;
	gchar *changekey;
	gboolean res = FALSE;
	GError *local_error = NULL;

	if (!strcmp (old_name, new_name))
		return TRUE;

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary, old_name);
	if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Folder %s does not exist"), old_name);
		return FALSE;
	}

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

	parent_fid = camel_ews_store_summary_get_parent_folder_id (ews_summary, fid, NULL);
	if (g_strcmp0 (parent_fid, EWS_FOREIGN_FOLDER_ROOT_ID) == 0) {
		gboolean success = FALSE;

		if (strcmp (old_slash, new_slash) != 0) {
			gint parent_len = old_slash - old_name;
			if (new_slash - new_name != parent_len ||
			    strncmp (old_name, new_name, parent_len)) {
				/* do nothing when moving the folder, the error is set below */
			} else {
				camel_ews_store_summary_set_folder_name (ews_summary, fid, new_slash);
				success = TRUE;
			}
		}

		g_free (parent_fid);
		g_free (fid);

		if (!success)
			g_set_error_literal (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_INVALID, _("Cannot move a foreign folder"));

		return success;
	}

	g_free (parent_fid);

	if (!camel_ews_store_connected (ews_store, cancellable, error)) {
		g_free (fid);
		return FALSE;
	}

	changekey = camel_ews_store_summary_get_change_key (ews_summary, fid, error);
	if (!changekey) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No change key record for folder %s"), fid);
		g_free (fid);
		return FALSE;
	}

	connection = camel_ews_store_ref_connection (ews_store);

	if (strcmp (old_slash, new_slash)) {
		gint parent_len = old_slash - old_name;
		struct _rename_cb_data rename_data;

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
			goto out;
		}

		rename_data.display_name = new_slash;
		rename_data.folder_id = fid;
		rename_data.change_key = changekey;

		res = e_ews_connection_update_folder_sync (
			connection, EWS_PRIORITY_MEDIUM,
			rename_folder_cb, &rename_data,
			cancellable, &local_error);

		if (!res) {
			goto out;
		}
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
			if (!pfid) {
				g_set_error (
					error, CAMEL_STORE_ERROR,
					CAMEL_STORE_ERROR_NO_FOLDER,
					_("Cannot find folder ID for parent folder %s"),
					parent_name);
				g_free (parent_name);
				goto out;
			}
			g_free (parent_name);
		} else {
			gchar *inbox_id;

			inbox_id = camel_ews_store_summary_get_folder_id_from_folder_type (ews_summary, CAMEL_FOLDER_TYPE_INBOX);

			if (!inbox_id) {
				g_set_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
					_("Folder %s does not exist"), _("Inbox"));
				goto out;
			}

			pfid = camel_ews_store_summary_get_parent_folder_id (ews_summary, inbox_id, NULL);

			g_free (inbox_id);

			if (!pfid) {
				g_set_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
					_("Cannot find folder ID for parent folder %s"), _("Inbox"));
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

	folder = camel_store_get_folder_sync (
		store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);

	if (folder) {
		GPtrArray *folders;
		gboolean can = TRUE;
		gint ii;

		/* Save content of all opened folders, thus any messages deleted in them
		   are moved to the Deleted Items folder first, thus in case of the trash
		   folder instance being used to expunge messages will contain all of them.
		*/
		folders = camel_store_dup_opened_folders (store);
		for (ii = 0; ii < folders->len; ii++) {
			CamelFolder *secfolder = folders->pdata[ii];

			if (secfolder != folder && can)
				can = camel_folder_synchronize_sync (secfolder, FALSE, cancellable, NULL);

			g_object_unref (secfolder);
		}
		g_ptr_array_free (folders, TRUE);

		/* To return 'Deleted Items' folder with current content,
		   not with possibly stale locally cached copy. */
		camel_folder_refresh_info_sync (folder, cancellable, NULL);
	}

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

	folder = camel_store_get_folder_sync (
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
	CamelFolder *folder;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	gboolean check_all, res = FALSE;
	GError *local_error = NULL;

	/* Skip unselectable folders from automatic refresh */
	if (info && (info->flags & CAMEL_FOLDER_NOSELECT) != 0)
		return FALSE;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	check_all = camel_ews_settings_get_check_all (ews_settings);

	g_object_unref (settings);

	if (check_all || CAMEL_STORE_CLASS (camel_ews_store_parent_class)->can_refresh_folder (store, info, &local_error))
		return TRUE;

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	folder = camel_store_get_folder_sync (store, info->full_name, 0, NULL, &local_error);
	if (folder && CAMEL_IS_EWS_FOLDER (folder))
		res = camel_ews_folder_get_check_folder (CAMEL_EWS_FOLDER (folder));

	g_clear_object (&folder);

	if (local_error)
		g_propagate_error (error, local_error);

	return res;
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

	if (!truth && camel_ews_store_summary_get_public (ews_store->summary, fid, &error) && !error) {
		truth = TRUE;
	}
	g_clear_error (&error);

	g_free (fid);

	return truth;
}

/* caller should hold ews_store->priv->get_finfo_lock already */
static EEwsFolder *
ews_store_find_public_folder (CamelEwsStore *ews_store,
			      const gchar *folder_name)
{
	EEwsFolder *folder = NULL;
	GSList *piter;
	gchar **folders;
	gint ii;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (ews_store), NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	folders = g_strsplit (folder_name, "/", -1);
	if (!folders || !folders[0] || g_strcmp0 (folders[0], EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME) != 0) {
		g_strfreev (folders);
		return NULL;
	}

	/* they are stored in public_folders from top level to bottom level */
	piter = ews_store->priv->public_folders;
	for (ii = 1; folders[ii] && piter; ii++) {
		const gchar *fname = folders[ii];

		while (piter) {
			EEwsFolder *subf = piter->data;
			const EwsFolderId *parent_id;

			if (!subf) {
				piter = NULL;
				break;
			}

			if (g_strcmp0 (e_ews_folder_get_name (subf), fname) == 0 ||
			    g_strcmp0 (e_ews_folder_get_escaped_name (subf), fname) == 0) {
				parent_id = e_ews_folder_get_parent_id (subf);
				if (!folder && (!parent_id || g_strcmp0 (parent_id->id, EWS_PUBLIC_FOLDER_ROOT_ID) == 0)) {
					folder = subf;
					break;
				} else if (parent_id && folder) {
					const EwsFolderId *fid = e_ews_folder_get_id (folder);

					if (fid && g_strcmp0 (fid->id, parent_id->id) == 0) {
						folder = subf;
						break;
					}
				}
			}

			piter = piter->next;
		}
	}

	if (!piter || folders[ii])
		folder = NULL;

	g_strfreev (folders);

	return folder;
}

/* ppath contains proposed path, this only makes sure that it's a unique path */
static void
ews_store_ensure_unique_path (CamelEwsStore *ews_store,
			      gchar **ppath)
{
	gboolean done;
	guint counter = 0;
	gchar *base_path = NULL;

	g_return_if_fail (ews_store != NULL);
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

static gboolean
ews_store_subscribe_folder_sync (CamelSubscribable *subscribable,
                                 const gchar *folder_name,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (subscribable);
	EEwsFolder *folder;
	const EwsFolderId *fid;
	gboolean res = TRUE;
	gchar *tmp;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Cannot subscribe EWS folders in offline mode"));
		return FALSE;
	}

	/* Folder's name is not stored with the "/" in the beginning, even when the
	   folder is in the root (same level as "Inbox", "Drafts", etc. */
	if (*folder_name == '/')
		folder_name++;

	/* can subscribe only public folders,
	   thus skip anything known */
	tmp = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_name);
	if (tmp) {
		g_free (tmp);
		return TRUE;
	}

	g_mutex_lock (&ews_store->priv->get_finfo_lock);
	if (!ews_store->priv->public_folders) {
		g_mutex_unlock (&ews_store->priv->get_finfo_lock);

		g_set_error (
			error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot subscribe folder “%s”, no public folder available"), folder_name);
		return FALSE;
	}

	folder = ews_store_find_public_folder (ews_store, folder_name);
	if (!folder) {
		g_mutex_unlock (&ews_store->priv->get_finfo_lock);

		g_set_error (
			error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot subscribe folder “%s”, folder not found"), folder_name);
		return FALSE;
	}

	fid = e_ews_folder_get_id (folder);
	if (!fid) {
		g_mutex_unlock (&ews_store->priv->get_finfo_lock);
		g_set_error (
			error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot subscribe folder “%s”, folder ID not found"), folder_name);
		return FALSE;
	}

	if (camel_ews_store_summary_has_folder (ews_store->summary, EWS_PUBLIC_FOLDER_ROOT_ID)) {
		gchar *parent_name = camel_ews_store_summary_get_folder_name (ews_store->summary, EWS_PUBLIC_FOLDER_ROOT_ID, NULL);

		if (!parent_name) {
			g_mutex_unlock (&ews_store->priv->get_finfo_lock);
			g_set_error (
				error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
				_("Cannot subscribe folder “%s”, public folder root not found"), folder_name);
			return FALSE;
		}

		tmp = g_strconcat (parent_name, "/", e_ews_folder_get_escaped_name (folder), NULL);
		g_free (parent_name);
	} else {
		tmp = g_strconcat (EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME, "/", e_ews_folder_get_escaped_name (folder), NULL);
	}

	if (e_ews_folder_get_folder_type (folder) != E_EWS_FOLDER_TYPE_MAILBOX) {
		CamelSettings *settings;
		CamelEwsSettings *ews_settings;

		settings = camel_service_ref_settings (CAMEL_SERVICE (ews_store));
		ews_settings = CAMEL_EWS_SETTINGS (settings);

		res = e_ews_folder_utils_add_as_esource (NULL,
			camel_ews_settings_get_hosturl (ews_settings),
			camel_network_settings_get_user (CAMEL_NETWORK_SETTINGS (ews_settings)),
			folder,
			E_EWS_ESOURCE_FLAG_OFFLINE_SYNC | E_EWS_ESOURCE_FLAG_PUBLIC_FOLDER,
			0,
			cancellable,
			error);

		g_object_unref (settings);
	}

	if (res) {
		ews_store_ensure_unique_path (ews_store, &tmp);

		camel_ews_store_summary_new_folder (ews_store->summary, fid->id, EWS_PUBLIC_FOLDER_ROOT_ID,
			NULL,
			strrchr (tmp, '/') + 1,
			e_ews_folder_get_folder_type (folder),
			CAMEL_FOLDER_SUBSCRIBED,
			e_ews_folder_get_total_count (folder),
			FALSE, TRUE);

		if (e_ews_folder_get_folder_type (folder) == E_EWS_FOLDER_TYPE_MAILBOX) {
			CamelFolderInfo *fi;

			camel_ews_store_ensure_virtual_folders (ews_store);

			fi = camel_ews_utils_build_folder_info (ews_store, fid->id);
			camel_store_folder_created (CAMEL_STORE (ews_store), fi);
			camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
			camel_folder_info_free (fi);
		}
	}

	camel_ews_store_summary_save (ews_store->summary, NULL);

	g_free (tmp);
	g_mutex_unlock (&ews_store->priv->get_finfo_lock);

	return res;
}

static gboolean
ews_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                   const gchar *folder_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (subscribable);
	EEwsFolderType folder_type;
	EEwsFolder *folder;
	gboolean is_public;
	gboolean res = TRUE;
	gchar *fid = NULL;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
		g_set_error_literal (
			error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Cannot unsubscribe EWS folders in offline mode"));
		return FALSE;
	}

	folder = ews_store_find_public_folder (ews_store, folder_name);
	if (folder) {
		const EwsFolderId *folder_id = e_ews_folder_get_id (folder);

		if (folder_id) {
			fid = g_strdup (folder_id->id);
			folder_type = e_ews_folder_get_folder_type (folder);
		}
	}

	if (!fid) {
		fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_name);
		if (fid)
			folder_type = camel_ews_store_summary_get_folder_type (ews_store->summary, fid, NULL);
	}

	if (!fid) {
		/* no such folder in the cache, might be unsubscribed already */
		return TRUE;
	}

	is_public = camel_ews_store_summary_get_public (ews_store->summary, fid, NULL);
	if (!is_public && !camel_ews_store_summary_get_foreign (ews_store->summary, fid, NULL)) {
		/* nothing to do for regular folders */
		res = TRUE;
	} else {
		CamelFolderInfo *fi;

		if (!is_public && camel_ews_store_summary_get_foreign_subfolders (ews_store->summary, fid, NULL)) {
			/* when subscribed with subfolders, then unsubscribe with subfolders as well */
			GSList *local_folders = NULL, *ii;
			gchar *full_name = camel_ews_store_summary_get_folder_full_name (ews_store->summary, fid, NULL);
			if (full_name) {
				local_folders = camel_ews_store_summary_get_folders (ews_store->summary, full_name, FALSE);
			}
			g_free (full_name);

			for (ii = local_folders; ii != NULL; ii = ii->next) {
				const gchar *lfid = ii->data;
				EEwsFolderType ftype;

				if (g_strcmp0 (lfid, fid) == 0)
					continue;

				ftype = camel_ews_store_summary_get_folder_type (ews_store->summary, lfid, NULL);
				if (ftype != E_EWS_FOLDER_TYPE_MAILBOX)
					continue;

				fi = camel_ews_utils_build_folder_info (ews_store, lfid);
				camel_ews_store_summary_remove_folder (ews_store->summary, lfid, NULL);

				camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
				camel_store_folder_deleted (CAMEL_STORE (ews_store), fi);
				camel_folder_info_free (fi);
			}

			g_slist_free_full (local_folders, g_free);
		}

		if (folder_type != E_EWS_FOLDER_TYPE_MAILBOX) {
			CamelSettings *settings;
			CamelEwsSettings *ews_settings;

			settings = camel_service_ref_settings (CAMEL_SERVICE (ews_store));
			ews_settings = CAMEL_EWS_SETTINGS (settings);

			res = e_ews_folder_utils_remove_as_esource (
				camel_ews_settings_get_hosturl (ews_settings),
				camel_network_settings_get_user (CAMEL_NETWORK_SETTINGS (ews_settings)),
				fid,
				cancellable,
				error);

			g_object_unref (settings);
		}

		if (res) {
			fi = camel_ews_utils_build_folder_info (ews_store, fid);
			camel_ews_store_summary_remove_folder (ews_store->summary, fid, error);

			if (folder_type == E_EWS_FOLDER_TYPE_MAILBOX) {
				camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), fi);
				camel_store_folder_deleted (CAMEL_STORE (ews_store), fi);
				camel_folder_info_free (fi);

				camel_ews_store_ensure_virtual_folders (ews_store);
			}
		}

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
                                  GError *error)
{
	CamelService *service;
	gboolean is_auth_failed;

	g_return_if_fail (store != NULL);

	if (!error)
		return;

	service = CAMEL_SERVICE (store);

	if (camel_service_get_connection_status (service) != CAMEL_SERVICE_CONNECTED)
		return;

	is_auth_failed = g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED);

	if (is_auth_failed || g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NORESPONSE))
		camel_service_disconnect_sync (service, FALSE, NULL, NULL);

	if (is_auth_failed) {
		ESourceRegistry *registry;

		error->domain = CAMEL_SERVICE_ERROR;
		error->code = CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE;

		registry = e_source_registry_new_sync (NULL, NULL);

		if (registry) {
			ESource *source, *collection = NULL;

			source = e_source_registry_ref_source (registry, camel_service_get_uid (service));

			if (source) {
				collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

				if (collection)
					e_source_emit_credentials_required (collection, E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, error);
			}

			g_clear_object (&collection);
			g_clear_object (&source);
			g_clear_object (&registry);
		}
	}
}

static void
ews_store_unset_oof_settings_state (CamelSession *session,
				    GCancellable *cancellable,
				    gpointer user_data,
				    GError **error)
{

	CamelEwsStore *ews_store = user_data;
	EEwsConnection *connection;
	EEwsOofSettings *oof_settings;
	EEwsOofState state;
	GError *local_error = NULL;

	camel_operation_push_message (cancellable, _("Unsetting the “Out of Office” status"));

	connection = camel_ews_store_ref_connection (ews_store);
	oof_settings = e_ews_oof_settings_new_sync (connection, cancellable, &local_error);
	g_object_unref (connection);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		camel_operation_pop_message (cancellable);
		return;
	}

	state = e_ews_oof_settings_get_state (oof_settings);
	if (state == E_EWS_OOF_STATE_DISABLED) {
		g_object_unref (oof_settings);
		camel_operation_pop_message (cancellable);
		return;
	}

	e_ews_oof_settings_set_state (oof_settings, E_EWS_OOF_STATE_DISABLED);
	e_ews_oof_settings_submit_sync (oof_settings, cancellable, error);
	g_object_unref (oof_settings);

	camel_operation_pop_message (cancellable);
}

void
camel_ews_store_unset_oof_settings_state (CamelEwsStore *ews_store)
{
	CamelService *service;
	CamelSession *session;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));

	service = CAMEL_SERVICE (ews_store);
	session = camel_service_ref_session (service);

	camel_session_submit_job (
		session, _("Unsetting the “Out of Office” status"),
		ews_store_unset_oof_settings_state,
		g_object_ref (ews_store),
		g_object_unref);

	g_object_unref (session);
}

static void
ews_store_dispose (GObject *object)
{
	CamelEwsStore *ews_store;
	CamelEwsSettings *ews_settings;

	ews_store = CAMEL_EWS_STORE (object);

	ews_settings = CAMEL_EWS_SETTINGS (camel_service_ref_settings (CAMEL_SERVICE (ews_store)));
	g_signal_handlers_disconnect_by_func (ews_settings, camel_ews_store_listen_notifications_cb, ews_store);
	g_signal_handlers_disconnect_by_func (ews_settings, camel_ews_store_check_all_cb, ews_store);
	g_object_unref (ews_settings);

	if (ews_store->summary != NULL) {
		camel_ews_store_summary_save (ews_store->summary, NULL);
		g_object_unref (ews_store->summary);
		ews_store->summary = NULL;
	}

	g_mutex_lock (&ews_store->priv->connection_lock);
	ews_store_unset_connection_locked (ews_store, FALSE);
	g_mutex_unlock (&ews_store->priv->connection_lock);

	g_slist_free_full (ews_store->priv->update_folder_names, g_free);
	ews_store->priv->update_folder_names = NULL;

	g_slist_free_full (ews_store->priv->public_folders, g_object_unref);
	ews_store->priv->public_folders = NULL;

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_ews_store_parent_class)->dispose (object);
}

static void
ews_store_finalize (GObject *object)
{
	CamelEwsStore *ews_store;

	ews_store = CAMEL_EWS_STORE (object);

	g_free (ews_store->storage_path);
	g_free (ews_store->priv->last_subscription_id);
	g_mutex_clear (&ews_store->priv->get_finfo_lock);
	g_mutex_clear (&ews_store->priv->connection_lock);
	g_rec_mutex_clear (&ews_store->priv->update_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_store_parent_class)->finalize (object);
}

static void
camel_ews_store_class_init (CamelEwsStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_store_set_property;
	object_class->get_property = ews_store_get_property;
	object_class->dispose = ews_store_dispose;
	object_class->finalize = ews_store_finalize;

	g_object_class_install_property (
		object_class,
		PROP_HAS_OOO_SET,
		g_param_spec_boolean (
			"has-ooo-set",
			"Has OOO Set",
			"Has Out of Office state set",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OOO_ALERT_STATE,
		g_param_spec_enum (
			"ooo-alert-state",
			"Out of Office Alert State",
			"The state of the Out of Office Alert",
			CAMEL_TYPE_EWS_STORE_OOO_ALERT_STATE,
			CAMEL_EWS_STORE_OOO_ALERT_STATE_UNKNOWN,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkService */
	g_object_class_override_property (
		object_class,
		PROP_CONNECTABLE,
		"connectable");

	/* Inherited from CamelNetworkService */
	g_object_class_override_property (
		object_class,
		PROP_HOST_REACHABLE,
		"host-reachable");

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
	store_class->initial_setup_sync = ews_initial_setup_sync;

	store_class->get_trash_folder_sync = ews_get_trash_folder_sync;
	store_class->get_junk_folder_sync = ews_get_junk_folder_sync;
	store_class->can_refresh_folder = ews_can_refresh_folder;
}

static void
camel_ews_subscribable_init (CamelSubscribableInterface *iface)
{
	iface->folder_is_subscribed = ews_store_folder_is_subscribed;
	iface->subscribe_folder_sync = ews_store_subscribe_folder_sync;
	iface->unsubscribe_folder_sync = ews_store_unsubscribe_folder_sync;
}

static void
camel_ews_store_init (CamelEwsStore *ews_store)
{
	ews_store->priv = camel_ews_store_get_instance_private (ews_store);

	ews_store->priv->last_refresh_time = time (NULL) - (FINFO_REFRESH_INTERVAL + 10);
	ews_store->priv->updates_cancellable = NULL;
	ews_store->priv->update_folder_names = NULL;
	ews_store->priv->subscription_key = 0;
	ews_store->priv->update_folder_id = 0;
	ews_store->priv->update_folder_list_id = 0;
	ews_store->priv->password_expires_in_days = -1;
	g_mutex_init (&ews_store->priv->get_finfo_lock);
	g_mutex_init (&ews_store->priv->connection_lock);
	g_rec_mutex_init (&ews_store->priv->update_lock);
}
