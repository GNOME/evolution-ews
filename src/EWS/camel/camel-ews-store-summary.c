/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "camel-ews-store-summary.h"

#include "common/e-ews-folder.h"

#define S_LOCK(x) (g_rec_mutex_lock(&(x)->priv->s_lock))
#define S_UNLOCK(x) (g_rec_mutex_unlock(&(x)->priv->s_lock))

#define STORE_GROUP_NAME "##storepriv"
#define CATEGORIES_KEY "Categories"
#define CURRENT_SUMMARY_VERSION 3

struct _CamelEwsStoreSummaryPrivate {
	GKeyFile *key_file;
	gboolean dirty;
	gchar *path;
	/* Note: We use the *same* strings in both of these hash tables, and
	 * only id_fname_hash has g_free() hooked up as the destructor func.
	 * So entries must always be removed from fname_id_hash *first*. */
	GHashTable *id_fname_hash;
	GHashTable *fname_id_hash;
	GRecMutex s_lock;

	GFileMonitor *monitor_delete;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelEwsStoreSummary, camel_ews_store_summary, G_TYPE_OBJECT)

static void
ews_store_summary_finalize (GObject *object)
{
	CamelEwsStoreSummary *ews_summary = CAMEL_EWS_STORE_SUMMARY (object);
	CamelEwsStoreSummaryPrivate *priv = ews_summary->priv;

	g_key_file_free (priv->key_file);
	g_free (priv->path);
	g_hash_table_destroy (priv->fname_id_hash);
	g_hash_table_destroy (priv->id_fname_hash);
	g_rec_mutex_clear (&priv->s_lock);
	if (priv->monitor_delete)
		g_object_unref (priv->monitor_delete);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_store_summary_parent_class)->finalize (object);
}

static void
camel_ews_store_summary_class_init (CamelEwsStoreSummaryClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = ews_store_summary_finalize;
}

static void
camel_ews_store_summary_init (CamelEwsStoreSummary *ews_summary)
{
	ews_summary->priv = camel_ews_store_summary_get_instance_private (ews_summary);

	ews_summary->priv->key_file = g_key_file_new ();
	ews_summary->priv->dirty = FALSE;
	ews_summary->priv->fname_id_hash = g_hash_table_new (g_str_hash, g_str_equal);
	ews_summary->priv->id_fname_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	g_rec_mutex_init (&ews_summary->priv->s_lock);
}

static gchar *build_full_name (CamelEwsStoreSummary *ews_summary, const gchar *fid)
{
	gchar *pfid, *dname, *ret;
	gchar *pname = NULL;

	dname = camel_ews_store_summary_get_folder_name (ews_summary, fid, NULL);
	if (!dname)
		return NULL;

	pfid = camel_ews_store_summary_get_parent_folder_id (ews_summary, fid, NULL);
	if (pfid) {
		pname = build_full_name (ews_summary, pfid);
		g_free (pfid);
	}

	if (pname) {
		ret = g_strdup_printf ("%s/%s", pname, dname);
		g_free (pname);
		g_free (dname);
	} else
		ret = dname;

	return ret;
}

static void
load_id_fname_hash (CamelEwsStoreSummary *ews_summary)
{
	GSList *folders, *l;

	g_hash_table_remove_all (ews_summary->priv->fname_id_hash);
	g_hash_table_remove_all (ews_summary->priv->id_fname_hash);

	folders = camel_ews_store_summary_get_folders (ews_summary, NULL, FALSE);

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		gchar *id = l->data;
		gchar *fname;

		fname = build_full_name (ews_summary, id);

		if (!fname) {
			/* eep */
			g_warning ("Cannot build full name for folder %s", id);
			g_free (id);
			continue;
		}
		g_hash_table_insert (ews_summary->priv->fname_id_hash, fname, id);
		g_hash_table_insert (ews_summary->priv->id_fname_hash, id, fname);
	}

	g_slist_free (folders);
}

