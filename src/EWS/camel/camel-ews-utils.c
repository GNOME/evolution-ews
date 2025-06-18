/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "common/camel-ews-settings.h"
#include "common/e-ews-camel-common.h"
#include "common/e-ews-item-change.h"
#include "common/e-ews-request.h"
#include "e-ews-common-utils.h"

#include "camel-ews-utils.h"

#define SUBFOLDER_DIR_NAME     "subfolders"
#define SUBFOLDER_DIR_NAME_LEN 10

#define EWS_MAPI_MSGFLAG_HASATTACH 0x10
#define EWS_MAPI_MSGFLAG_RN_PENDING 0x100

CamelFolderInfo *
camel_ews_utils_build_folder_info (CamelEwsStore *store,
                                   const gchar *fid)
{
	CamelEwsStoreSummary *ews_summary = store->summary;
	CamelFolderInfo *fi;
	gchar *folder_name;

	fi = camel_folder_info_new ();
	fi->full_name = camel_ews_store_summary_get_folder_full_name (
		ews_summary, fid, NULL);

	if (!fi->full_name) {
		camel_folder_info_free (fi);
		g_warn_if_reached ();

		return NULL;
	}

	folder_name = camel_ews_store_summary_get_folder_name (ews_summary, fid, NULL);

	fi->display_name = e_ews_folder_utils_unescape_name (folder_name);
	fi->flags = camel_ews_store_summary_get_folder_flags (ews_summary, fid, NULL);
	fi->unread = camel_ews_store_summary_get_folder_unread (ews_summary, fid, NULL);
	fi->total = camel_ews_store_summary_get_folder_total (ews_summary, fid, NULL);

	g_free (folder_name);

	if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_DRAFTS) {
		/* Do not propagate the Drafts flag to Evolution, because
		   it removes the Drafts icon in the folders tree when set */
		fi->flags = fi->flags & (~CAMEL_FOLDER_TYPE_DRAFTS);
	}

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

	if (g_strcmp0 (fid, EWS_PUBLIC_FOLDER_ROOT_ID) == 0)
		fi->flags |= CAMEL_FOLDER_CHILDREN | CAMEL_FOLDER_NOSELECT;

	if (g_strcmp0 (fid, EWS_FOREIGN_FOLDER_ROOT_ID) == 0) {
		fi->flags |= CAMEL_FOLDER_NOSELECT;
	} else {
		gchar *parent_fid;

		parent_fid = camel_ews_store_summary_get_parent_folder_id (ews_summary, fid, NULL);

		if (g_strcmp0 (parent_fid, EWS_FOREIGN_FOLDER_ROOT_ID) == 0)
			fi->flags |= CAMEL_FOLDER_NOSELECT;

		g_free (parent_fid);
	}

	return fi;
}

static void
add_folder_to_summary (CamelEwsStore *store,
                       EEwsFolder *folder);

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

			if (!camel_ews_store_summary_remove_folder (ews_summary, fid, &error)) {
				if (error != NULL) {
					g_warning ("%s: %s", G_STRFUNC, error->message);
					g_clear_error (&error);
				}
				continue;
			}

			camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (store), fi);
			camel_store_folder_deleted (CAMEL_STORE (store), fi);
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

		if (!folder_name) {
			/* in case the folder is not in the local store summary,
			   just add it as a new folder */
			add_folder_to_summary (store, ews_folder);
			continue;
		}

		pfid = e_ews_folder_get_parent_id (ews_folder);
		display_name = g_strdup (e_ews_folder_get_escaped_name (ews_folder));

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
		if (e_ews_folder_get_public (ews_folder)) {
			camel_ews_store_summary_set_folder_flags (ews_summary, fid->id,
				e_ews_folder_get_child_count (ews_folder) > 0 ? CAMEL_FOLDER_CHILDREN : CAMEL_FOLDER_NOCHILDREN);
		}

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
	guint64 flags = 0;
	EEwsFolderType ftype;

	fid = e_ews_folder_get_id (folder);
	pfid = e_ews_folder_get_parent_id (folder);
	dname = e_ews_folder_get_escaped_name (folder);
	total = e_ews_folder_get_total_count (folder);
	unread = e_ews_folder_get_unread_count (folder);
	ftype = e_ews_folder_get_folder_type (folder);
	flags = e_ews_folder_get_child_count (folder) ? CAMEL_FOLDER_CHILDREN : CAMEL_FOLDER_NOCHILDREN;

	camel_ews_store_summary_new_folder (
		ews_summary, fid->id,
		pfid ? pfid->id : NULL, fid->change_key,
		dname, ftype, flags, total,
		e_ews_folder_get_foreign (folder),
		e_ews_folder_get_public (folder));
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
                                    GSList *items_deleted,
				    CamelFolderChangeInfo *change_info)
{
	CamelStore *store;
	CamelFolder *folder;
	const gchar *full_name;
	CamelEwsStore *ews_store;
	GSList *l;
	GPtrArray *to_delete;

	folder = CAMEL_FOLDER (ews_folder);
	full_name = camel_folder_get_full_name (folder);

	store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (store);
	to_delete = g_ptr_array_sized_new (g_slist_length (items_deleted));

	for (l = items_deleted; l != NULL; l = g_slist_next (l)) {
		const gchar *id = l->data;

		g_ptr_array_add (to_delete, (gpointer) id);

		camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), id);
		camel_folder_change_info_remove_uid (change_info, id);
	}

	camel_store_db_delete_messages (
		camel_store_get_db (CAMEL_STORE (ews_store)),
		full_name, to_delete, NULL);
	g_ptr_array_unref (to_delete);

	g_slist_foreach (items_deleted, (GFunc) g_free, NULL);
	g_slist_free (items_deleted);
}

