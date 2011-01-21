/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-folder.c: class for an groupwise folder */

/*
 * Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *  parthasarathi susarla <sparthasarathi@novell.com>
 *  Sankar P <psankar@novell.com>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

/* This file is broken and suffers from multiple author syndrome.
This needs to be rewritten with a lot of functions cleaned up.

There are a lot of places where code is unneccesarily duplicated,
which needs to be better organized via functions */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include <e-ews-connection.h>

#include "camel-ews-folder.h"
#include "camel-ews-private.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"

#define JUNK_FOLDER "Junk Mail"
#define EWS_MAX_FETCH_COUNT 100
#define MAX_ATTACHMENT_SIZE 1*1024*1024   /*In bytes*/
#define SUMMARY_DEFAULT_PROPS	"IdOnly"
#define SUMMARY_ADD_PROPS	"item:Subject item:DateTimeReceived item:DateTimeSent item:DateTimeCreated item:Size item:HasAttachments message:InternetMessageId message:From message:Sender message:ToRecipients message:CcRecipient message:BccRecipients message:IsRead item:MimeContent"

#define CAMEL_EWS_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_FOLDER, CamelEwsFolderPrivate))

struct _CamelEwsFolderPrivate {
	GMutex *search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */

	/* For syncronizing refresh_info/sync_changes */
	gboolean refreshing;
	GMutex *refresh_lock;
};

extern gint camel_application_is_exiting;

#define d(x)

G_DEFINE_TYPE (CamelEwsFolder, camel_ews_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static gchar *
ews_get_filename (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelEwsFolder *ews_folder = CAMEL_EWS_FOLDER(folder);

	return camel_data_cache_get_filename (ews_folder->cache, "cache", uid, error);
}

/* Get a message from cache if available otherwise get it from server */
static CamelMimeMessage *
ews_folder_get_message_sync ( CamelFolder *folder, const gchar *uid, GCancellable *cancellable, GError **error )
{
	g_print ("\n Get message not implemented");
	
	return NULL;
}

/* code to rename a folder. all the "meta nonsense" code should simply go away */
static void
ews_folder_rename (CamelFolder *folder, const gchar *new)
{
	g_print ("\n Folder Rename not implemented");
	
}

static GPtrArray *
ews_folder_search_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	GPtrArray *matches;

	ews_folder = CAMEL_EWS_FOLDER (folder);
	priv = ews_folder->priv;

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (ews_folder->search, folder);
	matches = camel_folder_search_search (ews_folder->search, expression, NULL, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static guint32
ews_folder_count_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	guint32 matches;

	ews_folder = CAMEL_EWS_FOLDER (folder);
	priv = ews_folder->priv;

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (ews_folder->search, folder);
	matches = camel_folder_search_count (ews_folder->search, expression, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static GPtrArray *
ews_folder_search_by_uids(CamelFolder *folder, const gchar *expression, GPtrArray *uids, GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	GPtrArray *matches;

	ews_folder = CAMEL_EWS_FOLDER (folder);
	priv = ews_folder->priv;

	if (uids->len == 0)
		return g_ptr_array_new ();

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (ews_folder->search, folder);
	matches = camel_folder_search_search (ews_folder->search, expression, uids, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static void
ews_folder_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;

	ews_folder = CAMEL_EWS_FOLDER (folder);
	priv = ews_folder->priv;
	
	g_return_if_fail (ews_folder->search);
	
	g_mutex_lock (priv->search_lock);

	camel_folder_search_free_result (ews_folder->search, uids);

	g_mutex_unlock (priv->search_lock);

	return;
}

/********************* folder functions*************************/

static gboolean
ews_synchronize_sync (CamelFolder *folder, gboolean expunge, GCancellable *cancellable, GError **error)
{
	g_print ("\n You better write a good sync for EWS :)");
	
	return TRUE;
}

CamelFolder *
camel_ews_folder_new (CamelStore *store, const gchar *folder_name, const gchar *folder_dir, GCancellable *cancellable, GError **error)
{
	CamelFolder *folder;
	CamelEwsStore *ews_store;
	CamelEwsFolder *ews_folder;
	gchar *summary_file, *state_file;
	const gchar *short_name;

	ews_store = (CamelEwsStore *) store;

	short_name = camel_ews_store_summary_get_folder_name (ews_store->summary, folder_name, NULL);

	folder = g_object_new (
		CAMEL_TYPE_EWS_FOLDER,
		"name", short_name, "full-name", folder_name,
		"parent_store", store, NULL);

	ews_folder = CAMEL_EWS_FOLDER(folder);

	summary_file = g_build_filename (folder_dir, "summary", NULL);
	folder->summary = camel_ews_summary_new(folder, summary_file);
	g_free(summary_file);

	if (!folder->summary) {
		g_object_unref (CAMEL_OBJECT (folder));
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load summary for %s"), folder_name);
		return NULL;
	}

	/* set/load persistent state */
	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));
	g_free(state_file);

	ews_folder->cache = camel_data_cache_new (folder_dir, error);
	if (!ews_folder->cache) {
		g_object_unref (folder);
		return NULL;
	}

	if (!g_ascii_strcasecmp (folder_name, "Inbox")) {
		if (camel_url_get_param (((CamelService *) store)->url, "filter"))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
	}

	ews_folder->search = camel_folder_search_new ();
	if (!ews_folder->search) {
		g_object_unref (folder);
		return NULL;
	}

	return folder;
}

static void
ews_items_ready_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc;
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	CamelEwsStore *ews_store;
	GSList *items_created = NULL, *items_updated = NULL;
	GSList *items_deleted = NULL;
	gchar *sync_state = NULL;
	GError *error = NULL;

	cnc = (EEwsConnection *) obj;
	ews_folder = (CamelEwsFolder *) user_data;
	priv = ews_folder->priv;
	ews_store = (CamelEwsStore *) camel_folder_get_parent_store ((CamelFolder *) ews_folder);

	e_ews_connection_sync_folder_items_finish	(cnc, res, &sync_state,
							 &items_created, &items_updated,
							 &items_deleted, &error);

	g_slist_foreach (items_created, (GFunc) g_object_unref, NULL);
	g_slist_foreach (items_deleted, (GFunc) g_object_unref, NULL);
	g_slist_foreach (items_updated, (GFunc) g_object_unref, NULL);
	g_slist_free (items_created);
	g_slist_free (items_deleted);
	g_slist_free (items_updated);
	g_free (sync_state);
	g_object_unref (cnc);
}

static gboolean
ews_refresh_info_sync (CamelFolder *folder, GCancellable *cancellable, GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	EEwsConnection *cnc;
	CamelEwsStore *ews_store;
	const gchar *full_name, *sync_state, *id;

	full_name = camel_folder_get_full_name (folder);
	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	ews_folder = (CamelEwsFolder *) folder;
	priv = ews_folder->priv;

	if (!camel_ews_store_connected (ews_store, error))
		return FALSE;

	g_mutex_lock (priv->refresh_lock);

	if (priv->refreshing)
		goto exit;

	priv->refreshing = TRUE;
	
	cnc = camel_ews_store_get_connection (ews_store);
	sync_state = camel_ews_store_summary_get_sync_state	
						(ews_store->summary,
						 full_name, NULL);
	id = camel_ews_store_summary_get_folder_id
						(ews_store->summary,
						 full_name, NULL);
	e_ews_connection_sync_folder_items_start	
						(cnc, EWS_PRIORITY_MEDIUM,
						 sync_state, id, 
						 SUMMARY_DEFAULT_PROPS, SUMMARY_ADD_PROPS,
						 EWS_MAX_FETCH_COUNT, 
						 ews_items_ready_cb,
						 cancellable, error);

exit:
	g_mutex_unlock (priv->refresh_lock);
	return TRUE;
}

static gboolean
ews_append_message_sync (CamelFolder *folder, CamelMimeMessage *message,
		CamelMessageInfo *info, gchar **appended_uid,
		GCancellable *cancellable, GError **error)
{
	g_print ("\n append_message not implemented");

	return TRUE;
}

/* move messages */
static gboolean
ews_transfer_messages_to_sync	(CamelFolder *source, 
				 GPtrArray *uids,
				 CamelFolder *destination, 
				 gboolean delete_originals, 
				 GPtrArray **transferred_uids,
				 GCancellable *cancellable, 
				 GError **error)
{

	g_print ("\n transfer_messages_to not implemented");

	return TRUE;
}

static gboolean
ews_expunge_sync (CamelFolder *folder, GCancellable *cancellable, GError **error)
{
	g_print ("\n expunge not implemented");

	return TRUE;
}

static gint
ews_cmp_uids (CamelFolder *folder, const gchar *uid1, const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return strcmp (uid1, uid2);
}

static void
ews_folder_dispose (GObject *object)
{
	CamelEwsFolder *ews_folder = CAMEL_EWS_FOLDER (object);

	if (ews_folder->cache != NULL) {
		g_object_unref (ews_folder->cache);
		ews_folder->cache = NULL;
	}

	if (ews_folder->search != NULL) {
		g_object_unref (ews_folder->search);
		ews_folder->search = NULL;
	}

	g_mutex_free (ews_folder->priv->search_lock);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_ews_folder_parent_class)->dispose (object);
}

