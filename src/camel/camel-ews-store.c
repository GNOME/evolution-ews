/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-store.c : class for an groupwise store */

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

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <libedataserver/e-flag.h>
#include <e-ews-item-change.h>
#include <e-ews-message.h>

#include "camel-ews-folder.h"
#include "camel-ews-settings.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"
#include "ews-esource-utils.h"

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
	GMutex *get_finfo_lock;
	EEwsConnection *cnc;
};

extern CamelServiceAuthType camel_ews_password_authtype; /*for the query_auth_types function*/
static gboolean	ews_store_construct	(CamelService *service, CamelSession *session,
			 		 CamelProvider *provider, GError **error);

static void camel_ews_store_initable_init (GInitableIface *interface);
static GInitableIface *parent_initable_interface;

G_DEFINE_TYPE_WITH_CODE (
	CamelEwsStore, camel_ews_store, CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE, camel_ews_store_initable_init))

static gboolean
ews_store_initable_init		(GInitable *initable,
				 GCancellable *cancellable,
				 GError **error)
{
	CamelService *service;
	CamelSession *session;
	gboolean ret;
	
	service = CAMEL_SERVICE (initable);
	session = camel_service_get_session (service);

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
ews_store_construct	(CamelService *service, CamelSession *session,
			 CamelProvider *provider, GError **error)
{
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	gchar *summary_file, *session_storage_path;


	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;

	/* Disable virtual trash and junk folders. Exchange has real
	   folders for that */
	((CamelStore *)ews_store)->flags &= ~(CAMEL_STORE_VTRASH|CAMEL_STORE_VJUNK);

	/*storage path*/
	session_storage_path = g_strdup (camel_service_get_user_data_dir (service));
	if (!session_storage_path) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Session has no storage path"));
		return FALSE;
	}
	ews_store->storage_path = session_storage_path;

	/* Note. update account-listener plugin if filename is changed here, as it would remove the summary
	   by forming the path itself */
	g_mkdir_with_parents (ews_store->storage_path, 0700);
	summary_file = g_build_filename (ews_store->storage_path, "folder-tree-v2", NULL);
	ews_store->summary = camel_ews_store_summary_new (summary_file);
	camel_ews_store_summary_load (ews_store->summary, NULL);

	g_free (summary_file);
	return TRUE;
}

static guint
ews_hash_folder_name (gconstpointer key)
{
	return g_str_hash (key);
}

static gint
ews_compare_folder_name (gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	return g_str_equal (aname, bname);
}

static void
ews_update_folder_hierarchy (CamelEwsStore *ews_store, gchar *sync_state,
			     gboolean includes_last_folder, GSList *folders_created,
			     GSList *folders_deleted, GSList *folders_updated)
{
	ews_utils_sync_folders (ews_store, folders_created, folders_deleted, folders_updated);
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

static void
ews_store_authenticate	(EEwsConnection *cnc,
			 SoupMessage *msg, SoupAuth *auth,
			 gboolean retrying, gpointer data)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelService *service = data;
	const gchar *password;
	const gchar *user;

	password = camel_service_get_password (service);
	settings = camel_service_get_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_get_user (network_settings);

	g_return_if_fail (password != NULL);

	e_ews_connection_authenticate (cnc, auth, user, password, NULL);
}

static gboolean
ews_connect_sync (CamelService *service, GCancellable *cancellable, GError **error)
{
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	CamelEwsSettings *ews_settings;
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelSession *session;
	const gchar *hosturl;
	const gchar *user;
	gboolean success;

	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;
	session = camel_service_get_session (service);
	settings = camel_service_get_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_get_user (network_settings);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	hosturl = camel_ews_settings_get_hosturl (ews_settings);

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (priv->cnc) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		return TRUE;
	}

	priv->cnc = e_ews_connection_new (hosturl, user, NULL,
					  G_CALLBACK (ews_store_authenticate), service,
					  error);

	if (!priv->cnc) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		camel_service_disconnect_sync (service, TRUE, NULL);
		return FALSE;
	}

	/* Try running an operation that requires authentication
	 * to make sure we have a valid password available. */
	success = camel_session_authenticate_sync (
		session, service, NULL, cancellable, error);

	if (!success) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		camel_service_disconnect_sync (service, TRUE, NULL);
		return FALSE;
	}

	camel_offline_store_set_online_sync (
		CAMEL_OFFLINE_STORE (ews_store), TRUE, cancellable, NULL);

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return TRUE;
}