static const gchar *
ews_utils_outlook_color_index_to_color_def (gint color_index)
{
	const gchar *colors_array[] = {
		"#ff1a36", /* Red */
		"#ff8c00", /* Orange */
		"#f4b10b", /* Peach */
		"#fff100", /* Yellow */
		"#009e48", /* Green */
		"#00b294", /* Teal */
		"#89933f", /* Olive */
		"#00bcf2", /* Blue */
		"#8e69df", /* Purple */
		"#f30092", /* Maroon */
		"#6c7e9a", /* Steel */
		"#425066", /* DarkSteel */
		"#969696", /* Gray */
		"#525552", /* DarkGray */
		"#282828", /* Black */
		"#a00023", /* DarkRed */
		"#c45502", /* DarkOrange */
		"#af7000", /* DarkPeach */
		"#b59b02", /* DarkYellow */
		"#176002", /* DarkGreen */
		"#00725c", /* DarkTeal */
		"#5c6022", /* DarkOlive */
		"#036393", /* DarkBlue */
		"#422f8e", /* DarkPurple */
		"#960269"  /* DarkMaroon */
	};

	if (color_index >= 0 && color_index < G_N_ELEMENTS (colors_array))
		return colors_array[color_index];

	return NULL;
}

static const gchar *
ews_utils_rename_label (const gchar *cat,
                        gboolean from_cat)
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

static gboolean
ews_utils_is_system_user_flag (const gchar *name)
{
	if (!name)
		return FALSE;

	return g_str_equal (name, "receipt-handled") ||
		g_str_equal (name, "$has-cal");
}

/* From Exchange name (which allows spaces) to evolution-name */
static gchar *
camel_ews_utils_encode_category_name (const gchar *name)
{
	if (name && strchr (name, ' ')) {
		GString *str;

		str = g_string_sized_new (strlen (name) + 16);

		while (*name) {
			if (*name == '_')
				g_string_append_c (str, '_');

			g_string_append_c (str, *name == ' ' ? '_' : *name);

			name++;
		}

		return g_string_free (str, FALSE);
	}

	return g_strdup (name);
}

/* From evolution-name to Exchange name (which allows spaces) */
static gchar *
camel_ews_utils_decode_category_name (const gchar *flag)
{
	if (flag && strchr (flag, '_')) {
		GString *str = g_string_sized_new (strlen (flag));

		while (*flag) {
			if (*flag == '_') {
				if (flag[1] == '_') {
					g_string_append_c (str, '_');
					flag++;
				} else {
					g_string_append_c (str, ' ');
				}
			} else {
				g_string_append_c (str, *flag);
			}

			flag++;
		}

		return g_string_free (str, FALSE);
	}

	return g_strdup (flag);
}

/* free with g_slist_free_full (flags, g_free);
   the lists' members are values for the String xml element. */
GSList *
ews_utils_gather_server_user_flags (ESoapRequest *request,
				    CamelMessageInfo *mi)
{
	GSList *out_user_flags = NULL;
	const CamelNamedFlags *user_flags;
	guint ii, len;

	camel_message_info_property_lock (mi);

	user_flags = camel_message_info_get_user_flags (mi);
	len = camel_named_flags_get_length (user_flags);

	/* transfer camel flags to become the categories as an XML
	 * array of strings */
	for (ii = 0; ii < len; ii++) {
		const gchar *n = ews_utils_rename_label (camel_named_flags_get (user_flags, ii), FALSE);

		if (*n == '\0')
			continue;

		/* Skip evolution-defined flags which are not supposed to
		   be categories on an Exchange server */
		if (ews_utils_is_system_user_flag (n))
			continue;

		out_user_flags = g_slist_prepend (out_user_flags, camel_ews_utils_decode_category_name (n));
	}

	camel_message_info_property_unlock (mi);

	return g_slist_reverse (out_user_flags);
}

static void
ews_utils_merge_server_user_flags (EEwsItem *item,
                                   CamelMessageInfo *mi)
{
	CamelFolderSummary *summary;
	GSList *list = NULL;
	const GSList *p;
	const CamelNamedFlags *user_flags;
	guint ii, len;

	summary = camel_message_info_ref_summary (mi);
	if (summary)
		camel_folder_summary_lock (summary);
	camel_message_info_property_lock (mi);
	camel_message_info_freeze_notifications (mi);

	user_flags = camel_message_info_get_user_flags (mi);
	len = camel_named_flags_get_length (user_flags);

	/* transfer camel flags to a list */
	for (ii = 0; ii < len; ii++) {
		const gchar *name = camel_named_flags_get (user_flags, ii);

		if (!ews_utils_is_system_user_flag (name))
			list = g_slist_prepend (list, (gchar *) name);
	}

	for (p = list; p; p = p->next) {
		/* remove custom user flags */
		camel_message_info_set_user_flag (mi, p->data, FALSE);
	}

	g_slist_free (list);

	/* now transfer over all the categories */
	for (p = e_ews_item_get_categories (item); p; p = p->next) {
		const gchar *name = ews_utils_rename_label (p->data, 1);
		gchar *flag;

		if (!name || !*name)
			continue;

		flag = camel_ews_utils_encode_category_name (name);

		camel_message_info_set_user_flag (mi, flag, TRUE);

		g_free (flag);
	}

	camel_message_info_thaw_notifications (mi);
	camel_message_info_property_unlock (mi);
	if (summary)
		camel_folder_summary_unlock (summary);
	g_clear_object (&summary);
}

static guint32
ews_utils_get_server_flags (EEwsItem *item)
{
	gboolean flag;
	EwsImportance importance;
	guint32 server_flags = 0, msg_flags;

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

	msg_flags = e_ews_item_get_message_flags (item);
	if ((msg_flags & EWS_MAPI_MSGFLAG_RN_PENDING) != 0)
		server_flags |= CAMEL_EWS_MESSAGE_MSGFLAG_RN_PENDING;

	if ((msg_flags & EWS_MAPI_MSGFLAG_HASATTACH) != 0)
		server_flags |= CAMEL_MESSAGE_ATTACHMENTS;

	return server_flags;
}

static gchar *
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
			g_string_append_c (str, ' ');
		}

		if (mb->email || email) {
			g_string_append_c (str, '<');
			g_string_append (str, email ? email : mb->email);
			g_string_append_c (str, '>');
		}

		return g_string_free (str, FALSE);
	} else
		return NULL;
}

