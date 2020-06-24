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
	CamelO365StoreSummary *o365_store_summary;
	CamelStore *parent_store;
	CamelStream *cache_stream = NULL;
	EO365Connection *cnc = NULL;
	GError *local_error = NULL;
	gchar *folder_id;
	gboolean success = TRUE, remove_from_hash = FALSE;

	g_return_val_if_fail (CAMEL_IS_O365_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	parent_store = camel_folder_get_parent_store (folder);

	if (!parent_store)
		return NULL;

	o365_folder = CAMEL_O365_FOLDER (folder);
	o365_store = CAMEL_O365_STORE (parent_store);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return NULL;

	o365_store_summary = camel_o365_store_ref_store_summary (o365_store);

	folder_id = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store_summary,
		camel_folder_get_full_name (folder));

	if (!folder_id) {
		g_set_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER, _("No such folder: %s"),
			camel_folder_get_full_name (folder));

		g_clear_object (&o365_store_summary);
		g_clear_object (&cnc);

		return NULL;
	}

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

	g_clear_object (&o365_store_summary);
	g_clear_object (&cache_stream);
	g_clear_object (&cnc);
	g_free (folder_id);

	if (remove_from_hash) {
		g_mutex_lock (&o365_folder->priv->get_message_lock);
		g_hash_table_remove (o365_folder->priv->get_message_hash, uid);
		g_cond_broadcast (&o365_folder->priv->get_message_cond);
		g_mutex_unlock (&o365_folder->priv->get_message_lock);
	}

	return message;
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

	if (!mi) {
		EO365Recipient *from;
		const gchar *ctmp;
		time_t tt;
		gchar *tmp;

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

		camel_message_info_set_abort_notifications (mi, FALSE);
	}

	camel_message_info_set_abort_notifications (mi, TRUE);
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
	CamelO365StoreSummary *o365_store_summary;
	CamelFolderSummary *folder_summary;
	CamelStore *parent_store;
	EO365Connection *cnc = NULL;
	SummaryDeltaData sdd;
	GError *local_error = NULL;
	gchar *folder_id, *curr_delta_link, *new_delta_link = NULL;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_O365_FOLDER (folder), FALSE);

	parent_store = camel_folder_get_parent_store (folder);

	if (!parent_store)
		return FALSE;

	o365_folder = CAMEL_O365_FOLDER (folder);
	o365_store = CAMEL_O365_STORE (parent_store);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
		return FALSE;

	o365_store_summary = camel_o365_store_ref_store_summary (o365_store);

	folder_id = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store_summary,
		camel_folder_get_full_name (folder));

	if (!folder_id) {
		g_set_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER, _("No such folder: %s"),
			camel_folder_get_full_name (folder));

		g_clear_object (&o365_store_summary);
		g_clear_object (&cnc);

		return FALSE;
	}

	folder_summary = camel_folder_get_folder_summary (folder);
	o365_folder_summary = CAMEL_O365_FOLDER_SUMMARY (folder_summary);

	curr_delta_link = camel_o365_folder_summary_dup_delta_link (o365_folder_summary);

	sdd.folder = folder;
	sdd.changes = NULL;
	sdd.removed_uids = NULL;

	success = e_o365_connection_get_mail_messages_delta_sync (cnc, NULL, folder_id, O365_FETCH_SUMMARY_PROPERTIES,
		curr_delta_link, 0, o365_folder_got_summary_messages_cb, &sdd,
		&new_delta_link, cancellable, &local_error);

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

	g_clear_object (&o365_store_summary);
	g_clear_object (&cnc);
	g_free (curr_delta_link);
	g_free (new_delta_link);
	g_free (folder_id);

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
#if 0
	folder_class->append_message_sync = o365_folder_append_message_sync;
#endif
	folder_class->get_message_sync = o365_folder_get_message_sync;
	folder_class->refresh_info_sync = o365_folder_refresh_info_sync;
#if 0
	folder_class->synchronize_sync = o365_folder_synchronize_sync;
	folder_class->expunge_sync = o365_folder_expunge_sync;
	folder_class->transfer_messages_to_sync = o365_folder_transfer_messages_to_sync;
#endif
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
	CamelSettings *settings;
	gboolean filter_inbox = FALSE;
	gboolean filter_junk = FALSE;
	gboolean filter_junk_inbox = FALSE;
	gboolean offline_limit_by_age = FALSE;
	CamelTimeUnit offline_limit_unit;
	gint offline_limit_value = 0;
	guint32 add_folder_flags = 0;
	gchar *state_file;

	folder = g_object_new (CAMEL_TYPE_O365_FOLDER,
		"display_name", display_name,
		"full-name", full_name,
		"parent_store", store,
		NULL);

	o365_folder = CAMEL_O365_FOLDER (folder);

	folder_summary = camel_o365_folder_summary_new (folder);

	if (!folder_summary) {
		g_object_unref (folder);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load summary for %s"), full_name);
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
