/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: parthasrathi susarla <sparthasrathi@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-ews-folder.h"
#include "camel-ews-summary.h"

#define EXTRACT_FIRST_DIGIT(val) part ? val=strtoul (part, &part, 10) : 0;
#define EXTRACT_DIGIT(val) part++; part ? val=strtoul (part, &part, 10) : 0;

#define d(x)

/*Prototypes*/
static gboolean summary_header_load (CamelFolderSummary *s, CamelStoreDBFolderRecord *record);
static gboolean summary_header_save (CamelFolderSummary *s, CamelStoreDBFolderRecord *inout_record, GError **error);

/*End of Prototypes*/

struct _CamelEwsSummaryPrivate {
	GMutex property_lock;
	gchar *sync_state;
	gint32 version;
	guint sync_tag_stamp;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelEwsSummary, camel_ews_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static void
ews_summary_finalize (GObject *object)
{
	CamelEwsSummary *ews_summary = CAMEL_EWS_SUMMARY (object);

	g_free (ews_summary->priv->sync_state);
	g_mutex_clear (&ews_summary->priv->property_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_summary_parent_class)->finalize (object);
}

static void
camel_ews_summary_class_init (CamelEwsSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = ews_summary_finalize;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_type = CAMEL_TYPE_EWS_MESSAGE_INFO;
	folder_summary_class->summary_header_save = summary_header_save;
	folder_summary_class->summary_header_load = summary_header_load;
}

static void
camel_ews_summary_init (CamelEwsSummary *ews_summary)
{
	ews_summary->priv = camel_ews_summary_get_instance_private (ews_summary);

	g_mutex_init (&ews_summary->priv->property_lock);
}

/**
 * camel_ews_summary_new:
 *
 * This will create a new CamelEwsSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Returns: A new CamelEwsSummary object.
 **/
CamelFolderSummary *
camel_ews_summary_new (struct _CamelFolder *folder)
{
	CamelFolderSummary *summary;

	summary = g_object_new (CAMEL_TYPE_EWS_SUMMARY, "folder", folder, NULL);

	camel_folder_summary_load (summary, NULL);

	return summary;
}

static gboolean
summary_header_load (CamelFolderSummary *s,
		     CamelStoreDBFolderRecord *record)
{
	CamelEwsSummary *ews_summary = CAMEL_EWS_SUMMARY (s);
	const gchar *sync_state = NULL;
	gchar *part;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->summary_header_load (s, record))
		return FALSE;

	ews_summary->priv->version = 0;
	ews_summary->priv->sync_tag_stamp = 0;

	part = record->bdata;

	if (part) {
		EXTRACT_FIRST_DIGIT (ews_summary->priv->version);

		if (part && !part++)
			part = NULL;
	}

	if (part && strcmp (part, "(null)") &&
	    ews_summary->priv->version >= CAMEL_EWS_SUMMARY_VERSION) {
		EXTRACT_FIRST_DIGIT (ews_summary->priv->sync_tag_stamp);

		if (part && !part++)
			part = NULL;
	}

	if (part && strcmp (part, "(null)") &&
	    ews_summary->priv->version >= CAMEL_EWS_SUMMARY_VERSION) {
		sync_state = part;
	}

	/* Do not call camel_ews_summary_set_sync_state() here,
	   to not mark the summary dirty after load. */
	g_mutex_lock (&ews_summary->priv->property_lock);

	if (g_strcmp0 (ews_summary->priv->sync_state, sync_state) != 0) {
		g_free (ews_summary->priv->sync_state);
		ews_summary->priv->sync_state = g_strdup (sync_state);
	}

	g_mutex_unlock (&ews_summary->priv->property_lock);

	return TRUE;
}

static gboolean
summary_header_save (CamelFolderSummary *s,
		     CamelStoreDBFolderRecord *inout_record,
		     GError **error)
{
	CamelEwsSummary *ews_summary = CAMEL_EWS_SUMMARY (s);
	gchar *sync_state;
	guint sync_tag_stamp;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->summary_header_save (s, inout_record, error))
		return FALSE;

	sync_state = camel_ews_summary_dup_sync_state (ews_summary);
	sync_tag_stamp = camel_ews_summary_get_sync_tag_stamp (ews_summary);

	inout_record->bdata = g_strdup_printf ("%d %u %s", CAMEL_EWS_SUMMARY_VERSION, sync_tag_stamp, sync_state);

	g_free (sync_state);

	ews_summary->priv->version = CAMEL_EWS_SUMMARY_VERSION;

	return TRUE;
}

