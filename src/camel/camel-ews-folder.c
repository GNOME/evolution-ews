/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-folder.c: class for an groupwise folder */

/*
 * Authors:
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
#include <glib/gstdio.h>
#include <libedataserver/e-flag.h>
#include <e-ews-connection.h>
#include <e-ews-compat.h>

#include "camel-ews-folder.h"
#include "camel-ews-private.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"

#define EWS_MAX_FETCH_COUNT 100

#define MAX_ATTACHMENT_SIZE 1*1024*1024   /*In bytes*/

#define SUMMARY_ITEM_FLAGS "item:ResponseObjects item:Sensitivity item:Importance"
#define ITEM_PROPS "item:Subject item:DateTimeReceived item:DateTimeSent item:DateTimeCreated item:Size " \
		   "item:HasAttachments item:InReplyTo"
#define SUMMARY_ITEM_PROPS ITEM_PROPS " " SUMMARY_ITEM_FLAGS 

#define SUMMARY_MESSAGE_FLAGS SUMMARY_ITEM_FLAGS " message:IsRead"
#define SUMMARY_MESSAGE_PROPS ITEM_PROPS " message:From message:Sender message:ToRecipients message:CcRecipients " \
		   "message:BccRecipients message:IsRead message:References message:InternetMessageId " \
		   SUMMARY_MESSAGE_FLAGS


#define CAMEL_EWS_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_FOLDER, CamelEwsFolderPrivate))

struct _CamelEwsFolderPrivate {
	GMutex *search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */

	/* For syncronizing refresh_info/sync_changes */
	gboolean refreshing;
	gboolean fetch_pending;
	GMutex *state_lock;
	GHashTable *uid_eflags;
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

#if ! EDS_CHECK_VERSION(2,33,0)
static gboolean camel_data_wrapper_construct_from_stream_sync(CamelDataWrapper *data_wrapper,
							     CamelStream *stream,
							     GCancellable *cancellable,
							     GError **error)
{
	/* In 2.32 this returns an int, which is zero for success */
	return !camel_data_wrapper_construct_from_stream(data_wrapper, stream, error);
}

#endif


static CamelMimeMessage *
camel_ews_folder_get_message_from_cache (CamelEwsFolder *ews_folder, const gchar *uid, GCancellable *cancellable, GError **error)
{
	CamelStream *stream;
	CamelMimeMessage *msg;
	CamelEwsFolderPrivate *priv;

	priv = ews_folder->priv;
	
	g_static_rec_mutex_lock (&priv->cache_lock);
	stream = camel_data_cache_get (ews_folder->cache, "cur", uid, error);
	if (!stream) {
			g_static_rec_mutex_unlock (&priv->cache_lock);
		return NULL;
	}
	
	msg = camel_mime_message_new ();

	if (!camel_data_wrapper_construct_from_stream_sync (
				(CamelDataWrapper *)msg, stream, cancellable, error)) {
		g_object_unref (msg);
		msg = NULL;
	}
	
	g_static_rec_mutex_unlock (&priv->cache_lock);
	g_object_unref (stream);

	return msg;
}

static CamelMimeMessage *
camel_ews_folder_get_message (CamelFolder *folder, const gchar *uid, gint pri, GCancellable *cancellable, GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	EEwsConnection *cnc;
	CamelEwsStore *ews_store;
	const gchar *full_name, *mime_content;
	CamelMimeMessage *message = NULL;
	CamelStream *tmp_stream = NULL;
	GSList *ids = NULL, *items = NULL;
	EFlag *flag = NULL;

	full_name = camel_folder_get_full_name (folder);
	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);
	ews_folder = (CamelEwsFolder *) folder;
	priv = ews_folder->priv;

