/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "common/camel-o365-settings.h"
#include "common/e-o365-connection.h"

#include "camel-o365-folder-summary.h"
#include "camel-o365-store.h"
#include "camel-o365-store-summary.h"
#include "camel-o365-utils.h"

#include "camel-o365-folder.h"

#define O365_LOCAL_CACHE_PATH "cur"

/* https://docs.microsoft.com/en-us/graph/api/resources/message?view=graph-rest-1.0 */
#define O365_FETCH_SUMMARY_PROPERTIES	"categories," \
					"ccRecipients," \
					"changeKey," \
					"flag," \
					"from," \
					"hasAttachments," \
					"id," \
					"importance," \
					"internetMessageHeaders," \
					"internetMessageId," \
					"isRead," \
					"receivedDateTime," \
					"sender," \
					"sentDateTime," \
					"subject," \
					"toRecipients"

#define LOCK_CACHE(_folder) g_rec_mutex_lock (&_folder->priv->cache_lock)
#define UNLOCK_CACHE(_folder) g_rec_mutex_unlock (&_folder->priv->cache_lock)

#define LOCK_SEARCH(_folder) g_mutex_lock (&_folder->priv->search_lock)
#define UNLOCK_SEARCH(_folder) g_mutex_unlock (&_folder->priv->search_lock)

struct _CamelO365FolderPrivate {
	gchar *id; /* folder ID; stays the same for the full life of the folder */

	GRecMutex cache_lock;
	CamelDataCache *cache;

	GMutex search_lock;
	CamelFolderSearch *search;

	/* To not download the same message multiple times from different threads */
	GMutex get_message_lock;
	GCond get_message_cond;
	GHashTable *get_message_hash; /* borrowed gchar *uid ~> NULL */
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelO365Folder, camel_o365_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static GChecksum *
o365_folder_cache_new_checksum (const gchar *id)
{
	GChecksum *checksum;

	g_return_val_if_fail (id != NULL, NULL);

	/* No need to use SHA here, the string is short, will not collide with the others */
	checksum = g_checksum_new (G_CHECKSUM_MD5);

	g_checksum_update (checksum, (const guchar *) id, strlen (id));

	return checksum;
}

static CamelStream *
o365_folder_cache_add (CamelO365Folder *o365_folder,
		       const gchar *id,
		       GError **error)
{
	GIOStream *base_stream;
	CamelStream *stream = NULL;
	GChecksum *checksum;

	checksum = o365_folder_cache_new_checksum (id);

	LOCK_CACHE (o365_folder);
	base_stream = camel_data_cache_add (o365_folder->priv->cache, O365_LOCAL_CACHE_PATH, g_checksum_get_string (checksum), error);
	UNLOCK_CACHE (o365_folder);

	g_checksum_free (checksum);

	if (base_stream) {
		stream = camel_stream_new (base_stream);
		g_object_unref (base_stream);
	}

	return stream;
}

static gint
o365_folder_cache_remove (CamelO365Folder *o365_folder,
			  const gchar *id,
			  GError **error)
{
	GChecksum *checksum;
	gint ret;

	checksum = o365_folder_cache_new_checksum (id);

	LOCK_CACHE (o365_folder);
	ret = camel_data_cache_remove (o365_folder->priv->cache, O365_LOCAL_CACHE_PATH, g_checksum_get_string (checksum), error);
	UNLOCK_CACHE (o365_folder);

	g_checksum_free (checksum);

	return ret;
}

static CamelStream *
o365_folder_cache_get (CamelO365Folder *o365_folder,
		       const gchar *id,
		       GError **error)
{
	GChecksum *checksum;
	CamelStream *stream = NULL;
	GIOStream *base_stream;

	checksum = o365_folder_cache_new_checksum (id);

	LOCK_CACHE (o365_folder);
	base_stream = camel_data_cache_get (o365_folder->priv->cache, O365_LOCAL_CACHE_PATH, g_checksum_get_string (checksum), error);
	UNLOCK_CACHE (o365_folder);

	g_checksum_free (checksum);

	if (base_stream) {
		stream = camel_stream_new (base_stream);
		g_object_unref (base_stream);
	}

	return stream;
}

static gchar *
o365_folder_cache_dup_filename (CamelO365Folder *o365_folder,
				const gchar *id)
{
	GChecksum *checksum;
	gchar *filename;

	checksum = o365_folder_cache_new_checksum (id);

	LOCK_CACHE (o365_folder);
	filename = camel_data_cache_get_filename (o365_folder->priv->cache, O365_LOCAL_CACHE_PATH, g_checksum_get_string (checksum));
	UNLOCK_CACHE (o365_folder);

	g_checksum_free (checksum);

	return filename;
}

static CamelMimeMessage *
o365_folder_get_message_from_cache (CamelO365Folder *o365_folder,
				    const gchar *uid,
				    GCancellable *cancellable,
				    GError **error)
{
	CamelStream *stream;
	CamelMimeMessage *msg;

	stream = o365_folder_cache_get (o365_folder, uid, error);

	if (!stream)
		return NULL;

	msg = camel_mime_message_new ();

	if (!camel_data_wrapper_construct_from_stream_sync (CAMEL_DATA_WRAPPER (msg), stream, cancellable, error))
		g_clear_object (&msg);

	g_object_unref (stream);

	return msg;
}

static void
o365_folder_save_summary (CamelO365Folder *o365_folder)
{
	CamelFolderSummary *summary;

	g_return_if_fail (CAMEL_IS_O365_FOLDER (o365_folder));

	summary = camel_folder_get_folder_summary (CAMEL_FOLDER (o365_folder));

	if (summary) {
		GError *error = NULL;

		if (!camel_folder_summary_save (summary, &error))
			g_warning ("%s: Failed to save summary: %s", G_STRFUNC, error ? error->message : "Unknown error");

		g_clear_error (&error);
	}
}

static void
o365_folder_forget_all_mails (CamelO365Folder *o365_folder)
{
	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
	CamelFolderSummary *folder_summary;
	GPtrArray *known_uids;
	gint ii;

	g_return_if_fail (CAMEL_IS_O365_FOLDER (o365_folder));

	folder = CAMEL_FOLDER (o365_folder);
	g_return_if_fail (folder != NULL);

	known_uids = camel_folder_summary_get_array (camel_folder_get_folder_summary (folder));

	if (!known_uids)
		return;

	changes = camel_folder_change_info_new ();
	folder_summary = camel_folder_get_folder_summary (folder);

	camel_folder_summary_lock (folder_summary);

	for (ii = 0; ii < known_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (known_uids, ii);

		camel_folder_change_info_remove_uid (changes, uid);
		o365_folder_cache_remove (o365_folder, uid, NULL);
	}

	camel_folder_summary_clear (folder_summary, NULL);
	camel_folder_summary_unlock (folder_summary);

	o365_folder_save_summary (o365_folder);

	if (camel_folder_change_info_changed (changes))
		camel_folder_changed (folder, changes);

	camel_folder_change_info_free (changes);
	camel_folder_summary_free_array (known_uids);
}

static guint32
o365_folder_get_permanent_flags (CamelFolder *folder)
{
	return CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN |
		CAMEL_MESSAGE_FORWARDED |
		CAMEL_MESSAGE_USER;
}

static CamelMimeMessage *
o365_folder_get_message_cached (CamelFolder *folder,
				const gchar *message_uid,
				GCancellable *cancellable)
{
	return o365_folder_get_message_from_cache (CAMEL_O365_FOLDER (folder), message_uid, cancellable, NULL);
}

static void
o365_folder_exec_search (CamelFolder *folder,
			 const gchar *expression,
			 GPtrArray *uids,
			 GPtrArray **out_matches,
			 guint32 *out_count,
			 GCancellable *cancellable,
			 GError **error)
{
	CamelO365Folder *o365_folder;

	g_return_if_fail (CAMEL_IS_O365_FOLDER (folder));

	o365_folder = CAMEL_O365_FOLDER (folder);

	LOCK_SEARCH (o365_folder);

	camel_folder_search_set_folder (o365_folder->priv->search, folder);

	if (out_matches)
		*out_matches = camel_folder_search_search (o365_folder->priv->search, expression, uids, cancellable, error);

	if (out_count)
		*out_count = camel_folder_search_count (o365_folder->priv->search, expression, cancellable, error);

	UNLOCK_SEARCH (o365_folder);
}

static GPtrArray *
o365_folder_search_by_expression (CamelFolder *folder,
				  const gchar *expression,
				  GCancellable *cancellable,
				  GError **error)
{
	GPtrArray *matches = NULL;

	o365_folder_exec_search (folder, expression, NULL, &matches, NULL, cancellable, error);

	return matches;
}

static guint32
o365_folder_count_by_expression (CamelFolder *folder,
				 const gchar *expression,
				 GCancellable *cancellable,
				 GError **error)
{
	guint32 count = 0;

	o365_folder_exec_search (folder, expression, NULL, NULL, &count, cancellable, error);

	return count;
}

static GPtrArray *
o365_folder_search_by_uids (CamelFolder *folder,
			    const gchar *expression,
			    GPtrArray *uids,
			    GCancellable *cancellable,
			    GError **error)
{
	GPtrArray *matches = NULL;

	if (uids->len == 0)
		return g_ptr_array_new ();

	o365_folder_exec_search (folder, expression, uids, &matches, NULL, cancellable, error);

	return matches;
}

static void
o365_folder_search_free (CamelFolder *folder,
			 GPtrArray *uids)
{
	CamelO365Folder *o365_folder;

	if (!uids)
		return;

	g_return_if_fail (CAMEL_IS_O365_FOLDER (folder));

	o365_folder = CAMEL_O365_FOLDER (folder);

	LOCK_SEARCH (o365_folder);

	camel_folder_search_free_result (o365_folder->priv->search, uids);

	UNLOCK_SEARCH (o365_folder);
}

static gint
o365_folder_cmp_uids (CamelFolder *folder,
		      const gchar *uid1,
		      const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return strcmp (uid1, uid2);
}

static gboolean
o365_folder_download_message_cb (EO365Connection *cnc,
				 SoupMessage *message,
				 GInputStream *raw_data_stream,
				 gpointer user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	CamelStream *cache_stream = user_data;
	gssize expected_size = 0, wrote_size = 0, last_percent = -1;
	gint last_progress_notify = 0;
	gsize buffer_size = 65535;
	gchar *buffer;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STREAM (cache_stream), FALSE);
	g_return_val_if_fail (G_IS_INPUT_STREAM (raw_data_stream), FALSE);