static gchar *
form_recipient_list (EEwsConnection *cnc,
                     const GSList *recipients,
                     GCancellable *cancellable)
{
	const GSList *l;
	GString *str = NULL;

	if (!recipients)
		return NULL;

	for (l = recipients; l != NULL; l = g_slist_next (l)) {
		EwsMailbox *mb = (EwsMailbox *) l->data;
		gchar *mb_str = form_email_string_from_mb (cnc, mb, cancellable);

		if (!str)
			str = g_string_new ("");
		else
			g_string_append (str, ", ");

		g_string_append (str, mb_str);

		g_free (mb_str);
	}

	return g_string_free (str, FALSE);
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
ews_set_threading_data (CamelMessageInfo *mi,
                        EEwsItem *item)
{
	const gchar *references_str, *inreplyto_str;
	const gchar *message_id;
	GSList *refs, *irt, *link;
	guint8 *digest;
	gchar *msgid;
	CamelSummaryMessageID tmp_msgid;
	GArray *references;

	/* set message id */
	message_id = e_ews_item_get_msg_id (item);
	msgid = camel_header_msgid_decode (message_id);
	if (msgid) {
		digest = get_md5_digest ((const guchar *) msgid);
		memcpy (tmp_msgid.id.hash, digest, sizeof (tmp_msgid.id.hash));
		g_free (digest);
		g_free (msgid);

		camel_message_info_set_message_id (mi, tmp_msgid.id.id);
	}

	/* Process References: header */
	references_str = e_ews_item_get_references (item);
	refs = camel_header_references_decode (references_str);

	/* Prepend In-Reply-To: contents to References: for summary info */
	inreplyto_str = e_ews_item_get_in_replyto (item);
	irt = camel_header_references_decode (inreplyto_str);
	if (irt) {
		refs = g_slist_concat (irt, refs);
	}
	if (!refs)
		return;

	references = g_array_sized_new (FALSE, FALSE, sizeof (guint64), g_slist_length (refs));

	for (link = refs; link; link = g_slist_next (link)) {
		digest = get_md5_digest ((const guchar *) link->data);
		memcpy (tmp_msgid.id.hash, digest, sizeof (tmp_msgid.id.hash));
		g_free (digest);

		g_array_append_val (references, tmp_msgid.id.id);
	}

	g_slist_free_full (refs, g_free);

	camel_message_info_take_references (mi, references);
}

static gboolean
camel_ews_utils_update_follow_up_flags (EEwsItem *item,
					CamelMessageInfo *info)
{
	gboolean changed = FALSE, found;
	time_t completed_tt, dueby_tt;
	const gchar *followup_name;
	gint flag_status;

	/* PidTagFlagStatus */
	found = FALSE;
	flag_status = e_ews_item_get_extended_property_as_int (item, NULL, 0x1090, &found);
	if (!found)
		flag_status = 0;

	/* PidTagFlagCompleteTime */
	found = FALSE;
	completed_tt = e_ews_item_get_extended_property_as_time (item, NULL, 0x1091, &found);
	if (!found)
		completed_tt = (time_t) 0;

	/* PidLidFlagRequest */
	found = FALSE;
	followup_name = e_ews_item_get_extended_property_as_string (item, "Common", 0x8530, &found);
	if (!found)
		followup_name = NULL;

	/* PidLidTaskDueDate */
	found = FALSE;
	dueby_tt = e_ews_item_get_extended_property_as_time (item, "Task", 0x8105, &found);
	if (!found)
		dueby_tt = (time_t) 0;

	if (flag_status == 1) {
		/* complete */
		if (!camel_message_info_get_user_tag (info, "follow-up"))
			changed = camel_message_info_set_user_tag (info, "follow-up", followup_name ? followup_name : "follow-up") || changed;
		if (completed_tt != (time_t) 0) {
			gchar *text = camel_header_format_date (completed_tt, 0);
			changed = camel_message_info_set_user_tag (info, "completed-on", text) || changed;
			g_free (text);
		} else {
			changed = camel_message_info_set_user_tag (info, "completed-on", NULL) || changed;
		}
	} else if (flag_status == 2) {
		/* follow-up */
		changed = camel_message_info_set_user_tag (info, "follow-up", followup_name ? followup_name : "follow-up") || changed;
		changed = camel_message_info_set_user_tag (info, "completed-on", NULL) || changed;
		if (dueby_tt != (time_t) 0) {
			gchar *text = camel_header_format_date (dueby_tt, 0);
			changed = camel_message_info_set_user_tag (info, "due-by", text) || changed;
			g_free (text);
		} else {
			changed = camel_message_info_set_user_tag (info, "due-by", NULL) || changed;
		}
	} else {
		changed = camel_message_info_set_user_tag (info, "follow-up", NULL) || changed;
		changed = camel_message_info_set_user_tag (info, "completed-on", NULL) || changed;
		changed = camel_message_info_set_user_tag (info, "due-by", NULL) || changed;
	}

	return changed;
}

static gboolean
camel_ews_utils_update_read_receipt_flags (EEwsItem *item,
					   CamelMessageInfo *info,
					   guint32 server_flags,
					   gboolean requests_read_receipt)
{
	gboolean changed = FALSE;

	/* PidTagReadReceiptRequested */
	if ((requests_read_receipt || e_ews_item_get_extended_property_as_boolean (item, NULL, 0x0029, NULL)) &&
	    (server_flags & CAMEL_EWS_MESSAGE_MSGFLAG_RN_PENDING) == 0) {
		changed = camel_message_info_set_user_flag (info, "receipt-handled", TRUE) || changed;
	}

	return changed;
}

gboolean
camel_ews_utils_folder_is_drafts_folder (CamelEwsFolder *ews_folder)
{
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	gchar *drafts_uid, *folder_uid;
	gboolean is_drafts_folder;

	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (ews_folder), FALSE);

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (ews_folder));
	if (!parent_store)
		return FALSE;

	ews_store = CAMEL_EWS_STORE (parent_store);
	g_return_val_if_fail (ews_store != NULL, FALSE);

	drafts_uid = camel_ews_store_summary_get_folder_id_from_folder_type (ews_store->summary, CAMEL_FOLDER_TYPE_DRAFTS);
	if (!drafts_uid)
		return FALSE;

	folder_uid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary,
		camel_folder_get_full_name (CAMEL_FOLDER (ews_folder)));

	is_drafts_folder = g_strcmp0 (drafts_uid, folder_uid) == 0;

	g_free (drafts_uid);
	g_free (folder_uid);

	return is_drafts_folder;
}

