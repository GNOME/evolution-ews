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

#include "camel-o365-store-summary.h"

#define STORE_GROUP_NAME "##storepriv##"
#define DATA_VERSION 1

#define LOCK(_summary) g_rec_mutex_lock (&(_summary->priv->property_lock))
#define UNLOCK(_summary) g_rec_mutex_unlock (&(_summary->priv->property_lock))

struct _CamelO365StoreSummaryPrivate {
	GRecMutex property_lock;
	gchar *filename;
	GKeyFile *key_file;
	GFileMonitor *monitor_delete;
	gboolean dirty;

	/* Note: We use the *same* strings in both of these hash tables, and
	 * only id_full_name_hash has g_free() hooked up as the destructor func.
	 * So entries must always be removed from full_name_id_hash *first*. */
	GHashTable *id_full_name_hash; /* id ~> folder full name */
	GHashTable *full_name_id_hash; /* folder full name ~> id */
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelO365StoreSummary, camel_o365_store_summary, G_TYPE_OBJECT)

static gchar *
o365_store_summary_encode_folder_name (const gchar *display_name)
{
	GString *encoded;
	const gchar *pos;

	if (!display_name || !*display_name)
		return NULL;

	encoded = g_string_sized_new (strlen (display_name) + 4);

	for (pos = display_name; *pos; pos++) {
		if (strchr ("%?/", *pos))
			g_string_append_printf (encoded, "%%%02x", *pos);
		else
			g_string_append_c (encoded, *pos);
	}

	return g_string_free (encoded, FALSE);
}

#if 0
static gchar *
o365_store_summary_decode_folder_name (gchar *pathpart)
{
	gchar *pos, *write_pos;

	if (!pathpart || !*pathpart)
		return pathpart;

	pos = pathpart;
	write_pos = pathpart;

	while (*pos) {
		if (*pos == '%' &&
		    g_ascii_isxdigit (pos[1]) &&
		    g_ascii_isxdigit (pos[2])) {
			*write_pos = (g_ascii_xdigit_value (pos[1]) << 4) + g_ascii_xdigit_value (pos[2]);

			pos += 2;
		} else if (write_pos != pos) {
			*write_pos = *pos;
		}

		pos++;
		write_pos++;
	}

	if (write_pos != pos)
		*write_pos = 0;

	return pathpart;
}
#endif

static void
camel_o365_store_summary_migrate_data_locked (CamelO365StoreSummary *store_summary,
					      gint from_version)
{
	/* Here will be any future migration of old data in the summary to new data. */

	g_key_file_set_integer (store_summary->priv->key_file, STORE_GROUP_NAME, "Version", DATA_VERSION);
}

static void
o365_store_summary_delete_cb (GFileMonitor *monitor,
			      GFile *file,
			      GFile *other_file,
			      GFileMonitorEvent event,
			      gpointer user_data)
{
	CamelO365StoreSummary *store_summary = user_data;

	if (event == G_FILE_MONITOR_EVENT_DELETED) {
		LOCK (store_summary);

		if (store_summary->priv->key_file)
			camel_o365_store_summary_clear (store_summary);

		UNLOCK (store_summary);
	}
}

static void
o365_store_summary_dispose (GObject *object)
{
	CamelO365StoreSummary *store_summary = CAMEL_O365_STORE_SUMMARY (object);

	LOCK (store_summary);

	if (store_summary->priv->monitor_delete) {
		g_signal_handlers_disconnect_by_func (store_summary->priv->monitor_delete,
			G_CALLBACK (o365_store_summary_delete_cb), store_summary);

		g_clear_object (&store_summary->priv->monitor_delete);
	}

	UNLOCK (store_summary);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_store_summary_parent_class)->dispose (object);
}

static void
o365_store_summary_finalize (GObject *object)
{
	CamelO365StoreSummary *store_summary = CAMEL_O365_STORE_SUMMARY (object);

	g_rec_mutex_clear (&store_summary->priv->property_lock);
	g_hash_table_destroy (store_summary->priv->full_name_id_hash);
	g_hash_table_destroy (store_summary->priv->id_full_name_hash);
	g_key_file_free (store_summary->priv->key_file);
	g_free (store_summary->priv->filename);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_store_summary_parent_class)->finalize (object);
}

static void
camel_o365_store_summary_class_init (CamelO365StoreSummaryClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = o365_store_summary_dispose;
	object_class->finalize = o365_store_summary_finalize;
}