	if (message && message->response_headers) {
		const gchar *content_length_str;

		content_length_str = soup_message_headers_get_one (message->response_headers, "Content-Length");

		if (content_length_str && *content_length_str)
			expected_size = (gssize) g_ascii_strtoll (content_length_str, NULL, 10);
	}

	buffer = g_malloc (buffer_size);

	do {
		success = !g_cancellable_set_error_if_cancelled (cancellable, error);

		if (success) {
			gssize n_read, n_wrote;

			n_read = g_input_stream_read (raw_data_stream, buffer, buffer_size, cancellable, error);

			if (n_read == -1) {
				success = FALSE;
			} else if (!n_read) {
				break;
			} else {
				n_wrote = camel_stream_write (cache_stream, buffer, n_read, cancellable, error);
				success = n_read == n_wrote;

				if (success && expected_size > 0) {
					gssize percent;

					wrote_size += n_wrote;

					percent = wrote_size * 100.0 / expected_size;

					if (percent > 100)
						percent = 100;

					if (percent != last_percent) {
						gint64 now = g_get_monotonic_time ();

						/* Notify only 10 times per second, not more */
						if (percent == 100 || now - last_progress_notify > G_USEC_PER_SEC / 10) {
							last_progress_notify = now;
							last_percent = percent;

							camel_operation_progress (cancellable, percent);
						}
					}
				}
			}
		}
	} while (success);

	g_free (buffer);

	if (success)
		camel_stream_flush (cache_stream, cancellable, NULL);

	return success;
}

static void
o365_folder_get_message_cancelled_cb (GCancellable *cancellable,
				      gpointer user_data)
{
	CamelO365Folder *o365_folder = user_data;

	g_return_if_fail (CAMEL_IS_O365_FOLDER (o365_folder));

	g_mutex_lock (&o365_folder->priv->get_message_lock);
	g_cond_broadcast (&o365_folder->priv->get_message_cond);
	g_mutex_unlock (&o365_folder->priv->get_message_lock);
}