static void
ews_utils_copy_message_info (CamelMessageInfo *des_mi,
			     const CamelMessageInfo *src_mi)
{
	gchar *had_color;
	gchar *had_score;
	gboolean had_cal;

	g_return_if_fail (CAMEL_IS_MESSAGE_INFO (des_mi));
	g_return_if_fail (CAMEL_IS_MESSAGE_INFO (src_mi));
	g_return_if_fail (g_strcmp0 (camel_message_info_get_uid (des_mi), camel_message_info_get_uid (src_mi)) == 0);

	camel_message_info_property_lock (des_mi);

	/* Preserve custom information used by evolution */
	had_cal = camel_message_info_get_user_flag (des_mi, "$has_cal");
	had_color = camel_message_info_dup_user_tag (des_mi, "color");
	had_score = camel_message_info_dup_user_tag (des_mi, "score");

	camel_message_info_set_flags (des_mi, ~CAMEL_MESSAGE_FOLDER_FLAGGED, camel_message_info_get_flags (src_mi));
	camel_message_info_take_user_flags (des_mi, camel_message_info_dup_user_flags (src_mi));
	camel_message_info_take_user_tags (des_mi, camel_message_info_dup_user_tags (src_mi));
	camel_message_info_set_subject (des_mi, camel_message_info_get_subject (src_mi));
	camel_message_info_set_from (des_mi, camel_message_info_get_from (src_mi));
	camel_message_info_set_to (des_mi, camel_message_info_get_to (src_mi));
	camel_message_info_set_cc (des_mi, camel_message_info_get_cc (src_mi));
	camel_message_info_set_mlist (des_mi, camel_message_info_get_mlist (src_mi));
	camel_message_info_set_size (des_mi, camel_message_info_get_size (src_mi));
	camel_message_info_set_date_sent (des_mi, camel_message_info_get_date_sent (src_mi));
	camel_message_info_set_date_received (des_mi, camel_message_info_get_date_received (src_mi));
	camel_message_info_set_message_id (des_mi, camel_message_info_get_message_id (src_mi));
	camel_message_info_take_references (des_mi, camel_message_info_dup_references (src_mi));
	camel_message_info_take_headers (des_mi, camel_message_info_dup_headers (src_mi));

	if (had_cal)
		camel_message_info_set_user_flag (des_mi, "$has_cal", TRUE);
	if (had_color)
		camel_message_info_set_user_tag (des_mi, "color", had_color);
	if (had_score)
		camel_message_info_set_user_tag (des_mi, "score", had_score);

	if (CAMEL_IS_EWS_MESSAGE_INFO (des_mi) && CAMEL_IS_EWS_MESSAGE_INFO (src_mi)) {
		camel_ews_message_info_set_change_key (CAMEL_EWS_MESSAGE_INFO (des_mi),
			camel_ews_message_info_get_change_key (CAMEL_EWS_MESSAGE_INFO (src_mi)));
	}

	camel_message_info_property_unlock (des_mi);

	g_free (had_color);
	g_free (had_score);
}

void
camel_ews_utils_sync_updated_items (CamelEwsFolder *ews_folder,
				    EEwsConnection *cnc,
				    gboolean is_drafts_folder,
                                    GSList *items_updated,
				    CamelFolderChangeInfo *change_info,
				    GCancellable *cancellable)
{
	CamelFolder *folder;
	CamelFolderSummary *folder_summary;
	GSList *l;

	folder = CAMEL_FOLDER (ews_folder);
	folder_summary = camel_folder_get_folder_summary (folder);

	for (l = items_updated; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id;
		CamelMessageInfo *mi;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
			g_object_unref (item);
			continue;
		}

		id = e_ews_item_get_id (item);
		if (!id) {
			g_warning ("%s: Missing ItemId for item type %d (subject:%s)", G_STRFUNC, e_ews_item_get_item_type (item),
				e_ews_item_get_subject (item) ? e_ews_item_get_subject (item) : "???");
			g_object_unref (item);
			continue;
		}

		if (is_drafts_folder) {
			mi = camel_folder_summary_get (folder_summary, id->id);
			if (mi) {
				CamelMessageInfo *tmp_mi;

				if (g_strcmp0 (camel_ews_message_info_get_change_key (CAMEL_EWS_MESSAGE_INFO (mi)), id->change_key) != 0)
					camel_ews_folder_remove_cached_message (ews_folder, id->id);

				/* There can changed also subject and other values in the Drafts folder,
				   thus recreate it and update it. */
				tmp_mi = camel_ews_utils_item_to_message_info (ews_folder, cnc, item, cancellable);
				if (!tmp_mi) {
					g_warn_if_reached ();
					g_object_unref (mi);
					g_object_unref (item);
					continue;
				}

				ews_utils_copy_message_info (mi, tmp_mi);
				camel_ews_message_info_set_change_key (CAMEL_EWS_MESSAGE_INFO (mi), id->change_key);

				camel_folder_change_info_change_uid (change_info, id->id);

				g_clear_object (&tmp_mi);
				g_clear_object (&mi);
			}
		} else {
			mi = camel_folder_summary_get (folder_summary, id->id);
			if (mi) {
				guint32 server_flags;
				gboolean changed, was_changed;

				camel_message_info_freeze_notifications (mi);
				was_changed = camel_message_info_get_folder_flagged (mi);

				server_flags = ews_utils_get_server_flags (item);
				ews_utils_merge_server_user_flags (item, mi);
				changed = camel_ews_update_message_info_flags (folder_summary, mi, server_flags, NULL);
				changed = camel_ews_utils_update_follow_up_flags (item, mi) || changed;
				changed = camel_ews_utils_update_read_receipt_flags (item, mi, server_flags, FALSE) || changed;

				if (changed)
					camel_folder_change_info_change_uid (change_info, id->id);

				camel_ews_message_info_set_change_key (CAMEL_EWS_MESSAGE_INFO (mi), id->change_key);
				if (!was_changed) {
					/* do not save to the server what was just read, when did not change locally before */
					camel_message_info_set_folder_flagged (mi, FALSE);
				}

				camel_message_info_thaw_notifications (mi);
				g_clear_object (&mi);
			}
		}

		g_object_unref (item);
	}

	g_slist_free (items_updated);
}