static void
camel_o365_store_summary_init (CamelO365StoreSummary *store_summary)
{
	store_summary->priv = camel_o365_store_summary_get_instance_private (store_summary);
	store_summary->priv->key_file = g_key_file_new ();
	store_summary->priv->id_full_name_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	store_summary->priv->full_name_id_hash = g_hash_table_new (g_str_hash, g_str_equal); /* shared data with 'id_full_name_hash' */

	g_rec_mutex_init (&store_summary->priv->property_lock);
}

CamelO365StoreSummary *
camel_o365_store_summary_new (const gchar *filename)
{
	CamelO365StoreSummary *store_summary;
	GError *error = NULL;
	GFile *file;

	g_return_val_if_fail (filename != NULL, NULL);

	file = g_file_new_for_path (filename);

	store_summary = g_object_new (CAMEL_TYPE_O365_STORE_SUMMARY, NULL);
	store_summary->priv->filename = g_strdup (filename);
	store_summary->priv->monitor_delete = g_file_monitor_file (file, G_FILE_MONITOR_SEND_MOVED, NULL, &error);

	if (!error) {
		g_signal_connect (
			store_summary->priv->monitor_delete, "changed",
			G_CALLBACK (o365_store_summary_delete_cb), store_summary);
	} else {
		g_warning ("%s: Failed to create monitor_delete: %s", G_STRFUNC, error->message);
		g_clear_error (&error);
	}

	g_object_unref (file);

	return store_summary;
}

gboolean
camel_o365_store_summary_load (CamelO365StoreSummary *store_summary,
			       GError **error)
{
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), FALSE);

	LOCK (store_summary);

	g_hash_table_remove_all (store_summary->priv->full_name_id_hash);
	g_hash_table_remove_all (store_summary->priv->id_full_name_hash);

	store_summary->priv->dirty = FALSE;

	success = g_key_file_load_from_file (store_summary->priv->key_file, store_summary->priv->filename, G_KEY_FILE_NONE, &local_error);

	if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_key_file_set_integer (store_summary->priv->key_file, STORE_GROUP_NAME, "Version", DATA_VERSION);

		g_clear_error (&local_error);
		success = TRUE;
	} else if (local_error) {
		g_propagate_error (error, local_error);
	} else {
		gint version;

		version = g_key_file_get_integer (store_summary->priv->key_file, STORE_GROUP_NAME, "Version", NULL);

		if (version && version < DATA_VERSION)
			camel_o365_store_summary_migrate_data_locked (store_summary, version);

		camel_o365_store_summary_rebuild_hashes (store_summary);
	}

	UNLOCK (store_summary);

	return success;
}

gboolean
camel_o365_store_summary_save (CamelO365StoreSummary *store_summary,
			       GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), FALSE);

	LOCK (store_summary);

	if (store_summary->priv->dirty) {
		success = g_key_file_save_to_file (store_summary->priv->key_file, store_summary->priv->filename, error);

		if (success)
			store_summary->priv->dirty = FALSE;
	}

	UNLOCK (store_summary);

	return success;
}

void
camel_o365_store_summary_clear (CamelO365StoreSummary *store_summary)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));

	LOCK (store_summary);

	store_summary->priv->dirty = g_hash_table_size (store_summary->priv->id_full_name_hash) > 0;

	g_key_file_free (store_summary->priv->key_file);
	store_summary->priv->key_file = g_key_file_new ();

	g_hash_table_remove_all (store_summary->priv->full_name_id_hash);
	g_hash_table_remove_all (store_summary->priv->id_full_name_hash);

	UNLOCK (store_summary);
}

void
camel_o365_store_summary_lock (CamelO365StoreSummary *store_summary)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));

	LOCK (store_summary);
}

void
camel_o365_store_summary_unlock (CamelO365StoreSummary *store_summary)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));

	UNLOCK (store_summary);
}

static void
o365_store_summary_build_full_name (const gchar *id,
				    GHashTable *id_folder_name,
				    GHashTable *id_parent_id,
				    GHashTable *covered,
				    GString *inout_full_name)
{
	const gchar *parent_id;

	g_return_if_fail (id != NULL);

	if (g_hash_table_contains (covered, id))
		return;

	g_hash_table_insert (covered, (gpointer) id, NULL);

	parent_id = g_hash_table_lookup (id_parent_id, id);

	if (parent_id && *parent_id && g_hash_table_contains (id_folder_name, parent_id))
		o365_store_summary_build_full_name (parent_id, id_folder_name, id_parent_id, covered, inout_full_name);

	if (inout_full_name->len)
		g_string_append_c (inout_full_name, '/');

	g_string_append (inout_full_name, g_hash_table_lookup (id_folder_name, id));
}