static CamelMimeMessage *
o365_folder_get_message_sync (CamelFolder *folder,
			      const gchar *uid,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelMimeMessage *message = NULL;
	CamelO365Folder *o365_folder;
	CamelO365Store *o365_store;
	CamelStore *parent_store;
	CamelStream *cache_stream = NULL;
	EO365Connection *cnc = NULL;
	GError *local_error = NULL;
	const gchar *folder_id;
	gboolean success = TRUE, remove_from_hash = FALSE;

	g_return_val_if_fail (CAMEL_IS_O365_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	parent_store = camel_folder_get_parent_store (folder);

	if (!parent_store) {
		g_set_error_literal (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Invalid folder state (missing parent store)"));
		return NULL;
	}

	o365_folder = CAMEL_O365_FOLDER (folder);
	o365_store = CAMEL_O365_STORE (parent_store);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return NULL;

	folder_id = camel_o365_folder_get_id (o365_folder);

	g_mutex_lock (&o365_folder->priv->get_message_lock);

	if (g_hash_table_contains (o365_folder->priv->get_message_hash, uid)) {
		gulong handler_id = 0;

		if (cancellable) {
			handler_id = g_signal_connect (cancellable, "cancelled",
				G_CALLBACK (o365_folder_get_message_cancelled_cb), o365_folder);
		}

		while (success = !g_cancellable_set_error_if_cancelled (cancellable, error),
		       success && g_hash_table_contains (o365_folder->priv->get_message_hash, uid)) {
			g_cond_wait (&o365_folder->priv->get_message_cond, &o365_folder->priv->get_message_lock);
		}

		if (success)
			message = o365_folder_get_message_from_cache (o365_folder, uid, cancellable, NULL);

		if (handler_id)
			g_signal_handler_disconnect (cancellable, handler_id);
	}

	if (success && !message) {
		g_hash_table_insert (o365_folder->priv->get_message_hash, (gpointer) uid, NULL);
		remove_from_hash = TRUE;
	}

	g_mutex_unlock (&o365_folder->priv->get_message_lock);

	if (success && !message) {
		cache_stream = o365_folder_cache_add (o365_folder, uid, error);

		success = cache_stream != NULL;

		success = success && e_o365_connection_get_mail_message_sync (cnc, NULL, folder_id, uid,
			o365_folder_download_message_cb, cache_stream, cancellable, &local_error);

		if (local_error) {
			camel_o365_store_maybe_disconnect (o365_store, local_error);

			g_propagate_error (error, local_error);
			success = FALSE;
		}

		if (success) {
			/* First free the cache stream, thus the follwing call opens a new instance,
			   which is rewinded at the beginning of the stream. */
			g_clear_object (&cache_stream);

			message = o365_folder_get_message_from_cache (o365_folder, uid, cancellable, error);
		}
	}

	g_clear_object (&cache_stream);
	g_clear_object (&cnc);

	if (remove_from_hash) {
		g_mutex_lock (&o365_folder->priv->get_message_lock);
		g_hash_table_remove (o365_folder->priv->get_message_hash, uid);
		g_cond_broadcast (&o365_folder->priv->get_message_cond);
		g_mutex_unlock (&o365_folder->priv->get_message_lock);
	}

	return message;
}

static gboolean
o365_folder_append_message_sync (CamelFolder *folder,
				 CamelMimeMessage *message,
				 CamelMessageInfo *info,
				 gchar **appended_uid,
				 GCancellable *cancellable,
				 GError **error)
{
	/* Cannot put existing messages from other providers, because:
	   1) those are always set as drafts
	   2) the set sentDateTime property is not respected
	   3) internetMessageHeaders is limited to 5! headers only:
	      {
		"error": {
			"code": "InvalidInternetMessageHeaderCollection",
			"message": "Maximum number of headers in one message should be less than or equal to 5.",
			"innerError": {
				"date": "2020-07-01T10:03:34",
				"request-id": "a46da0ea-8933-43c6-932d-7c751f226516"
			}
		}
	      }
	   4) there are likely to be more limitations on the graph API, not spotted yet.

	   There is opened a feture request, which may eventually fix this, but it's currently not done yet (as of 2020-07-01):
	   https://microsoftgraph.uservoice.com/forums/920506-microsoft-graph-feature-requests/suggestions/35049175-put-edit-mime-email-content-with-microsoft-graph

	   Thus just error out for now.
	*/

#ifdef ENABLE_MAINTAINER_MODE /* Only for easier testing */
	CamelStore *parent_store;
	CamelO365Store *o365_store;
	EO365Connection *cnc = NULL;
	gboolean success;
	GError *local_error = NULL;

	parent_store = camel_folder_get_parent_store (folder);

	if (!CAMEL_IS_O365_STORE (parent_store)) {
		g_set_error_literal (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Invalid folder state (missing parent store)"));
		return FALSE;
	}

	o365_store = CAMEL_O365_STORE (parent_store);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return FALSE;

	success = camel_o365_utils_create_message_sync (cnc, camel_o365_folder_get_id (CAMEL_O365_FOLDER (folder)),
		message, info, appended_uid, cancellable, &local_error);

	g_clear_object (&cnc);

	if (!success)
		camel_o365_store_maybe_disconnect (o365_store, local_error);

	if (local_error)
		g_propagate_error (error, local_error);

	return success;
#else
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Cannot add messages into an Microsoft 365 account from another account. Only messages from the same account can be moved/copied between the Microsoft 365 folders."));
	return FALSE;
#endif
}

static gboolean
o365_folder_merge_server_user_flags (CamelMessageInfo *mi,
				     EO365MailMessage *mail)
{
	CamelFolderSummary *summary;
	JsonArray *categories;
	GHashTable *current_labels;
	const CamelNamedFlags *user_flags;
	guint ii, len;
	gboolean changed = FALSE;

	summary = camel_message_info_ref_summary (mi);
	if (summary)
		camel_folder_summary_lock (summary);
	camel_message_info_property_lock (mi);
	camel_message_info_freeze_notifications (mi);

	current_labels = g_hash_table_new (g_str_hash, g_str_equal);

	user_flags = camel_message_info_get_user_flags (mi);
	len = camel_named_flags_get_length (user_flags);

	for (ii = 0; ii < len; ii++) {
		const gchar *name = camel_named_flags_get (user_flags, ii);

		if (!camel_o365_utils_is_system_user_flag (name))
			g_hash_table_insert (current_labels, (gpointer) name, NULL);
	}

	categories = e_o365_mail_message_get_categories (mail);

	if (categories) {
		len = json_array_get_length (categories);

		for (ii = 0; ii < len; ii++) {
			const gchar *name = json_array_get_string_element (categories, ii);

			name = camel_o365_utils_rename_label (name, TRUE);

			if (name && *name) {
				gchar *flag;

				flag = camel_o365_utils_encode_category_name (name);

				if (!g_hash_table_remove (current_labels, flag)) {
					changed = TRUE;

					camel_message_info_set_user_flag (mi, flag, TRUE);
				}

				g_free (flag);
			}
		}
	}

	/* Those left here are to be removed */
	if (g_hash_table_size (current_labels)) {
		GHashTableIter iter;
		gpointer key;

		changed = TRUE;

		g_hash_table_iter_init (&iter, current_labels);

		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			camel_message_info_set_user_flag (mi, key, FALSE);
		}
	}

	camel_message_info_thaw_notifications (mi);
	camel_message_info_property_unlock (mi);
	if (summary)
		camel_folder_summary_unlock (summary);
	g_clear_object (&summary);

	return changed;
}

static gboolean
o365_folder_update_message_info (CamelMessageInfo *mi,
				 EO365MailMessage *mail)
{
	CamelO365MessageInfo *o365_mi;
	guint32 flags = 0;
	gboolean changed = FALSE;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (mi), FALSE);
	g_return_val_if_fail (mail != NULL, FALSE);

	o365_mi = CAMEL_O365_MESSAGE_INFO (mi);

	if (e_o365_mail_message_get_has_attachments (mail))
		flags |= CAMEL_MESSAGE_ATTACHMENTS;

	if (e_o365_mail_message_get_is_draft (mail))
		flags |= CAMEL_MESSAGE_DRAFT;

	if (e_o365_mail_message_get_is_read (mail))
		flags |= CAMEL_MESSAGE_SEEN;

	if (e_o365_mail_message_get_importance (mail) == E_O365_IMPORTANCE_HIGH)
		flags |= CAMEL_MESSAGE_FLAGGED;

	/* 2020-06-24 - cannot make it work, even with https://stackoverflow.com/questions/58205494/access-the-replied-forwarded-etc-state-from-rest */
	/* CAMEL_MESSAGE_ANSWERED
	CAMEL_MESSAGE_FORWARDED */

	if (camel_o365_message_info_set_server_flags (o365_mi, flags)) {
		guint32 mask;

		mask = CAMEL_MESSAGE_ATTACHMENTS | CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_FLAGGED;

		camel_message_info_set_flags (mi, mask, flags);

		changed = TRUE;
	}

	changed = o365_folder_merge_server_user_flags (mi, mail) || changed;

	return changed;
}

static gchar *
o365_folder_recipients_as_string (JsonArray *recipients) /* EO365Recipient * */
{
	CamelInternetAddress *addrs;
	guint ii, len;
	gchar *res;

	if (!recipients)
		return NULL;

	addrs = camel_internet_address_new ();

	len = json_array_get_length (recipients);
	for (ii = 0; ii < len; ii++) {
		EO365Recipient *recipient = json_array_get_object_element (recipients, ii);
		const gchar *name, *address;

		name = e_o365_recipient_get_name (recipient);
		address = e_o365_recipient_get_address (recipient);

		if (address && *address)
			camel_internet_address_add (addrs, name, address);
	}

	if (camel_address_length (CAMEL_ADDRESS (addrs)) > 0) {
		res = camel_address_format (CAMEL_ADDRESS (addrs));
	} else {
		res = NULL;
	}

	g_clear_object (&addrs);

	return res;
}

static CamelMessageInfo *
o365_folder_new_message_info_from_mail_message (CamelFolder *folder,
						EO365MailMessage *mail)
{
	CamelMessageInfo *mi = NULL;
	CamelNameValueArray *headers = NULL;
	JsonArray *json_headers;
	EO365Recipient *from;
	const gchar *ctmp;
	time_t tt;
	gchar *tmp;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (mail != NULL, NULL);

	json_headers = e_o365_mail_message_get_internet_message_headers (mail);

	if (json_headers && json_array_get_length (json_headers) > 0) {
		guint ii, len = json_array_get_length (json_headers);

		headers = camel_name_value_array_new_sized (len);

		for (ii = 0; ii < len; ii++) {
			EO365InternetMessageHeader *header = json_array_get_object_element (json_headers, ii);
			const gchar *name, *value;

			name = e_o365_internet_message_header_get_name (header);
			value = e_o365_internet_message_header_get_value (header);

			if (name && *name)
				camel_name_value_array_append (headers, name, value ? value : "");
		}

		if (camel_name_value_array_get_length (headers)) {
			mi = camel_message_info_new_from_headers (camel_folder_get_folder_summary (folder), headers);
		} else {
			camel_name_value_array_free (headers);
			headers = NULL;
		}
	}

	if (!mi)
		mi = camel_message_info_new (camel_folder_get_folder_summary (folder));

	camel_message_info_set_abort_notifications (mi, TRUE);

	ctmp = e_o365_mail_message_get_subject (mail);

	if (ctmp)
		camel_message_info_set_subject	(mi, ctmp);

	from = e_o365_mail_message_get_from (mail);

	if (from) {
		const gchar *name, *address;

		name = e_o365_recipient_get_name (from);
		address = e_o365_recipient_get_address (from);

		if (address && *address) {
			tmp = camel_internet_address_format_address (name, address);

			if (tmp) {
				camel_message_info_set_from (mi, tmp);

				g_free (tmp);
			}
		}
	}

	tmp = o365_folder_recipients_as_string (e_o365_mail_message_get_to_recipients (mail));

	if (tmp) {
		camel_message_info_set_to (mi, tmp);
		g_free (tmp);
	}

	tmp = o365_folder_recipients_as_string (e_o365_mail_message_get_cc_recipients (mail));

	if (tmp) {
		camel_message_info_set_cc (mi, tmp);
		g_free (tmp);
	}

	tt = e_o365_mail_message_get_sent_date_time (mail);

	if (tt)
		camel_message_info_set_date_sent (mi, (gint64) tt);

	tt = e_o365_mail_message_get_received_date_time (mail);

	if (tt)
		camel_message_info_set_date_received (mi, (gint64) tt);

	ctmp = e_o365_mail_message_get_internet_message_id (mail);

	if (ctmp && *ctmp) {
		GChecksum *checksum;
		CamelSummaryMessageID message_id;
		guint8 *digest;
		gsize length;

		length = g_checksum_type_get_length (G_CHECKSUM_MD5);
		digest = g_alloca (length);

		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, (const guchar *) ctmp, -1);
		g_checksum_get_digest (checksum, digest, &length);
		g_checksum_free (checksum);

		memcpy (message_id.id.hash, digest, sizeof (message_id.id.hash));

		camel_message_info_set_message_id (mi, message_id.id.id);
	}

	camel_message_info_set_uid (mi, e_o365_mail_message_get_id (mail));

	if (headers)
		camel_message_info_take_headers (mi, headers);

	camel_message_info_set_abort_notifications (mi, FALSE);

	o365_folder_update_message_info (mi, mail);

	return mi;
}

