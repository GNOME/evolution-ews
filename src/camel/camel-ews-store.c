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
#include <e-ews-compat.h>
#include <e-ews-item-change.h>
#include <e-ews-message.h>

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

	gchar *host_url;
	time_t last_refresh_time;
	GMutex *get_finfo_lock;
	EEwsConnection *cnc;
};

#if ! EDS_CHECK_VERSION(2,33,0)
static inline void camel_offline_store_set_online_sync(CamelOfflineStore *store,
						       gboolean online,
						       GCancellable *cancellable,
						       GError *error)
{
	camel_offline_store_set_network_state(store,
			online ? CAMEL_OFFLINE_STORE_NETWORK_AVAIL :
				 CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL,
			NULL);
}

static inline gboolean camel_offline_store_get_online(CamelOfflineStore *store)
{
	return (camel_offline_store_get_network_state(store, NULL) ==
		CAMEL_OFFLINE_STORE_NETWORK_AVAIL);
}
#endif

extern CamelServiceAuthType camel_ews_password_authtype; /*for the query_auth_types function*/

G_DEFINE_TYPE (CamelEwsStore, camel_ews_store, CAMEL_TYPE_OFFLINE_STORE)

static gboolean
ews_store_construct	(CamelService *service, CamelSession *session,
			 CamelProvider *provider, CamelURL *url,
			 GError **error)
{
	CamelServiceClass *service_class;
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	gchar *summary_file, *session_storage_path;

	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_ews_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	/*storage path*/
	session_storage_path = camel_session_get_storage_path (session, service, error);
	if (!session_storage_path) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Session has no storage path"));
		return FALSE;
	}
	ews_store->storage_path = session_storage_path;

	priv->host_url = g_strdup (camel_url_get_param (url, "hosturl"));
	if (!priv->host_url) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("EWS service has no host URL"));
		return FALSE;
	}

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
ews_store_authenticate	(EEwsConnection *cnc,
			 SoupMessage *msg, SoupAuth *auth,
			 gboolean retrying, gpointer data)
{
	CamelService *service = data;
	CamelSession *session = camel_service_get_session (service);
	GError *error = NULL;

	printf("%s\n", __func__);
	if (retrying)
		service->url->passwd = NULL;

	if (!service->url->passwd) {
		gchar *prompt;

		prompt = camel_session_build_password_prompt ("Exchange Web Services",
				      service->url->user, service->url->host);
		service->url->passwd =
		camel_session_get_password (session, service, "Exchange Web Services",
					    prompt, "password",
					    CAMEL_SESSION_PASSWORD_SECRET,
					    &error);
		g_free (prompt);
	}

	e_ews_connection_authenticate (cnc, auth, service->url->user,
				       service->url->passwd, error);
}

static gboolean
ews_connect_sync (CamelService *service, EVO3(GCancellable *cancellable,) GError **error)
{
	EVO2(GCancellable *cancellable = NULL;)
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;

	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;

	if (service->status == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (priv->cnc) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		return TRUE;
	}

	priv->cnc = e_ews_connection_new (priv->host_url, service->url->user, NULL,
					  G_CALLBACK (ews_store_authenticate), service,
					  error);

	if (!priv->cnc) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		EVO3_sync(camel_service_disconnect) (service, TRUE, NULL);
		return FALSE;
	}

	service->status = CAMEL_SERVICE_CONNECTED;
	camel_offline_store_set_online_sync (
		CAMEL_OFFLINE_STORE (ews_store), TRUE, cancellable, NULL);

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return TRUE;
}

static gboolean
ews_disconnect_sync (CamelService *service, gboolean clean, EVO3(GCancellable *cancellable,) GError **error)
{
	CamelEwsStore *ews_store = (CamelEwsStore *) service;
	CamelServiceClass *service_class;

	service_class = CAMEL_SERVICE_CLASS (camel_ews_store_parent_class);
	if (!service_class->EVO3_sync(disconnect) (service, clean, EVO3(cancellable,) error))
		return FALSE;

	camel_service_lock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	/* TODO cancel all operations in the connection */
	g_object_unref (ews_store->priv->cnc);
	ews_store->priv->cnc = NULL;

	camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);

	return TRUE;
}

static  GList*
ews_store_query_auth_types_sync (CamelService *service, EVO3(GCancellable *cancellable,) GError **error)
{
	GList *auth_types = NULL;

	d(printf("in query auth types\n"));
	auth_types = g_list_prepend (auth_types,  &camel_ews_password_authtype);
	return auth_types;
}

static CamelFolderInfo* ews_create_folder_sync (CamelStore *store, const gchar *parent_name,const gchar *folder_name,EVO3(GCancellable *cancellable,)GError **error);