void
camel_o365_store_summary_rebuild_hashes (CamelO365StoreSummary *store_summary)
{
	GHashTable *id_folder_name;
	GHashTable *id_parent_id;
	gchar **groups;
	gint ii;

	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));

	LOCK (store_summary);

	g_hash_table_remove_all (store_summary->priv->full_name_id_hash);
	g_hash_table_remove_all (store_summary->priv->id_full_name_hash);

	id_folder_name = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	id_parent_id = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

	groups = g_key_file_get_groups (store_summary->priv->key_file, NULL);

	for (ii = 0; groups[ii]; ii++) {
		const gchar *group = groups[ii];

		if (g_ascii_strcasecmp (group, STORE_GROUP_NAME) != 0 &&
		    g_key_file_has_key (store_summary->priv->key_file, group, "DisplayName", NULL)) {
			gchar *display_name, *folder_name;

			display_name = g_key_file_get_string (store_summary->priv->key_file, group, "DisplayName", NULL);
			folder_name = o365_store_summary_encode_folder_name (display_name);

			g_hash_table_insert (id_folder_name, (gpointer) group, folder_name);
			g_hash_table_insert (id_parent_id, (gpointer) group,
				camel_o365_store_summary_dup_folder_parent_id (store_summary, group));

			g_free (display_name);
		}
	}

	if (g_hash_table_size (id_folder_name)) {
		GHashTable *covered;
		GHashTableIter iter;
		gpointer key;

		covered = g_hash_table_new (g_str_hash, g_str_equal);

		g_hash_table_iter_init (&iter, id_folder_name);

		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			const gchar *id = key;
			GString *full_name_str;

			g_hash_table_remove_all (covered);

			full_name_str = g_string_sized_new (16);

			o365_store_summary_build_full_name (id, id_folder_name, id_parent_id, covered, full_name_str);

			if (full_name_str->len) {
				gchar *id_dup = g_strdup (id);
				gchar *full_name = g_string_free (full_name_str, FALSE);

				g_hash_table_insert (store_summary->priv->id_full_name_hash, id_dup, full_name);
				g_hash_table_insert (store_summary->priv->full_name_id_hash, full_name, id_dup);
			} else {
				g_string_free (full_name_str, TRUE);
			}
		}

		g_hash_table_destroy (covered);
	}

	g_hash_table_destroy (id_folder_name);
	g_hash_table_destroy (id_parent_id);
	g_strfreev (groups);

	UNLOCK (store_summary);
}

void
camel_o365_store_summary_set_delta_link (CamelO365StoreSummary *store_summary,
					 const gchar *delta_link)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));

	LOCK (store_summary);

	g_key_file_set_string (store_summary->priv->key_file, STORE_GROUP_NAME, "DeltaLink", delta_link ? delta_link : "");

	store_summary->priv->dirty = TRUE;

	UNLOCK (store_summary);
}

gchar *
camel_o365_store_summary_dup_delta_link (CamelO365StoreSummary *store_summary)
{
	gchar *value;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), NULL);

	LOCK (store_summary);

	value = g_key_file_get_string (store_summary->priv->key_file, STORE_GROUP_NAME, "DeltaLink", NULL);

	UNLOCK (store_summary);

	if (value && !*value) {
		g_clear_pointer (&value, g_free);
	}

	return value;
}

gboolean
camel_o365_store_summary_has_folder (CamelO365StoreSummary *store_summary,
				     const gchar *id)
{
	gboolean has;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	LOCK (store_summary);

	has = g_hash_table_contains (store_summary->priv->id_full_name_hash, id);

	UNLOCK (store_summary);

	return has;
}

void
camel_o365_store_summary_remove_folder (CamelO365StoreSummary *store_summary,
					const gchar *id)
{
	const gchar *full_name;

	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);

	LOCK (store_summary);

	full_name = g_hash_table_lookup (store_summary->priv->id_full_name_hash, id);

	if (full_name) {
		g_hash_table_remove (store_summary->priv->full_name_id_hash, full_name);
		g_hash_table_remove (store_summary->priv->id_full_name_hash, id);

		store_summary->priv->dirty = store_summary->priv->dirty ||
			g_key_file_has_group (store_summary->priv->key_file, id);

		g_key_file_remove_group (store_summary->priv->key_file, id, NULL);
	}

	UNLOCK (store_summary);
}