typedef struct _SummaryDeltaData {
	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
	GList *removed_uids; /* gchar * - from the Camel string pool */
} SummaryDeltaData;

static gboolean
o365_folder_got_summary_messages_cb (EO365Connection *cnc,
				     const GSList *results, /* JsonObject * - the returned objects from the server */
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	SummaryDeltaData *sdd = user_data;
	CamelFolderSummary *summary;
	GSList *link;

	g_return_val_if_fail (sdd != NULL, FALSE);

	summary = camel_folder_get_folder_summary (sdd->folder);

	if (!summary)
		return FALSE;

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		EO365MailMessage *mail = link->data;
		const gchar *id;

		id = e_o365_mail_message_get_id (mail);

		if (!id)
			continue;

		if (!sdd->changes)
			sdd->changes = camel_folder_change_info_new ();

		if (e_o365_delta_is_removed_object (mail)) {
			sdd->removed_uids = g_list_prepend (sdd->removed_uids, (gpointer) camel_pstring_strdup (id));

			camel_folder_change_info_remove_uid (sdd->changes, id);
		} else {
			CamelMessageInfo *info;

			info = camel_folder_summary_get (summary, id);

			if (info) {
				if (o365_folder_update_message_info (info, mail))
					camel_folder_change_info_change_uid (sdd->changes, id);

				g_object_unref (info);
			} else {
				info = o365_folder_new_message_info_from_mail_message (sdd->folder, mail);

				if (info) {
					camel_folder_summary_add (summary, info, TRUE);

					/* Unset folder-flagged flag, which ahd been set by the camel_folder_summary_add(),
					   to avoid re-sync on the just added message. */
					camel_message_info_set_folder_flagged (info, FALSE);

					camel_folder_change_info_add_uid (sdd->changes, id);
					camel_folder_change_info_recent_uid (sdd->changes, id);

					g_object_unref (info);
				}
			}
		}
	}

	return TRUE;
}