CamelMessageInfo * /* (transfer full) */
camel_ews_utils_item_to_message_info (CamelEwsFolder *ews_folder,
				      EEwsConnection *cnc,
				      EEwsItem *item,
				      GCancellable *cancellable)
{
	CamelFolderSummary *folder_summary;
	CamelMessageInfo *mi = NULL;
	CamelContentType *content_type = NULL;
	const EwsId *id;
	const EwsMailbox *from;
	gchar *tmp;
	EEwsItemType item_type;
	const gchar *msg_headers;
	gboolean has_attachments, found_property, message_requests_read_receipt = FALSE;
	guint32 server_flags;

	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (ews_folder), NULL);

	if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
		return NULL;

	id = e_ews_item_get_id (item);
	if (!id)
		return NULL;

	folder_summary = camel_folder_get_folder_summary (CAMEL_FOLDER (ews_folder));

	/* PidTagTransportMessageHeaders */
	found_property = FALSE;
	msg_headers = e_ews_item_get_extended_property_as_string (item, NULL, 0x007D, &found_property);
	if (!found_property)
		msg_headers = NULL;

	if (msg_headers && *msg_headers) {
		CamelMimePart *part = camel_mime_part_new ();
		CamelStream *stream;
		CamelMimeParser *parser;

		stream = camel_stream_mem_new_with_buffer (msg_headers, strlen (msg_headers));
		parser = camel_mime_parser_new ();
		camel_mime_parser_init_with_stream (parser, stream, NULL);
		camel_mime_parser_scan_from (parser, FALSE);
		g_object_unref (stream);

		if (camel_mime_part_construct_from_parser_sync (part, parser, NULL, NULL)) {
			CamelContentType *ct;

			mi = camel_folder_summary_info_new_from_headers (folder_summary, camel_medium_get_headers (CAMEL_MEDIUM (part)));
			if (camel_medium_get_header (CAMEL_MEDIUM (part), "Disposition-Notification-To"))
				message_requests_read_receipt = TRUE;

			ct = camel_mime_part_get_content_type (part);
			if (ct)
				content_type = camel_content_type_ref (ct);
		}

		g_object_unref (parser);
		g_object_unref (part);
	}

	if (!mi)
		mi = camel_message_info_new (folder_summary);

	camel_message_info_set_abort_notifications (mi, TRUE);

	item_type = e_ews_item_get_item_type (item);
	if (item_type == E_EWS_ITEM_TYPE_EVENT ||
	    item_type == E_EWS_ITEM_TYPE_MEETING_MESSAGE ||
	    item_type == E_EWS_ITEM_TYPE_MEETING_REQUEST ||
	    item_type == E_EWS_ITEM_TYPE_MEETING_RESPONSE ||
	    item_type == E_EWS_ITEM_TYPE_MEETING_RESPONSE)
		camel_message_info_set_user_flag (mi, "$has_cal", TRUE);

	camel_message_info_set_uid (mi, id->id);
	camel_message_info_set_size (mi, e_ews_item_get_size (item));
	camel_message_info_set_subject (mi, e_ews_item_get_subject (item));
	camel_message_info_set_preview (mi, e_ews_item_get_preview (item));
	camel_ews_message_info_set_item_type (CAMEL_EWS_MESSAGE_INFO (mi), item_type);
	camel_ews_message_info_set_change_key (CAMEL_EWS_MESSAGE_INFO (mi), id->change_key);

	camel_message_info_set_date_sent (mi, e_ews_item_get_date_sent (item));
	camel_message_info_set_date_received (mi, e_ews_item_get_date_received (item));

	from = e_ews_item_get_from (item);
	if (!from)
		from = e_ews_item_get_sender (item);
	tmp = form_email_string_from_mb (cnc, from, cancellable);
	if (tmp && *tmp)
		camel_message_info_set_from (mi, tmp);
	g_free (tmp);

	tmp = form_recipient_list (cnc, e_ews_item_get_to_recipients (item), cancellable);
	camel_message_info_set_to (mi, tmp);
	g_free (tmp);

	tmp = form_recipient_list (cnc, e_ews_item_get_cc_recipients (item), cancellable);
	camel_message_info_set_cc (mi, tmp);
	g_free (tmp);

	e_ews_item_has_attachments (item, &has_attachments);
	if (has_attachments)
		camel_message_info_set_flags (mi, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);

	ews_set_threading_data (mi, item);
	server_flags = ews_utils_get_server_flags (item);
	ews_utils_merge_server_user_flags (item, mi);

	/* It serves as "should inherit CAMEL_MESSAGE_ATTACHMENTS from server_flags" now */
	has_attachments = !has_attachments;

	if (has_attachments && (server_flags & CAMEL_MESSAGE_ATTACHMENTS) != 0 && content_type) {
		/* The server can have set the attachment flag, even when there is no attachment.
		   This will be fixed once the message is loaded, but let's fine tune the guess. */
		if (!camel_content_type_is (content_type, "multipart", "*") ||
		    camel_content_type_is (content_type, "multipart", "alternative")) {
			has_attachments = FALSE;
		} else if (camel_content_type_is (content_type, "multipart", "related")) {
			const gchar *related_type;

			related_type = camel_content_type_param (content_type, "type");
			if (related_type && *related_type) {
				CamelContentType *ct;

				ct = camel_content_type_decode (related_type);
				if (ct) {
					if (camel_content_type_is (ct, "multipart", "alternative"))
						has_attachments = FALSE;

					camel_content_type_unref (ct);
				}
			}
		}
	}

	camel_message_info_set_flags (mi, server_flags & ~(has_attachments ? 0 : CAMEL_MESSAGE_ATTACHMENTS), server_flags);
	camel_ews_message_info_set_server_flags (CAMEL_EWS_MESSAGE_INFO (mi), server_flags);

	camel_ews_utils_update_follow_up_flags (item, mi);
	camel_ews_utils_update_read_receipt_flags (item, mi, server_flags, message_requests_read_receipt);

	camel_message_info_set_abort_notifications (mi, FALSE);

	if (content_type)
		camel_content_type_unref (content_type);

	return mi;
}