	if (!camel_ews_store_connected (ews_store, error)) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("This message is not available in offline mode."));
		return NULL;
	}

	g_mutex_lock (priv->state_lock);

	if ((flag = g_hash_table_lookup (priv->uid_eflags, uid))) {
		g_mutex_unlock (priv->state_lock);
		e_flag_wait (flag);
		
		message = camel_ews_folder_get_message_from_cache (ews_folder, uid, cancellable, error);
		return message;
	}
	
	flag = e_flag_new ();
	g_hash_table_insert (priv->uid_eflags, g_strdup (uid), flag);
	
	g_mutex_unlock (priv->state_lock);

	cnc = camel_ews_store_get_connection (ews_store);
	ids = g_slist_append (ids, (gchar *) uid);
	e_ews_connection_get_items	(cnc, pri, ids, "IdOnly", "item:MimeContent", TRUE,
					 &items, cancellable, error);

	if (error && *error)
		goto exit;	

	mime_content = e_ews_item_get_mime_content (items->data);
	tmp_stream = camel_data_cache_add (ews_folder->cache, "tmp", uid, error);
	if (!tmp_stream)
		goto exit;

	if (camel_stream_write_string (tmp_stream, mime_content,
				       EVO3(cancellable,) error) < 0)
		goto exit;

	if (camel_stream_flush (tmp_stream, EVO3(cancellable,) error) == 0 && camel_stream_close (tmp_stream, EVO3(cancellable,) error) == 0) {
		gchar *tmp, *cache_file, *dir;
		const gchar *temp;
		
		tmp = camel_data_cache_get_filename (ews_folder->cache, "tmp", uid, error);
		cache_file = camel_data_cache_get_filename  (ews_folder->cache, "cur", uid, error);
		temp = g_strrstr (cache_file, "/");
		dir = g_strndup (cache_file, temp - cache_file);

		g_mkdir_with_parents (dir, 0700);
		g_free (dir);
		
		if (g_rename (tmp, cache_file) != 0)
			g_set_error (
				error, CAMEL_ERROR, 1,
				"failed to copy the tmp file");
		g_free (cache_file);
		g_free (tmp);
	}

	message = camel_ews_folder_get_message_from_cache (ews_folder, uid, cancellable, error);

exit:
	e_flag_set (flag);
	
	/* HACK FIXME just sleep for sometime so that the other waiting locks gets released by that time. Think of a
	 better way..*/
	g_usleep (1000);
	g_mutex_lock (priv->state_lock);
	g_hash_table_remove (priv->uid_eflags, uid);
	g_mutex_unlock (priv->state_lock);

	if (!message && !error)
		g_set_error (
			error, CAMEL_ERROR, 1,
			"Could not retrieve the message");
	if (ids)
		g_slist_free (ids);
	if (items) {
		g_object_unref (items->data);
		g_slist_free (items);
	}

	if (tmp_stream)
		g_object_unref (tmp_stream);

	return message;
}