static gboolean
ews_disconnect_sync (CamelService *service, gboolean clean, GCancellable *cancellable, GError **error)
{
	CamelEwsStore *ews_store = (CamelEwsStore *) service;
	CamelServiceClass *service_class;

	service_class = CAMEL_SERVICE_CLASS (camel_ews_store_parent_class);
	if (!service_class->disconnect_sync (service, clean, cancellable, error))
		return FALSE;

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	/* TODO cancel all operations in the connection */
	g_object_unref (ews_store->priv->cnc);
	ews_store->priv->cnc = NULL;

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return TRUE;
}

static CamelAuthenticationResult
ews_authenticate_sync (CamelService *service, const gchar *mechanism, GCancellable *cancellable, GError **error)
{
	CamelAuthenticationResult result;
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	GSList *folders_created = NULL;
	GSList *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder = FALSE;
	gchar *sync_state = NULL;
	GError *local_error = NULL;

	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;

	/* XXX We need to run some operation that requires authentication
	 *     but does not change any server-side state, so we can check
	 *     the error status and determine if our password is valid.
	 *     David suggested e_ews_connection_sync_folder_hierarchy(),
	 *     since we have to do that eventually anyway. */

	e_ews_connection_sync_folder_hierarchy (
		priv->cnc, EWS_PRIORITY_MEDIUM,
		&sync_state, &includes_last_folder,
		&folders_created, &folders_updated, &folders_deleted,
		cancellable, &local_error);

	if (local_error == NULL) {
		/* This consumes all allocated result data. */
		ews_update_folder_hierarchy (
			ews_store, sync_state, includes_last_folder,
			folders_created, folders_updated, folders_deleted);
	} else {
		/* Make sure we're not leaking anything. */
		g_warn_if_fail (sync_state == NULL);
		g_warn_if_fail (folders_created == NULL);
		g_warn_if_fail (folders_updated == NULL);
		g_warn_if_fail (folders_deleted == NULL);
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

	return result;
}

static  GList*
ews_store_query_auth_types_sync (CamelService *service, GCancellable *cancellable, GError **error)
{
	GList *auth_types = NULL;

	d(printf("in query auth types\n"));
	auth_types = g_list_prepend (auth_types,  &camel_ews_password_authtype);
	return auth_types;
}

static CamelFolderInfo* ews_create_folder_sync (CamelStore *store, const gchar *parent_name,const gchar *folder_name, GCancellable *cancellable, GError **error);

static CamelFolder *
ews_get_folder_sync (CamelStore *store, const gchar *folder_name, guint32 flags, GCancellable *cancellable, GError **error)
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
		g_set_error (error, CAMEL_STORE_ERROR,
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
folder_info_from_store_summary (CamelEwsStore *store, const gchar *top, guint32 flags, GError **error)
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
		gint64 ftype;

		ftype = camel_ews_store_summary_get_folder_type (ews_summary, l->data, NULL);
		if (ftype != EWS_FOLDER_TYPE_MAILBOX)
			continue;

		fi = camel_ews_utils_build_folder_info (store, l->data);
		g_ptr_array_add	(folder_infos, fi);
	}

	root_fi = camel_folder_info_build (folder_infos, top, '/', TRUE);

	g_ptr_array_free (folder_infos, TRUE);
	g_slist_foreach (folders, (GFunc) g_free, NULL);
	g_slist_free (folders);

	return root_fi;
}

static void
ews_folder_hierarchy_ready_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	CamelEwsStore *ews_store = (CamelEwsStore *) user_data;
	CamelEwsStorePrivate *priv = ews_store->priv;
	EEwsConnection *cnc = (EEwsConnection *) obj;
	gchar *sync_state = NULL;
	gboolean includes_last_folder;
	GError *error = NULL;

	e_ews_connection_sync_folder_hierarchy_finish	(cnc, res, &sync_state, &includes_last_folder,
							 &folders_created, &folders_updated,
							 &folders_deleted, &error);

	if (error != NULL) {
		g_warning ("Unable to fetch the folder hierarchy: %s :%d \n", error->message, error->code);

		g_mutex_lock (priv->get_finfo_lock);
		ews_store->priv->last_refresh_time -= FINFO_REFRESH_INTERVAL;
		g_mutex_unlock (priv->get_finfo_lock);
		goto exit;
	}
	ews_update_folder_hierarchy (ews_store, sync_state, includes_last_folder,
				     folders_created, folders_deleted, folders_updated);

	g_mutex_lock (priv->get_finfo_lock);
	ews_store->priv->last_refresh_time = time (NULL);
	g_mutex_unlock (priv->get_finfo_lock);