void
camel_ews_utils_sync_created_items (CamelEwsFolder *ews_folder,
                                    EEwsConnection *cnc,
				    gboolean is_drafts_folder,
                                    GSList *items_created,
				    CamelFolderChangeInfo *change_info,
                                    GCancellable *cancellable)
{
	CamelFolder *folder;
	CamelFolderSummary *folder_summary;
	GSList *l;

	if (!items_created)
		return;

	folder = CAMEL_FOLDER (ews_folder);
	folder_summary = camel_folder_get_folder_summary (folder);

	for (l = items_created; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		CamelMessageInfo *mi, *tmp_mi;
		const EwsId *id;

		if (!item)
			continue;

		if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
			g_object_unref (item);
			continue;
		}

		id = e_ews_item_get_id (item);
		if (!id) {
			g_warning ("%s: Missing ItemId for item type %d (subject:%s)", G_STRFUNC, e_ews_item_get_item_type (item),
				e_ews_item_get_subject (item) ? e_ews_item_get_subject (item) : "???");
			g_object_unref (item);
			continue;
		}

		mi = camel_folder_summary_get (folder_summary, id->id);
		/* If it didn't change, then skip it. */
		if (mi && g_strcmp0 (camel_ews_message_info_get_change_key (CAMEL_EWS_MESSAGE_INFO (mi)), id->change_key) == 0) {
			g_clear_object (&mi);
			g_object_unref (item);
			continue;
		} else if (mi && is_drafts_folder) {
			/* The message in the Drafts folder changed, thus reload also locally cached message */
			camel_ews_folder_remove_cached_message (ews_folder, id->id);
		}

		tmp_mi = camel_ews_utils_item_to_message_info (ews_folder, cnc, item, cancellable);
		if (!tmp_mi) {
			g_warn_if_reached ();
			g_clear_object (&mi);
			g_object_unref (item);
			continue;
		}

		if (mi) {
			ews_utils_copy_message_info (mi, tmp_mi);
			camel_ews_message_info_set_change_key (CAMEL_EWS_MESSAGE_INFO (mi), id->change_key);

			camel_folder_change_info_change_uid (change_info, id->id);
		} else {
			camel_folder_summary_add (folder_summary, tmp_mi, FALSE);

			/* camel_folder_summary_add() sets folder_flagged flag
			 * on the message info, but this is a fresh item downloaded
			 * from the server, thus unset it, to avoid resync up to the server
			 * on folder leave/store
			 */
			camel_message_info_set_folder_flagged (tmp_mi, FALSE);

			camel_folder_change_info_add_uid (change_info, id->id);
			camel_folder_change_info_recent_uid (change_info, id->id);
		}

		g_object_unref (tmp_mi);
		g_object_unref (item);
		g_clear_object (&mi);
	}

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

void
ews_utils_update_followup_flags (ESoapRequest *request,
				 CamelMessageInfo *mi)
{
	const gchar *followup, *completed, *dueby;
	time_t completed_tt = (time_t) 0 , dueby_tt = (time_t) 0;

	g_return_if_fail (request != NULL);
	g_return_if_fail (mi != NULL);

	followup = camel_message_info_get_user_tag (mi, "follow-up");
	completed = camel_message_info_get_user_tag (mi, "completed-on");
	dueby = camel_message_info_get_user_tag (mi, "due-by");

	if (followup && !*followup)
		followup = NULL;

	if (completed && *completed)
		completed_tt = camel_header_decode_date (completed, NULL);

	if (dueby && *dueby)
		dueby_tt = camel_header_decode_date (dueby, NULL);

	if (followup) {
		/* PidTagFlagStatus */
		e_ews_request_add_set_item_field_extended_tag_int (request, NULL, "Message", 0x1090,
			completed_tt != (time_t) 0 ? 0x01 /* followupComplete */: 0x02 /* followupFlagged */);

		/* PidLidFlagRequest */
		e_ews_request_add_set_item_field_extended_distinguished_tag_string (request, NULL, "Message", "Common", 0x8530, followup);

		/* PidTagToDoItemFlags */
		e_ews_request_add_set_item_field_extended_tag_int (request, NULL, "Message", 0x0e2b, 1);

		if (completed_tt == (time_t) 0 && dueby_tt == (time_t) 0) {
			time_t now_tt = time (NULL);

			/* PidLidTaskStatus */
			e_ews_request_add_set_item_field_extended_distinguished_tag_int (request, NULL, "Message", "Task", 0x8101, 0);

			/* PidLidPercentComplete */
			e_ews_request_add_set_item_field_extended_distinguished_tag_double (request, NULL, "Message", "Task", 0x8102, 0.0);

			/* PidLidTaskStartDate */
			e_ews_request_add_set_item_field_extended_distinguished_tag_time (request, NULL, "Message", "Task", 0x8104, now_tt);

			/* PidLidTaskComplete */
			e_ews_request_add_set_item_field_extended_distinguished_tag_boolean (request, NULL, "Message", "Task", 0x811c, FALSE);
		}
	} else {
		/* PidTagFlagStatus */
		e_ews_request_add_delete_item_field_extended_tag (request, 0x1090, E_EWS_MESSAGE_DATA_TYPE_INT);

		/* PidTagFlagCompleteTime */
		e_ews_request_add_delete_item_field_extended_tag (request, 0x1091, E_EWS_MESSAGE_DATA_TYPE_TIME);

		/* PidTagToDoItemFlags */
		e_ews_request_add_delete_item_field_extended_tag (request, 0x0e2b, E_EWS_MESSAGE_DATA_TYPE_INT);

		/* PidTagFollowupIcon */
		e_ews_request_add_delete_item_field_extended_tag (request, 0x1095, E_EWS_MESSAGE_DATA_TYPE_INT);

		/* PidLidFlagRequest */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Common", 0x8530, E_EWS_MESSAGE_DATA_TYPE_STRING);

		/* PidLidFlagString */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Common", 0x85c0, E_EWS_MESSAGE_DATA_TYPE_INT);

		/* PidLidTaskStatus */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Task", 0x8101, E_EWS_MESSAGE_DATA_TYPE_INT);

		/* PidLidPercentComplete */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Task", 0x8102, E_EWS_MESSAGE_DATA_TYPE_DOUBLE);

		/* PidLidTaskStartDate */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Task", 0x8104, E_EWS_MESSAGE_DATA_TYPE_TIME);

		/* PidLidTaskDueDate */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Task", 0x8105, E_EWS_MESSAGE_DATA_TYPE_TIME);

		/* PidLidTaskDateCompleted */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Task", 0x810f, E_EWS_MESSAGE_DATA_TYPE_TIME);

		/* PidLidTaskComplete */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Task", 0x811c, E_EWS_MESSAGE_DATA_TYPE_BOOLEAN);
	}

	if (followup && completed_tt != (time_t) 0) {
		/* minute precision */
		completed_tt = completed_tt - (completed_tt % 60);

		/* PidTagFlagCompleteTime */
		e_ews_request_add_set_item_field_extended_tag_time (request, NULL, "Message", 0x1091, completed_tt);

		/* PidTagFollowupIcon */
		e_ews_request_add_delete_item_field_extended_tag (request, 0x1095, E_EWS_MESSAGE_DATA_TYPE_INT);

		/* PidLidTaskDateCompleted */
		e_ews_request_add_set_item_field_extended_distinguished_tag_time (request, NULL, "Message", "Task", 0x810f, completed_tt);

		/* PidLidTaskStatus */
		e_ews_request_add_set_item_field_extended_distinguished_tag_int (request, NULL, "Message", "Task", 0x8101, 2);

		/* PidLidPercentComplete */
		e_ews_request_add_set_item_field_extended_distinguished_tag_double (request, NULL, "Message", "Task", 0x8102, 1.0);

		/* PidLidTaskComplete */
		e_ews_request_add_set_item_field_extended_distinguished_tag_boolean (request, NULL, "Message", "Task", 0x811c, TRUE);
	}

	if (followup && dueby_tt != (time_t) 0 && completed_tt == (time_t) 0) {
		time_t now_tt = time (NULL);

		if (now_tt > dueby_tt)
			now_tt = dueby_tt - 1;

		/* PidLidTaskStatus */
		e_ews_request_add_set_item_field_extended_distinguished_tag_int (request, NULL, "Message", "Task", 0x8101, 0);

		/* PidLidPercentComplete */
		e_ews_request_add_set_item_field_extended_distinguished_tag_double (request, NULL, "Message", "Task", 0x8102, 0.0);

		/* PidLidTaskStartDate */
		e_ews_request_add_set_item_field_extended_distinguished_tag_time (request, NULL, "Message", "Task", 0x8104, now_tt);

		/* PidLidTaskDueDate */
		e_ews_request_add_set_item_field_extended_distinguished_tag_time (request, NULL, "Message", "Task", 0x8105, dueby_tt);

		/* PidLidTaskComplete */
		e_ews_request_add_set_item_field_extended_distinguished_tag_boolean (request, NULL, "Message", "Task", 0x811c, FALSE);
	} else if (followup && dueby_tt == (time_t) 0) {
		/* PidLidTaskDueDate */
		e_ews_request_add_delete_item_field_extended_distinguished_tag (request, "Task", 0x8105, E_EWS_MESSAGE_DATA_TYPE_TIME);
	}
}
gboolean
camel_ews_utils_delete_folders_from_summary_recursive (CamelEwsStore *ews_store,
						       CamelFolderInfo *folder_info,
						       gboolean send_signals,
						       GError **error)
{
	gboolean success = TRUE;

	while (folder_info != NULL) {
		gchar *fid;

		if (folder_info->child != NULL) {
			success = camel_ews_utils_delete_folders_from_summary_recursive (
				ews_store, folder_info->child, send_signals, error);

			if (!success)
				break;
		}

		fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_info->full_name);
		success = camel_ews_store_summary_remove_folder (ews_store->summary, fid, error);
		g_free (fid);

		if (!success)
			break;

		if (send_signals) {
			camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (ews_store), folder_info);
			camel_store_folder_deleted (CAMEL_STORE (ews_store), folder_info);
		}

		folder_info = folder_info->next;
	}

	return success;
}

