/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-folder.c: class for an ews folder */

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
 *
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
#include <libical/icalcomponent.h>
#include <libical/icalparser.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-connection.h"
#include "server/e-ews-item-change.h"
#include "server/e-ews-message.h"

#include "utils/ews-camel-common.h"

#include "camel-ews-folder.h"
#include "camel-ews-private.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"

#define EWS_MAX_FETCH_COUNT 100

#define MAX_ATTACHMENT_SIZE 1*1024*1024   /*In bytes*/

#define SUMMARY_ITEM_FLAGS "item:ResponseObjects item:Sensitivity item:Importance item:Categories"
#define ITEM_PROPS "item:Subject item:DateTimeReceived item:DateTimeSent item:DateTimeCreated item:Size " \
		   "item:HasAttachments item:InReplyTo"
#define SUMMARY_ITEM_PROPS ITEM_PROPS " " SUMMARY_ITEM_FLAGS

#define SUMMARY_MESSAGE_FLAGS SUMMARY_ITEM_FLAGS " message:IsRead mapi:int:0x0e07 mapi:int:0x0e17 mapi:int:0x1080 mapi:int:0x1081"
#define SUMMARY_MESSAGE_PROPS ITEM_PROPS " message:From message:Sender message:ToRecipients message:CcRecipients " \
		   "message:BccRecipients message:IsRead message:References message:InternetMessageId " \
		   SUMMARY_MESSAGE_FLAGS

#define SUMMARY_POSTITEM_PROPS ITEM_PROPS " " SUMMARY_ITEM_FLAGS " message:From message:Sender"

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
	GCond *fetch_cond;
	GHashTable *uid_eflags;
};

extern gint camel_application_is_exiting;

static gboolean ews_delete_messages (CamelFolder *folder, GSList *deleted_items, gboolean expunge, GCancellable *cancellable, GError **error);
static gboolean ews_refresh_info_sync (CamelFolder *folder, GCancellable *cancellable, GError **error);

#define d(x)