static gboolean
o365_folder_refresh_info_sync (CamelFolder *folder,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelO365Folder *o365_folder;
	CamelO365FolderSummary *o365_folder_summary;
	CamelO365Store *o365_store;
	CamelFolderSummary *folder_summary;
	CamelStore *parent_store;
	EO365Connection *cnc = NULL;
	SummaryDeltaData sdd;
	GError *local_error = NULL;
	const gchar *folder_id;
	gchar *curr_delta_link, *new_delta_link = NULL;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_O365_FOLDER (folder), FALSE);

	parent_store = camel_folder_get_parent_store (folder);

	if (!parent_store) {
		g_set_error_literal (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Invalid folder state (missing parent store)"));
		return FALSE;
	}

	o365_folder = CAMEL_O365_FOLDER (folder);
	o365_store = CAMEL_O365_STORE (parent_store);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return FALSE;

	folder_id = camel_o365_folder_get_id (o365_folder);
	folder_summary = camel_folder_get_folder_summary (folder);
	o365_folder_summary = CAMEL_O365_FOLDER_SUMMARY (folder_summary);

	curr_delta_link = camel_o365_folder_summary_dup_delta_link (o365_folder_summary);

	sdd.folder = folder;
	sdd.changes = NULL;
	sdd.removed_uids = NULL;

	success = e_o365_connection_get_objects_delta_sync (cnc, NULL, E_O365_FOLDER_KIND_MAIL, folder_id, O365_FETCH_SUMMARY_PROPERTIES,
		curr_delta_link, 0, o365_folder_got_summary_messages_cb, &sdd,
		&new_delta_link, cancellable, &local_error);

	if (curr_delta_link && e_o365_connection_util_delta_token_failed (local_error)) {
		g_clear_error (&local_error);
		g_clear_pointer (&curr_delta_link, g_free);

		camel_o365_folder_summary_set_delta_link (o365_folder_summary, NULL);

		o365_folder_forget_all_mails (o365_folder);

		success = e_o365_connection_get_objects_delta_sync (cnc, NULL, E_O365_FOLDER_KIND_MAIL, folder_id, O365_FETCH_SUMMARY_PROPERTIES,
			NULL, 0, o365_folder_got_summary_messages_cb, &sdd,
			&new_delta_link, cancellable, &local_error);
	}

	if (success && new_delta_link)
		camel_o365_folder_summary_set_delta_link (o365_folder_summary, new_delta_link);

	if (sdd.removed_uids) {
		camel_folder_summary_remove_uids (folder_summary, sdd.removed_uids);

		g_list_free_full (sdd.removed_uids, (GDestroyNotify) camel_pstring_free);
	}

	o365_folder_save_summary (o365_folder);

	if (sdd.changes) {
		if (camel_folder_change_info_changed (sdd.changes))
			camel_folder_changed (folder, sdd.changes);

		camel_folder_change_info_free (sdd.changes);
	}

	if (local_error) {
		camel_o365_store_maybe_disconnect (o365_store, local_error);

		g_propagate_error (error, local_error);
		success = FALSE;
	}

	g_clear_object (&cnc);
	g_free (curr_delta_link);
	g_free (new_delta_link);

	return success;
}

static gboolean
o365_folder_copy_move_to_folder_sync (CamelFolder *folder,
				      CamelO365Store *o365_store,
				      const GSList *uids,
				      const gchar *des_folder_id,
				      gboolean do_copy,
				      GCancellable *cancellable,
				      GError **error)
{
	CamelO365StoreSummary *o365_store_summary;
	EO365Connection *cnc = NULL;
	GSList *des_ids = NULL;
	gboolean success;

	g_return_val_if_fail (des_folder_id != NULL, FALSE);

	o365_store_summary = camel_o365_store_ref_store_summary (o365_store);

	if (g_strcmp0 (des_folder_id, "junkemail") == 0) {
		des_folder_id = camel_o365_store_summary_dup_folder_id_for_type (o365_store_summary, CAMEL_FOLDER_TYPE_JUNK);
	} else if (g_strcmp0 (des_folder_id, "deleteditems") == 0) {
		des_folder_id = camel_o365_store_summary_dup_folder_id_for_type (o365_store_summary, CAMEL_FOLDER_TYPE_TRASH);
	} else if (g_strcmp0 (des_folder_id, "inbox") == 0) {
		des_folder_id = camel_o365_store_summary_dup_folder_id_for_type (o365_store_summary, CAMEL_FOLDER_TYPE_INBOX);
	}

	g_clear_object (&o365_store_summary);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return FALSE;

	success = e_o365_connection_copy_move_mail_messages_sync (cnc, NULL, uids, des_folder_id, do_copy,
		&des_ids, cancellable, error);

	g_clear_object (&cnc);

	if (!do_copy) {
		CamelFolderChangeInfo *src_changes;
		CamelO365Folder *o365_folder;
		GSList *des_link, *src_link;
		GList *removed_uids = NULL;

		src_changes = camel_folder_change_info_new ();
		o365_folder = CAMEL_O365_FOLDER (folder);

		camel_folder_lock (folder);

		/* Can succeed partially, thus always check the moved ids */
		for (src_link = (GSList *) uids, des_link = des_ids;
		     src_link && des_link;
		     src_link = g_slist_next (src_link), des_link = g_slist_next (des_link)) {
			const gchar *src_uid = src_link->data;

			o365_folder_cache_remove (o365_folder, src_uid, NULL);

			removed_uids = g_list_prepend (removed_uids, (gpointer) src_uid);
			camel_folder_change_info_remove_uid (src_changes, src_uid);
		}

		if (removed_uids) {
			CamelFolderSummary *summary;

			summary = camel_folder_get_folder_summary (folder);
			camel_folder_summary_remove_uids (summary, removed_uids);

			g_list_free (removed_uids);
		}

		if (camel_folder_change_info_changed (src_changes))
			camel_folder_changed (folder, src_changes);

		camel_folder_change_info_free (src_changes);

		camel_folder_unlock (folder);
	}

	g_slist_free_full (des_ids, (GDestroyNotify) camel_pstring_free);

	return success;
}

