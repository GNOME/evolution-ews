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

#include <glib.h>

#include "camel-o365-folder.h"

#include "camel-o365-folder-summary.h"

#define LOCK(_summary) g_mutex_lock (&_summary->priv->property_lock)
#define UNLOCK(_summary) g_mutex_unlock (&_summary->priv->property_lock)

struct _CamelO365FolderSummaryPrivate {
	GMutex property_lock;
	gchar *delta_link;
	gint32 version;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelO365FolderSummary, camel_o365_folder_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static gboolean
o365_folder_summary_header_load (CamelFolderSummary *summary,
				 CamelFIRecord *mir)
{
	CamelO365FolderSummary *o365_summary = CAMEL_O365_FOLDER_SUMMARY (summary);
	const gchar *delta_link = NULL;
	gchar *part;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_o365_folder_summary_parent_class)->summary_header_load (summary, mir))
		return FALSE;

	o365_summary->priv->version = 0;

	part = mir->bdata;

	if (part)
		o365_summary->priv->version = camel_util_bdata_get_number (&part, 0);

	if (part && *part && part[1])
		delta_link = part + 1;

	/* Do not call camel_o365_folder_summary_set_sync_state() here,
	   to not mark the summary dirty after load. */
	LOCK (o365_summary);

	if (g_strcmp0 (o365_summary->priv->delta_link, delta_link) != 0) {
		g_free (o365_summary->priv->delta_link);
		o365_summary->priv->delta_link = g_strdup (delta_link);
	}

	UNLOCK (o365_summary);

	return TRUE;
}

static CamelFIRecord *
o365_folder_summary_header_save (CamelFolderSummary *summary,
				 GError **error)
{
	CamelO365FolderSummary *o365_summary = CAMEL_O365_FOLDER_SUMMARY (summary);
	CamelFIRecord *fir;
	gchar *delta_link;

	fir = CAMEL_FOLDER_SUMMARY_CLASS (camel_o365_folder_summary_parent_class)->summary_header_save (summary, error);

	if (!fir)
		return NULL;

	delta_link = camel_o365_folder_summary_dup_delta_link (o365_summary);

	fir->bdata = g_strdup_printf ("%d %s", CAMEL_O365_FOLDER_SUMMARY_VERSION, delta_link ? delta_link : "");

	g_free (delta_link);

	LOCK (o365_summary);

	o365_summary->priv->version = CAMEL_O365_FOLDER_SUMMARY_VERSION;

	UNLOCK (o365_summary);

	return fir;
}

static void
o365_folder_summary_finalize (GObject *object)
{
	CamelO365FolderSummary *o365_summary = CAMEL_O365_FOLDER_SUMMARY (object);

	g_free (o365_summary->priv->delta_link);
	g_mutex_clear (&o365_summary->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_folder_summary_parent_class)->finalize (object);
}

static void
camel_o365_folder_summary_class_init (CamelO365FolderSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = o365_folder_summary_finalize;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_type = CAMEL_TYPE_O365_MESSAGE_INFO;
	folder_summary_class->summary_header_save = o365_folder_summary_header_save;
	folder_summary_class->summary_header_load = o365_folder_summary_header_load;
}

static void
camel_o365_folder_summary_init (CamelO365FolderSummary *o365_summary)
{
	o365_summary->priv = camel_o365_folder_summary_get_instance_private (o365_summary);

	g_mutex_init (&o365_summary->priv->property_lock);
}

CamelFolderSummary *
camel_o365_folder_summary_new (struct _CamelFolder *folder)
{
	CamelFolderSummary *summary;

	summary = g_object_new (CAMEL_TYPE_O365_FOLDER_SUMMARY, "folder", folder, NULL);

	camel_folder_summary_load (summary, NULL);

	return summary;
}

gint
camel_o365_folder_summary_get_version (CamelO365FolderSummary *o365_summary)
{
	gint version;

	g_return_val_if_fail (CAMEL_IS_O365_FOLDER_SUMMARY (o365_summary), -1);

	LOCK (o365_summary);

	version = o365_summary->priv->version;

	UNLOCK (o365_summary);

	return version;
}

void
camel_o365_folder_summary_set_delta_link (CamelO365FolderSummary *o365_summary,
					  const gchar *delta_link)
{
	gboolean changed = FALSE;

	g_return_if_fail (CAMEL_IS_O365_FOLDER_SUMMARY (o365_summary));

	LOCK (o365_summary);

	if (g_strcmp0 (o365_summary->priv->delta_link, delta_link) != 0) {
		gchar *dup = g_strdup (delta_link);

		g_free (o365_summary->priv->delta_link);
		o365_summary->priv->delta_link = dup;

		changed = TRUE;
	}

	UNLOCK (o365_summary);

	if (changed)
		camel_folder_summary_touch (CAMEL_FOLDER_SUMMARY (o365_summary));
}

