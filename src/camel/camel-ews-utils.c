/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* camel-ews-utils.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-message.h"

#include "camel-ews-utils.h"

#define SUBFOLDER_DIR_NAME     "subfolders"
#define SUBFOLDER_DIR_NAME_LEN 10

CamelFolderInfo *
camel_ews_utils_build_folder_info (CamelEwsStore *store,
                                   const gchar *fid)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	CamelFolderInfo *fi;

	fi = camel_folder_info_new ();
	fi->full_name = camel_ews_store_summary_get_folder_full_name (
		ews_summary, fid, NULL);

	if (!fi->full_name) {
		camel_folder_info_free (fi);
		g_warn_if_reached ();

		return NULL;
	}

	fi->display_name = camel_ews_store_summary_get_folder_name (
		ews_summary, fid, NULL);
	fi->flags = camel_ews_store_summary_get_folder_flags (
		ews_summary, fid, NULL);
	fi->unread = camel_ews_store_summary_get_folder_unread (
		ews_summary, fid, NULL);
	fi->total = camel_ews_store_summary_get_folder_total (
		ews_summary, fid, NULL);

	if (!(fi->flags & CAMEL_FOLDER_TYPE_MASK)) {
		switch (camel_ews_store_summary_get_folder_type (ews_summary, fid, NULL)) {
		case E_EWS_FOLDER_TYPE_CALENDAR:
			fi->flags |= CAMEL_FOLDER_TYPE_EVENTS;
			break;
		case E_EWS_FOLDER_TYPE_CONTACTS:
			fi->flags |= CAMEL_FOLDER_TYPE_CONTACTS;
			break;
		case E_EWS_FOLDER_TYPE_TASKS:
			fi->flags |= CAMEL_FOLDER_TYPE_TASKS;
			break;
		case E_EWS_FOLDER_TYPE_MEMOS:
			fi->flags |= CAMEL_FOLDER_TYPE_MEMOS;
			break;
		default:
			break;
		}
	}

	return fi;
}

static void
sync_deleted_folders (CamelEwsStore *store,
                      GSList *deleted_folders)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	GSList *l;

	for (l = deleted_folders; l != NULL; l = g_slist_next (l)) {
		const gchar *fid = l->data;
		EEwsFolderType ftype;
		CamelFolderInfo *fi;
		GError *error = NULL;

		if (!camel_ews_store_summary_has_folder (ews_summary, fid))
			continue;

		ftype = camel_ews_store_summary_get_folder_type (
			ews_summary, fid, NULL);
		if (ftype == E_EWS_FOLDER_TYPE_MAILBOX) {
			fi = camel_ews_utils_build_folder_info (store, fid);

			camel_ews_store_summary_remove_folder (ews_summary, fid, &error);

			if ((fi->flags & CAMEL_FOLDER_SUBSCRIBED) != 0) {
				camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (store), fi);
				camel_store_folder_deleted (CAMEL_STORE (store), fi);
			}

			g_clear_error (&error);
		}
	}
}

static gboolean
ews_utils_rename_folder (CamelEwsStore *store,
                         EEwsFolderType ftype,
                         const gchar *fid,
                         const gchar *changekey,
                         const gchar *pfid,
                         const gchar *display_name,
                         const gchar *old_fname,
                         GError **error)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	CamelFolderInfo *fi;

	camel_ews_store_summary_set_change_key (ews_summary, fid, changekey);
	if (display_name)
		camel_ews_store_summary_set_folder_name (
			ews_summary, fid, display_name);
	if (pfid)
		camel_ews_store_summary_set_parent_folder_id (
			ews_summary, fid, pfid);

	if (ftype == E_EWS_FOLDER_TYPE_MAILBOX) {
		fi = camel_ews_utils_build_folder_info (store, fid);
		camel_store_folder_renamed (CAMEL_STORE (store), old_fname, fi);
	}

	return TRUE;
}