/* we only care about delete and ignore create */
static void
monitor_delete_cb (GFileMonitor *monitor,
                   GFile *file,
                   GFile *other_file,
                   GFileMonitorEvent event,
                   gpointer user_data)
{
	CamelEwsStoreSummary *ews_summary = (CamelEwsStoreSummary *) user_data;

	if (event == G_FILE_MONITOR_EVENT_DELETED) {
		S_LOCK (ews_summary);

		if (ews_summary->priv->key_file)
			camel_ews_store_summary_clear (ews_summary);

		S_UNLOCK (ews_summary);
	}
}

CamelEwsStoreSummary *
camel_ews_store_summary_new (const gchar *path)
{
	CamelEwsStoreSummary *ews_summary;
	GError *error = NULL;
	GFile *file;

	ews_summary = g_object_new (CAMEL_TYPE_EWS_STORE_SUMMARY, NULL);

	ews_summary->priv->path = g_strdup (path);
	file = g_file_new_for_path (path);
	ews_summary->priv->monitor_delete = g_file_monitor_file (
		file, G_FILE_MONITOR_SEND_MOVED, NULL, &error);

	/* Remove this once we have camel_store_remove_storage api,
	 * which should be available from 3.2 */
	if (!error) {
		g_signal_connect (
			ews_summary->priv->monitor_delete, "changed",
			G_CALLBACK (monitor_delete_cb), ews_summary);
	} else {
		g_warning (
			"CamelEwsStoreSummary: "
			"Error create monitor_delete: %s \n",
			error->message);
		g_clear_error (&error);
	}

	g_object_unref (file);

	return ews_summary;
}

gboolean
camel_ews_store_summary_load (CamelEwsStoreSummary *ews_summary,
                              GError **error)
{
	CamelEwsStoreSummaryPrivate *priv = ews_summary->priv;
	gboolean ret;
	gint version;

	S_LOCK (ews_summary);

	ret = g_key_file_load_from_file (
		priv->key_file, priv->path, 0, error);

	version = g_key_file_get_integer (
		priv->key_file, STORE_GROUP_NAME, "Version", NULL);

	if (version != CURRENT_SUMMARY_VERSION) {
		/* version doesn't match, get folders again */
		camel_ews_store_summary_clear (ews_summary);

		g_key_file_set_integer (
			priv->key_file, STORE_GROUP_NAME,
			"Version", CURRENT_SUMMARY_VERSION);
	}

	load_id_fname_hash (ews_summary);

	S_UNLOCK (ews_summary);

	return ret;
}

gboolean
camel_ews_store_summary_save (CamelEwsStoreSummary *ews_summary,
                              GError **error)
{
	CamelEwsStoreSummaryPrivate *priv = ews_summary->priv;
	gboolean ret = TRUE;
	GFile *file;
	gchar *contents = NULL;

	S_LOCK (ews_summary);

	if (!priv->dirty)
		goto exit;

	contents = g_key_file_to_data (
		priv->key_file, NULL, NULL);
	file = g_file_new_for_path (priv->path);
	ret = g_file_replace_contents (
		file, contents, strlen (contents),
		NULL, FALSE, G_FILE_CREATE_PRIVATE,
		NULL, NULL, error);
	g_object_unref (file);
	priv->dirty = FALSE;

exit:
	S_UNLOCK (ews_summary);

	g_free (contents);
	return ret;
}

gboolean
camel_ews_store_summary_clear (CamelEwsStoreSummary *ews_summary)
{

	S_LOCK (ews_summary);

	g_key_file_free (ews_summary->priv->key_file);
	ews_summary->priv->key_file = g_key_file_new ();
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);

	return TRUE;
}