static CamelFolder *
ews_get_folder_sync (CamelStore *store, const gchar *folder_name, guint32 flags, EVO3(GCancellable *cancellable,) GError **error)
{
	EVO2(GCancellable *cancellable = NULL;)
	CamelEwsStore *ews_store;
	CamelFolder *folder = NULL;
	CamelFolderInfo* fi;
	gchar *fid;

	ews_store = (CamelEwsStore *) store;

	/*check is this is a request for copy folder*/
	if (flags == CAMEL_STORE_FOLDER_CREATE){
		gchar *copy, *parent, *slash;
		const gchar *top;

		copy = strdup(folder_name);

		if ((slash = strrchr (copy, '/')) != NULL) {
			parent = copy;
			slash[0] = 0;
			top = slash + 1;
		} else {
			free(copy);
			parent = strdup("");
			top = folder_name;
		}

		fi = ews_create_folder_sync (store, parent, top, error);
		free(parent);

		if (!fi) {
			g_set_error(
				error, CAMEL_STORE_ERROR,
				CAMEL_ERROR_GENERIC,
				_("Cannot create folder: %s"), folder_name);
			return NULL;
		}
	}

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_name);

	if (fid) {
		gchar *folder_dir;

		folder_dir = g_build_filename (ews_store->storage_path, "folders", folder_name, NULL);
		folder = camel_ews_folder_new (store, folder_name, folder_dir, cancellable, error);

		g_free (folder_dir);
		g_free (fid);
	}

	if (folder == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return NULL;
	}

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

struct _store_sync_data
{
	CamelEwsStore *ews_store;

	/* Used if caller wants it to be  a sync call */
	EFlag *sync;
	GError **error;
};

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
ews_folder_hierarchy_ready_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	struct _store_sync_data *sync_data = (struct _store_sync_data *) user_data;
	CamelEwsStore *ews_store = sync_data->ews_store;
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

		if (!sync_data->sync) {
			g_mutex_lock (priv->get_finfo_lock);
			ews_store->priv->last_refresh_time -= FINFO_REFRESH_INTERVAL;
			g_mutex_unlock (priv->get_finfo_lock);
		}
		goto exit;
	}
	ews_update_folder_hierarchy (ews_store, sync_state, includes_last_folder,
				     folders_created, folders_deleted, folders_updated);

	if (!sync_data->sync) {
		g_mutex_lock (priv->get_finfo_lock);
		ews_store->priv->last_refresh_time = time (NULL);
		g_mutex_unlock (priv->get_finfo_lock);
	}

exit:
	if (sync_data->sync) {
		e_flag_set (sync_data->sync);
		if (error)
			g_propagate_error (sync_data->error, error);

	} else {
		g_free (sync_data);
		g_clear_error (&error);
	}
}

struct _ews_refresh_msg {
	CamelSessionThreadMsg msg;
	CamelStore *store;
};

static void
ews_refresh_finfo (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _ews_refresh_msg *m = (struct _ews_refresh_msg *)msg;
	CamelEwsStore *ews_store = (CamelEwsStore *) m->store;
	gchar *sync_state;
	struct _store_sync_data *sync_data;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store)))
		return;

	if (!EVO3_sync(camel_service_connect) ((CamelService *) ews_store, &msg->error))
		return;

	sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);

	sync_data = g_new0 (struct _store_sync_data, 1);
	sync_data->ews_store = ews_store;
	e_ews_connection_sync_folder_hierarchy_start	(ews_store->priv->cnc, EWS_PRIORITY_MEDIUM,
							 sync_state, ews_folder_hierarchy_ready_cb,
							 NULL, sync_data);
	g_free (sync_state);
}

static void
ews_refresh_free (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _ews_refresh_msg *m = (struct _ews_refresh_msg *)msg;

	g_object_unref (m->store);
}


static CamelSessionThreadOps ews_refresh_ops = {
	ews_refresh_finfo,
	ews_refresh_free,
};