/* Unref with g_object_unref() when done with it */
ESource *
camel_ews_utils_ref_corresponding_source (CamelService *service,
					  GCancellable *cancellable)
{
	ESourceRegistry *registry;
	ESource *source = NULL;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	registry = e_source_registry_new_sync (cancellable, NULL);

	if (registry) {
		source = e_source_registry_ref_source (registry, camel_service_get_uid (service));

		while (source && e_source_get_parent (source) &&
		       !e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
			ESource *parent;

			parent = e_source_registry_ref_source (registry, e_source_get_parent (source));
			if (!parent)
				break;

			g_clear_object (&source);
			source = parent;
		}
	}

	g_clear_object (&registry);

	return source;
}

static gboolean
ews_util_equal_label_tag_cb (gconstpointer ptr1,
			     gconstpointer ptr2)
{
	const gchar *evo_label_def = ptr1;
	const gchar *tag = ptr2;
	const gchar *pos;

	if (!evo_label_def || !tag || !*tag)
		return FALSE;

	pos = g_strrstr (evo_label_def, tag);

	return pos > evo_label_def && pos[-1] == '|' && !pos[strlen (tag)];
}

static gboolean
ews_utils_find_in_ptr_array (GPtrArray *haystack,
			     gconstpointer needle,
			     GEqualFunc equal_func,
			     guint *out_index)
{
	guint ii;

	if (!haystack)
		return FALSE;

	if (!equal_func)
		equal_func = g_direct_equal;

	for (ii = 0; ii < haystack->len; ii++) {
		if (equal_func (haystack->pdata[ii], needle)) {
			if (out_index)
				*out_index = ii;

			return TRUE;
		}
	}

	return FALSE;
}