static void
sync_updated_folders (CamelEwsStore *store,
                      GSList *updated_folders)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	GSList *l;

	for (l = updated_folders; l != NULL; l = g_slist_next (l)) {
		EEwsFolder *ews_folder = (EEwsFolder *) l->data;
		EEwsFolderType ftype;
		gchar *folder_name;
		gchar *display_name;
		const EwsFolderId *fid, *pfid;

		ftype = e_ews_folder_get_folder_type (ews_folder);
		if (ftype != E_EWS_FOLDER_TYPE_MAILBOX)
			continue;

		fid = e_ews_folder_get_id (ews_folder);
		folder_name = camel_ews_store_summary_get_folder_full_name (
			ews_summary, fid->id, NULL);

		pfid = e_ews_folder_get_parent_id (ews_folder);
		display_name = g_strdup (e_ews_folder_get_name (ews_folder));

		/* If the folder is moved or renamed (which are separate
		 * operations in Exchange, unfortunately, then the name
		 * or parent folder will change. Handle both... */
		if (pfid || display_name) {
			GError *error = NULL;
			gchar *new_fname = NULL;

			if (pfid) {
				gchar *pfname;

				/* If the display name wasn't changed, its basename is still
				 * the same as it was before... */
				if (!display_name)
					display_name = camel_ews_store_summary_get_folder_name (
						ews_summary, fid->id, NULL);
				if (!display_name)
					goto done;

				pfname = camel_ews_store_summary_get_folder_full_name (
					ews_summary, pfid->id, NULL);

				/* If the lookup failed, it'll be because the new parent folder
				 * is the message folder root. */
				if (pfname) {
					new_fname = g_strconcat (
						pfname, "/", display_name, NULL);
					g_free (pfname);
				} else
					new_fname = g_strdup (display_name);
			} else {
				/* Parent folder not changed; just basename */
				const gchar *last_slash;

				/* Append new display_name to old parent directory name... */
				last_slash = g_strrstr (folder_name, "/");
				if (last_slash)
					new_fname = g_strdup_printf (
						"%.*s/%s",
						(gint)(last_slash - folder_name),
						folder_name, display_name);
				else /* ...unless it was a child of the root folder */
					new_fname = g_strdup (display_name);
			}

			if (strcmp (new_fname, folder_name))
				ews_utils_rename_folder (
					store, ftype,
					fid->id, fid->change_key,
					pfid ? pfid->id : NULL,
					display_name, folder_name, &error);
			g_free (new_fname);
			g_clear_error (&error);
		}
 done:
		g_free (folder_name);
		g_free (display_name);
	}
}

/* FIXME get the real folder ids of the system folders using
 * by fetching them using distinguished folder ids once */
static void
add_folder_to_summary (CamelEwsStore *store,
                       EEwsFolder *folder)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	const EwsFolderId *pfid, *fid;
	const gchar *dname;
	gint64 unread, total;
	EEwsFolderType ftype;

	fid = e_ews_folder_get_id (folder);
	pfid = e_ews_folder_get_parent_id (folder);
	dname = e_ews_folder_get_name (folder);
	total = e_ews_folder_get_total_count (folder);
	unread = e_ews_folder_get_unread_count (folder);
	ftype = e_ews_folder_get_folder_type (folder);

	camel_ews_store_summary_new_folder (
		ews_summary, fid->id,
		pfid ? pfid->id : NULL, fid->change_key,
		dname, ftype, 0, total,
		e_ews_folder_get_foreign (folder),
		FALSE);
	camel_ews_store_summary_set_folder_unread (
		ews_summary, fid->id, unread);
}

