/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-ews-folder.h"
#include "camel-ews-journal.h"
#include "camel-ews-store.h"

#define d(x)

static void ews_entry_free (CamelOfflineJournal *journal, CamelDListNode *entry);
static CamelDListNode *ews_entry_load (CamelOfflineJournal *journal, FILE *in);
static gint ews_entry_write (CamelOfflineJournal *journal, CamelDListNode *entry, FILE *out);
static gint ews_entry_play (CamelOfflineJournal *journal, CamelDListNode *entry, GCancellable *cancellable, GError **error);

G_DEFINE_TYPE (CamelEwsJournal, camel_ews_journal, CAMEL_TYPE_OFFLINE_JOURNAL)

static void
camel_ews_journal_class_init (CamelEwsJournalClass *class)
{
	CamelOfflineJournalClass *offline_journal_class;

	offline_journal_class = CAMEL_OFFLINE_JOURNAL_CLASS (class);
	offline_journal_class->entry_free = ews_entry_free;
	offline_journal_class->entry_load = ews_entry_load;
	offline_journal_class->entry_write = ews_entry_write;
	offline_journal_class->entry_play = ews_entry_play;
}

static void
camel_ews_journal_init (CamelEwsJournal *ews_journal)
{
}

static void
ews_entry_free (CamelOfflineJournal *journal, CamelDListNode *entry)
{
	CamelEwsJournalEntry *ews_entry = (CamelEwsJournalEntry *) entry;

	g_free (ews_entry->uid);
	g_free (ews_entry->original_uid);
	g_free (ews_entry->source_container);
	g_free (ews_entry);
}

static CamelDListNode *
ews_entry_load (CamelOfflineJournal *journal, FILE *in)
{
	CamelEwsJournalEntry *entry;

	entry = g_malloc0 (sizeof (CamelEwsJournalEntry));

	if (camel_file_util_decode_uint32 (in, &entry->type) == -1)
		goto exception;

	switch (entry->type) {
	case CAMEL_EWS_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_decode_string (in, &entry->uid) == -1)
			goto exception;
		break;
	case CAMEL_EWS_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_decode_string (in, &entry->uid) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &entry->original_uid) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &entry->source_container) == -1)
			goto exception;
		break;
	default:
		goto exception;
	}

	return (CamelDListNode *) entry;

 exception:

	if (entry->type == CAMEL_EWS_JOURNAL_ENTRY_TRANSFER)
		g_free (entry->source_container);

	g_free (entry->uid);
	g_free (entry);

	return NULL;
}

static gint
ews_entry_write (CamelOfflineJournal *journal, CamelDListNode *entry, FILE *out)
{
	CamelEwsJournalEntry *ews_entry = (CamelEwsJournalEntry *) entry;

	if (camel_file_util_encode_uint32 (out, ews_entry->type) == -1)
		return -1;

	switch (ews_entry->type) {
	case CAMEL_EWS_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_encode_string (out, ews_entry->uid))
			return -1;
		break;
	case CAMEL_EWS_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_encode_string (out, ews_entry->uid))
			return -1;
		if (camel_file_util_encode_string (out, ews_entry->original_uid))
			return -1;
		if (camel_file_util_encode_string (out, ews_entry->source_container))
			return -1;
		break;
	default:
		g_assert_not_reached ();
	}

	return 0;
}

static void
ews_message_info_dup_to (CamelMessageInfoBase *dest, CamelMessageInfoBase *src)
{
	camel_flag_list_copy (&dest->user_flags, &src->user_flags);
	camel_tag_list_copy (&dest->user_tags, &src->user_tags);
	dest->date_received = src->date_received;
	dest->date_sent = src->date_sent;
	dest->flags = src->flags;
	dest->size = src->size;
}