void
camel_o365_store_summary_set_folder (CamelO365StoreSummary *store_summary,
				     gboolean with_hashes_update,
				     const gchar *id,
				     const gchar *parent_id,
				     const gchar *display_name,
				     gint32 total_count,
				     gint32 unread_count,
				     guint32 flags,
				     EO365FolderKind kind,
				     gboolean is_foreign,
				     gboolean is_public)
{
	gboolean changed = FALSE;

	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);
	g_return_if_fail (display_name != NULL);

	LOCK (store_summary);

	camel_o365_store_summary_update_folder (store_summary, with_hashes_update, id, parent_id, display_name, total_count, unread_count, -1);

	camel_o365_store_summary_set_folder_parent_id (store_summary, id, parent_id);
	camel_o365_store_summary_set_folder_total_count (store_summary, id, total_count);
	camel_o365_store_summary_set_folder_unread_count (store_summary, id, unread_count);
	camel_o365_store_summary_set_folder_flags (store_summary, id, flags);

	if (g_key_file_get_integer (store_summary->priv->key_file, id, "Kind", NULL) != kind) {
		g_key_file_set_integer (store_summary->priv->key_file, id, "Kind", kind);
		changed = TRUE;
	}

	if (g_key_file_get_boolean (store_summary->priv->key_file, id, "IsForeign", NULL) != is_foreign) {
		g_key_file_set_boolean (store_summary->priv->key_file, id, "IsForeign", is_foreign);
		changed = TRUE;
	}

	if (g_key_file_get_boolean (store_summary->priv->key_file, id, "IsPublic", NULL) != is_public) {
		g_key_file_set_boolean (store_summary->priv->key_file, id, "IsPublic", is_public);
		changed = TRUE;
	}

	/* Set display name as the last, because it updates internal hashes and depends on the stored data */
	camel_o365_store_summary_set_folder_display_name (store_summary, id, display_name, with_hashes_update);

	if (changed)
		store_summary->priv->dirty = TRUE;

	UNLOCK (store_summary);
}

void
camel_o365_store_summary_update_folder (CamelO365StoreSummary *store_summary,
					gboolean with_hashes_update,
					const gchar *id,
					const gchar *parent_id,
					const gchar *display_name,
					gint32 total_count,
					gint32 unread_count,
					gint32 children_count)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);
	g_return_if_fail (display_name != NULL);

	LOCK (store_summary);

	camel_o365_store_summary_set_folder_parent_id (store_summary, id, parent_id);
	camel_o365_store_summary_set_folder_total_count (store_summary, id, total_count);
	camel_o365_store_summary_set_folder_unread_count (store_summary, id, unread_count);

	if (children_count != -1) {
		guint32 flags = camel_o365_store_summary_get_folder_flags (store_summary, id);

		flags = (flags & (~(CAMEL_FOLDER_CHILDREN | CAMEL_FOLDER_NOCHILDREN))) |
			(children_count ? CAMEL_FOLDER_CHILDREN : CAMEL_FOLDER_NOCHILDREN);

		camel_o365_store_summary_set_folder_flags (store_summary, id, flags);
	}

	/* Set display name as the last, because it updates internal hashes and depends on the stored data */
	camel_o365_store_summary_set_folder_display_name (store_summary, id, display_name, with_hashes_update);

	UNLOCK (store_summary);
}