G_DEFINE_TYPE (CamelEwsFolder, camel_ews_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static gchar *
ews_get_filename (CamelFolder *folder,
                  const gchar *uid,
                  GError **error)
{
	CamelEwsFolder *ews_folder = CAMEL_EWS_FOLDER (folder);
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gchar *filename;

	g_checksum_update (sha, (guchar *) uid, strlen (uid));
	filename = camel_data_cache_get_filename (
		ews_folder->cache, "cur", g_checksum_get_string (sha));
	g_checksum_free (sha);

	return filename;
}

static gint
ews_data_cache_remove (CamelDataCache *cdc,
                       const gchar *path,
                       const gchar *key,
                       GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gint ret;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	ret = camel_data_cache_remove (cdc, path, g_checksum_get_string (sha),
				       error);
	g_checksum_free (sha);
	return ret;
}

static CamelStream *
ews_data_cache_get (CamelDataCache *cdc,
                    const gchar *path,
                    const gchar *key,
                    GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	CamelStream *ret;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	ret = camel_data_cache_get (cdc, path, g_checksum_get_string (sha),
				    error);
	g_checksum_free (sha);
	return ret;
}

static gchar *
ews_data_cache_get_filename (CamelDataCache *cdc,
                             const gchar *path,
                             const gchar *key,
                             GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gchar *filename;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	filename = camel_data_cache_get_filename (
		cdc, path, g_checksum_get_string (sha));
	g_checksum_free (sha);

	return filename;
}

static CamelMimeMessage *
camel_ews_folder_get_message_from_cache (CamelEwsFolder *ews_folder,
                                         const gchar *uid,
                                         GCancellable *cancellable,
                                         GError **error)
{
	CamelStream *stream;
	CamelMimeMessage *msg;
	CamelEwsFolderPrivate *priv;

	priv = ews_folder->priv;

	g_static_rec_mutex_lock (&priv->cache_lock);
	stream = ews_data_cache_get (ews_folder->cache, "cur", uid, error);
	if (!stream) {
		gchar *old_fname = camel_data_cache_get_filename (
			ews_folder->cache, "cur", uid);
		if (!g_access (old_fname, R_OK)) {
			gchar *new_fname = ews_data_cache_get_filename (ews_folder->cache,
									"cur", uid, error);
			g_rename (old_fname, new_fname);
			g_free (new_fname);
			stream = ews_data_cache_get (ews_folder->cache, "cur", uid, error);
		}
		g_free (old_fname);
		if (!stream) {
			g_static_rec_mutex_unlock (&priv->cache_lock);
			return NULL;
		}
	}

	msg = camel_mime_message_new ();

	if (!camel_data_wrapper_construct_from_stream_sync (
				(CamelDataWrapper *) msg, stream, cancellable, error)) {
		g_object_unref (msg);
		msg = NULL;
	}

	g_static_rec_mutex_unlock (&priv->cache_lock);
	g_object_unref (stream);

	return msg;
}

static CamelMimePart *
ews_get_calendar_mime_part (CamelMimePart *mimepart)
{
	guint partnumber, i;
	CamelDataWrapper *datawrapper;
	datawrapper = camel_medium_get_content (CAMEL_MEDIUM (mimepart));

	if (CAMEL_IS_MULTIPART (datawrapper)) {
		partnumber = camel_multipart_get_number (CAMEL_MULTIPART (datawrapper));
		for (i = 0; i < partnumber; i++) {
			CamelMimePart *child_mimepart = NULL;
			CamelMimePart *ret_mimepart = NULL;
			child_mimepart = camel_multipart_get_part (CAMEL_MULTIPART (datawrapper), i);

			if (!child_mimepart)
				goto exit;

			ret_mimepart = ews_get_calendar_mime_part (child_mimepart);
			if (ret_mimepart)
				return ret_mimepart;
		}
	}
	else {
		gchar *type;
		type = camel_data_wrapper_get_mime_type (datawrapper);
		if (!g_ascii_strcasecmp (type, "text/calendar")) {
			g_free (type);
			return mimepart;
		}
		g_free (type);
	}
	exit:
	return NULL;
}

static gchar *
ews_update_mgtrequest_mime_calendar_itemid (const gchar *mime_fname,
                                            const EwsId *item_id,
                                            gboolean is_calendar_UID,
                                            GError **error)
{
	CamelMimeParser *mimeparser;
	CamelMimeMessage *msg;
	CamelMimePart *mimepart = NULL;
	gint fd_old;
	gchar *mime_fname_new = NULL;

	// original mime file
	fd_old = open (mime_fname, O_RDONLY);
	if (fd_old == -1) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Unable to open mimecontent temporary file!"));
		return NULL;
	}

	mimeparser = camel_mime_parser_new ();
	if (camel_mime_parser_init_with_fd (mimeparser, fd_old) == -1) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Unable to generate parser from mimecontent!"));
		goto exit_parser;
	}

	msg = camel_mime_message_new ();
	if (camel_mime_part_construct_from_parser_sync (CAMEL_MIME_PART (msg),
							mimeparser, NULL,
							error) == -1) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Unable to parse meeting request mimecontent!"));
		goto exit_msg;
	}

	mimepart = ews_get_calendar_mime_part (CAMEL_MIME_PART (msg));
	if (mimepart) {
		CamelDataWrapper *dw;
		CamelStream *tmpstream = NULL, *newstream = NULL;
		GByteArray *ba;
		icalcomponent *icalcomp, *subcomp;
		icalproperty *icalprop;
		gchar *calstring_new, *dir;
		const gchar *temp;
		gint fd;
		gboolean success = FALSE;

		dw = camel_medium_get_content (CAMEL_MEDIUM (mimepart));
		tmpstream = camel_stream_mem_new ();
		if (camel_data_wrapper_decode_to_stream_sync (dw, tmpstream,
							      NULL, error) == -1) {
			g_object_unref (tmpstream);
			goto exit_msg;
		}
		ba = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (tmpstream));
		g_byte_array_append (ba, (guint8 *) "\0", 1);
		icalcomp = icalparser_parse_string ((gchar *) ba->data);
		subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VEVENT_COMPONENT);
		icalprop = icalproperty_new_x (item_id->change_key);
		icalproperty_set_x_name (icalprop, "X-EVOLUTION-CHANGEKEY");

		/* In order to accept items we have to store AssociatedCalendarItemId (X-EVOLUTION-ITEMID)
		 * or mail id (X-EVOLUTION-ACCEPT-ID ) when we do not have AssociatedCalendarItemId */
		icalcomponent_add_property (subcomp, icalprop);
		if (is_calendar_UID) {
			icalprop = icalproperty_new_x (item_id->id);
			icalproperty_set_x_name (icalprop, "X-EVOLUTION-ITEMID");
			icalcomponent_add_property (subcomp, icalprop);
		} else {
			icalprop = icalproperty_new_x (item_id->id);
			icalproperty_set_x_name (icalprop, "X-EVOLUTION-ACCEPT-ID");
			icalcomponent_add_property (subcomp, icalprop);
		}
		calstring_new = icalcomponent_as_ical_string_r (icalcomp);
		camel_mime_part_set_content (mimepart,
					     (const gchar *) calstring_new, strlen (calstring_new),
					     "text/calendar");
		g_free (calstring_new);
		icalcomponent_free (icalcomp);
		g_object_unref (tmpstream);

		// Create a new file to store updated mimecontent
		temp = g_strrstr (mime_fname, "/");
		dir = g_strndup (mime_fname, temp - mime_fname);
		mime_fname_new = g_build_filename ((const gchar*) dir, "XXXXXX", NULL);
		fd = g_mkstemp (mime_fname_new);
		if (fd == -1) {
			g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				     _("Unable to create cache file"));
			goto exit_save;
		}
		newstream = camel_stream_fs_new_with_fd (fd);
		if (camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (msg),
							     newstream, NULL, error) == -1)
			goto exit_save;
		if (camel_stream_flush (newstream, NULL, error) == -1)
			goto exit_save;
		if (camel_stream_close (newstream, NULL, error) == -1)
			goto exit_save;
		g_remove (mime_fname);
		success = TRUE;
 exit_save:
		if (fd != -1) {
			close (fd);
			fd = -1;
		}
		g_free (dir);
		if (newstream)
			g_object_unref (newstream);
		if (!success) {
			g_free (mime_fname_new);
			mime_fname_new = NULL;
		}
	}
 exit_msg:
	g_object_unref (msg);
 exit_parser:
	g_object_unref (mimeparser);
	close (fd_old);
	fd_old = -1;

	// must be freed in the caller
	return mime_fname_new;
}