static gint
ews_entry_play_append (CamelOfflineJournal *journal, CamelEwsJournalEntry *entry, GCancellable *cancellable, GError **error)
{
	CamelEwsFolder *ews_folder = (CamelEwsFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelStream *stream;
	gboolean success = FALSE;

	/* if the message isn't in the cache, the user went behind our backs so "not our problem" */
	if (!ews_folder->cache || !(stream = camel_data_cache_get (ews_folder->cache, "cache", entry->uid, error))) {
		success = TRUE;
		goto done;
	}

	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream_sync ((CamelDataWrapper *) message, stream, cancellable, error)) {
		g_object_unref (message);
		g_object_unref (stream);
		goto done;
	}

	g_object_unref (stream);

	if (!(info = camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Note: this should never happen, but rather than crash lets make a new info */
		info = camel_message_info_new (NULL);
	}

	success = camel_folder_append_message_sync (folder, message, info, NULL, cancellable, error);
	camel_message_info_free (info);
	g_object_unref (message);

done:

	camel_folder_summary_remove_uid (folder->summary, entry->uid);
	camel_data_cache_remove (ews_folder->cache, "cache", entry->uid, NULL);

	return (success == 0);
}

static gint
ews_entry_play_transfer (CamelOfflineJournal *journal, CamelEwsJournalEntry *entry, GCancellable *cancellable, GError **error)
{
	CamelEwsFolder *ews_folder = (CamelEwsFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelEwsMessageInfo *real;
	CamelMessageInfoBase *info;
	GPtrArray *xuids, *uids;
	CamelFolder *src;
	CamelStore *parent_store;
	const gchar *name = NULL;

	parent_store = camel_folder_get_parent_store (folder);

	if (!(info = (CamelMessageInfoBase *) camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Note: this should never happen, but rather than crash lets make a new info */
		info = camel_message_info_new (NULL);
	}

	/* name = camel_ews_store_folder_lookup ((CamelEwsStore *) parent_store, entry->source_container); */
	if (name && (src = camel_store_get_folder_sync (parent_store, name, 0, cancellable, error))) {
		uids = g_ptr_array_sized_new (1);
		g_ptr_array_add (uids, entry->original_uid);

		if (camel_folder_transfer_messages_to_sync (src, uids, folder, FALSE, &xuids, cancellable, error)) {
			real = (CamelEwsMessageInfo *) camel_folder_summary_uid (folder->summary, xuids->pdata[0]);

			/* transfer all the system flags, user flags/tags, etc */
			ews_message_info_dup_to ((CamelMessageInfoBase *) real, (CamelMessageInfoBase *) info);
			camel_message_info_free (real);
		} else {
			goto exception;
		}

		g_ptr_array_free (xuids, TRUE);
		g_ptr_array_free (uids, TRUE);
		g_object_unref (src);
	} else if (!name) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot get folder container %s"),
			entry->source_container);
		goto exception;
	}

	/* message was successfully transferred, remove the fake item from the cache/summary */
	camel_folder_summary_remove_uid (folder->summary, entry->uid);
	camel_data_cache_remove (ews_folder->cache, "cache", entry->uid, NULL);
	camel_message_info_free (info);

	return 0;

 exception:

	camel_message_info_free (info);

	return -1;
}

static gint
ews_entry_play (CamelOfflineJournal *journal, CamelDListNode *entry, GCancellable *cancellable, GError **error)
{
	CamelEwsJournalEntry *ews_entry = (CamelEwsJournalEntry *) entry;

	switch (ews_entry->type) {
	case CAMEL_EWS_JOURNAL_ENTRY_APPEND:
		return ews_entry_play_append (journal, ews_entry, cancellable, error);
	case CAMEL_EWS_JOURNAL_ENTRY_TRANSFER:
		return ews_entry_play_transfer (journal, ews_entry, cancellable, error);
	default:
		g_assert_not_reached ();
		return -1;
	}
}

CamelOfflineJournal *
camel_ews_journal_new (CamelEwsFolder *folder, const gchar *filename)
{
	CamelOfflineJournal *journal;

	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (folder), NULL);

	journal = g_object_new (CAMEL_TYPE_OFFLINE_JOURNAL, NULL);
	camel_offline_journal_construct (journal, (CamelFolder *) folder, filename);

	return journal;
}