gboolean
camel_o365_store_summary_get_folder (CamelO365StoreSummary *store_summary,
				     const gchar *id,
				     gchar **out_full_name,
				     gchar **out_display_name,
				     gchar **out_parent_id,
				     gint32 *out_total_count,
				     gint32 *out_unread_count,
				     guint32 *out_flags,
				     EO365FolderKind *out_kind,
				     gboolean *out_is_foreign,
				     gboolean *out_is_public)
{
	gboolean found;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	LOCK (store_summary);

	found = g_key_file_has_group (store_summary->priv->key_file, id);

	if (found) {
		if (out_full_name)
			*out_full_name = g_strdup (g_hash_table_lookup (store_summary->priv->id_full_name_hash, id));

		if (out_display_name)
			*out_display_name = g_key_file_get_string (store_summary->priv->key_file, id, "DisplayName", NULL);

		if (out_parent_id)
			*out_parent_id = g_key_file_get_string (store_summary->priv->key_file, id, "ParentId", NULL);

		if (out_total_count)
			*out_total_count = g_key_file_get_integer (store_summary->priv->key_file, id, "TotalCount", NULL);

		if (out_unread_count)
			*out_unread_count = g_key_file_get_integer (store_summary->priv->key_file, id, "UnreadCount", NULL);

		if (out_flags)
			*out_flags = g_key_file_get_uint64 (store_summary->priv->key_file, id, "Flags", NULL);

		if (out_kind)
			*out_kind = g_key_file_get_integer (store_summary->priv->key_file, id, "Kind", NULL);

		if (out_is_foreign)
			*out_is_foreign = g_key_file_get_boolean (store_summary->priv->key_file, id, "IsForeign", NULL);

		if (out_is_public)
			*out_is_public = g_key_file_get_boolean (store_summary->priv->key_file, id, "IsPublic", NULL);
	}

	UNLOCK (store_summary);

	return found;
}

gchar *
camel_o365_store_summary_dup_folder_full_name (CamelO365StoreSummary *store_summary,
					       const gchar *id)
{
	gchar *value = NULL;

	if (!camel_o365_store_summary_get_folder (store_summary, id, &value, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
		value = NULL;

	return value;
}

gchar *
camel_o365_store_summary_dup_folder_id_for_full_name (CamelO365StoreSummary *store_summary,
						      const gchar *full_name)
{
	gchar *id;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), NULL);
	g_return_val_if_fail (full_name != NULL, NULL);

	LOCK (store_summary);

	id = g_strdup (g_hash_table_lookup (store_summary->priv->full_name_id_hash, full_name));

	UNLOCK (store_summary);

	return id;
}

gchar *
camel_o365_store_summary_dup_folder_id_for_type (CamelO365StoreSummary *store_summary,
						 guint32 folder_type)
{
	GHashTableIter iter;
	gpointer key;
	gchar *id = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), NULL);

	folder_type = folder_type & CAMEL_FOLDER_TYPE_MASK;
	g_return_val_if_fail (folder_type != 0, NULL);

	LOCK (store_summary);

	g_hash_table_iter_init (&iter, store_summary->priv->id_full_name_hash);

	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		guint32 flags;

		flags = camel_o365_store_summary_get_folder_flags (store_summary, key);

		if ((flags & CAMEL_FOLDER_TYPE_MASK) == folder_type) {
			id = g_strdup (key);
			break;
		}
	}

	UNLOCK (store_summary);

	return id;
}

typedef struct _IdFullNameData {
	gchar *id;
	gchar *full_name;
} IdFullNameData;

static IdFullNameData *
id_full_name_data_new (gchar *id,
		       gchar *full_name)
{
	IdFullNameData *ifnd;

	ifnd = g_slice_new (IdFullNameData);
	ifnd->id = id;
	ifnd->full_name = full_name;

	return ifnd;
}

static void
id_full_name_data_free (gpointer ptr)
{
	IdFullNameData *ifnd = ptr;

	if (ifnd) {
		g_free (ifnd->id);
		g_free (ifnd->full_name);
		g_slice_free (IdFullNameData, ifnd);
	}
}

typedef struct _RemovePrefixedData {
	GHashTable *full_name_id_hash;
	const gchar *prefix;
	gint prefix_len;
	GSList *removed; /* IdFullNameData * */
} RemovePrefixedData;

static gboolean
o365_remove_prefixed_cb (gpointer key,
			 gpointer value,
			 gpointer user_data)
{
	RemovePrefixedData *rpd = user_data;
	gchar *id = key, *full_name = value;

	g_return_val_if_fail (rpd != NULL, FALSE);
	g_return_val_if_fail (full_name != NULL, FALSE);

	if (g_str_has_prefix (full_name, rpd->prefix) &&
	    (!full_name[rpd->prefix_len] || full_name[rpd->prefix_len] == '/')) {
		g_hash_table_remove (rpd->full_name_id_hash, full_name);

		rpd->removed = g_slist_prepend (rpd->removed, id_full_name_data_new (id, full_name));

		return TRUE;
	}

	return FALSE;
}