static CamelMimeMessage *
camel_ews_folder_get_message (CamelFolder *folder,
                              const gchar *uid,
                              gint pri,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	EEwsConnection *cnc;
	CamelEwsStore *ews_store;
	const gchar *mime_content;
	CamelMimeMessage *message = NULL;
	CamelStream *tmp_stream = NULL;
	GSList *ids = NULL, *items = NULL;
	gchar *mime_dir;
	gchar *cache_file;
	gchar *dir;
	const gchar *temp;
	gboolean res;
	gchar *mime_fname_new = NULL;
	GError *local_error = NULL;

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);
	ews_folder = (CamelEwsFolder *) folder;
	priv = ews_folder->priv;

	if (!camel_ews_store_connected (ews_store, error))
		return NULL;

	g_mutex_lock (priv->state_lock);

	/* If another thread is already fetching this message, wait for it */

	/* FIXME: We might end up refetching a message anyway, if another
	 * thread has already finished fetching it by the time we get to
	 * this point in the code — ews_folder_get_message_sync() doesn't
	 * hold any locks when it calls get_message_from_cache() and then
	 * falls back to this function. */
	if (g_hash_table_lookup (priv->uid_eflags, uid)) {
		do {
			g_cond_wait (priv->fetch_cond, priv->state_lock);
		} while (g_hash_table_lookup (priv->uid_eflags, uid));

		g_mutex_unlock (priv->state_lock);

		message = camel_ews_folder_get_message_from_cache (ews_folder, uid, cancellable, error);
		return message;
	}

	/* Because we're using this as a form of mutex, we *know* that
	 * we won't be inserting where an entry already exists. So it's
	 * OK to insert uid itself, not g_strdup (uid) */
	g_hash_table_insert (priv->uid_eflags, (gchar *) uid, (gchar *) uid);
	g_mutex_unlock (priv->state_lock);

	cnc = camel_ews_store_get_connection (ews_store);
	ids = g_slist_append (ids, (gchar *) uid);

	mime_dir = g_build_filename (camel_data_cache_get_path (ews_folder->cache),
				     "mimecontent", NULL);

	if (g_access (mime_dir, F_OK) == -1 &&
	    g_mkdir_with_parents (mime_dir, 0700) == -1) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Unable to create cache path"));
		g_free (mime_dir);
		goto exit;
	}

	res = e_ews_connection_get_items_sync (
		cnc, pri, ids, "IdOnly", "item:MimeContent",
		TRUE, mime_dir,
		&items,
		(ESoapProgressFn) camel_operation_progress,
		(gpointer) cancellable,
		cancellable, &local_error);
	g_free (mime_dir);

	if (!res || !items) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		goto exit;
	}

	/* The mime_content actually contains the *filename*, due to the
	 * streaming hack in ESoapMessage */
	mime_content = e_ews_item_get_mime_content (items->data);

	/* Exchange returns random UID for associated calendar item, which has no way
	 * to match with calendar components saved in calendar cache. So manually get
	 * AssociatedCalendarItemId, replace the random UID with this ItemId,
	 * And save updated message data to a new temp file */
	if (e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_REQUEST ||
		e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_CANCELLATION ||
		e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_MESSAGE ||
		e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_RESPONSE) {
		GSList *items_req = NULL;
		const EwsId *calendar_item_accept_id;
		gboolean is_calendar_UID = TRUE;

		// Get AssociatedCalendarItemId with second get_items call
		res = e_ews_connection_get_items_sync (
			cnc, pri, ids, "IdOnly",
			"meeting:AssociatedCalendarItemId",
			FALSE, NULL,
			&items_req,
			(ESoapProgressFn) camel_operation_progress,
			(gpointer) cancellable,
			cancellable, &local_error);
		if (!res || (items_req && e_ews_item_get_item_type (items_req->data) == E_EWS_ITEM_TYPE_ERROR)) {
			if (items_req) {
				g_object_unref (items_req->data);
				g_slist_free (items_req);
			}
			if (local_error) {
				camel_ews_store_maybe_disconnect (ews_store, local_error);
				g_propagate_error (error, local_error);
			}
			goto exit;
		}
		calendar_item_accept_id = e_ews_item_get_calendar_item_accept_id (items_req->data);
		/*In case of non-exchange based meetings invites the calendar backend have to create the meeting*/
		if (!calendar_item_accept_id) {
			calendar_item_accept_id = e_ews_item_get_id (items->data);
			is_calendar_UID = FALSE;
		}
		mime_fname_new = ews_update_mgtrequest_mime_calendar_itemid (mime_content, calendar_item_accept_id, is_calendar_UID, error);
		if (mime_fname_new)
			mime_content = (const gchar *) mime_fname_new;

		if (items_req) {
			g_object_unref (items_req->data);
			g_slist_free (items_req);
		}
	}

	cache_file = ews_data_cache_get_filename (ews_folder->cache, "cur",
						  uid, error);
	temp = g_strrstr (cache_file, "/");
	dir = g_strndup (cache_file, temp - cache_file);

	if (g_mkdir_with_parents (dir, 0700) == -1) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Unable to create cache path"));
		g_free (dir);
		g_free (cache_file);
		goto exit;
	}
	g_free (dir);

	if (g_rename (mime_content, cache_file) != 0) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Failed to move message cache file"));
		g_free (cache_file);
		goto exit;
	}
	g_free (cache_file);

	message = camel_ews_folder_get_message_from_cache (ews_folder, uid, cancellable, error);

exit:
	g_mutex_lock (priv->state_lock);
	g_hash_table_remove (priv->uid_eflags, uid);
	g_mutex_unlock (priv->state_lock);
	g_cond_broadcast (priv->fetch_cond);

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

	if (mime_fname_new)
		g_free (mime_fname_new);
	g_object_unref (cnc);

	return message;
}

/* Get the message from cache if available otherwise get it from server */
static CamelMimeMessage *
ews_folder_get_message_sync (CamelFolder *folder,
                             const gchar *uid,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelMimeMessage *message;

	message = camel_ews_folder_get_message_from_cache ((CamelEwsFolder *) folder, uid, cancellable, NULL);
	if (!message)
		message = camel_ews_folder_get_message (folder, uid, EWS_ITEM_HIGH, cancellable, error);

	return message;
}

