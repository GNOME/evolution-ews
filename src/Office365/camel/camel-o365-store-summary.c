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

#define LOCK(summary) g_rec_mutex_lock (&(summary->priv->property_lock))
#define UNLOCK(summary) g_rec_mutex_unlock (&(summary->priv->property_lock))

struct _CamelO365StoreSummaryPrivate {
	GRecMutex property_lock;
	gchar *path;
	GKeyFile *key_file;
	GFileMonitor *monitor_delete;
	gboolean dirty;

	/* Note: We use the *same* strings in both of these hash tables, and
	 * only id_fname_hash has g_free() hooked up as the destructor func.
	 * So entries must always be removed from fname_id_hash *first*. */
	GHashTable *id_fname_hash; /* id ~> folder name */
	GHashTable *fname_id_hash; /* folder name ~> id */
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelO365StoreSummary, camel_o365_store_summary, G_TYPE_OBJECT)

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
	g_hash_table_destroy (store_summary->priv->id_fname_hash);
	g_hash_table_destroy (store_summary->priv->fname_id_hash);
	g_key_file_free (store_summary->priv->key_file);
	g_free (store_summary->priv->path);

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
	store_summary->priv->id_fname_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	store_summary->priv->fname_id_hash = g_hash_table_new (g_str_hash, g_str_equal); /* shared data with 'id_fname_hash' */

	g_rec_mutex_init (&store_summary->priv->property_lock);
}

CamelO365StoreSummary *
camel_o365_store_summary_new (const gchar *path)
{
	CamelO365StoreSummary *store_summary;
	GError *error = NULL;
	GFile *file;

	g_return_val_if_fail (path != NULL, NULL);

	file = g_file_new_for_path (path);

	store_summary = g_object_new (CAMEL_TYPE_O365_STORE_SUMMARY, NULL);
	store_summary->priv->path = g_strdup (path);
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

	success = g_key_file_load_from_file (store_summary->priv->key_file, store_summary->priv->path, G_KEY_FILE_NONE, &local_error);

	if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
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
	}

	UNLOCK (store_summary);

	return success;
}

gboolean
camel_o365_store_summary_save (CamelO365StoreSummary *store_summary,
			       GError **error)
{
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary), FALSE);

	LOCK (store_summary);

	if (store_summary->priv->dirty) {
		success = g_key_file_save_to_file (store_summary->priv->key_file, store_summary->priv->path, error);

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

	store_summary->priv->dirty = g_hash_table_size (store_summary->priv->id_fname_hash) > 0;

	g_key_file_free (store_summary->priv->key_file);
	store_summary->priv->key_file = g_key_file_new ();

	g_hash_table_remove_all (store_summary->priv->fname_id_hash);
	g_hash_table_remove_all (store_summary->priv->id_fname_hash);

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

void
camel_o365_store_summary_set_folder (CamelO365StoreSummary *store_summary,
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
	gboolean changed;

	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);
	g_return_if_fail (display_name != NULL);

	LOCK (store_summary);

	camel_o365_store_summary_set_folder_display_name (store_summary, id, display_name);
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

	if (changed)
		store_summary->priv->dirty = TRUE;

	UNLOCK (store_summary);
}

gboolean
camel_o365_store_summary_get_folder (CamelO365StoreSummary *store_summary,
				     const gchar *id,
				     gchar **out_parent_id,
				     gchar **out_display_name,
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

	if (!camel_o365_store_summary_get_folder (store_summary, id, &value, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
		value = NULL;

	return value;
}

void
camel_o365_store_summary_set_folder_display_name (CamelO365StoreSummary *store_summary,
						  const gchar *id,
						  const gchar *display_name)
{
	const gchar *current_display_name;

	g_return_if_fail (CAMEL_IS_O365_STORE_SUMMARY (store_summary));
	g_return_if_fail (id != NULL);
	g_return_if_fail (display_name != NULL);

	LOCK (store_summary);

	current_display_name = g_hash_table_lookup (store_summary->priv->id_fname_hash, id);

	if (g_strcmp0 (current_display_name, display_name) != 0) {
		g_key_file_set_string (store_summary->priv->key_file, id, "DisplayName", display_name);
		store_summary->priv->dirty = TRUE;

		// TODO: update hashes on display name change			
	}

	UNLOCK (store_summary);
}

gchar *
camel_o365_store_summary_dup_folder_display_name (CamelO365StoreSummary *store_summary,
						  const gchar *id)
{
	gchar *value = NULL;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, &value, NULL, NULL, NULL, NULL, NULL, NULL))
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

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, &value, NULL, NULL, NULL, NULL, NULL))
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

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, &value, NULL, NULL, NULL, NULL))
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

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, &value, NULL, NULL, NULL))
		value = 0;

	return value;
}

EO365FolderKind
camel_o365_store_summary_get_folder_kind (CamelO365StoreSummary *store_summary,
					  const gchar *id)
{
	EO365FolderKind value = E_O365_FOLDER_KIND_UNKNOWN;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, NULL, &value, NULL, NULL))
		value = E_O365_FOLDER_KIND_UNKNOWN;

	return value;
}

gboolean
camel_o365_store_summary_get_folder_is_foreign (CamelO365StoreSummary *store_summary,
						const gchar *id)
{
	gboolean value = FALSE;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, NULL, NULL, &value, NULL))
		value = FALSE;

	return value;
}

gboolean
camel_o365_store_summary_get_folder_is_public (CamelO365StoreSummary *store_summary,
					       const gchar *id)
{
	gboolean value = FALSE;

	if (!camel_o365_store_summary_get_folder (store_summary, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &value))
		value = FALSE;

	return value;
}