static gchar *
o365_store_summary_build_new_full_name (const gchar *old_full_name,
					const gchar *new_display_name)
{
	gchar *encoded;
	GString *full_name;
	const gchar *last_slash;

	g_return_val_if_fail (old_full_name != NULL, NULL);
	g_return_val_if_fail (new_display_name != NULL, NULL);

	last_slash = strrchr (old_full_name, '/');
	encoded = o365_store_summary_encode_folder_name (new_display_name);
	full_name = g_string_sized_new ((last_slash ? (last_slash - old_full_name) : 0) + strlen (encoded) + 2);

	if (last_slash)
		g_string_append_len (full_name, old_full_name, last_slash - old_full_name + 1);

	g_string_append (full_name, encoded);

	g_free (encoded);

	return g_string_free (full_name, FALSE);
}

gboolean
camel_o365_store_summary_set_folder_display_name (CamelO365StoreSummary *store_summary,
						  const gchar *id,
						  const gchar *display_name,
						  gboolean with_hashes_update)
{
	gchar *current_display_name;
	gboolean changed = FALSE;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);

	LOCK (store_summary);

	current_display_name = g_key_file_get_string (store_summary->priv->key_file, id, "DisplayName", NULL);

	if (g_strcmp0 (current_display_name, display_name) != 0) {
		const gchar *old_full_name;

		g_key_file_set_string (store_summary->priv->key_file, id, "DisplayName", display_name);
		store_summary->priv->dirty = TRUE;

		changed = TRUE;

		if (with_hashes_update) {
			old_full_name = g_hash_table_lookup (store_summary->priv->id_full_name_hash, id);

			if (old_full_name) {
				RemovePrefixedData rpd;
				gchar *new_full_name;
				gint diff;
				GSList *link;

				rpd.full_name_id_hash = store_summary->priv->full_name_id_hash;
				rpd.prefix = old_full_name;
				rpd.prefix_len = strlen (old_full_name);
				rpd.removed = NULL;

				g_hash_table_foreach_remove (store_summary->priv->id_full_name_hash, o365_remove_prefixed_cb, &rpd);

				new_full_name = o365_store_summary_build_new_full_name (old_full_name, display_name);
				diff = strlen (new_full_name) - rpd.prefix_len;

				for (link = rpd.removed; link; link = g_slist_next (link)) {
					IdFullNameData *ifnd = link->data;
					GString *fixed_full_name_str;
					gchar *fixed_full_name;
					gint old_full_name_len;

					old_full_name_len = strlen (ifnd->full_name);
					fixed_full_name_str = g_string_sized_new (old_full_name_len + diff + 2);

					g_string_append (fixed_full_name_str, new_full_name);

					if (old_full_name_len > rpd.prefix_len)
						g_string_append (fixed_full_name_str, ifnd->full_name + rpd.prefix_len);

					fixed_full_name = g_string_free (fixed_full_name_str, FALSE);

					g_hash_table_insert (store_summary->priv->id_full_name_hash, ifnd->id, fixed_full_name);
					g_hash_table_insert (store_summary->priv->full_name_id_hash, fixed_full_name, ifnd->id);

					/* To not be freed by id_full_name_data_free() below */
					ifnd->id = NULL;
				}

				g_slist_free_full (rpd.removed, id_full_name_data_free);
				g_free (new_full_name);
			}
		}
	}

	g_free (current_display_name);

	UNLOCK (store_summary);

	return changed;
}

gchar *
camel_o365_store_summary_dup_folder_display_name (CamelO365StoreSummary *store_summary,
						  const gchar *id)
{
	gchar *value = NULL;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, &value, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
		value = NULL;

	return value;
}

void
camel_o365_store_summary_set_folder_parent_id (CamelO365StoreSummary *store_summary,
					       const gchar *id,
					       const gchar *parent_id)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);

	LOCK (store_summary);

	if (parent_id && *parent_id) {
		gchar *current_parent_id;

		current_parent_id = g_key_file_get_string (store_summary->priv->key_file, id, "ParentId", NULL);

		if (g_strcmp0 (current_parent_id, parent_id) != 0) {
			g_key_file_set_string (store_summary->priv->key_file, id, "ParentId", parent_id);
			store_summary->priv->dirty = TRUE;
		}

		g_free (current_parent_id);
	} else if (g_key_file_has_key (store_summary->priv->key_file, id, "ParentId", NULL)) {
		g_key_file_remove_key (store_summary->priv->key_file, id, "ParentId", NULL);
		store_summary->priv->dirty = TRUE;
	}

	UNLOCK (store_summary);
}