static GPtrArray *
ews_folder_search_by_expression (CamelFolder *folder,
                                 const gchar *expression,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	GPtrArray *matches;

	ews_folder = CAMEL_EWS_FOLDER (folder);
	priv = ews_folder->priv;

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (ews_folder->search, folder);
	matches = camel_folder_search_search (ews_folder->search, expression, NULL, cancellable, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static guint32
ews_folder_count_by_expression (CamelFolder *folder,
                                const gchar *expression,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	guint32 matches;

	ews_folder = CAMEL_EWS_FOLDER (folder);
	priv = ews_folder->priv;

	g_mutex_lock (priv->search_lock);

	camel_folder_search_set_folder (ews_folder->search, folder);
	matches = camel_folder_search_count (ews_folder->search, expression, cancellable, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static GPtrArray *
ews_folder_search_by_uids (CamelFolder *folder,
                           const gchar *expression,
                           GPtrArray *uids,
                           GCancellable *cancellable,
                           GError **error)
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
	matches = camel_folder_search_search (ews_folder->search, expression, uids, cancellable, error);

	g_mutex_unlock (priv->search_lock);

	return matches;
}

static void
ews_folder_search_free (CamelFolder *folder,
                        GPtrArray *uids)
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

static void
msg_update_flags (ESoapMessage *msg,
                  gpointer user_data)
{
	GSList *mi_list = user_data;
	CamelEwsMessageInfo *mi;

	while ((mi = g_slist_nth_data (mi_list, 0))) {
		guint32 flags_changed;

		mi_list = g_slist_remove (mi_list, mi);

		flags_changed = mi->server_flags ^ mi->info.flags;

		e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
						 mi->info.uid, mi->change_key, 0);
		if (flags_changed & CAMEL_MESSAGE_FLAGGED) {
			const gchar *flag;

			if (mi->info.flags & CAMEL_MESSAGE_FLAGGED)
				flag = "High";
			else
				flag = "Normal";

			e_soap_message_start_element (msg, "SetItemField", NULL, NULL);

			e_soap_message_start_element (msg, "FieldURI", NULL, NULL);
			e_soap_message_add_attribute (msg, "FieldURI", "item:Importance", NULL, NULL);
			e_soap_message_end_element (msg);

			e_soap_message_start_element (msg, "Message", NULL, NULL);

			e_ews_message_write_string_parameter (msg, "Importance", NULL, flag);

			e_soap_message_end_element (msg); /* Message */
			e_soap_message_end_element (msg); /* SetItemField */
		}

		if (flags_changed & CAMEL_MESSAGE_SEEN) {
			e_soap_message_start_element (msg, "SetItemField", NULL, NULL);

			e_soap_message_start_element (msg, "FieldURI", NULL, NULL);
			e_soap_message_add_attribute (msg, "FieldURI", "message:IsRead", NULL, NULL);
			e_soap_message_end_element (msg);

			e_soap_message_start_element (msg, "Message", NULL, NULL);
			e_ews_message_write_string_parameter (msg, "IsRead", NULL,
					      (mi->info.flags & CAMEL_MESSAGE_SEEN)?"true":"false");

			e_soap_message_end_element (msg); /* Message */
			e_soap_message_end_element (msg); /* SetItemField */
		}
		/* Ick Ick Ick. Why in hell is there a field in the database for the Icon
		 * *anyway*? Why isn't there a better place for forwarded/answered status? */
		if (flags_changed & (CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_ANSWERED)) {
			gint icon = (mi->info.flags & CAMEL_MESSAGE_SEEN) ? 0x100 : 0x101;

			if (mi->info.flags & CAMEL_MESSAGE_ANSWERED)
				icon = 0x105;
			if (mi->info.flags & CAMEL_MESSAGE_FORWARDED)
				icon = 0x106;

			e_soap_message_start_element (msg, "SetItemField", NULL, NULL);

			e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
			e_soap_message_add_attribute (msg, "PropertyTag", "0x1080", NULL, NULL);
			e_soap_message_add_attribute (msg, "PropertyType", "Integer", NULL, NULL);
			e_soap_message_end_element (msg);

			e_soap_message_start_element (msg, "Message", NULL, NULL);
			e_soap_message_start_element (msg, "ExtendedProperty", NULL, NULL);

			/* And now we have to specify the field *again*. Yay for XML crap */
			e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
			e_soap_message_add_attribute (msg, "PropertyTag", "0x1080", NULL, NULL);
			e_soap_message_add_attribute (msg, "PropertyType", "Integer", NULL, NULL);
			e_soap_message_end_element (msg);

			e_ews_message_write_int_parameter (msg, "Value", NULL, icon);

			e_soap_message_end_element (msg); /* ExtendedProperty */
			e_soap_message_end_element (msg); /* Message */
			e_soap_message_end_element (msg); /* SetItemField */
		}
		/* now update the Categories */
		e_soap_message_start_element (msg, "SetItemField", NULL, NULL);

		e_soap_message_start_element (msg, "FieldURI", NULL, NULL);
		e_soap_message_add_attribute (msg, "FieldURI", "item:Categories", NULL, NULL);
		e_soap_message_end_element (msg);

		e_soap_message_start_element (msg, "Message", NULL, NULL);
		e_soap_message_start_element (msg, "Categories", NULL, NULL);

		ews_utils_replace_server_user_flags (msg, mi);

		e_soap_message_end_element (msg); /* Categories */
		e_soap_message_end_element (msg); /* Message */
		e_soap_message_end_element (msg); /* SetItemField */

		e_ews_message_end_item_change (msg);

		mi->info.flags = mi->info.flags & (~CAMEL_MESSAGE_FOLDER_FLAGGED);
		mi->info.dirty = TRUE;

		camel_folder_summary_touch (mi->info.summary);

		camel_message_info_free (mi);
	}
	/* Don't think we need to free the list; we already freed every element */
}

static gboolean
ews_sync_mi_flags (CamelFolder *folder,
                   GSList *mi_list,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelEwsStore *ews_store;
	EEwsConnection *cnc;
	GError *local_error = NULL;
	gboolean res;

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	if (!camel_ews_store_connected (ews_store, error)) {
		return FALSE;
	}

	cnc = camel_ews_store_get_connection (ews_store);

	res = e_ews_connection_update_items_sync (
		cnc, EWS_PRIORITY_LOW,
		"AlwaysOverwrite", "SaveOnly",
		NULL, NULL,
		msg_update_flags, mi_list, NULL,
		cancellable, &local_error);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
	}

	g_object_unref (cnc);

	return res;
}

static gboolean
ews_folder_is_of_type (CamelFolder *folder,
                       guint32 folder_type)
{
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	gboolean is_of_type;
	gchar *folder_id;

	g_return_val_if_fail (folder != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	g_return_val_if_fail (ews_store != NULL, FALSE);

	folder_type = folder_type & CAMEL_FOLDER_TYPE_MASK;
	folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, camel_folder_get_full_name (folder));
	is_of_type = folder_id &&
		(camel_ews_store_summary_get_folder_flags (ews_store->summary, folder_id, NULL) & CAMEL_FOLDER_TYPE_MASK) == folder_type;
	g_free (folder_id);

	return is_of_type;
}

static gboolean
ews_move_to_junk_folder (CamelFolder *folder,
                         GSList *junk_uids,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	EEwsConnection *cnc;
	gboolean status = TRUE;

	if (ews_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_JUNK)) {
		/* cannot move to itself, but treat it as success */
		g_slist_free_full (junk_uids, (GDestroyNotify) camel_pstring_free);
		return TRUE;
	}

	ews_store = CAMEL_EWS_STORE (camel_folder_get_parent_store (folder));

	parent_store = camel_folder_get_parent_store (folder);
	ews_folder = CAMEL_EWS_FOLDER (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	if (!camel_ews_store_connected (ews_store, error))
		return FALSE;

	cnc = camel_ews_store_get_connection (ews_store);

	if (junk_uids) {
		GSList *moved_items = NULL;
		GError *local_error = NULL;
		gchar *folder_id;

		folder_id = camel_ews_store_summary_get_folder_id_from_folder_type (
			ews_store->summary, CAMEL_FOLDER_TYPE_JUNK);

		status = e_ews_connection_move_items_sync (cnc, EWS_PRIORITY_MEDIUM, folder_id, FALSE,
			junk_uids, &moved_items, cancellable, &local_error);

		if (!status && local_error && local_error->code == EWS_CONNECTION_ERROR_ITEMNOTFOUND) {
			/* If move failed due to the item not found, ignore the error,
			 * trigger folder info refresh and then go on to clear the
			 * cache of the remove items anyway. */
			g_clear_error (&local_error);
			status = ews_refresh_info_sync (folder, cancellable, &local_error);
		}

		g_slist_free_full (moved_items, g_object_unref);
		g_free (folder_id);

		if (status) {
			CamelFolderChangeInfo *changes;
			GSList *iter;

			changes = camel_folder_change_info_new ();

			for (iter = junk_uids; iter; iter = g_slist_next (iter)) {
				const gchar *uid = iter->data;

				camel_folder_summary_lock (folder->summary, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

				camel_folder_change_info_remove_uid (changes, uid);
				camel_folder_summary_remove_uid (folder->summary, uid);
				ews_data_cache_remove (ews_folder->cache, "cur", uid, NULL);

				camel_folder_summary_unlock (folder->summary, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
			}

			if (camel_folder_change_info_changed (changes)) {
				camel_folder_summary_touch (folder->summary);
				camel_folder_changed (folder, changes);
			}
			camel_folder_change_info_free (changes);
		}

		if (local_error) {
			camel_ews_store_maybe_disconnect (ews_store, local_error);
			g_propagate_error (error, local_error);
		}

		g_slist_free_full (junk_uids, (GDestroyNotify) camel_pstring_free);
	}

	g_object_unref (cnc);

	return status;
}

static gboolean
ews_synchronize_sync (CamelFolder *folder,
                      gboolean expunge,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelEwsStore *ews_store;
	GPtrArray *uids;
	GSList *mi_list = NULL, *deleted_uids = NULL, *junk_uids = NULL;
	gint mi_list_len = 0;
	gboolean success = TRUE;
	gint i;

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	if (!camel_ews_store_connected (ews_store, error))
		return FALSE;

	if (camel_folder_summary_get_deleted_count (folder->summary) > 0 ||
	    camel_folder_summary_get_junk_count (folder->summary) > 0) {
		camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
		uids = camel_folder_summary_get_array (folder->summary);
	} else {
		uids = camel_folder_summary_get_changed (folder->summary);
	}
	if (!uids || !uids->len) {
		camel_folder_summary_free_array (uids);
		return TRUE;
	}

	for (i = 0; success && i < uids->len; i++) {
		guint32 flags_changed, flags_set;
		CamelEwsMessageInfo *mi = (gpointer) camel_folder_summary_get (folder->summary, uids->pdata[i]);
		if (!mi)
			continue;

		flags_set = camel_message_info_flags (mi);
		flags_changed = mi->server_flags ^ flags_set;

		/* Exchange doesn't seem to have a sane representation
		 * for most flags — not even replied/forwarded. */
		if ((flags_set & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0 &&
		    (flags_changed & (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_FLAGGED)) != 0) {
			mi_list = g_slist_append (mi_list, mi);
			mi_list_len++;

			if (flags_set & CAMEL_MESSAGE_DELETED)
				deleted_uids = g_slist_prepend (deleted_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			else if (flags_set & CAMEL_MESSAGE_JUNK)
				junk_uids = g_slist_prepend (junk_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
		} else if (flags_set & CAMEL_MESSAGE_DELETED) {
			deleted_uids = g_slist_prepend (deleted_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			camel_message_info_free (mi);
		} else if (flags_set & CAMEL_MESSAGE_JUNK) {
			junk_uids = g_slist_prepend (junk_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			camel_message_info_free (mi);
		} else if ((flags_set & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0) {
			/* OK, the change must have been the labels */
			mi_list = g_slist_append (mi_list, mi);
			mi_list_len++;
		} else {
			camel_message_info_free (mi);
		}

		if (mi_list_len == EWS_MAX_FETCH_COUNT) {
			success = ews_sync_mi_flags (folder, mi_list, cancellable, error);
			mi_list = NULL;
			mi_list_len = 0;
		}
	}

	if (mi_list_len)
		success = ews_sync_mi_flags (folder, mi_list, cancellable, error);

	if (deleted_uids && success)
		success = ews_delete_messages (folder, deleted_uids, ews_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_TRASH), cancellable, error);
	else
		g_slist_free_full (deleted_uids, (GDestroyNotify) camel_pstring_free);

	if (junk_uids && success)
		success = ews_move_to_junk_folder (folder, junk_uids, cancellable, error);
	else
		g_slist_free_full (junk_uids, (GDestroyNotify) camel_pstring_free);

	camel_folder_summary_save_to_db (folder->summary, NULL);
	camel_folder_summary_free_array (uids);

	return success;
}

static void
ews_folder_count_notify_cb (CamelFolderSummary *folder_summary,
                            GParamSpec *param,
                            CamelFolder *folder)
{
	gint count;
	CamelEwsStore *ews_store;
	CamelEwsStoreSummary *store_summary;
	gchar *folder_id;

	g_return_if_fail (folder_summary != NULL);
	g_return_if_fail (param != NULL);
	g_return_if_fail (folder != NULL);
	g_return_if_fail (folder->summary == folder_summary);

	ews_store = CAMEL_EWS_STORE (camel_folder_get_parent_store (folder));
	g_return_if_fail (ews_store != NULL);

	store_summary = ews_store->summary;
	folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, camel_folder_get_full_name (folder));

	if (g_strcmp0 (g_param_spec_get_name (param), "saved-count") == 0) {
		count = camel_folder_summary_get_saved_count (folder_summary);
		camel_ews_store_summary_set_folder_total (store_summary, folder_id, count);
	} else if (g_strcmp0 (g_param_spec_get_name (param), "unread-count") == 0) {
		count = camel_folder_summary_get_unread_count (folder_summary);
		camel_ews_store_summary_set_folder_unread (store_summary, folder_id, count);
	} else {
		g_warn_if_reached ();
	}

	g_free (folder_id);
}

CamelFolder *
camel_ews_folder_new (CamelStore *store,
                      const gchar *folder_name,
                      const gchar *folder_dir,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelFolder *folder;
	CamelEwsFolder *ews_folder;
	gchar *state_file;
	const gchar *short_name;

	short_name = strrchr (folder_name, '/');
	if (!short_name)
		short_name = folder_name;
	else
		short_name++;

	folder = g_object_new (
		CAMEL_TYPE_EWS_FOLDER,
		"display_name", short_name, "full-name", folder_name,
		"parent_store", store, NULL);

	ews_folder = CAMEL_EWS_FOLDER (folder);

	folder->summary = camel_ews_summary_new (folder);

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
	g_free (state_file);

	ews_folder->cache = camel_data_cache_new (folder_dir, error);
	if (!ews_folder->cache) {
		g_object_unref (folder);
		return NULL;
	}

	if (!g_ascii_strcasecmp (folder_name, "Inbox")) {
		CamelStoreSettings *settings = CAMEL_STORE_SETTINGS (camel_service_get_settings (CAMEL_SERVICE (store)));

		if (camel_store_settings_get_filter_inbox (settings))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
	}

	ews_folder->search = camel_folder_search_new ();
	if (!ews_folder->search) {
		g_object_unref (folder);
		return NULL;
	}

	g_signal_connect (folder->summary, "notify::saved-count", G_CALLBACK (ews_folder_count_notify_cb), folder);
	g_signal_connect (folder->summary, "notify::unread-count", G_CALLBACK (ews_folder_count_notify_cb), folder);

	return folder;
}

static void
sync_updated_items (CamelEwsFolder *ews_folder,
                    EEwsConnection *cnc,
                    GSList *updated_items,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelEwsStore *ews_store;
	CamelFolder *folder = (CamelFolder *) ews_folder;
	GSList *items = NULL, *l;
	GSList *generic_item_ids = NULL, *msg_ids = NULL;
	GError *local_error = NULL;

	ews_store = CAMEL_EWS_STORE (camel_folder_get_parent_store (folder));

	for (l = updated_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id = e_ews_item_get_id (item);
		CamelMessageInfo *mi;

		/* Compare the item_type from summary as the updated items seems to
		 * arrive as generic types while its not the case */
		mi = camel_folder_summary_get (folder->summary, id->id);
		if (!mi) {
			g_object_unref (item);
			continue;
		}

		/* Check if the item has really changed */
		if (!strcmp (((CamelEwsMessageInfo *) mi)->change_key, id->change_key)) {
			camel_message_info_free (mi);
			g_object_unref (item);
			continue;
		}

		if (((CamelEwsMessageInfo *) mi)->item_type == E_EWS_ITEM_TYPE_GENERIC_ITEM)
			generic_item_ids = g_slist_append (generic_item_ids, g_strdup (id->id));
		else
			msg_ids = g_slist_append (msg_ids, g_strdup (id->id));

		camel_message_info_free (mi);
		g_object_unref (item);
	}
	g_slist_free (updated_items);

	if (msg_ids)
		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			 msg_ids, "IdOnly", SUMMARY_MESSAGE_FLAGS,
			 FALSE, NULL, &items, NULL, NULL,
			 cancellable, &local_error);

	camel_ews_utils_sync_updated_items (ews_folder, items);
	items = NULL;
	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		goto exit;
	}

	if (generic_item_ids)
		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			generic_item_ids, "IdOnly", SUMMARY_ITEM_FLAGS,
			FALSE, NULL, &items, NULL, NULL,
			cancellable, &local_error);
	camel_ews_utils_sync_updated_items (ews_folder, items);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
	}

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
sync_created_items (CamelEwsFolder *ews_folder,
                    EEwsConnection *cnc,
                    GSList *created_items,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelEwsStore *ews_store;
	GSList *items = NULL, *l;
	GSList *generic_item_ids = NULL, *msg_ids = NULL, *post_item_ids = NULL;
	GError *local_error = NULL;

	ews_store = CAMEL_EWS_STORE (camel_folder_get_parent_store (CAMEL_FOLDER (ews_folder)));

	for (l = created_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id;
		EEwsItemType item_type;

		if (!item)
			continue;

		id = e_ews_item_get_id (item);
		item_type = e_ews_item_get_item_type (item);
		/* created_msg_ids are items other than generic item. We fetch them
		 * separately since the property sets vary */
		/* FIXME: Do we need to handle any other item types
		 * "specially"? */
		if (item_type == E_EWS_ITEM_TYPE_MESSAGE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_REQUEST ||
			item_type == E_EWS_ITEM_TYPE_MEETING_MESSAGE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_RESPONSE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_CANCELLATION)
			msg_ids = g_slist_append (msg_ids, g_strdup (id->id));
		else if (item_type == E_EWS_ITEM_TYPE_POST_ITEM)
			post_item_ids = g_slist_append (post_item_ids, g_strdup (id->id));
		else
			generic_item_ids = g_slist_append (generic_item_ids, g_strdup (id->id));

		g_object_unref (item);
	}
	g_slist_free (created_items);

	if (msg_ids)
		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			msg_ids, "IdOnly", SUMMARY_MESSAGE_PROPS,
			FALSE, NULL, &items, NULL, NULL,
			cancellable, &local_error);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		goto exit;
	}

	camel_ews_utils_sync_created_items (ews_folder, cnc, items);
	items = NULL;

	if (post_item_ids)
		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			post_item_ids, "IdOnly", SUMMARY_POSTITEM_PROPS,
			FALSE, NULL, &items, NULL, NULL,
			cancellable, &local_error);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		goto exit;
	}

	camel_ews_utils_sync_created_items (ews_folder, cnc, items);
	items = NULL;

	if (generic_item_ids)
		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			generic_item_ids, "IdOnly", SUMMARY_ITEM_PROPS,
			FALSE, NULL, &items, NULL, NULL,
			cancellable, &local_error);

	camel_ews_utils_sync_created_items (ews_folder, cnc, items);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
	}