static gboolean
o365_folder_delete_messages_sync (CamelFolder *folder,
				  CamelO365Store *o365_store,
				  const GSList *uids,
				  gboolean is_trash_folder,
				  GCancellable *cancellable,
				  GError **error)
{
	EO365Connection *cnc = NULL;
	gboolean success;

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return FALSE;

	if (is_trash_folder) {
		GSList *deleted_uids = NULL, *link;

		success = e_o365_connection_delete_mail_messages_sync (cnc, NULL, uids, &deleted_uids, cancellable, error);

		if (deleted_uids) {
			CamelFolderChangeInfo *changes;
			CamelO365Folder *o365_folder;
			GList *removed_uids = NULL;

			o365_folder = CAMEL_O365_FOLDER (folder);
			changes = camel_folder_change_info_new ();

			camel_folder_lock (folder);

			/* Can succeed partially, thus always check the moved ids */
			for (link = deleted_uids; link; link = g_slist_next (link)) {
				const gchar *uid = link->data;

				o365_folder_cache_remove (o365_folder, uid, NULL);

				removed_uids = g_list_prepend (removed_uids, (gpointer) uid);
				camel_folder_change_info_remove_uid (changes, uid);
			}

			if (removed_uids) {
				CamelFolderSummary *summary;

				summary = camel_folder_get_folder_summary (folder);
				camel_folder_summary_remove_uids (summary, removed_uids);

				g_list_free (removed_uids);
			}

			if (camel_folder_change_info_changed (changes))
				camel_folder_changed (folder, changes);

			camel_folder_change_info_free (changes);

			camel_folder_unlock (folder);

			g_slist_free (deleted_uids);
		}
	} else {
		success = o365_folder_copy_move_to_folder_sync (folder, o365_store,
			uids, "deleteditems", FALSE, cancellable, error);
	}

	g_clear_object (&cnc);

	return success;
}

static JsonBuilder *
o365_folder_message_info_changes_to_json (CamelMessageInfo *mi)
{
	JsonBuilder *builder;

	builder = json_builder_new_immutable ();
	e_o365_json_begin_object_member (builder, NULL);

	camel_o365_utils_add_message_flags (builder, mi, NULL);

	e_o365_json_end_object_member (builder);

	return builder;
}

static gboolean
o365_folder_save_flags_sync (CamelFolder *folder,
			     CamelO365Store *o365_store,
			     GSList *mi_list, /* CamelMessageInfo * */
			     GCancellable *cancellable,
			     GError **error)
{
	EO365Connection *cnc = NULL;
	gboolean success = TRUE;

	/* Trap an error, but do not stop other processing */
	g_return_val_if_fail (mi_list != NULL, TRUE);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return FALSE;

	if (mi_list->next) {
		GSList *link;
		GPtrArray *requests;

		requests = g_ptr_array_new_full (g_slist_length (mi_list), g_object_unref);

		for (link = mi_list; link && success; link = g_slist_next (link)) {
			CamelMessageInfo *mi = link->data;
			SoupMessage *message;
			JsonBuilder *builder;

			builder = o365_folder_message_info_changes_to_json (mi);

			message = e_o365_connection_prepare_update_mail_message (cnc, NULL,
				camel_message_info_get_uid (mi), builder, error);

			g_clear_object (&builder);

			if (!message)
				success = FALSE;
			else
				g_ptr_array_add (requests, message);
		}

		if (success)
			success = e_o365_connection_batch_request_sync (cnc, E_O365_API_V1_0, requests, cancellable, error);

		g_ptr_array_free (requests, TRUE);
	} else {
		CamelMessageInfo *mi = mi_list->data;
		JsonBuilder *builder;

		builder = o365_folder_message_info_changes_to_json (mi);

		success = e_o365_connection_update_mail_message_sync (cnc, NULL,
			camel_message_info_get_uid (mi), builder, cancellable, error);

		g_clear_object (&builder);
	}

	g_object_unref (cnc);

	if (success) {
		GSList *link;

		camel_folder_lock (folder);

		for (link = mi_list; link; link = g_slist_next (link)) {
			CamelMessageInfo *mi = link->data;

			camel_message_info_set_folder_flagged (mi, FALSE);
		}

		camel_folder_unlock (folder);
	}

	return success;
}

