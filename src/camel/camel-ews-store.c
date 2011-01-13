/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-store.c : class for an groupwise store */

/*
 *  Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *  parthasarathi susarla <sparthasarathi@novell.com>
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

struct _CamelEwsStorePrivate {

	gchar *host_url;
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
	gchar *summary_file;

	ews_store = (CamelEwsStore *) service;
	priv = ews_store->priv;
	
	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_ews_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	/*storage path*/
	ews_store->storage_path = camel_session_get_storage_path (session, service, error);
	if (!ews_store->storage_path)
		return FALSE;

	priv->host_url = g_strdup (camel_url_get_param (url, "hosturl"));
	if (!priv->host_url)
		return FALSE;

	summary_file = g_build_filename (ews_store->storage_path, "folder-tree", NULL);
	ews_store->summary = camel_ews_store_summary_new (ews_store->storage_path);
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
ews_connect_sync (CamelService *service, GCancellable *cancellable, GError **error)
{
	return TRUE;
}

static gboolean
ews_disconnect_sync (CamelService *service, gboolean clean, GCancellable *cancellable, GError **error)
{
	d(printf("in ews store disconnect\n"));

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
	return NULL;
}

gboolean
ews_store_reload_folder (CamelEwsStore *ews_store, CamelFolder *folder, guint32 flags, GCancellable *cancellable, GError **error)
{
	return TRUE;
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

static CamelFolderInfo *
ews_get_folder_info_sync (CamelStore *store, const gchar *top, guint32 flags, GCancellable *cancellable, GError **error)
{
	CamelFolderInfo *fi = NULL;

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		goto offline;

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
cnc_lookup (CamelEwsStorePrivate *priv)
{
	return priv->cnc;
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

/*
 * Function to check if we are both connected and are _actually_
 * online. Based on an equivalient function in IMAP
 */
gboolean
camel_ews_store_connected (CamelEwsStore *store, GCancellable *cancellable, GError **error)
{
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
}