exit:
	if (msg_ids) {
		g_slist_foreach (msg_ids, (GFunc) g_free, NULL);
		g_slist_free (msg_ids);
	}

	if (post_item_ids) {
		g_slist_foreach (post_item_ids, (GFunc) g_free, NULL);
		g_slist_free (post_item_ids);
	}

	if (generic_item_ids) {
		g_slist_foreach (generic_item_ids, (GFunc) g_free, NULL);
		g_slist_free (generic_item_ids);
	}
}

static gboolean
ews_refresh_info_sync (CamelFolder *folder,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	EEwsConnection *cnc;
	CamelEwsStore *ews_store;
	const gchar *full_name;
	gchar *id;
	gchar *sync_state;
	gboolean includes_last_item = FALSE;
	GError *local_error = NULL;

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
	g_return_val_if_fail (cnc != NULL, FALSE);

	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);

	id = camel_ews_store_summary_get_folder_id_from_name
						(ews_store->summary,
						 full_name);

	/* Sync folder items does not return the fields ToRecipients,
	 * CCRecipients. With the item_type unknown, its not possible
	 * to fetch the right properties which are valid for an item type.
	 * Due to these reasons we just get the item ids and its type in
	 * SyncFolderItem request and fetch the item using the
	 * GetItem request. */
	sync_state = ((CamelEwsSummary *) folder->summary)->sync_state;
	do
	{
		GSList *items_created = NULL, *items_updated = NULL;
		GSList *items_deleted = NULL;
		guint32 total, unread;

		e_ews_connection_sync_folder_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			&sync_state, id,
			"IdOnly", NULL,
			EWS_MAX_FETCH_COUNT, &includes_last_item,
			&items_created, &items_updated,
			&items_deleted, cancellable, &local_error);

		if (local_error) {
			camel_ews_store_maybe_disconnect (ews_store, local_error);
			break;
		}

		if (items_deleted)
			camel_ews_utils_sync_deleted_items (ews_folder, items_deleted);

		if (items_created)
			sync_created_items (ews_folder, cnc, items_created, cancellable, &local_error);

		if (local_error) {
			if (items_updated) {
				g_slist_foreach (items_updated, (GFunc) g_object_unref, NULL);
				g_slist_free (items_updated);
			}

			break;
		}

		if (items_updated)
			sync_updated_items (ews_folder, cnc, items_updated, cancellable, &local_error);

		if (local_error)
			break;

		total = camel_folder_summary_count (folder->summary);
		unread = camel_folder_summary_get_unread_count (folder->summary);

		camel_ews_store_summary_set_folder_total (ews_store->summary, id, total);
		camel_ews_store_summary_set_folder_unread (ews_store->summary, id, unread);
		camel_ews_store_summary_save (ews_store->summary, NULL);

		g_free (((CamelEwsSummary *) folder->summary)->sync_state);
		((CamelEwsSummary *) folder->summary)->sync_state = sync_state;

		camel_folder_summary_touch (folder->summary);
	} while (!local_error && !includes_last_item);

	camel_folder_summary_save_to_db (folder->summary, NULL);

	if (local_error)
		g_propagate_error (error, local_error);

	g_mutex_lock (priv->state_lock);
	priv->refreshing = FALSE;
	g_mutex_unlock (priv->state_lock);
	if (sync_state != ((CamelEwsSummary *) folder->summary)->sync_state)
		g_free (sync_state);
	g_object_unref (cnc);
	g_free (id);

	return !local_error;
}