static void
sync_created_folders (CamelEwsStore *ews_store,
                      GSList *created_folders,
		      GSList **created_folder_ids)
{
	GSList *l;

	for (l = created_folders; l != NULL; l = g_slist_next (l)) {
		EEwsFolder *folder = (EEwsFolder *) l->data;
		EEwsFolderType ftype;
		CamelFolderInfo *fi;
		const EwsFolderId *fid;

		ftype = e_ews_folder_get_folder_type (folder);
		if (ftype != E_EWS_FOLDER_TYPE_MAILBOX)
			continue;

		fid = e_ews_folder_get_id (folder);

		/* FIXME: Sort folders so that a child is always added *after*
		 * its parent. But since the old code was already completely
		 * broken and would just go into an endless loop if the server
		 * didn't return the folders in the 'right' order for that,
		 * let's worry about that in a later commit. */
		add_folder_to_summary (ews_store, folder);

		if (ftype != E_EWS_FOLDER_TYPE_MAILBOX)
			continue;

		if (created_folder_ids) {
			*created_folder_ids = g_slist_append (*created_folder_ids, g_strdup (fid->id));
		} else {
			fi = camel_ews_utils_build_folder_info (
				ews_store, fid->id);
			camel_store_folder_created (
				CAMEL_STORE (ews_store), fi);
			camel_subscribable_folder_subscribed (
				CAMEL_SUBSCRIBABLE (ews_store), fi);
			camel_folder_info_free (fi);
		}
	}
}

void
ews_utils_sync_folders (CamelEwsStore *ews_store,
                        GSList *created_folders,
                        GSList *deleted_folders,
                        GSList *updated_folders,
			GSList **created_folder_ids)
{
	GError *error = NULL;

	sync_deleted_folders (ews_store, deleted_folders);
	sync_updated_folders (ews_store, updated_folders);
	sync_created_folders (ews_store, created_folders, created_folder_ids);

	camel_ews_store_summary_save (ews_store->summary, &error);
	if (error != NULL) {
		g_print (
			"Error while saving store summary %s \n",
			error->message);
		g_clear_error (&error);
	}
}

void
camel_ews_utils_sync_deleted_items (CamelEwsFolder *ews_folder,
                                    GSList *items_deleted)
{
	CamelStore *store;
	CamelFolder *folder;
	const gchar *full_name;
	CamelFolderChangeInfo *ci;
	CamelEwsStore *ews_store;
	GSList *l;
	GList *items_deleted_list = NULL;

	ci = camel_folder_change_info_new ();

	folder = CAMEL_FOLDER (ews_folder);
	full_name = camel_folder_get_full_name (folder);

	store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (store);

	for (l = items_deleted; l != NULL; l = g_slist_next (l)) {
		const gchar *id = l->data;

		items_deleted_list = g_list_prepend (
			items_deleted_list, (gpointer) id);

		camel_folder_summary_remove_uid (folder->summary, id);
		camel_folder_change_info_remove_uid (ci, id);
	}

	items_deleted_list = g_list_reverse (items_deleted_list);
	camel_db_delete_uids (
		CAMEL_STORE (ews_store)->cdb_w,
		full_name, items_deleted_list, NULL);
	g_list_free (items_deleted_list);

	if (camel_folder_change_info_changed (ci)) {
		camel_folder_summary_touch (folder->summary);
		camel_folder_summary_save_to_db (folder->summary, NULL);
		camel_folder_changed (folder, ci);
	}
	camel_folder_change_info_free (ci);

	g_slist_foreach (items_deleted, (GFunc) g_free, NULL);
	g_slist_free (items_deleted);
}

static const gchar *
ews_utils_rename_label (const gchar *cat,
                        gint from_cat)
{
	gint i;

	/* this is a mapping from Exchange/Outlook categories to
	 * evolution labels based on the standard colours */
	const gchar *labels[] = {
		"Red Category", "$Labelimportant",
		"Orange Category", "$Labelwork",
		"Green Category", "$Labelpersonal",
		"Blue Category", "$Labeltodo",
		"Purple Category", "$Labellater",
		NULL, NULL
	};

	if (!cat || !*cat)
		return "";

	for (i = 0; labels[i]; i += 2) {
		if (from_cat) {
			if (!g_ascii_strcasecmp (cat, labels[i]))
				return labels[i + 1];
		} else {
			if (!g_ascii_strcasecmp (cat, labels[i + 1]))
				return labels[i];
		}
	}
	return cat;
}