/* Returns whether had been done any changes */
static gboolean
ews_utils_save_category_changes (GHashTable *old_categories, /* gchar *guid ~> CamelEwsCategory * */
				 GHashTable *new_categories) /* gchar *guid ~> CamelEwsCategory * */
{
	GHashTableIter iter;
	GSettings *settings;
	GPtrArray *evo_labels; /* gchar * (encoded label definition) */
	gchar **strv;
	gint ii;
	gpointer value;
	gboolean changed = FALSE;

	/* cannot save, when evolution is not installed */
	if (!e_ews_common_utils_gsettings_schema_exists ("org.gnome.evolution.mail"))
		return FALSE;

	if (!old_categories || !new_categories)
		return new_categories != NULL;

	evo_labels = g_ptr_array_new_full (5, g_free);

	settings = g_settings_new ("org.gnome.evolution.mail");
	strv = g_settings_get_strv (settings, "labels");

	for (ii = 0; strv && strv[ii]; ii++) {
		g_ptr_array_add (evo_labels, g_strdup (strv[ii]));
	}

	g_strfreev (strv);

	g_hash_table_iter_init (&iter, new_categories);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		CamelEwsCategory *new_cat = value, *old_cat;
		gchar *tag = NULL;

		if (!new_cat)
			continue;

		old_cat = g_hash_table_lookup (old_categories, new_cat->guid);
		if (old_cat) {
			if (g_strcmp0 (old_cat->name, new_cat->name) != 0 ||
			    g_strcmp0 (old_cat->color_def, new_cat->color_def) != 0) {
				/* Old category changed name or color */
				tag = camel_ews_utils_encode_category_name (new_cat->name);
			}
		} else {
			/* This is a new category */
			tag = camel_ews_utils_encode_category_name (new_cat->name);
		}

		if (tag && *tag) {
			guint index = (guint) -1;
			gchar *label_def;

			changed = TRUE;

			/* Sanitize value */
			for (ii = 0; tag[ii]; ii++) {
				if (tag[ii] == '|')
					tag[ii] = '-';
			}

			if (old_cat && g_strcmp0 (old_cat->name, new_cat->name) != 0) {
				gchar *old_tag = camel_ews_utils_encode_category_name (old_cat->name);

				if (old_tag && *old_tag) {
					if (!ews_utils_find_in_ptr_array (evo_labels, old_tag, ews_util_equal_label_tag_cb, &index))
						index = (guint) -1;
				}

				g_free (old_tag);
			}

			for (ii = 0; new_cat->name[ii]; ii++) {
				if (new_cat->name[ii] == '|')
					new_cat->name[ii] = '-';
			}

			if (index == (guint) -1 &&
			    !ews_utils_find_in_ptr_array (evo_labels, tag, ews_util_equal_label_tag_cb, &index))
				index = (guint) -1;

			label_def = g_strconcat (new_cat->name, "|", new_cat->color_def ? new_cat->color_def : "#FF0000", "|", tag, NULL);

			if (index == (guint) -1 || index >= (gint) evo_labels->len) {
				g_ptr_array_add (evo_labels, label_def);
			} else {
				g_free (evo_labels->pdata[index]);
				evo_labels->pdata[index] = label_def;
			}
		}

		g_hash_table_remove (old_categories, new_cat->guid);

		g_free (tag);
	}

	if (g_hash_table_size (old_categories) > 0) {
		/* Some categories had been removed */
		changed = TRUE;

		g_hash_table_iter_init (&iter, old_categories);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			CamelEwsCategory *old_cat = value;
			gchar *old_tag;
			guint index;

			if (!old_cat)
				continue;

			old_tag = camel_ews_utils_encode_category_name (old_cat->name);

			for (ii = 0; old_tag && old_tag[ii]; ii++) {
				if (old_tag[ii] == '|')
					old_tag[ii] = '-';
			}

			if (old_tag &&
			    ews_utils_find_in_ptr_array (evo_labels, old_tag, ews_util_equal_label_tag_cb, &index))
				g_ptr_array_remove_index (evo_labels, index);

			g_free (old_tag);
		}
	}

	if (changed) {
		/* NULL-terminated array of strings */
		g_ptr_array_add (evo_labels, NULL);

		g_settings_set_strv (settings, "labels", (const gchar * const *) evo_labels->pdata);
	}

	g_ptr_array_free (evo_labels, TRUE);
	g_object_unref (settings);

	return changed;
}

void
camel_ews_utils_merge_category_list (CamelEwsStore *ews_store,
				     const guchar *xml_data,
				     gsize xml_data_len)
{
	xmlDocPtr doc;
	xmlXPathContextPtr xpath_ctx;

	g_return_if_fail (CAMEL_IS_EWS_STORE (ews_store));
	g_return_if_fail (xml_data != NULL);

	doc = e_xml_parse_data (xml_data, xml_data_len);
	if (!doc)
		return;

	xpath_ctx = e_xml_new_xpath_context_with_namespaces (doc, "C", "CategoryList.xsd", NULL);

	if (xpath_ctx) {
		xmlXPathObjectPtr xpath_obj_categories;

		xpath_obj_categories = e_xml_xpath_eval (xpath_ctx, "%s", "/C:categories/C:category");

		if (xpath_obj_categories) {
			GHashTable *old_categories, *new_categories;
			gint response_index, response_length;

			new_categories = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, camel_ews_category_free);

			response_length = xmlXPathNodeSetGetLength (xpath_obj_categories->nodesetval);

			for (response_index = 0; response_index < response_length; response_index++) {
				xmlXPathObjectPtr xpath_obj_category;

				xpath_obj_category = e_xml_xpath_eval (xpath_ctx,
					"/C:categories/C:category[%d]",
					response_index + 1);

				if (xpath_obj_category) {
					gchar *name;

					name = e_xml_xpath_eval_as_string (xpath_ctx, "/C:categories/C:category[%d]/@name", response_index + 1);

					if (name && ews_utils_rename_label (name, 1) == name) {
						const gchar *color_def = NULL;
						gchar *color, *guid;
						gint color_index = -1;

						color = e_xml_xpath_eval_as_string (xpath_ctx, "/C:categories/C:category[%d]/@color", response_index + 1);
						if (color) {
							gchar *endptr = NULL;

							color_index = (gint) g_ascii_strtoll (color, &endptr, 10);

							if (endptr == color)
								color_index = -1;
						}

						g_free (color);

						if (color_index >= 0)
							color_def = ews_utils_outlook_color_index_to_color_def (color_index);

						guid = e_xml_xpath_eval_as_string (xpath_ctx, "/C:categories/C:category[%d]/@guid", response_index + 1);

						if (guid && *guid) {
							CamelEwsCategory *cat;

							cat = camel_ews_category_new (guid, name, color_def);
							if (cat)
								g_hash_table_insert (new_categories, cat->guid, cat);
						}

						g_free (guid);
					}

					g_free (name);
					xmlXPathFreeObject (xpath_obj_category);
				}
			}

			xmlXPathFreeObject (xpath_obj_categories);

			old_categories = camel_ews_store_summary_get_categories (ews_store->summary);

			if (ews_utils_save_category_changes (old_categories, new_categories)) {
				camel_ews_store_summary_set_categories (ews_store->summary, new_categories);
				camel_ews_store_summary_save (ews_store->summary, NULL);
			}

			g_hash_table_destroy (new_categories);
			g_hash_table_destroy (old_categories);
		}
	}

	if (xpath_ctx)
		xmlXPathFreeContext (xpath_ctx);
	xmlFreeDoc (doc);
}