exit:
	g_object_unref (ews_store);
	g_clear_error (&error);
}

static gboolean
ews_refresh_finfo (CamelEwsStore *ews_store)
{
	gchar *sync_state;
	
	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store)))
		return FALSE;

	if (!camel_service_connect_sync ((CamelService *) ews_store, NULL))
		return FALSE;

	sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);


	e_ews_connection_sync_folder_hierarchy_start	(ews_store->priv->cnc, EWS_PRIORITY_MEDIUM,
							 sync_state, ews_folder_hierarchy_ready_cb,
							 NULL, g_object_ref (ews_store));
	g_free (sync_state);
	return TRUE;
}

static CamelFolderInfo *
ews_get_folder_info_sync (CamelStore *store, const gchar *top, guint32 flags, GCancellable *cancellable, GError **error)
{
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	CamelFolderInfo *fi = NULL;
	gchar *sync_state;
	gboolean initial_setup = FALSE;
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder;

	ews_store = (CamelEwsStore *) store;
	priv = ews_store->priv;

	g_mutex_lock (priv->get_finfo_lock);
	if (!(camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))
	      && camel_service_connect_sync ((CamelService *)store, error))) {
		g_mutex_unlock (priv->get_finfo_lock);
		goto offline;
	}

	sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	if (!sync_state)
		initial_setup = TRUE;

	if (!initial_setup && flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
		time_t now = time (NULL);
		
		g_free (sync_state);
		if (now - priv->last_refresh_time > FINFO_REFRESH_INTERVAL && ews_refresh_finfo (ews_store))
			ews_store->priv->last_refresh_time = time (NULL);

		g_mutex_unlock (priv->get_finfo_lock);
		goto offline;
	}

	if (!e_ews_connection_sync_folder_hierarchy (ews_store->priv->cnc, EWS_PRIORITY_MEDIUM,
						    &sync_state, &includes_last_folder,
						    &folders_created, &folders_updated,
						    &folders_deleted, cancellable, error)) {
		if (error)
			g_warning ("Unable to fetch the folder hierarchy: %s :%d \n",
				   (*error)->message, (*error)->code);
		else
			g_warning ("Unable to fetch the folder hierarchy.\n");

		g_mutex_unlock (priv->get_finfo_lock);
		return NULL;
	}
	ews_update_folder_hierarchy (ews_store, sync_state, includes_last_folder,
				     folders_created, folders_deleted, folders_updated);
	g_mutex_unlock (priv->get_finfo_lock);

offline:
	fi = folder_info_from_store_summary ( (CamelEwsStore *) store, top, flags, error);
	return fi;
}

static CamelFolderInfo*
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
	CamelFolderInfo *fi = NULL;

	
	/* Get Parent folder ID */
	if (parent_name && parent_name[0]) {
		fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary,
								       parent_name);
		if (!fid) {
			g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				     _("Parent folder %s does not exist"), parent_name);
			return NULL;
		}
	}

	if (!ews_store->priv->cnc) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Cant perform actions on the folder while in offline mode"));
		if (fid) g_free (fid);
		return NULL;
	}

	/* Make the call */
	if (!e_ews_connection_create_folder (ews_store->priv->cnc,
					     EWS_PRIORITY_MEDIUM, fid,
					     FALSE, folder_name, &folder_id,
					     cancellable, error)) {
		g_free (fid);
		return NULL;
	}

	/* Translate & store returned folder id */
	if (fid)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	camel_ews_store_summary_new_folder (ews_summary, folder_id->id,
					    fid, folder_id->change_key,
					    folder_name,
					    EWS_FOLDER_TYPE_MAILBOX,
					    0, 0);
	fi = camel_ews_utils_build_folder_info (ews_store, folder_id->id);
	e_ews_folder_free_fid (folder_id);

	camel_store_folder_created (store, fi);

	g_free (full_name);
	g_free (fid);
	return fi;
}