static gboolean
ews_append_message_sync (CamelFolder *folder,
                         CamelMimeMessage *message,
                         CamelMessageInfo *info,
                         gchar **appended_uid,
                         GCancellable *cancellable,
                         GError **error)
{
	gchar *itemid, *changekey;
	const gchar *folder_name;
	gchar *folder_id;
	CamelAddress *from;
	CamelEwsStore *ews_store;
	EEwsConnection *cnc;
	GError *local_error = NULL;

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	folder_name = camel_folder_get_full_name (folder);
	folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary,
								     folder_name);
	if (!folder_id)
		return FALSE;

	from = CAMEL_ADDRESS (camel_mime_message_get_from (message));

	if (!camel_ews_store_connected (ews_store, error)) {
		return FALSE;
	}

	cnc = camel_ews_store_get_connection (ews_store);

	if (!cnc) {
		g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			     _("Cant perform actions on the folder while in offline mode"));
		return FALSE;
	}

	if (!camel_ews_utils_create_mime_message (cnc, "SaveOnly", folder_id,
						  message,
						  camel_message_info_flags (info),
						  from, &itemid, &changekey,
						  cancellable, &local_error)) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		g_free (folder_id);
		g_object_unref (cnc);
		return FALSE;
	}
	g_free (folder_id);

	/* FIXME: Do we have to add it to the summary info ourselves?
	 * Hopefully, since we need to store the changekey with it... */
	if (appended_uid)
		*appended_uid = itemid;
	else
		g_free (itemid);
	g_free (changekey);

	g_object_unref (cnc);

	return TRUE;
}