gchar *
camel_o365_store_summary_dup_folder_parent_id (CamelO365StoreSummary *store_summary,
					       const gchar *id)
{
	gchar *value = NULL;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, &value, NULL, NULL, NULL, NULL, NULL, NULL))
		value = NULL;

	return value;
}

void
camel_o365_store_summary_set_folder_total_count (CamelO365StoreSummary *store_summary,
						 const gchar *id,
						 gint32 total_count)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);

	LOCK (store_summary);

	if (g_key_file_get_integer (store_summary->priv->key_file, id, "TotalCount", NULL) != total_count) {
		g_key_file_set_integer (store_summary->priv->key_file, id, "TotalCount", total_count);
		store_summary->priv->dirty = TRUE;
	}

	UNLOCK (store_summary);
}

gint32
camel_o365_store_summary_get_folder_total_count (CamelO365StoreSummary *store_summary,
						 const gchar *id)
{
	gint32 value = 0;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, &value, NULL, NULL, NULL, NULL, NULL))
		value = 0;

	return value;
}

void
camel_o365_store_summary_set_folder_unread_count (CamelO365StoreSummary *store_summary,
						  const gchar *id,
						  gint32 unread_count)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);

	LOCK (store_summary);

	if (g_key_file_get_integer (store_summary->priv->key_file, id, "UnreadCount", NULL) != unread_count) {
		g_key_file_set_integer (store_summary->priv->key_file, id, "UnreadCount", unread_count);
		store_summary->priv->dirty = TRUE;
	}

	UNLOCK (store_summary);
}

gint32
camel_o365_store_summary_get_folder_unread_count (CamelO365StoreSummary *store_summary,
						  const gchar *id)
{
	gint32 value = 0;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, &value, NULL, NULL, NULL, NULL))
		value = 0;

	return value;
}

void
camel_o365_store_summary_set_folder_flags (CamelO365StoreSummary *store_summary,
					   const gchar *id,
					   guint32 flags)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);

	LOCK (store_summary);

	if (g_key_file_get_uint64 (store_summary->priv->key_file, id, "Flags", NULL) != flags) {
		g_key_file_set_uint64 (store_summary->priv->key_file, id, "Flags", flags);
		store_summary->priv->dirty = TRUE;
	}

	UNLOCK (store_summary);
}

guint32
camel_o365_store_summary_get_folder_flags (CamelO365StoreSummary *store_summary,
					   const gchar *id)
{
	guint32 value = 0;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, NULL, &value, NULL, NULL, NULL))
		value = 0;

	return value;
}

guint32
camel_o365_store_summary_get_folder_flags_for_full_name (CamelO365StoreSummary *store_summary,
							 const gchar *full_name)
{
	const gchar *id;
	guint32 flags = 0;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), 0);
	g_return_val_if_fail (full_name != NULL, 0);

	LOCK (store_summary);

	id = g_hash_table_lookup (store_summary->priv->full_name_id_hash, full_name);

	if (id)
		flags = camel_o365_store_summary_get_folder_flags (store_summary, id);

	UNLOCK (store_summary);

	return flags;
}

EO365FolderKind
camel_o365_store_summary_get_folder_kind (CamelO365StoreSummary *store_summary,
					  const gchar *id)
{
	EO365FolderKind value = E_O365_FOLDER_KIND_UNKNOWN;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, NULL, NULL, &value, NULL, NULL))
		value = E_O365_FOLDER_KIND_UNKNOWN;

	return value;
}

gboolean
camel_o365_store_summary_get_folder_is_foreign (CamelO365StoreSummary *store_summary,
						const gchar *id)
{
	gboolean value = FALSE;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &value, NULL))
		value = FALSE;

	return value;
}

gboolean
camel_o365_store_summary_get_folder_is_public (CamelO365StoreSummary *store_summary,
					       const gchar *id)
{
	gboolean value = FALSE;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &value))
		value = FALSE;

	return value;
}