static gboolean
o365_folder_is_of_type (CamelFolder *folder,
			guint32 folder_type)
{
	CamelStore *parent_store;
	CamelO365Store *o365_store;
	CamelO365StoreSummary *o365_store_summary;
	gboolean is_of_type;
	const gchar *folder_id;

	g_return_val_if_fail (folder != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (folder);

	if (!parent_store)
		return FALSE;

	o365_store = CAMEL_O365_STORE (parent_store);

	g_return_val_if_fail (o365_store != NULL, FALSE);

	o365_store_summary = camel_o365_store_ref_store_summary (o365_store);

	folder_type = folder_type & CAMEL_FOLDER_TYPE_MASK;
	folder_id = camel_o365_folder_get_id (CAMEL_O365_FOLDER (folder));
	is_of_type = folder_id &&
		(camel_o365_store_summary_get_folder_flags (o365_store_summary, folder_id) & CAMEL_FOLDER_TYPE_MASK) == folder_type;

	g_clear_object (&o365_store_summary);

	return is_of_type;
}

static gboolean
o365_folder_synchronize_sync (CamelFolder *folder,
			      gboolean expunge,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelO365Store *o365_store;
	CamelStore *parent_store;
	CamelFolderSummary *folder_summary;
	GPtrArray *uids;
	GSList *mi_list = NULL, *deleted_uids = NULL, *junk_uids = NULL, *inbox_uids = NULL;
	gint mi_list_len = 0;
	gboolean is_junk_folder;
	gboolean success = TRUE;
	guint ii;
	GError *local_error = NULL;

	parent_store = camel_folder_get_parent_store (folder);

	if (!parent_store) {
		g_set_error_literal (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Invalid folder state (missing parent store)"));
		return FALSE;
	}

	o365_store = CAMEL_O365_STORE (parent_store);

	if (!camel_o365_store_ensure_connected (o365_store, NULL, cancellable, error))
		return FALSE;

	folder_summary = camel_folder_get_folder_summary (folder);

	if (camel_folder_summary_get_deleted_count (folder_summary) > 0 ||
	    camel_folder_summary_get_junk_count (folder_summary) > 0) {
		camel_folder_summary_prepare_fetch_all (folder_summary, NULL);
		uids = camel_folder_summary_get_array (folder_summary);
	} else {
		uids = camel_folder_summary_get_changed (folder_summary);
	}

	if (!uids || !uids->len) {
		camel_folder_summary_free_array (uids);
		return TRUE;
	}

	is_junk_folder = o365_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_JUNK);

	for (ii = 0; success && ii < uids->len; ii++) {
		guint32 flags_changed, flags_set;
		CamelMessageInfo *mi;
		const gchar *uid;

		uid = uids->pdata[ii];
		mi = camel_folder_summary_get (folder_summary, uid);

		if (!mi)
			continue;

		flags_set = camel_message_info_get_flags (mi);
		flags_changed = camel_o365_message_info_get_server_flags (CAMEL_O365_MESSAGE_INFO (mi)) ^ flags_set;

		if ((flags_set & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0 &&
		    (flags_changed & (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_FLAGGED)) != 0) {
			mi_list = g_slist_prepend (mi_list, mi);
			mi_list_len++;

			if (flags_set & CAMEL_MESSAGE_DELETED)
				deleted_uids = g_slist_prepend (deleted_uids, (gpointer) camel_pstring_strdup (uid));
			else if (flags_set & CAMEL_MESSAGE_JUNK)
				junk_uids = g_slist_prepend (junk_uids, (gpointer) camel_pstring_strdup (uid));
			else if (is_junk_folder && (flags_set & CAMEL_MESSAGE_NOTJUNK) != 0)
				inbox_uids = g_slist_prepend (inbox_uids, (gpointer) camel_pstring_strdup (uid));
		} else if (flags_set & CAMEL_MESSAGE_DELETED) {
			deleted_uids = g_slist_prepend (deleted_uids, (gpointer) camel_pstring_strdup (uid));
			g_clear_object (&mi);
		} else if (flags_set & CAMEL_MESSAGE_JUNK) {
			junk_uids = g_slist_prepend (junk_uids, (gpointer) camel_pstring_strdup (uid));
			g_clear_object (&mi);
		} else if (is_junk_folder && (flags_set & CAMEL_MESSAGE_NOTJUNK) != 0) {
			inbox_uids = g_slist_prepend (inbox_uids, (gpointer) camel_pstring_strdup (uid));
			g_clear_object (&mi);
		} else if ((flags_set & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0) {
			/* OK, the change must have been the labels */
			mi_list = g_slist_prepend (mi_list, mi);
			mi_list_len++;
		} else {
			g_clear_object (&mi);
		}

		if (mi_list_len == E_O365_BATCH_MAX_REQUESTS) {
			success = o365_folder_save_flags_sync (folder, o365_store, mi_list, cancellable, &local_error);
			g_slist_free_full (mi_list, g_object_unref);
			mi_list = NULL;
			mi_list_len = 0;
		}
	}

	if (mi_list != NULL && success)
		success = o365_folder_save_flags_sync (folder, o365_store, mi_list, cancellable, &local_error);
	g_slist_free_full (mi_list, g_object_unref);

	if (deleted_uids && success)
		success = o365_folder_delete_messages_sync (folder, o365_store, deleted_uids, o365_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_TRASH), cancellable, &local_error);
	g_slist_free_full (deleted_uids, (GDestroyNotify) camel_pstring_free);

	if (junk_uids && success)
		success = o365_folder_copy_move_to_folder_sync (folder, o365_store, junk_uids, "junkemail", FALSE, cancellable, &local_error);
	g_slist_free_full (junk_uids, (GDestroyNotify) camel_pstring_free);

	if (inbox_uids && success)
		success = o365_folder_copy_move_to_folder_sync (folder, o365_store, inbox_uids, "inbox", FALSE, cancellable, &local_error);
	g_slist_free_full (inbox_uids, (GDestroyNotify) camel_pstring_free);

	camel_folder_summary_save (folder_summary, NULL);
	camel_folder_summary_free_array (uids);

	if (local_error) {
		camel_o365_store_maybe_disconnect (o365_store, local_error);
		g_propagate_error (error, local_error);
	}

	return success;
}

static gboolean
o365_folder_expunge_sync (CamelFolder *folder,
			  GCancellable *cancellable,
			  GError **error)
{
	/* it does nothing special here, everything is done as part of the o365_folder_synchronize_sync() */

	return TRUE;
}

static gboolean
o365_folder_transfer_messages_to_sync (CamelFolder *source,
				       GPtrArray *uids,
				       CamelFolder *destination,
				       gboolean delete_originals,
				       GPtrArray **transferred_uids,
				       GCancellable *cancellable,
				       GError **error)
{
	CamelStore *parent_store;
	CamelO365Store *o365_store;
	GSList *uids_list = NULL;
	gboolean success;
	guint ii;
	GError *local_error = NULL;

	/* The parent class ensures this, but recheck anyway, for completeness */
	g_return_val_if_fail (CAMEL_IS_O365_FOLDER (source), FALSE);
	g_return_val_if_fail (CAMEL_IS_O365_FOLDER (destination), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (source);

	if (!parent_store) {
		g_set_error_literal (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("Invalid folder state (missing parent store)"));
		return FALSE;
	}

	/* The parent class ensures this, but recheck anyway, for completeness */
	g_return_val_if_fail (camel_folder_get_parent_store (destination) == parent_store, FALSE);

	o365_store = CAMEL_O365_STORE (parent_store);

	if (!camel_o365_store_ensure_connected (o365_store, NULL, cancellable, error))
		return FALSE;

	for (ii = 0; ii < uids->len; ii++) {
		uids_list = g_slist_prepend (uids_list, g_ptr_array_index (uids, ii));
	}

	uids_list = g_slist_reverse (uids_list);

	success = o365_folder_copy_move_to_folder_sync (source, o365_store,
		uids_list, camel_o365_folder_get_id (CAMEL_O365_FOLDER (destination)),
		!delete_originals, cancellable, &local_error);

	g_slist_free (uids_list);

	/* Update destination folder only if not frozen, to not update
	   for each single message transfer during filtering.
	 */
	if (success && !camel_folder_is_frozen (destination)) {
		camel_operation_progress (cancellable, -1);

		o365_folder_refresh_info_sync (destination, cancellable, NULL);
	}

	if (local_error) {
		camel_o365_store_maybe_disconnect (o365_store, local_error);
		g_propagate_error (error, local_error);
	}

	return success;
}

static void
o365_folder_prepare_content_refresh (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_O365_FOLDER (folder));

	camel_o365_folder_summary_set_delta_link (CAMEL_O365_FOLDER_SUMMARY (camel_folder_get_folder_summary (folder)), NULL);
}

static gchar *
o365_folder_get_filename (CamelFolder *folder,
			  const gchar *uid,
			  GError **error)
{
	CamelO365Folder *o365_folder = CAMEL_O365_FOLDER (folder);

	return o365_folder_cache_dup_filename (o365_folder, uid);
}

static void
o365_folder_constructed (GObject *object)
{
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelService *service;
	CamelFolder *folder;
	const gchar *full_name;
	gchar *description;
	gchar *user;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_folder_parent_class)->constructed (object);

	folder = CAMEL_FOLDER (object);
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);
	user = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (settings));

	g_object_unref (settings);

	description = g_strdup_printf ("%s@Office365:%s", user, full_name);
	camel_folder_set_description (folder, description);
	g_free (description);

	g_free (user);
}

static void
o365_folder_dispose (GObject *object)
{
	CamelO365Folder *o365_folder = CAMEL_O365_FOLDER (object);
	CamelFolderSummary *summary;

	summary = camel_folder_get_folder_summary (CAMEL_FOLDER (o365_folder));

	if (summary)
		o365_folder_save_summary (o365_folder);

	LOCK_CACHE (o365_folder);
	g_clear_object (&o365_folder->priv->cache);
	UNLOCK_CACHE (o365_folder);

	LOCK_SEARCH (o365_folder);
	g_clear_object (&o365_folder->priv->search);
	UNLOCK_SEARCH (o365_folder);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_folder_parent_class)->dispose (object);
}