static gboolean
update_cache (CamelEwsJournal *ews_journal, CamelMimeMessage *message,
	      const CamelMessageInfo *mi, gchar **updated_uid, GCancellable *cancellable, GError **error)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) ews_journal;
	CamelEwsFolder *ews_folder = (CamelEwsFolder *) journal->folder;
	CamelFolder *folder = (CamelFolder *) journal->folder;
	CamelMessageInfo *info;
	CamelStream *cache;
	guint32 nextuid;
	gchar *uid;

	if (ews_folder->cache == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot append message in offline mode: cache unavailable"));
		return FALSE;
	}

	nextuid = camel_folder_summary_next_uid (folder->summary);
	uid = g_strdup_printf ("-%u", nextuid);

	if (!(cache = camel_data_cache_add (ews_folder->cache, "cache", uid, error))) {
		folder->summary->nextuid--;
		g_free (uid);
		return FALSE;
	}

	if (camel_data_wrapper_write_to_stream_sync (
		(CamelDataWrapper *) message, cache, cancellable, error) == -1
	    || camel_stream_flush (cache, cancellable, error) == -1) {
		g_prefix_error (
			error, _("Cannot append message in offline mode: "));
		camel_data_cache_remove (ews_folder->cache, "cache", uid, NULL);
		folder->summary->nextuid--;
		g_object_unref (cache);
		g_free (uid);
		return FALSE;
	}

	g_object_unref (cache);

	info = camel_folder_summary_info_new_from_message (folder->summary, message, NULL);
	camel_pstring_free(info->uid);
	info->uid = camel_pstring_strdup (uid);

	ews_message_info_dup_to ((CamelMessageInfoBase *) info, (CamelMessageInfoBase *) mi);

	camel_folder_summary_add (folder->summary, info);

	if (updated_uid)
		*updated_uid = g_strdup (uid);

	g_free (uid);

	return TRUE;
}

gboolean
camel_ews_journal_append (CamelEwsJournal *ews_journal, CamelMimeMessage *message,
				const CamelMessageInfo *mi, gchar **appended_uid, GCancellable *cancellable, GError **error)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) ews_journal;
	CamelEwsJournalEntry *entry;
	gchar *uid;

	if (!update_cache (ews_journal, message, mi, &uid, cancellable, error))
		return FALSE;

	entry = g_new (CamelEwsJournalEntry, 1);
	entry->type = CAMEL_EWS_JOURNAL_ENTRY_APPEND;
	entry->uid = uid;

	camel_dlist_addtail (&journal->queue, (CamelDListNode *) entry);

	if (appended_uid)
		*appended_uid = g_strdup (uid);

	return TRUE;
}

gboolean
camel_ews_journal_transfer (CamelEwsJournal *ews_journal, CamelEwsFolder *source_folder,
				  CamelMimeMessage *message,  const CamelMessageInfo *mi,
				  const gchar *original_uid, gchar **transferred_uid,
				  GCancellable *cancellable, GError **error)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) ews_journal;
	CamelEwsStore *ews_store;
	CamelEwsJournalEntry *entry;
	CamelStore *parent_store;
	gchar *uid;

	parent_store = camel_folder_get_parent_store (journal->folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	if (!update_cache (ews_journal, message, mi, &uid, cancellable, error))
		return FALSE;

	entry = g_new (CamelEwsJournalEntry, 1);
	entry->type = CAMEL_EWS_JOURNAL_ENTRY_APPEND;
	entry->uid = uid;
	entry->original_uid = g_strdup (original_uid);

	camel_dlist_addtail (&journal->queue, (CamelDListNode *) entry);

	if (transferred_uid)
		*transferred_uid = g_strdup (uid);

	return TRUE;
}
