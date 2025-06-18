/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib.h>

#include "camel-m365-folder.h"

#include "camel-m365-folder-summary.h"

#define LOCK(_summary) g_mutex_lock (&_summary->priv->property_lock)
#define UNLOCK(_summary) g_mutex_unlock (&_summary->priv->property_lock)

struct _CamelM365FolderSummaryPrivate {
	GMutex property_lock;
	gchar *delta_link;
	gint32 version;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelM365FolderSummary, camel_m365_folder_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static gboolean
m365_folder_summary_header_load (CamelFolderSummary *summary,
				 CamelStoreDBFolderRecord *record)
{
	CamelM365FolderSummary *m365_summary = CAMEL_M365_FOLDER_SUMMARY (summary);
	const gchar *delta_link = NULL;
	gchar *part;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_m365_folder_summary_parent_class)->summary_header_load (summary, record))
		return FALSE;

	m365_summary->priv->version = 0;

	part = record->bdata;

	if (part)
		m365_summary->priv->version = camel_util_bdata_get_number (&part, 0);

	if (part && *part && part[1])
		delta_link = part + 1;

	/* Do not call camel_m365_folder_summary_set_delta_link() here,
	   to not mark the summary dirty after load. */
	LOCK (m365_summary);

	if (m365_summary->priv->version < 2) {
		/* the list of summary items to fetch changed for version 2,
		   thus use a new delta link, to reflect it */
		g_clear_pointer (&m365_summary->priv->delta_link, g_free);
	} else if (g_strcmp0 (m365_summary->priv->delta_link, delta_link) != 0) {
		g_free (m365_summary->priv->delta_link);
		m365_summary->priv->delta_link = g_strdup (delta_link);
	}

	UNLOCK (m365_summary);

	return TRUE;
}

static gboolean
m365_folder_summary_header_save (CamelFolderSummary *summary,
				 CamelStoreDBFolderRecord *inout_record,
				 GError **error)
{
	CamelM365FolderSummary *m365_summary = CAMEL_M365_FOLDER_SUMMARY (summary);
	gchar *delta_link;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_m365_folder_summary_parent_class)->summary_header_save (summary, inout_record, error))
		return FALSE;

	delta_link = camel_m365_folder_summary_dup_delta_link (m365_summary);

	inout_record->bdata = g_strdup_printf ("%d %s", CAMEL_M365_FOLDER_SUMMARY_VERSION, delta_link ? delta_link : "");

	g_free (delta_link);

	LOCK (m365_summary);

	m365_summary->priv->version = CAMEL_M365_FOLDER_SUMMARY_VERSION;

	UNLOCK (m365_summary);

	return TRUE;
}

static void
m365_folder_summary_finalize (GObject *object)
{
	CamelM365FolderSummary *m365_summary = CAMEL_M365_FOLDER_SUMMARY (object);

	g_free (m365_summary->priv->delta_link);
	g_mutex_clear (&m365_summary->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_folder_summary_parent_class)->finalize (object);
}

static void
camel_m365_folder_summary_class_init (CamelM365FolderSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = m365_folder_summary_finalize;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_type = CAMEL_TYPE_M365_MESSAGE_INFO;
	folder_summary_class->summary_header_save = m365_folder_summary_header_save;
	folder_summary_class->summary_header_load = m365_folder_summary_header_load;
}

static void
camel_m365_folder_summary_init (CamelM365FolderSummary *m365_summary)
{
	m365_summary->priv = camel_m365_folder_summary_get_instance_private (m365_summary);

	g_mutex_init (&m365_summary->priv->property_lock);
}

CamelFolderSummary *
camel_m365_folder_summary_new (struct _CamelFolder *folder)
{
	CamelFolderSummary *summary;

	summary = g_object_new (CAMEL_TYPE_M365_FOLDER_SUMMARY, "folder", folder, NULL);

	camel_folder_summary_load (summary, NULL);

	return summary;
}

gint
camel_m365_folder_summary_get_version (CamelM365FolderSummary *m365_summary)
{
	gint version;

	g_return_val_if_fail (CAMEL_IS_M365_FOLDER_SUMMARY (m365_summary), -1);

	LOCK (m365_summary);

	version = m365_summary->priv->version;

	UNLOCK (m365_summary);

	return version;
}

void
camel_m365_folder_summary_set_delta_link (CamelM365FolderSummary *m365_summary,
					  const gchar *delta_link)
{
	gboolean changed = FALSE;

	g_return_if_fail (CAMEL_IS_M365_FOLDER_SUMMARY (m365_summary));

	LOCK (m365_summary);

	if (g_strcmp0 (m365_summary->priv->delta_link, delta_link) != 0) {
		gchar *dup = g_strdup (delta_link);

		g_free (m365_summary->priv->delta_link);
		m365_summary->priv->delta_link = dup;

		changed = TRUE;
	}

	UNLOCK (m365_summary);

	if (changed)
		camel_folder_summary_touch (CAMEL_FOLDER_SUMMARY (m365_summary));
}

gchar *
camel_m365_folder_summary_dup_delta_link (CamelM365FolderSummary *m365_summary)
{
	gchar *delta_link;

	g_return_val_if_fail (CAMEL_IS_M365_FOLDER_SUMMARY (m365_summary), NULL);

	LOCK (m365_summary);

	delta_link = g_strdup (m365_summary->priv->delta_link);

	UNLOCK (m365_summary);

	return delta_link;
}

void
camel_m365_folder_summary_clear (CamelFolderSummary *summary)
{
	CamelFolderChangeInfo *changes;
	GPtrArray *known_uids;
	gint i;

	changes = camel_folder_change_info_new ();
	known_uids = camel_folder_summary_dup_uids (summary);

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
	g_ptr_array_unref (known_uids);
}

static gboolean
m365_folder_summary_update_user_flags (CamelMessageInfo *info,
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
camel_m365_folder_summary_update_message_info_flags (CamelFolderSummary *summary,
						     CamelMessageInfo *info,
						     guint32 server_flags,
						     const CamelNamedFlags *server_user_flags)
{
	CamelM365MessageInfo *omi;
	gboolean changed = FALSE;

	g_return_val_if_fail (CAMEL_IS_M365_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (CAMEL_IS_M365_MESSAGE_INFO (info), FALSE);

	omi = CAMEL_M365_MESSAGE_INFO (info);

	if (server_flags != camel_m365_message_info_get_server_flags (omi)) {
		guint32 server_set, server_cleared;

		server_set = server_flags & ~camel_m365_message_info_get_server_flags (omi);
		server_cleared = camel_m365_message_info_get_server_flags (omi) & ~server_flags;

		camel_message_info_set_flags (info, server_set | server_cleared, (camel_message_info_get_flags (info) | server_set) & ~server_cleared);
		camel_m365_message_info_set_server_flags (omi, server_flags);
		changed = TRUE;
	}

	if (server_user_flags && m365_folder_summary_update_user_flags (info, server_user_flags))
		changed = TRUE;

	return changed;
}

gboolean
camel_m365_folder_summary_add_message (CamelFolderSummary *summary,
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

	camel_m365_message_info_set_change_key (CAMEL_M365_MESSAGE_INFO (mi), change_key);
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