void
ews_utils_replace_server_user_flags (ESoapMessage *msg,
                                     CamelEwsMessageInfo *mi)
{
	const CamelFlag *flag;

	/* transfer camel flags to become the categories as an XML
	 * array of strings */
	for (flag = camel_message_info_user_flags (&mi->info); flag;
	     flag = flag->next) {
		const gchar *n = ews_utils_rename_label (flag->name, 0);
		if (*n == '\0')
			continue;
		/* This is a mismatch between evolution flags and
		 * exchange categories.  Evolution uses a
		 * receipt-handled flag for message receipts, which we
		 * don't want showing up in the categories, so
		 * silently drop it here */
		if (strcmp (n, "receipt-handled") == 0)
			continue;
		e_ews_message_write_string_parameter (msg, "String", NULL, n);
	}
}

static void
ews_utils_merge_server_user_flags (EEwsItem *item,
                                   CamelEwsMessageInfo *mi)
{
	GSList *list = NULL;
	const GSList *p;
	const CamelFlag *flag;

	/* transfer camel flags to a list */
	for (flag = camel_message_info_user_flags (&mi->info); flag;
	     flag = flag->next)
		list = g_slist_append (list, (gchar *) flag->name);

	/* we're transferring from server only, so just dump them */
	for (p = list; p; p = p->next) {
		camel_flag_set (&mi->info.user_flags, p->data, 0);
	}
	//g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);

	/* now transfer over all the categories */
	for (p = e_ews_item_get_categories (item); p; p = p->next) {
		camel_flag_set (
			&mi->info.user_flags,
			ews_utils_rename_label (p->data, 1), 1);
	}
}

static gint
ews_utils_get_server_flags (EEwsItem *item)
{
	gboolean flag;
	EwsImportance importance;
	gint server_flags = 0;

	e_ews_item_is_read (item, &flag);
	if (flag)
		server_flags |= CAMEL_MESSAGE_SEEN;
	else
		server_flags &= ~CAMEL_MESSAGE_SEEN;

	e_ews_item_is_forwarded (item, &flag);
	if (flag)
		server_flags |= CAMEL_MESSAGE_FORWARDED;
	else
		server_flags &= ~CAMEL_MESSAGE_FORWARDED;

	e_ews_item_is_answered (item, &flag);
	if (flag)
		server_flags |= CAMEL_MESSAGE_ANSWERED;
	else
		server_flags &= ~CAMEL_MESSAGE_ANSWERED;

	importance = e_ews_item_get_importance (item);
	if (importance == EWS_ITEM_HIGH)
		server_flags |= CAMEL_MESSAGE_FLAGGED;

	/* TODO Update replied flags */

	return server_flags;
}

static const gchar *
form_email_string_from_mb (EEwsConnection *cnc,
                           const EwsMailbox *mb,
                           GCancellable *cancellable)
{
	if (mb) {
		GString *str;
		const gchar *email = NULL;

		if (g_strcmp0 (mb->routing_type, "EX") == 0)
			email = e_ews_item_util_strip_ex_address (mb->email);

		str = g_string_new ("");
		if (mb->name && mb->name[0]) {
			g_string_append (str, mb->name);
			g_string_append (str, " ");
		}

		if (mb->email || email) {
			g_string_append (str, "<");
			g_string_append (str, email ? email : mb->email);
			g_string_append (str, ">");
		}

		return camel_pstring_add (g_string_free (str, FALSE), TRUE);
	} else
	       return camel_pstring_strdup ("");
}

static const gchar *
form_recipient_list (EEwsConnection *cnc,
                     const GSList *recipients,
                     GCancellable *cancellable)
{
	const GSList *l;
	GString *str = NULL;
	const gchar *ret;

	if (!recipients)
		return NULL;

	for (l = recipients; l != NULL; l = g_slist_next (l)) {
		EwsMailbox *mb = (EwsMailbox *) l->data;
		const gchar *mb_str = form_email_string_from_mb (cnc, mb, cancellable);

		if (!str)
			str = g_string_new ("");
		else
			str = g_string_append (str, ", ");

		str = g_string_append (str, mb_str);
	}

	ret = camel_pstring_add (str->str, TRUE);
	g_string_free (str, FALSE);

	return ret;
}