/* move messages */
static gboolean
ews_transfer_messages_to_sync (CamelFolder *source,
                               GPtrArray *uids,
                               CamelFolder *destination,
                               gboolean delete_originals,
                               GPtrArray **transferred_uids,
                               GCancellable *cancellable,
                               GError **error)
{
	EEwsConnection *cnc;
	CamelEwsStore *dst_ews_store;
	CamelFolderChangeInfo *changes = NULL;
	const gchar *dst_full_name;
	gchar *dst_id;
	GError *local_error = NULL;
	GSList *ids = NULL, *ret_items = NULL;
	gint i = 0;

	dst_full_name = camel_folder_get_full_name (destination);
	dst_ews_store = (CamelEwsStore *) camel_folder_get_parent_store (destination);

	if (!camel_ews_store_connected (dst_ews_store, error))
		return FALSE;

	cnc = camel_ews_store_get_connection (dst_ews_store);
	dst_id = camel_ews_store_summary_get_folder_id_from_name
						(dst_ews_store->summary,
						 dst_full_name);

	for (i = 0; i < uids->len; i++) {
		ids = g_slist_append (ids, (gchar *) uids->pdata[i]);
	}

	if (e_ews_connection_move_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			dst_id, !delete_originals,
			ids, &ret_items,
			cancellable, &local_error)) {

		if (delete_originals) {
			changes = camel_folder_change_info_new ();
			for (i = 0; i < uids->len; i++) {
				camel_folder_summary_remove_uid (source->summary, uids->pdata[i]);
				camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
			}
			if (camel_folder_change_info_changed (changes)) {
				camel_folder_summary_touch (source->summary);
				camel_folder_changed (source, changes);
			}
			camel_folder_change_info_free (changes);
		}

		/*update the store about the content of the source and destination folders*/
		ews_refresh_info_sync (source, cancellable, NULL);
		ews_refresh_info_sync (destination, cancellable, NULL);
	}
	g_free (dst_id);

	if (local_error) {
		camel_ews_store_maybe_disconnect (dst_ews_store, local_error);
		g_propagate_error (error, local_error);
	}

	g_object_unref (cnc);
	g_slist_free (ids);
	g_slist_free_full (ret_items, g_object_unref);

	return !local_error;
}

