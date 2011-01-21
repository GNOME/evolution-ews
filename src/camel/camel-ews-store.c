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
	const gchar *email_id;
	gchar *summary_file, *session_storage_path;

	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;
	
	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_ews_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	/*storage path*/
	session_storage_path = camel_session_get_storage_path (session, service, error);
	if (!session_storage_path)
		return FALSE;
	email_id = camel_url_get_param (url, "email");
	ews_store->storage_path = g_build_filename (session_storage_path, email_id, NULL);
	g_free (session_storage_path);

	priv->host_url = g_strdup (camel_url_get_param (url, "hosturl"));
	if (!priv->host_url)
		return FALSE;

	g_mkdir_with_parents (ews_store->storage_path, 0700);
	summary_file = g_build_filename (ews_store->storage_path, "folder-tree", NULL);
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

static gboolean
ews_store_authenticate	(CamelService *service,
			 GCancellable *cancellable,
			 GError **error)
{
	CamelSession *session;
	CamelStore *store;
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	gboolean authenticated = FALSE;
	guint32 prompt_flags = CAMEL_SESSION_PASSWORD_SECRET;
	
	session = camel_service_get_session (service);
	store = CAMEL_STORE (service);
	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;
	service->url->passwd = NULL;

	while (!authenticated) {

		if (!service->url->passwd) {
			gchar *prompt;

			prompt = camel_session_build_password_prompt (
				"Exchange Web Services", service->url->user, service->url->host);
			service->url->passwd =
				camel_session_get_password (session, service, "Exchange Web Services",
							    prompt, "password", prompt_flags, error);
			g_free (prompt);

			if (!service->url->passwd) {
				g_set_error (
					error, G_IO_ERROR,
					G_IO_ERROR_CANCELLED,
					_("You did not enter a password."));
				return FALSE;
			}
		}

		priv->cnc = e_ews_connection_new (priv->host_url, service->url->user, service->url->passwd, error);
		if (*error) {
			/*FIXME check for the right code */
			if ((*error)->code == ERROR_PASSWORDEXPIRED) {
				/* We need to un-cache the password before prompting again */
				prompt_flags |= CAMEL_SESSION_PASSWORD_REPROMPT;
				g_free (service->url->passwd);
				service->url->passwd = NULL;
			} else
				return FALSE;
		} else
			authenticated = TRUE;

	}

	return TRUE;
}