/* Get the message from cache if available otherwise get it from server */
static CamelMimeMessage *
ews_folder_get_message_sync (CamelFolder *folder, const gchar *uid, EVO3(GCancellable *cancellable,) GError **error )
{
	CamelMimeMessage *message;
	EVO2(GCancellable *cancellable = NULL);

	message = camel_ews_folder_get_message_from_cache ((CamelEwsFolder *)folder, uid, cancellable, NULL);
	if (!message)
		message = camel_ews_folder_get_message (folder, uid, EWS_ITEM_HIGH, cancellable, error);

	return message;
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
ews_synchronize_sync (CamelFolder *folder, gboolean expunge, EVO3(GCancellable *cancellable,) GError **error)
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
	folder->summary = camel_ews_summary_new (folder, summary_file);
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
sync_updated_items (CamelEwsFolder *ews_folder, EEwsConnection *cnc, GSList *updated_items, GCancellable *cancellable, GError **error)
{
	CamelFolder *folder = (CamelFolder *) ews_folder;
	GSList *items = NULL, *l;
	GSList *generic_item_ids = NULL, *msg_ids = NULL;

	
	for (l = updated_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id = e_ews_item_get_id (item);
		CamelMessageInfo *mi;

		/* Compare the item_type from summary as the updated items seems to 
		   arrive as generic types while its not the case */
		mi = camel_folder_summary_uid (folder->summary, id->id);
		if (!mi) {
			g_object_unref (item);
			continue;
		}

		/* Check if the item has really changed */
		if (!strcmp (((CamelEwsMessageInfo *)mi)->change_key, id->change_key)) {
			g_object_unref (item);
			continue;
		}

		if (((CamelEwsMessageInfo *)mi)->item_type == E_EWS_ITEM_TYPE_GENERIC_ITEM)
			generic_item_ids = g_slist_append (generic_item_ids, g_strdup (id->id));
		else
			msg_ids = g_slist_append (msg_ids, g_strdup (id->id));

		g_object_unref (item);
	}
	g_slist_free (updated_items);

	if (msg_ids)
		e_ews_connection_get_items
			(g_object_ref (cnc), EWS_PRIORITY_MEDIUM, 
			 msg_ids, "IdOnly", SUMMARY_MESSAGE_FLAGS,
			 FALSE, &items, cancellable, error);

	camel_ews_utils_sync_updated_items (ews_folder, items);
	items = NULL;
	if (*error)
		goto exit;

	if (generic_item_ids)
		e_ews_connection_get_items
			(g_object_ref (cnc), EWS_PRIORITY_MEDIUM, 
			 generic_item_ids, "IdOnly", SUMMARY_ITEM_FLAGS,
			 FALSE, &items, cancellable, error);
	camel_ews_utils_sync_updated_items (ews_folder, items);

exit:	
	if (msg_ids) {
		g_slist_foreach (msg_ids, (GFunc) g_free, NULL);
		g_slist_free (msg_ids);
	}

	if (generic_item_ids) {
		g_slist_foreach (generic_item_ids, (GFunc) g_free, NULL);
		g_slist_free (generic_item_ids);
	}
}

static void
sync_created_items (CamelEwsFolder *ews_folder, EEwsConnection *cnc, GSList *created_items, GCancellable *cancellable, GError **error)
{
	GSList *items = NULL, *l;
	GSList *generic_item_ids = NULL, *msg_ids = NULL;
	
	for (l = created_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id = e_ews_item_get_id (item);
		EEwsItemType item_type = e_ews_item_get_item_type (item);

		/* created_msg_ids are items other than generic item. We fetch them
		   separately since the property sets vary */
		if (item_type == E_EWS_ITEM_TYPE_GENERIC_ITEM)
			generic_item_ids = g_slist_append (generic_item_ids, g_strdup (id->id));
		else
			msg_ids = g_slist_append (msg_ids, g_strdup (id->id));

		g_object_unref (item);
	}
	g_slist_free (created_items);
	
	if (msg_ids)
		e_ews_connection_get_items
			(g_object_ref (cnc), EWS_PRIORITY_MEDIUM, 
			 msg_ids, "IdOnly", SUMMARY_MESSAGE_PROPS,
			 FALSE, &items, cancellable, error);

	if (*error)
		goto exit;
	
	camel_ews_utils_sync_created_items (ews_folder, items);
	items = NULL;

	if (generic_item_ids)
		e_ews_connection_get_items
			(g_object_ref (cnc), EWS_PRIORITY_MEDIUM, 
			 generic_item_ids, "IdOnly", SUMMARY_ITEM_PROPS,
			 FALSE, &items, cancellable, error);
	
	camel_ews_utils_sync_created_items (ews_folder, items);

exit:
	if (msg_ids) {
		g_slist_foreach (msg_ids, (GFunc) g_free, NULL);
		g_slist_free (msg_ids);
	}

	if (generic_item_ids) {
		g_slist_foreach (generic_item_ids, (GFunc) g_free, NULL);
		g_slist_free (generic_item_ids);
	}
}

static gboolean
ews_refresh_info_sync (CamelFolder *folder, EVO3(GCancellable *cancellable,) GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	EEwsConnection *cnc;
	CamelEwsStore *ews_store;
	const gchar *full_name, *id;
	gchar *sync_state;
	gboolean includes_last_item = FALSE;
	GError *rerror = NULL;
	EVO2(GCancellable *cancellable = NULL);

	full_name = camel_folder_get_full_name (folder);
	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	ews_folder = (CamelEwsFolder *) folder;
	priv = ews_folder->priv;

	if (!camel_ews_store_connected (ews_store, error))
		return FALSE;

	g_mutex_lock (priv->state_lock);

	if (priv->refreshing) {
		g_mutex_unlock (priv->state_lock);
		return TRUE;	
	}

	priv->refreshing = TRUE;
	g_mutex_unlock (priv->state_lock);
	
	cnc = camel_ews_store_get_connection (ews_store);
	id = camel_ews_store_summary_get_folder_id
						(ews_store->summary,
						 full_name, NULL);

	/* Sync folder items does not return the fileds ToRecipients, 
	   CCRecipients. With the item_type unknown, its not possible
	   to fetch the right properties which are valid for an item type.
	   Due to these reasons we just get the item ids and its type in
	   SyncFolderItem request and fetch the item using the 
	   GetItem request. */
	sync_state = ((CamelEwsSummary *) folder->summary)->sync_state;
	do
	{
		GSList *items_created = NULL, *items_updated = NULL;
		GSList *items_deleted = NULL;
		guint32 total, unread;

		e_ews_connection_sync_folder_items	
							(cnc, EWS_PRIORITY_MEDIUM,
							 &sync_state, id,
							 "IdOnly", NULL,
							 EWS_MAX_FETCH_COUNT, &includes_last_item,
							 &items_created, &items_updated, 
							 &items_deleted, cancellable, &rerror);

		if (rerror)
			break;

		if (items_deleted)
			camel_ews_utils_sync_deleted_items (ews_folder, items_deleted);

		if (items_created)
			sync_created_items (ews_folder, cnc, items_created, cancellable, &rerror);
	
		if (rerror) {
			if (items_updated) {
				g_slist_foreach (items_updated, (GFunc) g_object_unref, NULL);
				g_slist_free (items_updated);
			}

			break;
		}

		if (items_updated)
			sync_updated_items (ews_folder, cnc, items_updated, cancellable, &rerror);
	
		if (rerror)
			break;
		
		total = camel_folder_summary_count (folder->summary);
		unread = folder->summary->unread_count;

		camel_ews_store_summary_set_folder_total (ews_store->summary, full_name, total);
		camel_ews_store_summary_set_folder_unread (ews_store->summary, full_name, unread);
		camel_ews_store_summary_save (ews_store->summary, NULL);

		g_free (((CamelEwsSummary *) folder->summary)->sync_state);
		((CamelEwsSummary *) folder->summary)->sync_state = sync_state;

		camel_folder_summary_save_to_db (folder->summary, NULL);

	} while (!rerror && !includes_last_item);
	
	if (rerror)
		g_propagate_error (error, rerror);

	g_mutex_lock (priv->state_lock);
	priv->refreshing = FALSE;
	g_mutex_unlock (priv->state_lock);
	if (sync_state != ((CamelEwsSummary *) folder->summary)->sync_state)
		g_free(sync_state);
	g_object_unref (cnc);

	return !rerror;
}

static gboolean
ews_append_message_sync (CamelFolder *folder, CamelMimeMessage *message,
	 		 EVO2(const) CamelMessageInfo *info,
			 gchar **appended_uid,
	 		 EVO3(GCancellable *cancellable,) GError **error)
{
	g_print ("\n append_message not implemented");

	return TRUE;
}

/* move messages */
static gboolean
ews_transfer_messages_to_sync	(CamelFolder *source, 
				 GPtrArray *uids,
				 CamelFolder *destination, 
				 EVO2(GPtrArray **transferred_uids,)
				 gboolean delete_originals, 
				 EVO3(GPtrArray **transferred_uids,)
				 EVO3(GCancellable *cancellable,) 
				 GError **error)
{

	g_print ("\n transfer_messages_to not implemented");

	return TRUE;
}

static gboolean
ews_expunge_sync (CamelFolder *folder, EVO3(GCancellable *cancellable,) GError **error)
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
	g_hash_table_destroy (ews_folder->priv->uid_eflags);

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
	folder_class->EVO3_sync(get_message) = ews_folder_get_message_sync;
	folder_class->rename = ews_folder_rename;
	folder_class->search_by_expression = ews_folder_search_by_expression;
	folder_class->count_by_expression = ews_folder_count_by_expression;
	folder_class->cmp_uids = ews_cmp_uids;
	folder_class->search_by_uids = ews_folder_search_by_uids;
	folder_class->search_free = ews_folder_search_free;
	folder_class->EVO3_sync(append_message) = ews_append_message_sync;
	folder_class->EVO3_sync(refresh_info) = ews_refresh_info_sync;
	EVO3(folder_class->synchronize_sync = ews_synchronize_sync);
	EVO2(folder_class->sync = ews_synchronize_sync);
	folder_class->EVO3_sync(expunge) = ews_expunge_sync;
	folder_class->EVO3_sync(transfer_messages_to) = ews_transfer_messages_to_sync;
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
	ews_folder->priv->state_lock = g_mutex_new ();
	g_static_rec_mutex_init(&ews_folder->priv->cache_lock);
	
	ews_folder->priv->refreshing = FALSE;
	
	ews_folder->priv->uid_eflags = g_hash_table_new_full	(g_str_hash, g_str_equal, 
								 (GDestroyNotify)g_free, 
								 (GDestroyNotify) e_flag_free);
	camel_folder_set_lock_async (folder, TRUE);
}

/** End **/