gboolean
camel_ews_store_summary_remove (CamelEwsStoreSummary *ews_summary)
{
	gint ret;

	S_LOCK (ews_summary);

	if (ews_summary->priv->key_file)
		camel_ews_store_summary_clear (ews_summary);

	ret = g_unlink (ews_summary->priv->path);

	S_UNLOCK (ews_summary);

	return (ret == 0);
}

void
camel_ews_store_summary_rebuild_hashes (CamelEwsStoreSummary *ews_summary)
{
	g_return_if_fail (CAMEL_IS_EWS_STORE_SUMMARY (ews_summary));

	S_LOCK (ews_summary);
	load_id_fname_hash (ews_summary);
	S_UNLOCK (ews_summary);
}

struct subfolder_match {
	GSList *ids;
	gchar *match;
	gsize matchlen;
};

static void
match_subfolder (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
	struct subfolder_match *sm = user_data;

	if (!strncmp (key, sm->match, sm->matchlen))
		sm->ids = g_slist_prepend (sm->ids, g_strdup (value));
}

/* Must be called with the summary lock held, and gets to keep
 * both its string arguments */
static void
ews_ss_hash_replace (CamelEwsStoreSummary *ews_summary,
                     gchar *folder_id,
                     gchar *full_name,
                     gboolean recurse)
{
	const gchar *ofname;
	struct subfolder_match sm = { NULL, NULL };

	if (!full_name)
		full_name = build_full_name (ews_summary, folder_id);

	ofname = g_hash_table_lookup (
		ews_summary->priv->id_fname_hash, folder_id);
	/* Remove the old fullname->id hash entry *iff* it's pointing
	 * to this folder id. */
	if (ofname) {
		gchar *ofid = g_hash_table_lookup (
			ews_summary->priv->fname_id_hash, ofname);
		if (ofid && !strcmp (folder_id, ofid)) {
			g_hash_table_remove (
				ews_summary->priv->fname_id_hash, ofname);
			if (recurse)
				sm.match = g_strdup_printf ("%s/", ofname);
		}
	}

	g_hash_table_insert (ews_summary->priv->fname_id_hash, full_name, folder_id);

	/* Replace, not insert. The difference is that it frees the *old* folder_id
	 * key, not the new one which we just inserted into fname_id_hash too. */
	g_hash_table_replace (ews_summary->priv->id_fname_hash, folder_id, full_name);

	if (sm.match) {
		GSList *l;

		sm.matchlen = strlen (sm.match);

		g_hash_table_foreach (
			ews_summary->priv->fname_id_hash,
			match_subfolder, &sm);

		for (l = sm.ids; l; l = g_slist_next (l))
			ews_ss_hash_replace (ews_summary, l->data, NULL, FALSE);

		g_slist_free (sm.ids);
		g_free (sm.match);
	}
}