static gboolean
ews_connect_sync (CamelService *service, GCancellable *cancellable, GError **error)
{
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

	if (!ews_store_authenticate (service, cancellable, error)) {
		camel_service_unlock (service, CAMEL_SERVICE_REC_CONNECT_LOCK);
		camel_service_disconnect_sync (service, TRUE, NULL);
		return FALSE;
	}
	
	service->status = CAMEL_SERVICE_CONNECTED;
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

static  GList*
ews_store_query_auth_types_sync (CamelService *service, GCancellable *cancellable, GError **error)
{
	GList *auth_types = NULL;

	d(printf("in query auth types\n"));
	auth_types = g_list_prepend (auth_types,  &camel_ews_password_authtype);
	return auth_types;
}

static CamelFolder *
ews_get_folder_sync (CamelStore *store, const gchar *folder_name, guint32 flags, GCancellable *cancellable, GError **error)
{
	CamelEwsStore *ews_store;
	CamelFolder *folder = NULL;


	ews_store = (CamelEwsStore *) store;

	if (camel_ews_store_summary_has_folder (ews_store->summary, folder_name)) {
		gchar *folder_dir;

		folder_dir = g_build_filename (ews_store->storage_path, "folders", folder_name, NULL);
		folder = camel_ews_folder_new (store, folder_name, folder_dir, cancellable, error);

		g_free (folder_dir);
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
	folders = camel_ews_store_summary_get_folders (ews_summary);

	if (!folders)
		return NULL;

	folder_infos = g_ptr_array_new ();

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		CamelFolderInfo *fi;

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
	EEwsConnection *cnc = (EEwsConnection *) obj;
	gchar *sync_state = NULL;
	GError *error = NULL;

	e_ews_connection_sync_folder_hierarchy_finish	(cnc, res, &sync_state, 
							 &folders_created, &folders_updated,
							 &folders_deleted, &error);

	if (error != NULL) {
		g_warning ("Unable to fetch the folder hierarchy: %s :%d \n", error->message, error->code);
		return;	
	}

	ews_utils_sync_folders (ews_store, folders_created, folders_deleted, folders_updated);
	camel_ews_store_summary_store_string_val (ews_store->summary, "sync_state", sync_state);
	ews_store->priv->last_refresh_time = time (NULL);

	g_slist_foreach (folders_created, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_deleted, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_updated, (GFunc) g_object_unref, NULL);
	g_slist_free (folders_created);
	g_slist_free (folders_deleted);
	g_slist_free (folders_updated);
	g_free (sync_state);
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
	const gchar *sync_state;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (ews_store)))
		return;

	if (!camel_service_connect_sync ((CamelService *) ews_store, &msg->error))
		return;
	
	sync_state = camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	e_ews_connection_sync_folder_hierarchy_start	(ews_store->priv->cnc, EWS_PRIORITY_MEDIUM, 
							 sync_state, ews_folder_hierarchy_ready_cb, 
							 NULL, ews_store);
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
ews_get_folder_info_sync (CamelStore *store, const gchar *top, guint32 flags, GCancellable *cancellable, GError **error)
{
	CamelEwsStore *ews_store;
	CamelEwsStorePrivate *priv;
	CamelFolderInfo *fi = NULL;
	gchar *sync_state;
	GSList *folders = NULL;
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL;
	gboolean initial_setup = FALSE;

	ews_store = (CamelEwsStore *) store;
	priv = ews_store->priv;

	g_mutex_lock (priv->get_finfo_lock);

	if (!(camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))
	    && camel_service_connect_sync ((CamelService *)store, error)))
		goto offline;
	
	folders = camel_ews_store_summary_get_folders (ews_store->summary);
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
		g_mutex_unlock (priv->get_finfo_lock);
		goto offline;
	}

	g_slist_foreach (folders, (GFunc)g_free, NULL);
	g_slist_free (folders);
	
	sync_state = (gchar *) camel_ews_store_summary_get_string_val (ews_store->summary, "sync_state", NULL);
	e_ews_connection_sync_folder_hierarchy	(ews_store->priv->cnc, EWS_PRIORITY_MEDIUM, &sync_state,
						 &folders_created, &folders_updated, &folders_deleted,
						 cancellable, error);
	if (*error != NULL) {
		g_warning ("Unable to fetch the folder hierarchy: %s :%d \n", (*error)->message, (*error)->code);
		return NULL;	
	}

	ews_utils_sync_folders (ews_store, folders_created, folders_deleted, folders_updated);
	camel_ews_store_summary_store_string_val (ews_store->summary, "sync_state", sync_state);
	ews_store->priv->last_refresh_time = time (NULL);

	g_slist_foreach (folders_created, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_deleted, (GFunc) g_object_unref, NULL);
	g_slist_foreach (folders_updated, (GFunc) g_object_unref, NULL);
	g_slist_free (folders_created);
	g_slist_free (folders_deleted);
	g_slist_free (folders_updated);
	g_free (sync_state);

	
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
	return NULL;
}

static gboolean
ews_delete_folder_sync	(CamelStore *store,
			 const gchar *folder_name,
			 GCancellable *cancellable,
			 GError **error)
{
	return TRUE;
}

static gboolean
ews_rename_folder_sync	(CamelStore *store,
			const gchar *old_name,
			const gchar *new_name,
			GCancellable *cancellable,
			GError **error)
{
	g_print ("Rename not implemented yet");
	return TRUE;

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

static CamelFolder *
ews_get_trash_folder_sync (CamelStore *store, GCancellable *cancellable, GError **error)
{
	return NULL;
}

static gboolean
ews_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error)
{
	return TRUE;
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
	service_class->query_auth_types_sync = ews_store_query_auth_types_sync;
	service_class->get_name = ews_get_name;
	service_class->connect_sync = ews_connect_sync;
	service_class->disconnect_sync = ews_disconnect_sync;

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