static guint8 *
get_md5_digest (const guchar *str)
{
	guint8 *digest;
	gsize length;
	GChecksum *checksum;

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_malloc0 (length);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, str, -1);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	return digest;
}

static void
ews_set_threading_data (CamelEwsMessageInfo *mi,
                        EEwsItem *item)
{
	const gchar *references, *inreplyto;
	gint count = 0;
	const gchar *message_id;
	struct _camel_header_references *refs, *irt, *scan;
	guint8 *digest;
	gchar *msgid;

	/* set message id */
	message_id = e_ews_item_get_msg_id (item);
	msgid = camel_header_msgid_decode (message_id);
	if (msgid) {
		digest = get_md5_digest ((const guchar *) msgid);
		memcpy (
			mi->info.message_id.id.hash,
			digest, sizeof (mi->info.message_id.id.hash));
		g_free (digest);
		g_free (msgid);
	}

	/* Process References: header */
	references = e_ews_item_get_references (item);
	refs = camel_header_references_decode (references);

	/* Prepend In-Reply-To: contents to References: for summary info */
	inreplyto = e_ews_item_get_in_replyto (item);
	irt = camel_header_references_inreplyto_decode (inreplyto);
	if (irt) {
		irt->next = refs;
		refs = irt;
	}
	if (!refs)
		return;

	count = camel_header_references_list_size (&refs);
	mi->info.references = g_malloc (
		sizeof (*mi->info.references) + ((count - 1) *
		sizeof (mi->info.references->references[0])));
	scan = refs;
	count = 0;

	while (scan) {
		digest = get_md5_digest ((const guchar *) scan->id);
		memcpy (
			mi->info.references->references[count].id.hash,
			digest, sizeof (mi->info.message_id.id.hash));
		g_free (digest);

		count++;
		scan = scan->next;
	}

	mi->info.references->size = count;
	camel_header_references_list_clear (&refs);
}

void
camel_ews_utils_sync_updated_items (CamelEwsFolder *ews_folder,
                                    GSList *items_updated)
{
	CamelFolder *folder;
	CamelFolderChangeInfo *ci;
	GSList *l;

	ci = camel_folder_change_info_new ();
	folder = CAMEL_FOLDER (ews_folder);

	for (l = items_updated; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id;
		CamelEwsMessageInfo *mi;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
			g_object_unref (item);
			continue;
		}

		id = e_ews_item_get_id (item);
		mi = (CamelEwsMessageInfo *)
			camel_folder_summary_get (folder->summary, id->id);
		if (mi) {
			gint server_flags;

			server_flags = ews_utils_get_server_flags (item);
			ews_utils_merge_server_user_flags (item, mi);
			if (camel_ews_update_message_info_flags (
				folder->summary, (CamelMessageInfo *) mi,
				server_flags, NULL))
				camel_folder_change_info_change_uid (ci, mi->info.uid);

			g_free (mi->change_key);
			mi->change_key = g_strdup (id->change_key);
			mi->info.dirty = TRUE;

			camel_message_info_free (mi);
			g_object_unref (item);
			continue;
		}

		g_object_unref (item);
	}

	if (camel_folder_change_info_changed (ci)) {
		camel_folder_summary_touch (folder->summary);
		camel_folder_summary_save_to_db (folder->summary, NULL);
		camel_folder_changed (CAMEL_FOLDER (ews_folder), ci);
	}
	camel_folder_change_info_free (ci);
	g_slist_free (items_updated);
}