static CamelFolderInfo *
ews_get_folder_info_sync (CamelStore *store, const gchar *top, guint32 flags, EVO3(GCancellable *cancellable,) GError **error)
{
	EVO2(GCancellable *cancellable = NULL;)
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	CamelFolderInfo *fi = NULL;
	gchar *sync_state;
	GSList *folders = NULL;
	gboolean initial_setup = FALSE;
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean includes_last_folder;

	ews_store = (CamelEwsStore *) store;
	priv = ews_store->priv;

	g_mutex_lock (priv->get_finfo_lock);
	if (!(camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))
	      && EVO3_sync(camel_service_connect) ((CamelService *)store, error))) {
		g_mutex_unlock (priv->get_finfo_lock);
		goto offline;
	}

	folders = camel_ews_store_summary_get_folders (ews_store->summary, NULL);
	if (!folders)
		initial_setup = TRUE;

	if (!initial_setup && flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
		time_t now = time (NULL);

		if (now - priv->last_refresh_time > FINFO_REFRESH_INTERVAL) {
			struct _ews_refresh_msg *m;

			m = camel_session_thread_msg_new (((CamelService *)store)->session, &ews_refresh_ops, sizeof (*m));
			m->store = g_object_ref (store);
			camel_session_thread_queue (((CamelService *)store)->session, &m->msg, 0);
		}

		ews_store->priv->last_refresh_time = time (NULL);
		g_mutex_unlock (priv->get_finfo_lock);
		goto offline;
	}

	g_slist_foreach (folders, (GFunc)g_free, NULL);
	g_slist_free (folders);

	sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);


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
		EVO3(GCancellable *cancellable,)
		GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	gchar *fid = NULL;
	gchar *full_name;
	EwsFolderId *folder_id;
	EVO2(GCancellable *cancellable = NULL;)
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
			 EVO3(GCancellable *cancellable,)
			 GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	gchar *fid;
	EVO2(GCancellable *cancellable = NULL;)
	CamelFolderInfo *fi = NULL;

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary,
							       folder_name);
	if (!fid) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Folder does not exist"));
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
			EVO3(GCancellable *cancellable,)
			GError **error)
{
	CamelEwsStore *ews_store = CAMEL_EWS_STORE (store);
	CamelEwsStoreSummary *ews_summary = ews_store->summary;
	CamelFolderInfo *fi;
	const gchar *old_slash, *new_slash;
	EVO2(GCancellable *cancellable = NULL;)
	gchar *fid;
	gchar *changekey;
	gboolean res = FALSE;

	if (!strcmp (old_name, new_name))
		return TRUE;

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_summary, old_name);
	if (!fid)
		return FALSE;

	changekey = camel_ews_store_summary_get_change_key (ews_summary, fid, error);
	if (!changekey) {
		g_free (fid);
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
			if (!pfid)
				goto out;
		}
		if (!e_ews_connection_move_folder (ews_store->priv->cnc, EWS_PRIORITY_MEDIUM,
						   pfid, fid, cancellable, error)) {
			g_free (pfid);
			goto out;
		}
		camel_ews_store_summary_set_parent_folder_id (ews_summary, fid, pfid);
		g_free (pfid);
	}

	fi = camel_ews_utils_build_folder_info (ews_store, fid);
	camel_store_folder_renamed (store, old_name, fi);
	res = TRUE;
 out:
	g_free (changekey);
	g_free (fid);
	return res;
}

gchar *
ews_get_name (CamelService *service, gboolean brief)
{
	if (brief)
		return g_strdup_printf(_("Exchange server %s"), service->url->host);
	else
		return g_strdup_printf(_("Exchange service for %s on %s"),
				       service->url->user, service->url->host);
}

EEwsConnection *
camel_ews_store_get_connection (CamelEwsStore *ews_store)
{
	return g_object_ref (ews_store->priv->cnc);
}

#if EDS_CHECK_VERSION (2,33,0)
static CamelFolder *
ews_get_trash_folder_sync (CamelStore *store, EVO3(GCancellable *cancellable,) GError **error)
{
	return NULL;
}
#endif

static gboolean
ews_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error)
{
	/* Skip unselectable folders from automatic refresh */
	if (info && (info->flags & CAMEL_FOLDER_NOSELECT) != 0) return FALSE;

	/* Delegate decision to parent class */
	return CAMEL_STORE_CLASS(camel_ews_store_parent_class)->can_refresh_folder (store, info, error) ||
			(camel_url_get_param (((CamelService *)store)->url, "check_all") != NULL);
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

	if (!EVO3_sync(camel_service_connect) ((CamelService *) ews_store, error))
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
	g_free (ews_store->priv->host_url);
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
	service_class->construct = ews_store_construct;
	service_class->EVO3_sync(query_auth_types) = ews_store_query_auth_types_sync;
	service_class->get_name = ews_get_name;
	service_class->EVO3_sync(connect) = ews_connect_sync;
	service_class->EVO3_sync(disconnect) = ews_disconnect_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->hash_folder_name = ews_hash_folder_name;
	store_class->compare_folder_name = ews_compare_folder_name;
	store_class->EVO3_sync(get_folder) = ews_get_folder_sync;
	store_class->EVO3_sync(create_folder) = ews_create_folder_sync;
	store_class->EVO3_sync(delete_folder) = ews_delete_folder_sync;
	store_class->EVO3_sync(rename_folder) = ews_rename_folder_sync;
	store_class->EVO3_sync(get_folder_info) = ews_get_folder_info_sync;
	store_class->free_folder_info = camel_store_free_folder_info_full;

	EVO3(store_class->get_trash_folder_sync = ews_get_trash_folder_sync;)
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