static gboolean
ews_delete_folder_sync	(CamelStore *store,
			 const gchar *folder_name,
			 GCancellable *cancellable,
			 GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	gchar *fid;
	CamelFolderInfo *fi = NULL;

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary,
							       folder_name);
	if (!fid) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Folder does not exist"));
		return FALSE;
	}

	if (!ews_store->priv->cnc) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Cant perform actions on the folder while in offline mode"));
		g_free (fid);
		return FALSE;
	}

	if (!e_ews_connection_delete_folder (ews_store->priv->cnc,
					     EWS_PRIORITY_MEDIUM,
					     fid, FALSE, "HardDelete",
					     cancellable, error)) {
		g_free (fid);
		return FALSE;
	}

	fi = camel_ews_utils_build_folder_info (ews_store, fid);
	camel_ews_store_summary_remove_folder (ews_summary, fid, error);

	camel_store_folder_deleted (store, fi);
	camel_folder_info_free (fi);

	g_free (fid);

	return TRUE;
}

struct _rename_cb_data {
	const gchar *display_name;
	const gchar *change_key;
	const gchar *folder_id;
};

static void rename_folder_cb (ESoapMessage *msg, gpointer user_data)
{
	struct _rename_cb_data *rename_data = user_data;

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_FOLDER,
					 rename_data->folder_id, rename_data->change_key, 0);
	e_soap_message_start_element (msg, "SetFolderField", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL,
							      "FieldURI", "folder:DisplayName");

	e_soap_message_start_element (msg, "Folder", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", NULL, rename_data->display_name);
	e_soap_message_end_element (msg); /* Folder */

	e_soap_message_end_element (msg); /* SetFolderField */

	e_ews_message_end_item_change (msg);
}

static gboolean
ews_rename_folder_sync	(CamelStore *store,
			const gchar *old_name,
			const gchar *new_name,
			GCancellable *cancellable,
			GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	const gchar *old_slash, *new_slash;
	gchar *fid;
	gchar *changekey;
	gboolean res = FALSE;

	if (!strcmp (old_name, new_name))
		return TRUE;

	if (!ews_store->priv->cnc) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Cant perform actions on the folder while in offline mode"));
		return FALSE;
	}

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary, old_name);
	if (!fid) {
		g_set_error (error, CAMEL_STORE_ERROR,
			     CAMEL_STORE_ERROR_NO_FOLDER,
			     _("Folder %s does not exist"), old_name);
		return FALSE;
	}

	changekey = camel_ews_store_summary_get_change_key (ews_summary, fid, error);
	if (!changekey) {
		g_free (fid);
		g_set_error (error, CAMEL_STORE_ERROR,
			     CAMEL_STORE_ERROR_NO_FOLDER,
			     _("No change key record for folder %s"), fid);
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

	if (strcmp (old_slash, new_slash)) {
		int parent_len = old_slash - old_name;
		struct _rename_cb_data *rename_data;

		/* Folder basename changed (i.e. UpdateFolder needed).
		   Therefore, we can only do it if the folder hasn't also
		   been moved from one parent folder to another.

		   Strictly speaking, we could probably handle this, even
		   if there are name collisions. We could UpdateFolder to
		   a new temporary name that doesn't exist in either the
		   old or new parent folders, then MoveFolder, then issue
		   another UpdateFolder to the name we actually wanted.
		   But since the Evolution UI doesn't seem to let us
		   make both changes at the same time anyway, we'll just
		   bail out for now; we can deal with it later if we need
		   to.
		*/
		if (new_slash - new_name != parent_len ||
		    strncmp (old_name, new_name, parent_len)) {
			g_set_error (error, CAMEL_STORE_ERROR,
				     CAMEL_STORE_ERROR_INVALID,
				     _("Cannot both rename and move a folder at the same time"));
			g_free (changekey);
			goto out;
		}

		rename_data = g_new0 (struct _rename_cb_data, 1);
		rename_data->display_name = new_slash;
		rename_data->folder_id = fid;
		rename_data->change_key = changekey;

		if (!e_ews_connection_update_folder (ews_store->priv->cnc, EWS_PRIORITY_MEDIUM,
						     rename_folder_cb, rename_data, cancellable, error)) {
			g_free (rename_data);
			goto out;
		}
		g_free (rename_data);
		camel_ews_store_summary_set_folder_name (ews_summary, fid, new_slash);
	} else {
		gchar *pfid = NULL;
		gchar *parent_name;

		/* If we are not moving to the root folder, work out the ItemId of
		   the new parent folder */
		if (new_slash != new_name) {
			parent_name = g_strndup (new_name, new_slash - new_name - 1);
			pfid = camel_ews_store_summary_get_folder_id_from_name (ews_summary,
										parent_name);
			g_free (parent_name);
			if (!pfid) {
				g_set_error (error, CAMEL_STORE_ERROR,
					     CAMEL_STORE_ERROR_NO_FOLDER,
					     _("Cannot find folder ID for parent folder %s"),
					     parent_name);
				goto out;
			}
		}
		if (!e_ews_connection_move_folder (ews_store->priv->cnc, EWS_PRIORITY_MEDIUM,
						   pfid, fid, cancellable, error)) {
			g_free (pfid);
			goto out;
		}
		camel_ews_store_summary_set_parent_folder_id (ews_summary, fid, pfid);
		g_free (pfid);
	}

	res = TRUE;
 out:
	g_free (changekey);
	g_free (fid);
	return res;
}