static gboolean
ews_delete_messages (CamelFolder *folder,
                     GSList *deleted_items,
                     gboolean expunge,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	CamelFolderChangeInfo *changes;
	EEwsConnection *cnc;
	gboolean status = TRUE;
	GSList *deleted_head = NULL;

	parent_store = camel_folder_get_parent_store (folder);
	ews_folder = CAMEL_EWS_FOLDER (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);
	deleted_head = deleted_items;

	if (!camel_ews_store_connected (ews_store, error)) {
		return FALSE;
	}

	cnc = camel_ews_store_get_connection (ews_store);
	changes = camel_folder_change_info_new ();

	if (deleted_items) {
		GError *local_error = NULL;
		EwsDeleteType delete_type;

		delete_type = expunge ? EWS_HARD_DELETE : EWS_MOVE_TO_DELETED_ITEMS;

		status = e_ews_connection_delete_items_sync (
			cnc, EWS_PRIORITY_MEDIUM, deleted_items, delete_type,
			EWS_SEND_TO_NONE, FALSE, cancellable, &local_error);

		if (!status && local_error->code == EWS_CONNECTION_ERROR_ITEMNOTFOUND) {
			/* If delete failed due to the item not found, ignore the error,
			 * trigger folder info refresh and then go on to clear the
			 * cache of the deleted items anyway. */
			g_clear_error (&local_error);
			status = ews_refresh_info_sync (folder, cancellable, &local_error);
		}

		if (status) {
			while (deleted_items) {
				const gchar *uid = (gchar *) deleted_items->data;
				camel_folder_summary_lock (folder->summary, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
				camel_folder_change_info_remove_uid (changes, uid);
				camel_folder_summary_remove_uid (folder->summary, uid);
				ews_data_cache_remove(ews_folder->cache, "cur", uid, NULL);
				camel_folder_summary_unlock (folder->summary, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
				deleted_items = g_slist_next (deleted_items);
			}
		}

		if (local_error) {
			camel_ews_store_maybe_disconnect (ews_store, local_error);
			g_propagate_error (error, local_error);
		}

		if (camel_folder_change_info_changed (changes)) {
			camel_folder_summary_touch (folder->summary);
			camel_folder_changed (folder, changes);
		}

		g_slist_foreach (deleted_head, (GFunc) camel_pstring_free, NULL);
		g_slist_free (deleted_head);
	}

	camel_folder_change_info_free (changes);
	g_object_unref (cnc);

	return status;
}

static gboolean
ews_expunge_sync (CamelFolder *folder,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelEwsStore *ews_store;
	CamelEwsMessageInfo *ews_info;
	CamelMessageInfo *info;
	CamelStore *parent_store;
	GSList *deleted_items = NULL;
	gint i;
	gboolean is_trash;
	GPtrArray *known_uids;

	parent_store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	if (!camel_ews_store_connected (ews_store, error))
		return FALSE;

	is_trash = ews_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_TRASH);

	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
	known_uids = camel_folder_summary_get_array (folder->summary);
	if (!known_uids)
		return TRUE;

	/* Collect UIDs of deleted messages. */
	for (i = 0; i < known_uids->len; i++) {
		const gchar *uid = g_ptr_array_index (known_uids, i);

		info = camel_folder_summary_get (folder->summary, uid);
		ews_info = (CamelEwsMessageInfo *) info;
		if (ews_info && (is_trash || (ews_info->info.flags & CAMEL_MESSAGE_DELETED) != 0))
			deleted_items = g_slist_prepend (deleted_items, (gpointer) camel_pstring_strdup (uid));

		camel_message_info_free (info);
	}

	camel_folder_summary_free_array (known_uids);

	return ews_delete_messages (folder, deleted_items, TRUE, cancellable, error);
}

static gint
ews_cmp_uids (CamelFolder *folder,
              const gchar *uid1,
              const gchar *uid2)
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
	g_cond_free (ews_folder->priv->fetch_cond);

	if (CAMEL_FOLDER (ews_folder)->summary)
		g_signal_handlers_disconnect_by_func (CAMEL_FOLDER (ews_folder)->summary, G_CALLBACK (ews_folder_count_notify_cb), ews_folder);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_ews_folder_parent_class)->dispose (object);
}

static void
ews_folder_constructed (GObject *object)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelService *service;
	CamelFolder *folder;
	const gchar *full_name;
	const gchar *host;
	const gchar *user;
	gchar *description;

	folder = CAMEL_FOLDER (object);
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	service = CAMEL_SERVICE (parent_store);
	settings = camel_service_get_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_get_host (network_settings);
	user = camel_network_settings_get_user (network_settings);

	description = g_strdup_printf (
		"%s@%s:%s", user, host, full_name);
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
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN |
		CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_USER;

	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;

	ews_folder->priv->search_lock = g_mutex_new ();
	ews_folder->priv->state_lock = g_mutex_new ();
	g_static_rec_mutex_init (&ews_folder->priv->cache_lock);

	ews_folder->priv->refreshing = FALSE;

	ews_folder->priv->fetch_cond = g_cond_new ();
	ews_folder->priv->uid_eflags = g_hash_table_new (g_str_hash, g_str_equal);
	camel_folder_set_lock_async (folder, TRUE);
}

/** End **/