CamelFolderInfo *
camel_o365_store_summary_build_folder_info_for_id (CamelO365StoreSummary *store_summary,
						   const gchar *id)
{
	CamelFolderInfo *info;
	gchar *full_name = NULL;
	gchar *display_name = NULL;
	gint32 total_count = 0;
	gint32 unread_count = 0;
	guint32 flags = 0;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	LOCK (store_summary);

	if (camel_o365_store_summary_get_folder (store_summary, id, &full_name, &display_name, NULL, &total_count, &unread_count, &flags, NULL, NULL, NULL)) {
		info = camel_folder_info_new ();
		info->full_name = full_name;
		info->display_name = display_name;
		info->flags = flags;
		info->unread = unread_count;
		info->total = total_count;
	} else {
		info = NULL;
	}

	UNLOCK (store_summary);

	return info;
}

typedef struct _GatherInfosData {
	CamelO365StoreSummary *store_summary;
	GPtrArray *folder_infos;
	const gchar *prefix;
	gint prefix_len;
	gboolean recursive;
} GatherInfosData;

static void
o365_store_summary_gather_folder_infos (gpointer key,
					gpointer value,
					gpointer user_data)
{
	const gchar *id = key, *full_name = value;
	GatherInfosData *gid = user_data;

	g_return_if_fail (full_name != NULL);
	g_return_if_fail (gid != NULL);

	if (!gid->prefix_len || (g_str_has_prefix (full_name, gid->prefix) &&
	    full_name[gid->prefix_len] == '/')) {
		const gchar *without_prefix = full_name + gid->prefix_len + (gid->prefix_len > 0 ? 1 : 0);

		if (gid->recursive || !strchr (without_prefix, '/')) {
			CamelFolderInfo *info;

			info = camel_o365_store_summary_build_folder_info_for_id (gid->store_summary, id);

			if (info)
				g_ptr_array_add (gid->folder_infos, info);
			else
				g_warning ("%s: Failed to build folder info for id:'%s' full_name:'%s'", G_STRFUNC, id, full_name);
		}
	}
}

CamelFolderInfo *
camel_o365_store_summary_build_folder_info (CamelO365StoreSummary *store_summary,
					    const gchar *top,
					    gboolean recursive)
{
	CamelFolderInfo *info = NULL;
	GatherInfosData gid;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), NULL);

	if (!top)
		top = "";

	LOCK (store_summary);

	gid.store_summary = store_summary;
	gid.folder_infos = g_ptr_array_new ();
	gid.prefix = top;
	gid.prefix_len = strlen (top);
	gid.recursive = recursive;

	g_hash_table_foreach (store_summary->priv->id_full_name_hash, o365_store_summary_gather_folder_infos, &gid);

	info = camel_folder_info_build (gid.folder_infos, NULL, '/', TRUE);

	UNLOCK (store_summary);

	g_ptr_array_free (gid.folder_infos, TRUE);

	return info;
}

static void
o365_store_summary_folder_count_notify_cb (CamelFolderSummary *folder_summary,
					   GParamSpec *param,
					   CamelO365StoreSummary *store_summary)
{
	CamelFolder *folder;
	gchar *folder_id;
	gint count;

	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (folder_summary));
	g_return_if_fail (param != NULL);
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));

	folder = camel_folder_summary_get_folder (folder_summary);

	if (!folder)
		return;

	folder_id = camel_o365_store_summary_dup_folder_id_for_full_name (store_summary, camel_folder_get_full_name (folder));

	/* This can happen on folder delete/unsubscribe, after folder summary clear */
	if (!folder_id)
		return;

	if (g_strcmp0 (g_param_spec_get_name (param), "saved-count") == 0) {
		count = camel_folder_summary_get_saved_count (folder_summary);
		camel_o365_store_summary_set_folder_total_count (store_summary, folder_id, count);
	} else if (g_strcmp0 (g_param_spec_get_name (param), "unread-count") == 0) {
		count = camel_folder_summary_get_unread_count (folder_summary);
		camel_o365_store_summary_set_folder_unread_count (store_summary, folder_id, count);
	} else {
		g_warn_if_reached ();
	}

	g_free (folder_id);
}

void
camel_o365_store_summary_connect_folder_summary (CamelO365StoreSummary *store_summary,
						 CamelFolderSummary *folder_summary)
{
	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (folder_summary));

	g_signal_connect_object (folder_summary, "notify::saved-count", G_CALLBACK (o365_store_summary_folder_count_notify_cb), store_summary, 0);
	g_signal_connect_object (folder_summary, "notify::unread-count", G_CALLBACK (o365_store_summary_folder_count_notify_cb), store_summary, 0);
}