gboolean
camel_ews_summary_add_message (CamelFolderSummary *summary,
                               const gchar *uid,
			       const gchar *change_key,
			       CamelMessageInfo *info,
                               CamelMimeMessage *message)
{
	CamelMessageInfo *mi;

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (info != NULL, FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	/* Create summary entry */
	mi = camel_folder_summary_info_new_from_message (summary, message);
	g_return_val_if_fail (mi != NULL, FALSE);

	camel_message_info_set_abort_notifications (mi, TRUE);

	camel_ews_message_info_set_change_key (CAMEL_EWS_MESSAGE_INFO (mi), change_key);
	camel_message_info_set_flags (mi, ~0, camel_message_info_get_flags (info));
	camel_message_info_take_user_flags (mi, camel_message_info_dup_user_flags (info));
	camel_message_info_take_user_tags (mi, camel_message_info_dup_user_tags (info));
	camel_message_info_set_size (mi, camel_message_info_get_size (info));
	camel_message_info_set_uid (mi, uid);

	camel_message_info_set_abort_notifications (mi, FALSE);

	camel_folder_summary_add (summary, mi, FALSE);
	camel_folder_summary_touch (summary);
	camel_folder_summary_save (summary, NULL);

	g_object_unref (mi);

	return TRUE;
}

static gboolean
ews_update_user_flags (CamelMessageInfo *info,
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
camel_ews_update_message_info_flags (CamelFolderSummary *summary,
                                     CamelMessageInfo *info,
                                     guint32 server_flags,
                                     const CamelNamedFlags *server_user_flags)
{
	CamelEwsMessageInfo *emi;
	gboolean changed = FALSE;

	g_return_val_if_fail (CAMEL_IS_EWS_SUMMARY (summary), FALSE);
	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (info), FALSE);

	emi = CAMEL_EWS_MESSAGE_INFO (info);

	if (server_flags != camel_ews_message_info_get_server_flags (emi)) {
		guint32 server_set, server_cleared;

		server_set = server_flags & ~camel_ews_message_info_get_server_flags (emi);
		server_cleared = camel_ews_message_info_get_server_flags (emi) & ~server_flags;

		camel_message_info_set_flags (info, server_set | server_cleared, (camel_message_info_get_flags (info) | server_set) & ~server_cleared);
		camel_ews_message_info_set_server_flags (emi, server_flags);
		changed = TRUE;
	}

	/* TODO test user_flags after enabling it */
	if (server_user_flags && ews_update_user_flags (info, server_user_flags))
		changed = TRUE;

	return changed;
}

void
ews_summary_clear (CamelFolderSummary *summary,
                   gboolean uncache)
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
		camel_folder_summary_remove_uid (summary, uid);
	}

	camel_folder_summary_clear (summary, NULL);
	/*camel_folder_summary_save (summary);*/

	if (camel_folder_change_info_changed (changes))
		camel_folder_changed (camel_folder_summary_get_folder (summary), changes);
	camel_folder_change_info_free (changes);
	g_ptr_array_unref (known_uids);
}

gint32
camel_ews_summary_get_version (CamelEwsSummary *ews_summary)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SUMMARY (ews_summary), -1);

	return ews_summary->priv->version;
}

void
camel_ews_summary_set_sync_state (CamelEwsSummary *ews_summary,
				  const gchar *sync_state)
{
	gboolean changed = FALSE;

	g_return_if_fail (CAMEL_IS_EWS_SUMMARY (ews_summary));

	g_mutex_lock (&ews_summary->priv->property_lock);

	if (g_strcmp0 (ews_summary->priv->sync_state, sync_state) != 0) {
		g_free (ews_summary->priv->sync_state);
		ews_summary->priv->sync_state = g_strdup (sync_state);
		changed = TRUE;
	}

	g_mutex_unlock (&ews_summary->priv->property_lock);

	if (changed)
		camel_folder_summary_touch (CAMEL_FOLDER_SUMMARY (ews_summary));
}

gchar *
camel_ews_summary_dup_sync_state (CamelEwsSummary *ews_summary)
{
	gchar *sync_state;

	g_return_val_if_fail (CAMEL_IS_EWS_SUMMARY (ews_summary), NULL);

	g_mutex_lock (&ews_summary->priv->property_lock);

	sync_state = g_strdup (ews_summary->priv->sync_state);

	g_mutex_unlock (&ews_summary->priv->property_lock);

	return sync_state;
}

guint
camel_ews_summary_get_sync_tag_stamp (CamelEwsSummary *ews_summary)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SUMMARY (ews_summary), 0);

	return ews_summary->priv->sync_tag_stamp;
}

void
camel_ews_summary_set_sync_tag_stamp (CamelEwsSummary *ews_summary,
				      guint value)
{
	g_return_if_fail (CAMEL_IS_EWS_SUMMARY (ews_summary));

	if (ews_summary->priv->sync_tag_stamp != value)
		ews_summary->priv->sync_tag_stamp = value;
}