gchar *
camel_o365_folder_summary_dup_delta_link (CamelO365FolderSummary *o365_summary)
{
	gchar *delta_link;

	g_return_val_if_fail (CAMEL_IS_O365_FOLDER_SUMMARY (o365_summary), NULL);

	LOCK (o365_summary);

	delta_link = g_strdup (o365_summary->priv->delta_link);

	UNLOCK (o365_summary);

	return delta_link;
}

void
camel_o365_folder_summary_clear (CamelFolderSummary *summary)
{
	CamelFolderChangeInfo *changes;
	GPtrArray *known_uids;
	gint i;

	changes = camel_folder_change_info_new ();
	known_uids = camel_folder_summary_get_array (summary);

	for (i = 0; i < known_uids->len; i++) {
		const gchar *uid = g_ptr_array_index (known_uids, i);

		if (!uid)
			continue;

		camel_folder_change_info_remove_uid (changes, uid);
	}

	camel_folder_summary_clear (summary, NULL);

	if (camel_folder_change_info_changed (changes))
		camel_folder_changed (camel_folder_summary_get_folder (summary), changes);
	camel_folder_change_info_free (changes);
	camel_folder_summary_free_array (known_uids);
}

static gboolean
o365_folder_summary_update_user_flags (CamelMessageInfo *info,
				       const CamelNamedFlags *server_user_flags)
{
	gboolean changed = FALSE;
	gboolean set_cal = FALSE, set_note = FALSE;

	if (camel_message_info_get_user_flag (info, "$has_cal"))
		set_cal = TRUE;
	if (camel_message_info_get_user_flag (info, "$has_note"))
		set_note = TRUE;

	changed = camel_message_info_take_user_flags (info, camel_named_flags_copy (server_user_flags));

	/* reset the flags as they were set in messageinfo before */
	if (set_cal)
		camel_message_info_set_user_flag (info, "$has_cal", TRUE);
	if (set_note)
		camel_message_info_set_user_flag (info, "$has_note", TRUE);

	return changed;
}

gboolean
camel_o365_folder_summary_update_message_info_flags (CamelFolderSummary *summary,
						     CamelMessageInfo *info,
						     guint32 server_flags,
						     const CamelNamedFlags *server_user_flags)
{
	CamelO365MessageInfo *omi;
	gboolean changed = FALSE;

	g_return_val_if_fail (CAMEL_IS_O365_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (info), FALSE);

	omi = CAMEL_O365_MESSAGE_INFO (info);

	if (server_flags != camel_o365_message_info_get_server_flags (omi)) {
		guint32 server_set, server_cleared;

		server_set = server_flags & ~camel_o365_message_info_get_server_flags (omi);
		server_cleared = camel_o365_message_info_get_server_flags (omi) & ~server_flags;

		camel_message_info_set_flags (info, server_set | server_cleared, (camel_message_info_get_flags (info) | server_set) & ~server_cleared);
		camel_o365_message_info_set_server_flags (omi, server_flags);
		changed = TRUE;
	}

	if (server_user_flags && o365_folder_summary_update_user_flags (info, server_user_flags))
		changed = TRUE;

	return changed;
}

gboolean
camel_o365_folder_summary_add_message (CamelFolderSummary *summary,
				       const gchar *uid,
				       const gchar *change_key,
				       CamelMessageInfo *info,
				       CamelMimeMessage *message)
{
	CamelMessageInfo *mi;

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (info != NULL, FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	mi = camel_folder_summary_info_new_from_message (summary, message);
	g_return_val_if_fail (mi != NULL, FALSE);

	camel_message_info_set_abort_notifications (mi, TRUE);

	camel_o365_message_info_set_change_key (CAMEL_O365_MESSAGE_INFO (mi), change_key);
	camel_message_info_set_flags (mi, ~0, camel_message_info_get_flags (info));
	camel_message_info_take_user_flags (mi, camel_message_info_dup_user_flags (info));
	camel_message_info_take_user_tags (mi, camel_message_info_dup_user_tags (info));
	camel_message_info_set_size (mi, camel_message_info_get_size (info));
	camel_message_info_set_uid (mi, uid);

	camel_message_info_set_abort_notifications (mi, FALSE);

	camel_folder_summary_add (summary, mi, FALSE);

	g_object_unref (mi);

	return TRUE;
}
