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
#include "camel-ews-journal.h"
#include "camel-ews-private.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"

#define ADD_JUNK_ENTRY 1
#define REMOVE_JUNK_ENTRY -1
#define JUNK_FOLDER "Junk Mail"
#define READ_CURSOR_MAX_IDS 50
#define MAX_ATTACHMENT_SIZE 1*1024*1024   /*In bytes*/
#define GROUPWISE_BULK_DELETE_LIMIT 100

#define CAMEL_EWS_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_FOLDER, CamelEwsFolderPrivate))

struct _CamelEwsFolderPrivate {

#ifdef ENABLE_THREADS
	GStaticMutex search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */
#endif

};

extern gint camel_application_is_exiting;

/*prototypes*/
/*
static gboolean ews_transfer_messages_to (CamelFolder *source, GPtrArray *uids, CamelFolder *destination, GPtrArray **transferred_uids, gboolean delete_originals, GError **error);
void convert_to_calendar (EEwsItem *item, gchar **str, gint *len);
static void convert_to_task (EEwsItem *item, gchar **str, gint *len);
static void convert_to_note (EEwsItem *item, gchar **str, gint *len);
static void ews_update_all_items ( CamelFolder *folder, GList *item_list, GError **error);
static void ews_populate_details_from_item (CamelMimeMessage *msg, EEwsItem *item);
static void ews_populate_msg_body_from_item (EEwsConnection *cnc, CamelMultipart *multipart, EEwsItem *item, gchar *body);
static void ews_msg_set_recipient_list (CamelMimeMessage *msg, EEwsItem *item);
static void ews_update_cache ( CamelFolder *folder, GList *item_list, GError **error, gboolean uid_flag);
static CamelMimeMessage *ews_folder_item_to_msg ( CamelFolder *folder, EEwsItem *item, GError **error );
static gchar * ews_get_filename (CamelFolder *folder, const gchar *uid, GError **error);
static const gchar *get_from_from_org (EEwsItemOrganizer *org);
*/

static void ews_refresh_folder_sync (CamelFolder *folder, GCancellable *cancellable, GError **error);
#define d(x)

G_DEFINE_TYPE (CamelEwsFolder, camel_ews_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static gchar *
ews_get_filename (CamelFolder *folder, const gchar *uid, GCancellable *cancellable, GError **error)
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
ews_folder_search_by_expression (CamelFolder *folder, const gchar *expression, GCancellable *cancellable, GError **error)
{
	g_print ("\n Folder Search by Expression not implemented");
	
	return NULL;
}

static guint32
ews_folder_count_by_expression (CamelFolder *folder, const gchar *expression, GCancellable *cancellable, GError **error)
{
	g_print ("\n Folder count by Expression not implemented");
	
	return 0;
}

static GPtrArray *
ews_folder_search_by_uids(CamelFolder *folder, const gchar *expression, GPtrArray *uids, GCancellable *cancellable, GError **error)
{
	g_print ("\n Folder search by uids not implemented");
	
	return NULL;
}

static void
ews_folder_search_free (CamelFolder *folder, GPtrArray *uids)
{
	g_print ("\n Folder search free not implemented");
	
	return;
}

/********************* back to folder functions*************************/

static gboolean
ews_set_message_flags (CamelFolder *folder, const gchar *uid, guint32 flags, guint32 set)
{
	g_print ("\n Set message flags not implemented");
	
	return NULL;
}

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
	CamelEwsFolder *ews_folder;
	gchar *summary_file, *state_file, *journal_file;
	gchar *short_name;

	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = (gchar *) folder_name;

	folder = g_object_new (
		CAMEL_TYPE_EWS_FOLDER,
		"name", short_name, "full-name", folder_name,
		"parent_store", store, NULL);

	ews_folder = CAMEL_EWS_FOLDER(folder);

	summary_file = g_strdup_printf ("%s/summary",folder_dir);
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
	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	g_free(state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));

	ews_folder->cache = camel_data_cache_new (folder_dir, error);
	if (!ews_folder->cache) {
		g_object_unref (folder);
		return NULL;
	}

	journal_file = g_strdup_printf ("%s/journal",folder_dir);
	ews_folder->journal = camel_ews_journal_new (ews_folder, journal_file);
	g_free (journal_file);
	if (!ews_folder->journal) {
		g_object_unref (folder);
		return NULL;
	}

	if (!strcmp (folder_name, "Mailbox")) {
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

struct _folder_update_msg {
	CamelSessionThreadMsg msg;

	EEwsConnection *cnc;
	CamelFolder *folder;
	gchar *container_id;
	gchar *t_str;
	GSList *slist;
};

static gboolean
ews_refresh_info_sync (CamelFolder *folder, GCancellable *cancellable, GError **error)
{
	CamelEwsSummary *summary = (CamelEwsSummary *) folder->summary;
	CamelStoreInfo *si;
	CamelEwsStore *ews_store;
	CamelStore *parent_store;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	ews_store = CAMEL_EWS_STORE (parent_store);

	/*
	 * Checking for the summary->time_string here since the first the a
	 * user views a folder, the read cursor is in progress, and the getQM
	 * should not interfere with the process
	 */
	if (summary->time_string && (strlen (summary->time_string) > 0))  {
		ews_refresh_folder_sync (folder, cancellable, error);
		si = camel_store_summary_path ((CamelStoreSummary *)((CamelEwsStore *)parent_store)->summary, full_name);
		if (si) {
			guint32 unread, total;

			total = camel_folder_summary_count (folder->summary);
			unread = folder->summary->unread_count;

			if (si->total != total || si->unread != unread) {
				si->total = total;
				si->unread = unread;
				camel_store_summary_touch ((CamelStoreSummary *)((CamelEwsStore *)parent_store)->summary);
			}
			camel_store_summary_info_free ((CamelStoreSummary *)((CamelEwsStore *)parent_store)->summary, si);
		}
		/* camel_folder_summary_save_to_db (folder->summary, ex); */
		camel_store_summary_save ((CamelStoreSummary *)((CamelEwsStore *)parent_store)->summary);
	} else {
		/* We probably could not get the messages the first time. (get_folder) failed???!
		 * so do a get_folder again. And hope that it works
		 */
		g_print("Reloading folder...something wrong with the summary....\n");
		ews_store_reload_folder (ews_store, folder, 0, cancellable, error);
	}

	return TRUE;
}

static void
ews_refresh_folder_sync (CamelFolder *folder, GCancellable *cancellable, GError **error)
{
	g_print ("\n Refresh folder");
	
	return;
}

static gboolean
ews_append_message_sync (CamelFolder *folder, CamelMimeMessage *message,
		const CamelMessageInfo *info, gchar **appended_uid,
		GCancellable *cancellable, GError **error)
{
	g_print ("\n append_message not implemented");

	return TRUE;
}

/* move messages */
static gboolean
ews_transfer_messages_to_sync (CamelFolder *source, GPtrArray *uids,
		CamelFolder *destination, GPtrArray **transferred_uids,
		gboolean delete_originals, GCancellable *cancellable, GError **error)
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
	folder_class->set_message_flags = ews_set_message_flags;
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

#ifdef ENABLE_THREADS
	g_static_mutex_init(&ews_folder->priv->search_lock);
	g_static_rec_mutex_init(&ews_folder->priv->cache_lock);
#endif

	ews_folder->need_rescan = TRUE;
}

/** End **/