gchar *
ews_get_name (CamelService *service, gboolean brief)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	const gchar *host;
	const gchar *user;

	settings = camel_service_get_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_get_host (network_settings);
	user = camel_network_settings_get_user (network_settings);

	if (brief)
		return g_strdup_printf(_("Exchange server %s"), host);
	else
		return g_strdup_printf(_("Exchange service for %s on %s"),
				       user, host);
}

EEwsConnection *
camel_ews_store_get_connection (CamelEwsStore *ews_store)
{
	return g_object_ref (ews_store->priv->cnc);
}

static CamelFolder *
ews_get_trash_folder_sync (CamelStore *store, GCancellable *cancellable, GError **error)
{
	return NULL;
}

static gboolean
ews_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error)
{
	/* Skip unselectable folders from automatic refresh */
	if (info && (info->flags & CAMEL_FOLDER_NOSELECT) != 0) return FALSE;

	/* Delegate decision to parent class */
	return CAMEL_STORE_CLASS(camel_ews_store_parent_class)->can_refresh_folder (store, info, error) ||
		camel_ews_settings_get_check_all (CAMEL_EWS_SETTINGS (camel_service_get_settings (CAMEL_SERVICE (store))));
}

gboolean
camel_ews_store_connected (CamelEwsStore *ews_store, GError **error)
{

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (!camel_service_connect_sync ((CamelService *) ews_store, error))
		return FALSE;

	return TRUE;
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

	if (ews_store->priv->cnc != NULL) {
		g_object_unref (ews_store->priv->cnc);
		ews_store->priv->cnc = NULL;
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
	g_mutex_free (ews_store->priv->get_finfo_lock);

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
	store_class->hash_folder_name = ews_hash_folder_name;
	store_class->compare_folder_name = ews_compare_folder_name;
	store_class->get_folder_sync = ews_get_folder_sync;
	store_class->create_folder_sync = ews_create_folder_sync;
	store_class->delete_folder_sync = ews_delete_folder_sync;
	store_class->rename_folder_sync = ews_rename_folder_sync;
	store_class->get_folder_info_sync = ews_get_folder_info_sync;
	store_class->free_folder_info = camel_store_free_folder_info_full;

	store_class->get_trash_folder_sync = ews_get_trash_folder_sync;
	store_class->can_refresh_folder = ews_can_refresh_folder;
}

static void
camel_ews_store_init (CamelEwsStore *ews_store)
{
	ews_store->priv =
		CAMEL_EWS_STORE_GET_PRIVATE (ews_store);

	ews_store->priv->cnc = NULL;
	ews_store->priv->last_refresh_time = time (NULL) - (FINFO_REFRESH_INTERVAL + 10);
	ews_store->priv->get_finfo_lock = g_mutex_new ();
}