void
camel_ews_utils_sync_created_items (CamelEwsFolder *ews_folder,
                                    EEwsConnection *cnc,
                                    GSList *items_created,
                                    GCancellable *cancellable)
{
	CamelFolder *folder;
	CamelFolderChangeInfo *ci;
	GSList *l;

	if (!items_created)
		return;

	ci = camel_folder_change_info_new ();
	folder = CAMEL_FOLDER (ews_folder);

	for (l = items_created; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		CamelEwsMessageInfo *mi;
		const EwsId *id;
		const EwsMailbox *from;
		EEwsItemType item_type;
		const GSList *to, *cc;
		gboolean has_attachments;
		guint32 server_flags;

		if (!item)
			continue;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
			g_object_unref (item);
			continue;
		}

		id = e_ews_item_get_id (item);
		mi = (CamelEwsMessageInfo *)
			camel_folder_summary_get (folder->summary, id->id);
		if (mi) {
			camel_message_info_free (mi);
			g_object_unref (item);
			continue;
		}

		mi = (CamelEwsMessageInfo *)
			camel_message_info_new (folder->summary);

		if (mi->info.content == NULL) {
			mi->info.content =
				camel_folder_summary_content_info_new (
				folder->summary);
			mi->info.content->type =
				camel_content_type_new ("multipart", "mixed");
		}

		item_type = e_ews_item_get_item_type (item);
		if (item_type == E_EWS_ITEM_TYPE_EVENT ||
			 item_type == E_EWS_ITEM_TYPE_MEETING_MESSAGE ||
			 item_type == E_EWS_ITEM_TYPE_MEETING_REQUEST ||
			 item_type == E_EWS_ITEM_TYPE_MEETING_RESPONSE ||
			 item_type == E_EWS_ITEM_TYPE_MEETING_RESPONSE)
			camel_message_info_set_user_flag (
				(CamelMessageInfo *) mi, "$has_cal", TRUE);

		mi->info.uid = camel_pstring_strdup (id->id);
		mi->info.size = e_ews_item_get_size (item);
		mi->info.subject = camel_pstring_strdup (
			e_ews_item_get_subject (item));
		mi->item_type = item_type;
		mi->change_key = g_strdup (id->change_key);

		mi->info.date_sent = e_ews_item_get_date_sent (item);
		mi->info.date_received = e_ews_item_get_date_received (item);

		from = e_ews_item_get_from (item);
		if (!from)
			from = e_ews_item_get_sender (item);
		mi->info.from = form_email_string_from_mb (cnc, from, cancellable);

		to = e_ews_item_get_to_recipients (item);
		mi->info.to = form_recipient_list (cnc, to, cancellable);

		cc = e_ews_item_get_cc_recipients (item);
		mi->info.cc = form_recipient_list (cnc, cc, cancellable);

		e_ews_item_has_attachments (item, &has_attachments);
		if (has_attachments)
			mi->info.flags |= CAMEL_MESSAGE_ATTACHMENTS;

		ews_set_threading_data (mi, item);
		server_flags = ews_utils_get_server_flags (item);
		ews_utils_merge_server_user_flags (item, mi);

		mi->info.flags |= server_flags;
		mi->server_flags = server_flags;

		camel_folder_summary_add (
			folder->summary, (CamelMessageInfo *) mi);

		/* camel_folder_summary_add() sets folder_flagged flag
		 * on the message info, but this is a fresh item downloaded
		 * from the server, thus unset it, to avoid resync up to the server
		 * on folder leave/store
		*/
		mi->info.flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;

		camel_folder_change_info_add_uid (ci, id->id);
		camel_folder_change_info_recent_uid (ci, id->id);

		g_object_unref (item);
	}

	if (camel_folder_change_info_changed (ci)) {
		camel_folder_summary_touch (folder->summary);
		camel_folder_summary_save_to_db (folder->summary, NULL);
		camel_folder_changed (CAMEL_FOLDER (ews_folder), ci);
	}
	camel_folder_change_info_free (ci);
	g_slist_free (items_created);
}

gchar *
camel_ews_utils_get_host_name (CamelSettings *settings)
{
	CamelURL *url;
	gchar *host = NULL, *hosturl;

	g_return_val_if_fail (settings != NULL, NULL);

	hosturl = camel_ews_settings_dup_hosturl (CAMEL_EWS_SETTINGS (settings));

	url = camel_url_new (hosturl, NULL);
	if (url) {
		host = g_strdup (url->host);
		camel_url_free (url);
	}

	if (!host || !*host) {
		g_free (host);
		host = camel_network_settings_dup_host (CAMEL_NETWORK_SETTINGS (settings));
	}

	g_free (hosturl);

	return host;
}