static void
o365_folder_finalize (GObject *object)
{
	CamelO365Folder *o365_folder = CAMEL_O365_FOLDER (object);

	g_rec_mutex_clear (&o365_folder->priv->cache_lock);
	g_mutex_clear (&o365_folder->priv->search_lock);
	g_mutex_clear (&o365_folder->priv->get_message_lock);
	g_cond_clear (&o365_folder->priv->get_message_cond);

	g_hash_table_destroy (o365_folder->priv->get_message_hash);

	g_clear_pointer (&o365_folder->priv->id, g_free);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_folder_parent_class)->finalize (object);
}

static void
camel_o365_folder_class_init (CamelO365FolderClass *klass)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = o365_folder_constructed;
	object_class->dispose = o365_folder_dispose;
	object_class->finalize = o365_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (klass);
	folder_class->get_permanent_flags = o365_folder_get_permanent_flags;
	folder_class->get_message_cached = o365_folder_get_message_cached;
	folder_class->search_by_expression = o365_folder_search_by_expression;
	folder_class->count_by_expression = o365_folder_count_by_expression;
	folder_class->search_by_uids = o365_folder_search_by_uids;
	folder_class->search_free = o365_folder_search_free;
	folder_class->cmp_uids = o365_folder_cmp_uids;
	folder_class->append_message_sync = o365_folder_append_message_sync;
	folder_class->get_message_sync = o365_folder_get_message_sync;
	folder_class->refresh_info_sync = o365_folder_refresh_info_sync;
	folder_class->synchronize_sync = o365_folder_synchronize_sync;
	folder_class->expunge_sync = o365_folder_expunge_sync;
	folder_class->transfer_messages_to_sync = o365_folder_transfer_messages_to_sync;
	folder_class->prepare_content_refresh = o365_folder_prepare_content_refresh;
	folder_class->get_filename = o365_folder_get_filename;
}

static void
camel_o365_folder_init (CamelO365Folder *o365_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (o365_folder);

	o365_folder->priv = camel_o365_folder_get_instance_private (o365_folder);

	g_rec_mutex_init (&o365_folder->priv->cache_lock);
	g_mutex_init (&o365_folder->priv->search_lock);
	g_mutex_init (&o365_folder->priv->get_message_lock);
	g_cond_init (&o365_folder->priv->get_message_cond);

	o365_folder->priv->get_message_hash = g_hash_table_new (g_str_hash, g_str_equal);

	camel_folder_set_flags (folder, CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY);
	camel_folder_set_lock_async (folder, TRUE);
}

static gboolean
o365_folder_has_inbox_type (CamelO365Store *o365_store,
			    const gchar *full_name)
{
	CamelO365StoreSummary *summary;
	guint32 flags;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (o365_store), FALSE);
	g_return_val_if_fail (full_name != NULL, FALSE);

	summary = camel_o365_store_ref_store_summary (o365_store);

	if (!summary)
		return FALSE;

	flags = camel_o365_store_summary_get_folder_flags_for_full_name (summary, full_name);

	g_object_unref (summary);

	return (flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX;
}

CamelFolder *
camel_o365_folder_new (CamelStore *store,
		       const gchar *display_name,
		       const gchar *full_name,
		       const gchar *folder_dir,
		       GCancellable *cancellable,
		       GError **error)
{
	CamelFolder *folder;
	CamelFolderSummary *folder_summary;
	CamelO365Folder *o365_folder;
	CamelO365StoreSummary *o365_store_summary;
	CamelSettings *settings;
	gboolean filter_inbox = FALSE;
	gboolean filter_junk = FALSE;
	gboolean filter_junk_inbox = FALSE;
	gboolean offline_limit_by_age = FALSE;
	CamelTimeUnit offline_limit_unit;
	gint offline_limit_value = 0;
	guint32 add_folder_flags = 0;
	gchar *state_file;
	gchar *folder_id;

	o365_store_summary = camel_o365_store_ref_store_summary (CAMEL_O365_STORE (store));
	folder_id = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store_summary, full_name);
	g_clear_object (&o365_store_summary);

	if (!folder_id) {
		g_set_error (error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_PATH,
			_("Folder “%s” doesn't correspond to any known folder"), full_name);
		return NULL;
	}

	folder = g_object_new (CAMEL_TYPE_O365_FOLDER,
		"display_name", display_name,
		"full-name", full_name,
		"parent_store", store,
		NULL);

	o365_folder = CAMEL_O365_FOLDER (folder);
	o365_folder->priv->id = folder_id;

	folder_summary = camel_o365_folder_summary_new (folder);

	if (!folder_summary) {
		g_object_unref (folder);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load summary for “%s”"), full_name);
		return NULL;
	}

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	g_object_get (
		settings,
		"filter-inbox", &filter_inbox,
		"filter-junk", &filter_junk,
		"filter-junk-inbox", &filter_junk_inbox,
		"limit-by-age", &offline_limit_by_age,
		"limit-unit", &offline_limit_unit,
		"limit-value", &offline_limit_value,
		NULL);

	g_clear_object (&settings);

	camel_folder_take_folder_summary (folder, folder_summary);

	/* set/load persistent state */
	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));
	g_free (state_file);

	o365_folder->priv->cache = camel_data_cache_new (folder_dir, error);
	if (!o365_folder->priv->cache) {
		g_object_unref (folder);
		return NULL;
	}

	if (camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder))) {
		time_t when = (time_t) 0;

		if (offline_limit_by_age)
			when = camel_time_value_apply (when, offline_limit_unit, offline_limit_value);

		if (when <= (time_t) 0)
			when = (time_t) -1;

		/* Ensure cache will expire when set up, otherwise
		 * it causes redownload of messages too soon. */
		camel_data_cache_set_expire_age (o365_folder->priv->cache, when);
		camel_data_cache_set_expire_access (o365_folder->priv->cache, when);
	} else {
		/* Set cache expiration for one week. */
		camel_data_cache_set_expire_age (o365_folder->priv->cache, 60 * 60 * 24 * 7);
		camel_data_cache_set_expire_access (o365_folder->priv->cache, 60 * 60 * 24 * 7);
	}

	camel_binding_bind_property (store, "online",
		o365_folder->priv->cache, "expire-enabled",
		G_BINDING_SYNC_CREATE);

	if (o365_folder_has_inbox_type (CAMEL_O365_STORE (store), full_name)) {
		if (filter_inbox)
			add_folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

		if (filter_junk)
			add_folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else {
		if (filter_junk && !filter_junk_inbox)
			add_folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	}

	if (add_folder_flags)
		camel_folder_set_flags (folder, camel_folder_get_flags (folder) | add_folder_flags);

	camel_o365_store_connect_folder_summary (CAMEL_O365_STORE (store), folder_summary);

	o365_folder->priv->search = camel_folder_search_new ();

	return folder;
}

const gchar *
camel_o365_folder_get_id (CamelO365Folder *o365_folder)
{
	g_return_val_if_fail (CAMEL_IS_O365_FOLDER (o365_folder), NULL);

	return o365_folder->priv->id;
}