void
camel_ews_store_summary_set_folder_name (CamelEwsStoreSummary *ews_summary,
                                         const gchar *folder_id,
                                         const gchar *display_name)
{
	S_LOCK (ews_summary);

	g_key_file_set_string (
		ews_summary->priv->key_file,
		folder_id, "DisplayName", display_name);

	ews_ss_hash_replace (ews_summary, g_strdup (folder_id), NULL, TRUE);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_new_folder (CamelEwsStoreSummary *ews_summary,
                                    const gchar *folder_id,
                                    const gchar *parent_fid,
                                    const gchar *change_key,
                                    const gchar *display_name,
                                    EEwsFolderType folder_type,
                                    guint64 folder_flags,
                                    guint64 total,
                                    gboolean foreign,
				    gboolean public_folder)
{
	const gchar *folder_type_nick;

	/* Store the folder type by its nickname. */
	folder_type_nick = e_ews_folder_type_to_nick (folder_type);
	g_return_if_fail (folder_type_nick != NULL);

	S_LOCK (ews_summary);

	if (parent_fid)
		g_key_file_set_string (
			ews_summary->priv->key_file,
			folder_id, "ParentFolderId", parent_fid);
	if (change_key)
		g_key_file_set_string (
			ews_summary->priv->key_file,
			folder_id, "ChangeKey", change_key);
	g_key_file_set_string (
		ews_summary->priv->key_file,
		folder_id, "DisplayName", display_name);
	g_key_file_set_string (
		ews_summary->priv->key_file,
		folder_id, "FolderType", folder_type_nick);
	if (folder_flags)
		g_key_file_set_uint64 (
			ews_summary->priv->key_file,
			folder_id, "Flags", folder_flags);
	g_key_file_set_uint64 (
		ews_summary->priv->key_file,
		folder_id, "Total", total);
	g_key_file_set_boolean (
		ews_summary->priv->key_file,
		folder_id, "Foreign", foreign);
	g_key_file_set_boolean (
		ews_summary->priv->key_file,
		folder_id, "Public", public_folder);

	ews_ss_hash_replace (ews_summary, g_strdup (folder_id), NULL, FALSE);

	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_parent_folder_id (CamelEwsStoreSummary *ews_summary,
                                              const gchar *folder_id,
                                              const gchar *parent_id)
{
	S_LOCK (ews_summary);

	if (parent_id)
		g_key_file_set_string (
			ews_summary->priv->key_file,
			folder_id, "ParentFolderId", parent_id);
	else
		g_key_file_remove_key (
			ews_summary->priv->key_file,
			folder_id, "ParentFolderId", NULL);

	ews_ss_hash_replace (ews_summary, g_strdup (folder_id), NULL, TRUE);

	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_change_key (CamelEwsStoreSummary *ews_summary,
                                         const gchar *folder_id,
                                         const gchar *change_key)
{
	S_LOCK (ews_summary);

	g_key_file_set_string (
		ews_summary->priv->key_file,
		folder_id, "ChangeKey", change_key);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_sync_state (CamelEwsStoreSummary *ews_summary,
                                        const gchar *folder_id,
                                        const gchar *sync_state)
{
	S_LOCK (ews_summary);

	g_key_file_set_string (
		ews_summary->priv->key_file,
		folder_id, "SyncState", sync_state);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_folder_flags (CamelEwsStoreSummary *ews_summary,
                                          const gchar *folder_id,
                                          guint64 flags)
{
	S_LOCK (ews_summary);

	g_key_file_set_uint64 (
		ews_summary->priv->key_file,
		folder_id, "Flags", flags);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_folder_unread (CamelEwsStoreSummary *ews_summary,
                                           const gchar *folder_id,
                                           guint64 unread)
{
	S_LOCK (ews_summary);

	g_key_file_set_uint64 (
		ews_summary->priv->key_file,
		folder_id, "UnRead", unread);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_folder_total (CamelEwsStoreSummary *ews_summary,
                                          const gchar *folder_id,
                                          guint64 total)
{
	S_LOCK (ews_summary);

	g_key_file_set_uint64 (
		ews_summary->priv->key_file,
		folder_id, "Total", total);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_folder_type (CamelEwsStoreSummary *ews_summary,
                                         const gchar *folder_id,
                                         EEwsFolderType folder_type)
{
	const gchar *folder_type_nick;

	/* Store the folder type by its nickname. */
	folder_type_nick = e_ews_folder_type_to_nick (folder_type);
	g_return_if_fail (folder_type_nick != NULL);

	S_LOCK (ews_summary);

	g_key_file_set_string (
		ews_summary->priv->key_file,
		folder_id, "FolderType", folder_type_nick);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_foreign (CamelEwsStoreSummary *ews_summary,
                                     const gchar *folder_id,
                                     gboolean is_foreign)
{
	S_LOCK (ews_summary);

	g_key_file_set_boolean (
		ews_summary->priv->key_file,
		folder_id, "Foreign", is_foreign);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_foreign_subfolders (CamelEwsStoreSummary *ews_summary,
						const gchar *folder_id,
						gboolean foreign_subfolders)
{
	S_LOCK (ews_summary);

	g_key_file_set_boolean (
		ews_summary->priv->key_file,
		folder_id, "ForeignSubfolders", foreign_subfolders);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_set_public (CamelEwsStoreSummary *ews_summary,
                                    const gchar *folder_id,
                                    gboolean is_public)
{
	S_LOCK (ews_summary);

	g_key_file_set_boolean (
		ews_summary->priv->key_file,
		folder_id, "Public", is_public);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

void
camel_ews_store_summary_store_string_val (CamelEwsStoreSummary *ews_summary,
                                          const gchar *key,
                                          const gchar *value)
{
	S_LOCK (ews_summary);

	g_key_file_set_string (
		ews_summary->priv->key_file,
		STORE_GROUP_NAME, key, value);
	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);
}

gchar *
camel_ews_store_summary_get_folder_name (CamelEwsStoreSummary *ews_summary,
                                         const gchar *folder_id,
                                         GError **error)
{
	gchar *ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_string (
		ews_summary->priv->key_file, folder_id,
		"DisplayName", error);

	S_UNLOCK (ews_summary);

	return ret;
}

gchar *
camel_ews_store_summary_get_folder_full_name (CamelEwsStoreSummary *ews_summary,
                                              const gchar *folder_id,
                                              GError **error)
{
	gchar *ret;

	S_LOCK (ews_summary);

	ret = g_hash_table_lookup (ews_summary->priv->id_fname_hash, folder_id);

	if (ret)
		ret = g_strdup (ret);

	S_UNLOCK (ews_summary);

	return ret;
}

gchar *
camel_ews_store_summary_get_parent_folder_id (CamelEwsStoreSummary *ews_summary,
                                              const gchar *folder_id,
                                              GError **error)
{
	gchar *ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_string (
		ews_summary->priv->key_file, folder_id,
		"ParentFolderId", error);

	S_UNLOCK (ews_summary);

	return ret;
}

gchar *
camel_ews_store_summary_get_change_key (CamelEwsStoreSummary *ews_summary,
                                        const gchar *folder_id,
                                        GError **error)
{
	gchar *ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_string (
		ews_summary->priv->key_file, folder_id,
		"ChangeKey", error);

	S_UNLOCK (ews_summary);

	return ret;
}

gchar *
camel_ews_store_summary_get_sync_state (CamelEwsStoreSummary *ews_summary,
                                        const gchar *folder_id,
                                        GError **error)
{
	gchar *ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_string (
		ews_summary->priv->key_file, folder_id,
		"SyncState", error);

	S_UNLOCK (ews_summary);

	return ret;
}

guint64
camel_ews_store_summary_get_folder_flags (CamelEwsStoreSummary *ews_summary,
                                          const gchar *folder_id,
                                          GError **error)
{
	guint64 ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_uint64 (
		ews_summary->priv->key_file, folder_id,
		"Flags", error);

	S_UNLOCK (ews_summary);

	return ret;
}

guint64
camel_ews_store_summary_get_folder_unread (CamelEwsStoreSummary *ews_summary,
                                           const gchar *folder_id,
                                           GError **error)
{
	guint64 ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_uint64 (
		ews_summary->priv->key_file, folder_id,
		"UnRead", error);

	S_UNLOCK (ews_summary);

	return ret;
}

guint64
camel_ews_store_summary_get_folder_total (CamelEwsStoreSummary *ews_summary,
                                          const gchar *folder_id,
                                          GError **error)
{
	guint64 ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_uint64 (
		ews_summary->priv->key_file, folder_id,
		"Total", error);

	S_UNLOCK (ews_summary);

	return ret;
}

EEwsFolderType
camel_ews_store_summary_get_folder_type (CamelEwsStoreSummary *ews_summary,
                                         const gchar *folder_id,
                                         GError **error)
{
	EEwsFolderType folder_type;
	gchar *folder_type_nick;

	S_LOCK (ews_summary);

	folder_type_nick = g_key_file_get_string (
		ews_summary->priv->key_file,
		folder_id, "FolderType", error);

	S_UNLOCK (ews_summary);

	/* Look up the folder type by its nickname. */
	if (folder_type_nick != NULL)
		folder_type = e_ews_folder_type_from_nick (folder_type_nick);
	else
		folder_type = E_EWS_FOLDER_TYPE_UNKNOWN;

	g_free (folder_type_nick);

	return folder_type;
}

gboolean
camel_ews_store_summary_get_foreign (CamelEwsStoreSummary *ews_summary,
                                     const gchar *folder_id,
                                     GError **error)
{
	gboolean ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_boolean (
		ews_summary->priv->key_file, folder_id, "Foreign", error);

	S_UNLOCK (ews_summary);

	return ret;
}

gboolean
camel_ews_store_summary_get_foreign_subfolders (CamelEwsStoreSummary *ews_summary,
						const gchar *folder_id,
						GError **error)
{
	gboolean ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_boolean (
		ews_summary->priv->key_file, folder_id, "ForeignSubfolders", error);

	S_UNLOCK (ews_summary);

	return ret;
}

gboolean
camel_ews_store_summary_get_public (CamelEwsStoreSummary *ews_summary,
                                    const gchar *folder_id,
                                    GError **error)
{
	gboolean ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_boolean (
		ews_summary->priv->key_file, folder_id, "Public", error);

	S_UNLOCK (ews_summary);

	return ret;
}

gchar *
camel_ews_store_summary_get_string_val (CamelEwsStoreSummary *ews_summary,
                                         const gchar *key,
                                         GError **error)
{
	gchar *ret;

	S_LOCK (ews_summary);

	ret = g_key_file_get_string (
		ews_summary->priv->key_file, STORE_GROUP_NAME,
		key, error);

	S_UNLOCK (ews_summary);

	return ret;
}

GSList *
camel_ews_store_summary_get_folders (CamelEwsStoreSummary *ews_summary,
				     const gchar *prefix,
				     gboolean only_direct_subfolders)
{
	GSList *folders = NULL;
	gchar **groups = NULL;
	gsize length;
	gint prefixlen = 0;
	gint i;

	if (prefix)
		prefixlen = strlen (prefix);

	S_LOCK (ews_summary);

	groups = g_key_file_get_groups (ews_summary->priv->key_file, &length);

	S_UNLOCK (ews_summary);

	for (i = 0; i < length; i++) {
		if (!g_ascii_strcasecmp (groups[i], STORE_GROUP_NAME))
			continue;
		if (prefixlen) {
			const gchar *fname;

			fname = g_hash_table_lookup (
				ews_summary->priv->id_fname_hash, groups[i]);

			if (!fname || strncmp (fname, prefix, prefixlen) ||
			    (fname[prefixlen] && fname[prefixlen] != '/') ||
			    (only_direct_subfolders && (!fname[prefixlen] || strchr (fname + prefixlen + 1, '/'))))
				continue;
		}
		folders = g_slist_append (folders, g_strdup (groups[i]));
	}

	g_strfreev (groups);
	return folders;
}

/* get list of folder IDs, which are foreign folders */
GSList *
camel_ews_store_summary_get_foreign_folders (CamelEwsStoreSummary *ews_summary,
					     const gchar *prefix)
{
	GSList *folders = NULL;
	gchar **groups = NULL;
	gsize length;
	gint prefixlen = 0;
	gint i;

	if (prefix)
		prefixlen = strlen (prefix);

	S_LOCK (ews_summary);

	groups = g_key_file_get_groups (ews_summary->priv->key_file, &length);

	S_UNLOCK (ews_summary);

	for (i = 0; i < length; i++) {
		if (!g_ascii_strcasecmp (groups[i], STORE_GROUP_NAME))
			continue;

		if (!camel_ews_store_summary_get_foreign (ews_summary, groups[i], NULL))
			continue;

		if (prefixlen) {
			const gchar *fname;

			fname = g_hash_table_lookup (
				ews_summary->priv->id_fname_hash, groups[i]);

			if (!fname || strncmp (fname, prefix, prefixlen) ||
			    (fname[prefixlen] && fname[prefixlen] != '/'))
				continue;
		}

		folders = g_slist_append (folders, g_strdup (groups[i]));
	}

	g_strfreev (groups);

	return folders;
}

gboolean
camel_ews_store_summary_remove_folder (CamelEwsStoreSummary *ews_summary,
                                       const gchar *folder_id,
                                       GError **error)
{
	gboolean ret = FALSE;
	gchar *full_name;

	S_LOCK (ews_summary);

	full_name = g_hash_table_lookup (ews_summary->priv->id_fname_hash, folder_id);
	if (!full_name)
		goto unlock;

	ret = g_key_file_remove_group (
		ews_summary->priv->key_file, folder_id, error);

	g_hash_table_remove (ews_summary->priv->fname_id_hash, full_name);
	g_hash_table_remove (ews_summary->priv->id_fname_hash, folder_id);

	ews_summary->priv->dirty = TRUE;

 unlock:
	S_UNLOCK (ews_summary);

	return ret;
}

gchar *
camel_ews_store_summary_get_folder_id_from_name (CamelEwsStoreSummary *ews_summary,
                                                 const gchar *folder_name)
{
	gchar *folder_id;

	g_return_val_if_fail (ews_summary != NULL, NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	S_LOCK (ews_summary);

	folder_id = g_hash_table_lookup (ews_summary->priv->fname_id_hash, folder_name);
	if (folder_id)
		folder_id = g_strdup (folder_id);

	S_UNLOCK (ews_summary);

	return folder_id;
}

gchar *
camel_ews_store_summary_get_folder_id_from_folder_type (CamelEwsStoreSummary *ews_summary,
                                                        guint64 folder_type)
{
	gchar *folder_id = NULL;
	GSList *folders, *l;

	g_return_val_if_fail (ews_summary != NULL, NULL);
	g_return_val_if_fail ((folder_type & CAMEL_FOLDER_TYPE_MASK) != 0, NULL);

	folder_type = folder_type & CAMEL_FOLDER_TYPE_MASK;

	S_LOCK (ews_summary);

	folders = camel_ews_store_summary_get_folders (ews_summary, NULL, FALSE);

	for (l = folders; l != NULL; l = g_slist_next (l)) {
		gchar *id = l->data;
		guint64 folder_flags;

		folder_flags = camel_ews_store_summary_get_folder_flags (
			ews_summary, id, NULL);
		if ((folder_flags & CAMEL_FOLDER_TYPE_MASK) == folder_type &&
		    (folder_flags & CAMEL_FOLDER_SYSTEM) != 0) {
			folder_id = id;
			l->data = NULL;
			break;
		}
	}

	g_slist_free_full (folders, g_free);

	S_UNLOCK (ews_summary);

	return folder_id;
}

gboolean
camel_ews_store_summary_has_folder (CamelEwsStoreSummary *ews_summary,
                                    const gchar *folder_id)
{
	gboolean ret;

	S_LOCK (ews_summary);

	ret = g_key_file_has_group (ews_summary->priv->key_file, folder_id);

	S_UNLOCK (ews_summary);

	return ret;
}

static gchar *
camel_ews_category_to_string (const CamelEwsCategory *cat)
{
	gchar *guid, *name, *color_def = NULL, *str;

	g_return_val_if_fail (cat != NULL, NULL);

	guid = g_uri_escape_string (cat->guid, NULL, TRUE);
	name = g_uri_escape_string (cat->name, NULL, TRUE);

	if (cat->color_def)
		color_def = g_uri_escape_string (cat->color_def, NULL, TRUE);

	str = g_strconcat (
		guid ? guid : "", "\t",
		name ? name : "", "\t",
		color_def ? color_def : "",
		NULL);

	g_free (guid);
	g_free (name);
	g_free (color_def);

	return str;
}

static CamelEwsCategory *
camel_ews_category_from_string (const gchar *str)
{
	CamelEwsCategory *cat;
	gchar **strv, *guid, *name, *color_def;

	g_return_val_if_fail (str != NULL, NULL);

	strv = g_strsplit (str, "\t", -1);
	if (!strv || !strv[0] || !strv[1]) {
		g_strfreev (strv);
		return NULL;
	}

	guid = g_uri_unescape_string (strv[0], NULL);
	name = g_uri_unescape_string (strv[1], NULL);
	color_def = (strv[2] && strv[2][0]) ? g_uri_unescape_string (strv[2], NULL) : NULL;

	cat = camel_ews_category_new (guid, name, color_def);

	g_free (guid);
	g_free (name);
	g_free (color_def);
	g_strfreev (strv);

	return cat;
}

GHashTable * /* gchar *guid ~> CamelEwsCategory * */
camel_ews_store_summary_get_categories (CamelEwsStoreSummary *ews_summary)
{
	GHashTable *categories;
	gchar **strv;
	g_return_val_if_fail (CAMEL_IS_EWS_STORE_SUMMARY (ews_summary), NULL);

	S_LOCK (ews_summary);

	strv = g_key_file_get_string_list (ews_summary->priv->key_file, STORE_GROUP_NAME, CATEGORIES_KEY, NULL, NULL);

	S_UNLOCK (ews_summary);

	categories = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, camel_ews_category_free);

	if (strv) {
		gint ii;

		for (ii = 0; strv[ii]; ii++) {
			CamelEwsCategory *cat;

			cat = camel_ews_category_from_string (strv[ii]);
			if (cat)
				g_hash_table_insert (categories, cat->guid, cat);
		}

		g_strfreev (strv);
	}

	return categories;
}

void
camel_ews_store_summary_set_categories (CamelEwsStoreSummary *ews_summary,
					GHashTable *categories) /* gchar *guid ~> CamelEwsCategory * */
{
	GPtrArray *array;
	GHashTableIter iter;
	gpointer value;

	g_return_if_fail (CAMEL_IS_EWS_STORE_SUMMARY (ews_summary));
	g_return_if_fail (categories != NULL);

	array = g_ptr_array_new_full (g_hash_table_size (categories), g_free);

	g_hash_table_iter_init (&iter, categories);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		CamelEwsCategory *cat = value;

		if (cat) {
			gchar *str;

			str = camel_ews_category_to_string (cat);

			if (str)
				g_ptr_array_add (array, str);
		}
	}

	S_LOCK (ews_summary);

	g_key_file_set_string_list (ews_summary->priv->key_file, STORE_GROUP_NAME, CATEGORIES_KEY,
		(const gchar * const *) array->pdata, array->len);

	ews_summary->priv->dirty = TRUE;

	S_UNLOCK (ews_summary);

	g_ptr_array_free (array, TRUE);
}

CamelEwsCategory *
camel_ews_category_new (const gchar *guid,
			const gchar *name,
			const gchar *color_def)
{
	CamelEwsCategory *cat;

	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	cat = g_new0 (CamelEwsCategory, 1);
	cat->guid = g_strdup (guid);
	cat->name = g_strdup (name);
	cat->color_def = g_strdup (color_def);

	return cat;
}

void
camel_ews_category_free (gpointer ptr)
{
	CamelEwsCategory *cat = ptr;

	if (cat) {
		g_free (cat->guid);
		g_free (cat->name);
		g_free (cat->color_def);
		g_free (cat);
	}
}