static void
ews_folder_constructed (GObject *object)
{
	CamelFolder *folder;
	CamelStore *parent_store;
	CamelURL *url;
	const gchar *full_name;
	gchar *description;

	folder = CAMEL_FOLDER (object);
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	url = CAMEL_SERVICE (parent_store)->url;

	description = g_strdup_printf (
		"%s@%s:%s", url->user, url->host, full_name);
	camel_folder_set_description (folder, description);
	g_free (description);
}

static void
camel_ews_folder_class_init (CamelEwsFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelEwsFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = ews_folder_dispose;
	object_class->constructed = ews_folder_constructed;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->get_message_sync = ews_folder_get_message_sync;
	folder_class->rename = ews_folder_rename;
	folder_class->search_by_expression = ews_folder_search_by_expression;
	folder_class->count_by_expression = ews_folder_count_by_expression;
	folder_class->cmp_uids = ews_cmp_uids;
	folder_class->search_by_uids = ews_folder_search_by_uids;
	folder_class->search_free = ews_folder_search_free;
	folder_class->append_message_sync = ews_append_message_sync;
	folder_class->refresh_info_sync = ews_refresh_info_sync;
	folder_class->synchronize_sync = ews_synchronize_sync;
	folder_class->expunge_sync = ews_expunge_sync;
	folder_class->transfer_messages_to_sync = ews_transfer_messages_to_sync;
	folder_class->get_filename = ews_get_filename;
}

static void
camel_ews_folder_init (CamelEwsFolder *ews_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (ews_folder);

	ews_folder->priv = CAMEL_EWS_FOLDER_GET_PRIVATE (ews_folder);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY | CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	ews_folder->priv->search_lock = g_mutex_new ();
	ews_folder->priv->refresh_lock = g_mutex_new ();
	g_static_rec_mutex_init(&ews_folder->priv->cache_lock);
	
	ews_folder->priv->refreshing = FALSE;
	
	camel_folder_set_lock_async (folder, TRUE);
}

/** End **/
