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

#define LOCK_CACHE(_folder) g_rec_mutex_lock (&_folder->priv->cache_lock)
#define UNLOCK_CACHE(_folder) g_rec_mutex_unlock (&_folder->priv->cache_lock)

#define LOCK_SEARCH(_folder) g_mutex_lock (&_folder->priv->search_lock)
#define UNLOCK_SEARCH(_folder) g_mutex_unlock (&_folder->priv->search_lock)

struct _CamelO365FolderPrivate {
	GRecMutex cache_lock;
	CamelDataCache *cache;

	GMutex search_lock;
	CamelFolderSearch *search;
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

static gchar *
o365_folder_get_filename (CamelFolder *folder,
			  const gchar *uid,
			  GError **error)
{
	CamelO365Folder *o365_folder = CAMEL_O365_FOLDER (folder);

	return o365_folder_cache_dup_filename (o365_folder, uid);
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
	folder_class->get_message_sync = o365_folder_get_message_sync;
	folder_class->refresh_info_sync = o365_folder_refresh_info_sync;
	folder_class->synchronize_sync = o365_folder_synchronize_sync;
	folder_class->expunge_sync = o365_folder_expunge_sync;
	folder_class->transfer_messages_to_sync = o365_folder_transfer_messages_to_sync;
	folder_class->prepare_content_refresh = o365_folder_prepare_content_refresh;
#endif
	folder_class->get_filename = o365_folder_get_filename;
}

static void
camel_o365_folder_init (CamelO365Folder *o365_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (o365_folder);

	o365_folder->priv = camel_o365_folder_get_instance_private (o365_folder);

	g_rec_mutex_init (&o365_folder->priv->cache_lock);
	g_mutex_init (&o365_folder->priv->search_lock);

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
