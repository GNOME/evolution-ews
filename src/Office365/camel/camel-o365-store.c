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

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <e-util/e-util.h>

#include "common/camel-o365-settings.h"
#include "common/e-o365-connection.h"
#include "camel-o365-folder.h"
#include "camel-o365-store-summary.h"
#include "camel-o365-utils.h"

#include "camel-o365-store.h"

#define LOCK(_store) g_rec_mutex_lock (&(_store->priv->property_lock))
#define UNLOCK(_store) g_rec_mutex_unlock (&(_store->priv->property_lock))

struct _CamelO365StorePrivate {
	GRecMutex property_lock;
	gchar *storage_path;
	CamelO365StoreSummary *summary;
	EO365Connection *cnc;
	GHashTable *default_folders;
};

static void camel_o365_store_initable_init (GInitableIface *iface);
static void camel_o365_subscribable_init (CamelSubscribableInterface *iface);
static GInitableIface *parent_initable_interface;

enum {
	PROP_0,
	PROP_CONNECTABLE,
	PROP_HOST_REACHABLE
};

G_DEFINE_TYPE_WITH_CODE (CamelO365Store, camel_o365_store, CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, camel_o365_store_initable_init)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_NETWORK_SERVICE, NULL)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_SUBSCRIBABLE, camel_o365_subscribable_init)
	G_ADD_PRIVATE (CamelO365Store))

static gboolean
o365_store_construct (CamelService *service,
		      CamelSession *session,
		      CamelProvider *provider,
		      GError **error)
{
	CamelO365Store *o365_store;
	gchar *summary_file, *session_storage_path;
	guint32 store_flags;
	GError *local_error = NULL;

	o365_store = (CamelO365Store *) service;

	store_flags = camel_store_get_flags (CAMEL_STORE (o365_store));

	/* Disable virtual trash and junk folders. Office365 has real folders for that */
	store_flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
	store_flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	camel_store_set_flags (CAMEL_STORE (o365_store), store_flags);

	session_storage_path = g_strdup (camel_service_get_user_cache_dir (service));

	if (!session_storage_path) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Session has no storage path"));
		return FALSE;
	}

	o365_store->priv->storage_path = session_storage_path;

	g_mkdir_with_parents (o365_store->priv->storage_path, 0700);

	summary_file = g_build_filename (o365_store->priv->storage_path, "folder-tree", NULL);
	o365_store->priv->summary = camel_o365_store_summary_new (summary_file);

	if (!camel_o365_store_summary_load (o365_store->priv->summary, &local_error))
		g_warning ("%s: Failed to load store summary '%s': %s", G_STRFUNC, summary_file, local_error ? local_error->message : "Unknown error");

	g_clear_error (&local_error);
	g_free (summary_file);

	return TRUE;
}

static gboolean
o365_store_initable_init (GInitable *initable,
			  GCancellable *cancellable,
			  GError **error)
{
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	gboolean ret;

	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);

	camel_store_set_flags (store, camel_store_get_flags (store) |
		CAMEL_STORE_USE_CACHE_DIR |
		CAMEL_STORE_SUPPORTS_INITIAL_SETUP |
		CAMEL_STORE_CAN_DELETE_FOLDERS_AT_ONCE);

	/* Chain up to parent interface's method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	session = camel_service_ref_session (service);

	ret = o365_store_construct (service, session, NULL, error);

	g_object_unref (session);

	return ret;
}

static GList *
o365_store_query_auth_types_sync (CamelService *service,
				  GCancellable *cancellable,
				  GError **error)
{
	g_return_val_if_fail (CAMEL_IS_O365_STORE (service), NULL);

	return NULL;
}

static gchar *
o365_store_get_name (CamelService *service,
		     gboolean brief)
{
	gchar *name;

	if (brief)
		name = g_strdup (_("Office 365 server"));
	else
		name = g_strdup (_("Mail receive via Microsoft Office 365"));

	return name;
}

static gboolean
o365_store_read_default_folders (CamelO365Store *o365_store,
				 EO365Connection *cnc,
				 GCancellable *cancellable,
				 GError **error)
{
	struct _default_folders {
		const gchar *name;
		guint32 flags;
	} default_folders[] = {
		{ "archive",		CAMEL_FOLDER_TYPE_ARCHIVE },
		{ "deleteditems",	CAMEL_FOLDER_TYPE_TRASH },
		{ "drafts",		CAMEL_FOLDER_TYPE_DRAFTS },
		{ "inbox",		CAMEL_FOLDER_TYPE_INBOX },
		{ "junkemail",		CAMEL_FOLDER_TYPE_JUNK },
		{ "outbox",		CAMEL_FOLDER_TYPE_OUTBOX },
		{ "sentitems",		CAMEL_FOLDER_TYPE_SENT }
	};
	GPtrArray *requests;
	gboolean success;
	guint ii;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (o365_store), FALSE);
	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), FALSE);

	LOCK (o365_store);

	if (g_hash_table_size (o365_store->priv->default_folders)) {
		UNLOCK (o365_store);
		return TRUE;
	}

	UNLOCK (o365_store);

	requests = g_ptr_array_new_full (G_N_ELEMENTS (default_folders), g_object_unref);

	for (ii = 0; ii < G_N_ELEMENTS (default_folders); ii++) {
		SoupMessage *message;
		gchar *uri;

		uri = e_o365_connection_construct_uri (cnc, TRUE, NULL, E_O365_API_V1_0, NULL,
			"mailFolders",
			NULL,
			default_folders[ii].name,
			"$select", "id",
			NULL);

		message = soup_message_new (SOUP_METHOD_GET, uri);

		if (!message) {
			g_set_error (error, SOUP_HTTP_ERROR, SOUP_STATUS_MALFORMED, _("Malformed URI: “%s”"), uri);

			g_ptr_array_unref (requests);
			g_free (uri);

			return FALSE;
		}

		g_free (uri);

		g_ptr_array_add (requests, message);
	}

	success = e_o365_connection_batch_request_sync (cnc, E_O365_API_V1_0, requests, cancellable, error);

	if (success) {
		g_warn_if_fail (requests->len == G_N_ELEMENTS (default_folders));

		LOCK (o365_store);

		for (ii = 0; ii < requests->len; ii++) {
			SoupMessage *message = g_ptr_array_index (requests, ii);
			JsonNode *node = NULL;

			if (message->status_code > 0 && SOUP_STATUS_IS_SUCCESSFUL (message->status_code) &&
			    e_o365_connection_json_node_from_message (message, NULL, &node, cancellable, NULL) &&
			    node && JSON_NODE_HOLDS_OBJECT (node)) {
				JsonObject *object = json_node_get_object (node);

				if (object) {
					const gchar *id;

					id = e_o365_json_get_string_member (object, "id", NULL);

					if (id && *id) {
						g_hash_table_insert (o365_store->priv->default_folders, g_strdup (id),
							GUINT_TO_POINTER (default_folders[ii].flags));
					}
				}
			}

			if (node)
				json_node_unref (node);
		}

		UNLOCK (o365_store);
	}

	g_ptr_array_unref (requests);

	return success;
}

static gboolean
o365_store_equal_label_tag_cb (gconstpointer ptr1,
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
o365_store_find_in_ptr_array (GPtrArray *haystack,
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
o365_store_save_category_changes (GHashTable *old_categories, /* gchar *id ~> CamelO365Category * */
				  GHashTable *new_categories) /* gchar *id ~> CamelO365Category * */
{
	GHashTableIter iter;
	GSettings *settings;
	GPtrArray *evo_labels; /* gchar * (encoded label definition) */
	gchar **strv;
	gint ii;
	gpointer value;
	gboolean changed = FALSE;

	if (!old_categories || !new_categories)
		return new_categories != NULL;

	evo_labels = g_ptr_array_new_full (5, g_free);

	settings = e_util_ref_settings ("org.gnome.evolution.mail");
	strv = g_settings_get_strv (settings, "labels");

	for (ii = 0; strv && strv[ii]; ii++) {
		g_ptr_array_add (evo_labels, g_strdup (strv[ii]));
	}

	g_strfreev (strv);

	g_hash_table_iter_init (&iter, new_categories);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		CamelO365Category *new_cat = value, *old_cat;
		gchar *tag = NULL;

		if (!new_cat)
			continue;

		old_cat = g_hash_table_lookup (old_categories, new_cat->id);
		if (old_cat) {
			if (g_strcmp0 (old_cat->display_name, new_cat->display_name) != 0 ||
			    g_strcmp0 (old_cat->color, new_cat->color) != 0) {
				/* Old category changed name or color */
				tag = camel_o365_utils_encode_category_name (new_cat->display_name);
			}
		} else {
			/* This is a new category */
			tag = camel_o365_utils_encode_category_name (new_cat->display_name);
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

			if (old_cat && g_strcmp0 (old_cat->display_name, new_cat->display_name) != 0) {
				gchar *old_tag = camel_o365_utils_encode_category_name (old_cat->display_name);

				if (old_tag && *old_tag) {
					if (!o365_store_find_in_ptr_array (evo_labels, old_tag, o365_store_equal_label_tag_cb, &index))
						index = (guint) -1;
				}

				g_free (old_tag);
			}

			for (ii = 0; new_cat->display_name[ii]; ii++) {
				if (new_cat->display_name[ii] == '|')
					new_cat->display_name[ii] = '-';
			}

			if (index == (guint) -1 &&
			    !o365_store_find_in_ptr_array (evo_labels, tag, o365_store_equal_label_tag_cb, &index))
				index = (guint) -1;

			label_def = g_strconcat (new_cat->display_name, "|", new_cat->color ? new_cat->color : "#FF0000", "|", tag, NULL);

			if (index == (guint) -1 || index >= (gint) evo_labels->len) {
				g_ptr_array_add (evo_labels, label_def);
			} else {
				g_free (evo_labels->pdata[index]);
				evo_labels->pdata[index] = label_def;
			}
		}

		g_hash_table_remove (old_categories, new_cat->id);

		g_free (tag);
	}

	if (g_hash_table_size (old_categories) > 0) {
		/* Some categories had been removed */
		changed = TRUE;

		g_hash_table_iter_init (&iter, old_categories);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			CamelO365Category *old_cat = value;
			gchar *old_tag;
			guint index;

			if (!old_cat)
				continue;

			old_tag = camel_o365_utils_encode_category_name (old_cat->display_name);

			for (ii = 0; old_tag && old_tag[ii]; ii++) {
				if (old_tag[ii] == '|')
					old_tag[ii] = '-';
			}

			if (old_tag &&
			    o365_store_find_in_ptr_array (evo_labels, old_tag, o365_store_equal_label_tag_cb, &index))
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

static void
o365_store_get_categories_cb (CamelSession *session,
			      GCancellable *cancellable,
			      gpointer user_data,
			      GError **error)
{
	CamelO365Store *o365_store = user_data;
	EO365Connection *cnc;
	GSList *categories = NULL;

	g_return_if_fail (CAMEL_IS_O365_STORE (o365_store));

	cnc = camel_o365_store_ref_connection (o365_store);

	if (!cnc)
		return;

	if (e_o365_connection_get_categories_sync (cnc, NULL, &categories, cancellable, error)) {
		GHashTable *old_categories, *new_categories;
		GSList *link;

		new_categories = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, camel_o365_category_free);

		for (link = categories; link; link = g_slist_next (link)) {
			EO365Category *category = link->data;
			CamelO365Category *cat;
			const gchar *id, *display_name, *color;

			if (!category)
				continue;

			id = e_o365_category_get_id (category);
			display_name = e_o365_category_get_display_name (category);
			color = e_o365_category_get_color (category);

			if (!id || !display_name)
				continue;

			if (display_name != camel_o365_utils_rename_label (display_name, TRUE))
				continue;

			cat = camel_o365_category_new (id, display_name, color);

			if (cat)
				g_hash_table_insert (new_categories, cat->id, cat);
		}

		g_slist_free_full (categories, (GDestroyNotify) json_object_unref);

		old_categories = camel_o365_store_summary_get_categories (o365_store->priv->summary);

		if (o365_store_save_category_changes (old_categories, new_categories)) {
			camel_o365_store_summary_set_categories (o365_store->priv->summary, new_categories);
			camel_o365_store_summary_save (o365_store->priv->summary, NULL);
		}

		g_hash_table_destroy (new_categories);
		g_hash_table_destroy (old_categories);
	}

	g_object_unref (cnc);
}

static gboolean
o365_store_connect_sync (CamelService *service,
			 GCancellable *cancellable,
			 GError **error)
{
	CamelO365Store *o365_store;
	EO365Connection *cnc;
	gboolean success = FALSE;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_o365_store_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	o365_store = CAMEL_O365_STORE (service);
	cnc = camel_o365_store_ref_connection (o365_store);

	if (!cnc) {
		cnc = camel_o365_utils_new_connection (service, NULL);

		if (cnc) {
			LOCK (o365_store);

			o365_store->priv->cnc = g_object_ref (cnc);

			UNLOCK (o365_store);
		}
	}

	if (cnc) {
		CamelSession *session;

		session = camel_service_ref_session (service);

		success = camel_session_authenticate_sync (session, service, "Office365", cancellable, error);

		if (success) {
			camel_session_submit_job (
				session, _("Look up Office 365 categories"),
				o365_store_get_categories_cb,
				g_object_ref (o365_store),
				g_object_unref);
		}

		g_clear_object (&session);
		g_clear_object (&cnc);
	} else {
		g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE, _("Failed to create connection"));
	}

	return success;
}

static gboolean
o365_store_disconnect_sync (CamelService *service,
			    gboolean clean,
			    GCancellable *cancellable,
			    GError **error)
{
	CamelO365Store *o365_store = CAMEL_O365_STORE (service);
	EO365Connection *cnc;
	gboolean success = TRUE;

	cnc = camel_o365_store_ref_connection (o365_store);

	if (cnc) {
		success = e_o365_connection_disconnect_sync (cnc, cancellable, error);

		g_clear_object (&cnc);
	}

	if (!success)
		return FALSE;

	/* Chain up to parent's method. */
	return CAMEL_SERVICE_CLASS (camel_o365_store_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
o365_store_authenticate_sync (CamelService *service,
			      const gchar *mechanism,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelAuthenticationResult result;
	CamelO365Store *o365_store;
	EO365Connection *cnc;

	o365_store = CAMEL_O365_STORE (service);
	cnc = camel_o365_store_ref_connection (o365_store);

	if (!cnc)
		return CAMEL_AUTHENTICATION_ERROR;

	switch (e_o365_connection_authenticate_sync (cnc, cancellable, error)) {
	case E_SOURCE_AUTHENTICATION_ERROR:
	case E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED:
	default:
		result = CAMEL_AUTHENTICATION_ERROR;
		break;
	case E_SOURCE_AUTHENTICATION_ACCEPTED:
		result = CAMEL_AUTHENTICATION_ACCEPTED;

		o365_store_read_default_folders (o365_store, cnc, cancellable, NULL);
		break;
	case E_SOURCE_AUTHENTICATION_REJECTED:
	case E_SOURCE_AUTHENTICATION_REQUIRED:
		result = CAMEL_AUTHENTICATION_REJECTED;
		break;
	}

	g_clear_object (&cnc);

	return result;
}

static CamelFolder *
o365_store_get_folder_sync (CamelStore *store,
			    const gchar *folder_name,
			    guint32 flags,
			    GCancellable *cancellable,
			    GError **error)
{
	CamelO365Store *o365_store;
	CamelFolder *folder = NULL;
	gchar *fid, *folder_dir, *display_name;

	o365_store = CAMEL_O365_STORE (store);

	fid = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store->priv->summary, folder_name);

	if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return NULL;
	}

	display_name = camel_o365_store_summary_dup_folder_display_name (o365_store->priv->summary, fid);
	folder_dir = g_build_filename (o365_store->priv->storage_path, "folders", folder_name, NULL);

	folder = camel_o365_folder_new (store, display_name, folder_name, folder_dir, cancellable, error);

	g_free (display_name);
	g_free (folder_dir);
	g_free (fid);

	if (folder && (flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0)
		camel_folder_prepare_content_refresh (folder);

	return folder;
}

static void
o365_store_save_summary (CamelO365StoreSummary *summary,
			 const gchar *where)
{
	GError *error = NULL;

	if (!camel_o365_store_summary_save (summary, &error))
		g_warning ("%s: Failed to save store summary: %s", where, error ? error->message : "Unknown error");

	g_clear_error (&error);
}

static CamelFolderInfo *
o365_store_create_folder_sync (CamelStore *store,
			       const gchar *parent_name,
			       const gchar *folder_name,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelO365Store *o365_store;
	EO365MailFolder *mail_folder = NULL;
	gchar *fid = NULL;
	gchar *full_name;
	EO365Connection *cnc;
	CamelFolderInfo *fi = NULL;
	guint32 flags;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), NULL);

	o365_store = CAMEL_O365_STORE (store);

	if (parent_name && *parent_name)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	fid = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store->priv->summary, full_name);

	if (fid) {
		g_free (fid);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot create folder “%s”, folder already exists"),
			full_name);
		g_free (full_name);
		return NULL;
	}

	g_free (full_name);

	/* Get Parent folder ID */
	if (parent_name && parent_name[0]) {
		fid = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store->priv->summary, parent_name);

		if (!fid) {
			g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Parent folder “%s” does not exist"),
				parent_name);
			return NULL;
		}
	}

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error)) {
		g_free (fid);

		return NULL;
	}

	success = e_o365_connection_create_mail_folder_sync (cnc, NULL, fid, folder_name, &mail_folder, cancellable, &local_error);

	g_object_unref (cnc);
	g_free (fid);

	if (!success) {
		camel_o365_store_maybe_disconnect (o365_store, local_error);
		g_propagate_error (error, local_error);

		return NULL;
	}

	flags = e_o365_mail_folder_get_child_folder_count (mail_folder) ? CAMEL_STORE_INFO_FOLDER_CHILDREN : CAMEL_STORE_INFO_FOLDER_NOCHILDREN;

	camel_o365_store_summary_set_folder (o365_store->priv->summary, TRUE,
		e_o365_mail_folder_get_id (mail_folder),
		e_o365_mail_folder_get_parent_folder_id (mail_folder),
		e_o365_mail_folder_get_display_name (mail_folder),
		e_o365_mail_folder_get_total_item_count (mail_folder),
		e_o365_mail_folder_get_unread_item_count (mail_folder),
		flags, E_O365_FOLDER_KIND_MAIL, FALSE, FALSE);

	fi = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, e_o365_mail_folder_get_id (mail_folder));

	camel_store_folder_created (store, fi);
	camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (o365_store), fi);

	json_object_unref (mail_folder);

	o365_store_save_summary (o365_store->priv->summary, G_STRFUNC);

	return fi;
}

static void
o365_store_notify_created_recursive (CamelStore *store,
				     CamelFolderInfo *folder_info)
{
	while (folder_info) {
		camel_store_folder_created (store, folder_info);
		camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (store), folder_info);

		if (folder_info->child)
			o365_store_notify_created_recursive (store, folder_info->child);

		folder_info = folder_info->next;
	}
}

static gboolean
o365_store_move_mail_folder (CamelO365Store *o365_store,
			     EO365Connection *cnc,
			     const gchar *folder_id,
			     const gchar *des_folder_id,
			     GCancellable *cancellable,
			     GError **error)
{
	EO365MailFolder *moved_mail_folder = NULL;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (o365_store), FALSE);
	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (des_folder_id != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (folder_id, des_folder_id) != 0, FALSE);

	success = e_o365_connection_copy_move_mail_folder_sync (cnc, NULL, folder_id, des_folder_id, FALSE, &moved_mail_folder, cancellable, error);

	if (success && moved_mail_folder) {
		CamelFolderInfo *fi;
		gchar *new_full_name;

		fi = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, folder_id);

		camel_o365_store_summary_set_folder_parent_id (o365_store->priv->summary, folder_id, e_o365_mail_folder_get_parent_folder_id (moved_mail_folder));
		camel_o365_store_summary_rebuild_hashes (o365_store->priv->summary);

		camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (o365_store), fi);
		camel_store_folder_deleted (CAMEL_STORE (o365_store), fi);

		camel_folder_info_free (fi);

		new_full_name = camel_o365_store_summary_dup_folder_full_name (o365_store->priv->summary, folder_id);
		g_warn_if_fail (new_full_name != NULL);

		fi = camel_o365_store_summary_build_folder_info (o365_store->priv->summary, new_full_name, TRUE);

		o365_store_notify_created_recursive (CAMEL_STORE (o365_store), fi);

		json_object_unref (moved_mail_folder);
		camel_folder_info_free (fi);
		g_free (new_full_name);
	}

	return success;
}

static void
o365_store_delete_folders_from_summary_recursive (CamelO365Store *o365_store,
						  CamelFolderInfo *fi,
						  gboolean send_signals)
{
	CamelStore *store = send_signals ? CAMEL_STORE (o365_store) : NULL;
	CamelSubscribable *subscribable = send_signals ? CAMEL_SUBSCRIBABLE (o365_store) : NULL;

	while (fi) {
		gchar *folder_id;

		if (fi->child)
			o365_store_delete_folders_from_summary_recursive (o365_store, fi->child, send_signals);

		folder_id = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store->priv->summary, fi->full_name);
		if (folder_id) {
			camel_o365_store_summary_remove_folder (o365_store->priv->summary, folder_id);
			g_free (folder_id);
		}

		if (send_signals) {
			camel_subscribable_folder_unsubscribed (subscribable, fi);
			camel_store_folder_deleted (store, fi);
		}

		fi = fi->next;
	}
}

static gboolean
o365_store_delete_folder_sync (CamelStore *store,
			       const gchar *folder_name,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelO365Store *o365_store;
	CamelFolderInfo *folder_info;
	EO365Connection *cnc = NULL;
	gchar *folder_id;
	gchar *trash_folder_id;
	gchar *trash_full_name;
	gboolean success;
	gboolean is_under_trash_folder, claim_unsubscribe = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), FALSE);

	o365_store = CAMEL_O365_STORE (store);

	folder_info = camel_store_get_folder_info_sync (store, folder_name,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
		cancellable, &local_error);

	if (!folder_info) {
		if (local_error)
			g_propagate_error (error, local_error);

		return FALSE;
	}

	folder_id = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store->priv->summary, folder_name);

	if (!folder_id) {
		camel_folder_info_free (folder_info);

		g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Folder does not exist"));

		return FALSE;
	}

	trash_folder_id = camel_o365_store_summary_dup_folder_id_for_type (o365_store->priv->summary, CAMEL_FOLDER_TYPE_TRASH);
	trash_full_name = camel_o365_store_summary_dup_folder_full_name (o365_store->priv->summary, trash_folder_id);

	if (!trash_full_name) {
		camel_folder_info_free (folder_info);
		g_free (trash_folder_id);
		g_free (folder_id);

		g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Cannot find “Deleted Items” folder"));

		return FALSE;
	}

	is_under_trash_folder = g_str_has_prefix (folder_name, trash_full_name);

	if (is_under_trash_folder) {
		gint len = strlen (trash_full_name);

		is_under_trash_folder = len > 0 && (trash_full_name[len - 1] == '/' || folder_name[len] == '/');
	}

	g_free (trash_full_name);

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error)) {
		camel_folder_info_free (folder_info);
		g_free (trash_folder_id);
		g_free (folder_id);

		return FALSE;
	}

	if (camel_o365_store_summary_get_folder_is_foreign (o365_store->priv->summary, folder_id) ||
	    camel_o365_store_summary_get_folder_is_public (o365_store->priv->summary, folder_id)) {
		/* do not delete foreign or public folders,
		 * only remove them from the local cache */
		success = TRUE;
	} else if (is_under_trash_folder) {
		success = e_o365_connection_delete_mail_folder_sync (cnc, NULL, folder_id, cancellable, &local_error);
	} else {
		success = o365_store_move_mail_folder (o365_store, cnc, folder_id, "deleteditems", cancellable, &local_error);
		claim_unsubscribe = FALSE;
	}

	g_clear_object (&cnc);

	if (!success) {
		camel_folder_info_free (folder_info);
		g_free (trash_folder_id);
		g_free (folder_id);

		camel_o365_store_maybe_disconnect (o365_store, local_error);
		g_propagate_error (error, local_error);

		return FALSE;
	}

	if (is_under_trash_folder)
		o365_store_delete_folders_from_summary_recursive (o365_store, folder_info, FALSE);

	if (claim_unsubscribe) {
		camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (o365_store), folder_info);
		camel_store_folder_deleted (store, folder_info);
	}

	camel_folder_info_free (folder_info);

	o365_store_save_summary (o365_store->priv->summary, G_STRFUNC);

	g_free (trash_folder_id);
	g_free (folder_id);

	return TRUE;
}

static gboolean
o365_store_rename_folder_sync (CamelStore *store,
			       const gchar *old_name,
			       const gchar *new_name,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelO365Store *o365_store;
	EO365Connection *cnc;
	const gchar *old_slash, *new_slash;
	gint parent_len;
	gchar *folder_id;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), FALSE);

	if (!g_strcmp0 (old_name, new_name))
		return TRUE;

	o365_store = CAMEL_O365_STORE (store);
	folder_id = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store->priv->summary, old_name);

	if (!folder_id) {
		g_set_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
			_("Folder “%s” does not exist"), old_name);

		return FALSE;
	}

	if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error)) {
		g_free (folder_id);
		return FALSE;
	}

	old_slash = g_strrstr (old_name, "/");
	new_slash = g_strrstr (new_name, "/");

	if (old_slash)
		old_slash++;
	else
		old_slash = old_name;

	if (new_slash)
		new_slash++;
	else
		new_slash = new_name;

	parent_len = old_slash - old_name;

	/* First move the folder, if needed */
	if (new_slash - new_name != parent_len ||
	    strncmp (old_name, new_name, parent_len)) {
		gchar *new_parent_id;

		if (new_slash - new_name > 0) {
			gchar *new_parent;

			new_parent = g_strndup (new_name, new_slash - new_name - 1);
			new_parent_id = camel_o365_store_summary_dup_folder_id_for_full_name (o365_store->priv->summary, new_parent);

			if (!new_parent_id) {
				g_set_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
					_("Folder “%s” does not exist"), new_parent);

				g_free (new_parent);
				g_free (folder_id);

				return FALSE;
			}

			g_free (new_parent);
		} else {
			new_parent_id = NULL;
		}

		success = o365_store_move_mail_folder (o365_store, cnc, folder_id, new_parent_id ? new_parent_id : "msgfolderroot", cancellable, &local_error);

		g_free (new_parent_id);
	}

	/* Then rename the folder, if needed */
	if (success && g_strcmp0 (old_slash, new_slash) != 0) {
		EO365MailFolder *mail_folder = NULL;

		success = e_o365_connection_rename_mail_folder_sync (cnc, NULL, folder_id, new_slash, &mail_folder, cancellable, &local_error);

		if (mail_folder) {
			camel_o365_store_summary_set_folder_display_name (o365_store->priv->summary, folder_id,
				e_o365_mail_folder_get_display_name (mail_folder), TRUE);

			json_object_unref (mail_folder);
		}
	}

	if (success) {
		CamelFolderInfo *fi;

		fi = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, folder_id);

		if (fi) {
			camel_store_folder_renamed (store, old_name, fi);
			camel_folder_info_free (fi);
		}
	}

	o365_store_save_summary (o365_store->priv->summary, G_STRFUNC);

	if (!success && local_error) {
		camel_o365_store_maybe_disconnect (o365_store, local_error);
		g_propagate_error (error, local_error);
	}

	g_free (folder_id);

	return success;
}

typedef struct _FolderRenamedData {
	gchar *id;
	gchar *old_name;
} FolderRenamedData;

static FolderRenamedData *
folder_renamed_data_new (gchar *id,
			 gchar *old_name)
{
	FolderRenamedData *frd;

	frd = g_slice_new (FolderRenamedData);
	frd->id = id;
	frd->old_name = old_name;

	return frd;
}

static void
folder_renamed_data_free (gpointer ptr)
{
	FolderRenamedData *frd = ptr;

	if (frd) {
		g_free (frd->id);
		g_free (frd->old_name);
		g_slice_free (FolderRenamedData, frd);
	}
}

typedef struct _FoldersDeltaData {
	CamelO365Store *o365_store;
	GSList *added_ids; /* gchar *, folder ids */
	GSList *renamed_data; /* FolderRenamedData * */
	GSList *removed_fis; /* CamelFolderInfo * */
} FoldersDeltaData;

static gboolean
camel_o365_got_folders_delta_cb (EO365Connection *cnc,
				 const GSList *results, /* JsonObject * - the returned objects from the server */
				 gpointer user_data, /* expects GSList **, aka pointer to a GSList *, where it copies the 'results' */
				 GCancellable *cancellable,
				 GError **error)
{
	FoldersDeltaData *fdd = user_data;
	GSList *link;

	g_return_val_if_fail (fdd, FALSE);

	LOCK (fdd->o365_store);

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		JsonObject *object = link->data;
		const gchar *id = e_o365_mail_folder_get_id (object);

		if (e_o365_delta_is_removed_object (object)) {
			CamelFolderInfo *info;

			info = camel_o365_store_summary_build_folder_info_for_id (fdd->o365_store->priv->summary, id);

			if (info)
				fdd->removed_fis = g_slist_prepend (fdd->removed_fis, info);

			camel_o365_store_summary_remove_folder (fdd->o365_store->priv->summary, id);
		} else {
			gchar *old_full_name = NULL;
			guint32 flags;

			if (camel_o365_store_summary_has_folder (fdd->o365_store->priv->summary, id))
				old_full_name = camel_o365_store_summary_dup_folder_full_name (fdd->o365_store->priv->summary, id);

			flags = e_o365_mail_folder_get_child_folder_count (object) ? CAMEL_STORE_INFO_FOLDER_CHILDREN : CAMEL_STORE_INFO_FOLDER_NOCHILDREN;

			flags |= GPOINTER_TO_UINT (g_hash_table_lookup (fdd->o365_store->priv->default_folders, id));

			camel_o365_store_summary_set_folder (fdd->o365_store->priv->summary, FALSE, id,
				e_o365_mail_folder_get_parent_folder_id (object),
				e_o365_mail_folder_get_display_name (object),
				e_o365_mail_folder_get_total_item_count (object),
				e_o365_mail_folder_get_unread_item_count (object),
				flags, E_O365_FOLDER_KIND_MAIL, FALSE, FALSE);

			if (old_full_name)
				fdd->renamed_data = g_slist_prepend (fdd->renamed_data, folder_renamed_data_new (g_strdup (id), old_full_name));
			else
				fdd->added_ids = g_slist_prepend (fdd->added_ids, g_strdup (id));
		}
	}

	UNLOCK (fdd->o365_store);

	return TRUE;
}

static void
o365_store_forget_all_folders (CamelO365Store *o365_store)
{
	CamelStore *store;
	CamelSubscribable *subscribable;
	GSList *ids, *link;

	g_return_if_fail (CAMEL_IS_O365_STORE (o365_store));

	store = CAMEL_STORE (o365_store);
	subscribable = CAMEL_SUBSCRIBABLE (o365_store);
	ids = camel_o365_store_summary_list_folder_ids (o365_store->priv->summary);

	if (!ids)
		return;

	for (link = ids; link; link = g_slist_next (link)) {
		const gchar *id = link->data;
		CamelFolderInfo *fi;

		fi = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, id);
		camel_subscribable_folder_unsubscribed (subscribable, fi);
		camel_store_folder_deleted (store, fi);
		camel_folder_info_free (fi);
	}

	g_slist_free_full (ids, g_free);

	camel_o365_store_summary_set_delta_link (o365_store->priv->summary, "");
	camel_o365_store_summary_clear (o365_store->priv->summary);
}

static CamelFolderInfo *
o365_store_get_folder_info_sync (CamelStore *store,
				 const gchar *top,
				 guint32 flags,
				 GCancellable *cancellable,
				 GError **error)
{
	CamelO365Store *o365_store;
	CamelFolderInfo *fi;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), NULL);

	o365_store = CAMEL_O365_STORE (store);

	if (camel_offline_store_get_online (CAMEL_OFFLINE_STORE (o365_store))) {
		gboolean refresh_online;

		refresh_online = !(flags & CAMEL_STORE_FOLDER_INFO_FAST) ||
				  (flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0;

		if (!refresh_online) {
			gchar *delta_link;

			LOCK (o365_store);

			delta_link = camel_o365_store_summary_dup_delta_link (o365_store->priv->summary);
			refresh_online = !delta_link || !*delta_link;
			g_free (delta_link);

			UNLOCK (o365_store);
		}

		if (refresh_online) {
			EO365Connection *cnc;

			cnc = camel_o365_store_ref_connection (o365_store);

			if (cnc) {
				FoldersDeltaData fdd;
				gchar *old_delta_link, *new_delta_link = NULL;
				GError *local_error = NULL;

				LOCK (o365_store);

				old_delta_link = camel_o365_store_summary_dup_delta_link (o365_store->priv->summary);

				UNLOCK (o365_store);

				fdd.o365_store = o365_store;
				fdd.added_ids = NULL;
				fdd.renamed_data = NULL;
				fdd.removed_fis = NULL;

				success = e_o365_connection_get_mail_folders_delta_sync (cnc, NULL, NULL, old_delta_link, 0,
					camel_o365_got_folders_delta_cb, &fdd, &new_delta_link, cancellable, &local_error);

				if (old_delta_link && *old_delta_link && g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
					g_clear_pointer (&old_delta_link, g_free);
					g_clear_error (&local_error);

					o365_store_forget_all_folders (o365_store);

					success = e_o365_connection_get_mail_folders_delta_sync (cnc, NULL, NULL, NULL, 0,
						camel_o365_got_folders_delta_cb, &fdd, &new_delta_link, cancellable, error);
				}

				if (local_error)
					g_propagate_error (error, local_error);

				if (success) {
					CamelSubscribable *subscribable = CAMEL_SUBSCRIBABLE (o365_store);
					CamelFolderInfo *info;
					GSList *link;

					LOCK (o365_store);

					camel_o365_store_summary_set_delta_link (o365_store->priv->summary, new_delta_link);
					o365_store_save_summary (o365_store->priv->summary, G_STRFUNC);

					fdd.added_ids = g_slist_reverse (fdd.added_ids);
					fdd.renamed_data = g_slist_reverse (fdd.renamed_data);
					fdd.removed_fis = g_slist_reverse (fdd.removed_fis);

					if (fdd.added_ids || fdd.renamed_data || fdd.removed_fis)
						camel_o365_store_summary_rebuild_hashes (o365_store->priv->summary);

					for (link = fdd.removed_fis; link; link = g_slist_next (link)) {
						info = link->data;

						camel_subscribable_folder_unsubscribed (subscribable, info);
						camel_store_folder_deleted (store, info);
					}

					for (link = fdd.added_ids; link; link = g_slist_next (link)) {
						const gchar *id = link->data;

						info = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, id);

						if (info) {
							camel_store_folder_created (store, info);
							camel_subscribable_folder_subscribed (subscribable, info);
							camel_folder_info_free (info);
						}
					}

					for (link = fdd.renamed_data; link; link = g_slist_next (link)) {
						const FolderRenamedData *frd = link->data;

						info = camel_o365_store_summary_build_folder_info_for_id (o365_store->priv->summary, frd->id);

						if (info) {
							camel_store_folder_renamed (store, frd->old_name, info);
							camel_folder_info_free (info);
						}
					}

					UNLOCK (o365_store);
				}

				g_slist_free_full (fdd.added_ids, g_free);
				g_slist_free_full (fdd.renamed_data, folder_renamed_data_free);
				g_slist_free_full (fdd.removed_fis, (GDestroyNotify) camel_folder_info_free);

				g_clear_object (&cnc);
				g_free (old_delta_link);
				g_free (new_delta_link);
			}
		}
	}

	if (success) {
		LOCK (o365_store);

		fi = camel_o365_store_summary_build_folder_info (o365_store->priv->summary, top, (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) != 0);

		UNLOCK (o365_store);
	} else {
		fi = NULL;
	}

	return fi;
}

/* Hold the property lock before calling this function */
static void
o365_store_save_setup_folder_locked (CamelO365Store *o365_store,
				     GHashTable *save_setup,
				     guint32 folder_type, /* one of TYPE constants from CamelFolderInfoFlags */
				     const gchar *property_name)
{
	gchar *folder_id;

	g_return_if_fail (CAMEL_IS_O365_STORE (o365_store));
	g_return_if_fail (save_setup != NULL);
	g_return_if_fail (folder_type != 0);
	g_return_if_fail (property_name != NULL);

	folder_id = camel_o365_store_summary_dup_folder_id_for_type (o365_store->priv->summary, folder_type);

	if (folder_id) {
		gchar *fullname;

		fullname = camel_o365_store_summary_dup_folder_full_name (o365_store->priv->summary, folder_id);

		if (fullname && *fullname) {
			g_hash_table_insert (save_setup,
				g_strdup (property_name),
				fullname);

			fullname = NULL;
		}

		g_free (fullname);
		g_free (folder_id);
	}
}

static gboolean
o365_store_initial_setup_with_connection_sync (CamelStore *store,
					       GHashTable *save_setup,
					       EO365Connection *cnc,
					       GCancellable *cancellable,
					       GError **error)
{
	CamelO365Store *o365_store;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	o365_store = CAMEL_O365_STORE (store);

	if (cnc) {
		g_object_ref (cnc);
	} else {
		if (!camel_o365_store_ensure_connected (o365_store, &cnc, cancellable, error))
			return FALSE;

		g_return_val_if_fail (cnc != NULL, FALSE);
	}

	if (!o365_store_read_default_folders (o365_store, cnc, cancellable, error)) {
		g_clear_object (&cnc);
		return FALSE;
	}

	if (save_setup) {
		LOCK (o365_store);

		o365_store_save_setup_folder_locked (o365_store, save_setup, CAMEL_FOLDER_TYPE_SENT, CAMEL_STORE_SETUP_SENT_FOLDER);
		o365_store_save_setup_folder_locked (o365_store, save_setup, CAMEL_FOLDER_TYPE_DRAFTS, CAMEL_STORE_SETUP_DRAFTS_FOLDER);
		o365_store_save_setup_folder_locked (o365_store, save_setup, CAMEL_FOLDER_TYPE_ARCHIVE, CAMEL_STORE_SETUP_ARCHIVE_FOLDER);

		UNLOCK (o365_store);
	}

	g_clear_object (&cnc);

	return TRUE;
}

static gboolean
o365_store_initial_setup_sync (CamelStore *store,
			       GHashTable *save_setup,
			       GCancellable *cancellable,
			       GError **error)
{
	return o365_store_initial_setup_with_connection_sync (store, save_setup, NULL, cancellable, error);
}

static CamelFolder *
o365_store_get_trash_folder_sync (CamelStore *store,
				  GCancellable *cancellable,
				  GError **error)
{
	CamelO365Store *o365_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), NULL);

	o365_store = CAMEL_O365_STORE (store);

	LOCK (o365_store);

	folder_id = camel_o365_store_summary_dup_folder_id_for_type (o365_store->priv->summary, CAMEL_FOLDER_TYPE_TRASH);

	if (!folder_id) {
		UNLOCK (o365_store);
		g_set_error_literal (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER, _("Could not locate Trash folder"));
		return NULL;
	}

	folder_name = camel_o365_store_summary_dup_folder_full_name (o365_store->priv->summary, folder_id);

	UNLOCK (o365_store);

	folder = camel_store_get_folder_sync (store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);

	if (folder) {
		GPtrArray *folders;
		gboolean can = TRUE;
		guint ii;

		/* Save content of all opened folders, thus any messages deleted in them
		   are moved to the Deleted Items folder first, thus in case of the trash
		   folder instance being used to expunge messages will contain all of them.
		*/
		folders = camel_store_dup_opened_folders (store);

		for (ii = 0; ii < folders->len; ii++) {
			CamelFolder *secfolder = folders->pdata[ii];

			if (secfolder != folder && can)
				can = camel_folder_synchronize_sync (secfolder, FALSE, cancellable, NULL);

			g_object_unref (secfolder);
		}
		g_ptr_array_free (folders, TRUE);

		/* To return 'Deleted Items' folder with current content,
		   not with possibly stale locally cached copy. */
		camel_folder_refresh_info_sync (folder, cancellable, NULL);
	}

	return folder;
}

static CamelFolder *
o365_store_get_junk_folder_sync (CamelStore *store,
				 GCancellable *cancellable,
				 GError **error)
{
	CamelO365Store *o365_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (store), NULL);

	o365_store = CAMEL_O365_STORE (store);

	folder_id = camel_o365_store_summary_dup_folder_id_for_type (o365_store->priv->summary, CAMEL_FOLDER_TYPE_JUNK);

	if (!folder_id) {
		g_set_error_literal (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER, _("Could not locate Junk folder"));
		return NULL;
	}

	folder_name = camel_o365_store_summary_dup_folder_full_name (o365_store->priv->summary, folder_id);

	folder = camel_store_get_folder_sync (store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);

	return folder;
}

static gboolean
o365_store_can_refresh_folder (CamelStore *store,
			       CamelFolderInfo *info,
			       GError **error)
{
	CamelSettings *settings;
	CamelO365Settings *o365_settings;
	gboolean check_all;

	/* Skip unselectable folders from automatic refresh */
	if (info && (info->flags & CAMEL_FOLDER_NOSELECT) != 0)
		return FALSE;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	o365_settings = CAMEL_O365_SETTINGS (settings);
	check_all = camel_o365_settings_get_check_all (o365_settings);

	g_object_unref (settings);

	if (check_all)
		return TRUE;

	/* Delegate decision to parent class */
	return CAMEL_STORE_CLASS (camel_o365_store_parent_class)->can_refresh_folder (store, info, error);
}

static gboolean
o365_store_folder_is_subscribed (CamelSubscribable *subscribable,
				 const gchar *folder_name)
{
	CamelO365Store *o365_store = CAMEL_O365_STORE (subscribable);

	return camel_o365_store_summary_has_full_name (o365_store->priv->summary, folder_name);
}

static gboolean
o365_store_subscribe_folder_sync (CamelSubscribable *subscribable,
				  const gchar *folder_name,
				  GCancellable *cancellable,
				  GError **error)
{
	return TRUE;
}

static gboolean
o365_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
				    const gchar *folder_name,
				    GCancellable *cancellable,
				    GError **error)
{
	return TRUE;
}

static void
o365_store_set_property (GObject *object,
			 guint property_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			camel_network_service_set_connectable (
				CAMEL_NETWORK_SERVICE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
o365_store_get_property (GObject *object,
			 guint property_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			g_value_take_object (
				value,
				camel_network_service_ref_connectable (
					CAMEL_NETWORK_SERVICE (object)));
			return;
		case PROP_HOST_REACHABLE:
			g_value_set_boolean (
				value,
				camel_network_service_get_host_reachable (
					CAMEL_NETWORK_SERVICE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
o365_store_dispose (GObject *object)
{
	CamelO365Store *o365_store = CAMEL_O365_STORE (object);

	LOCK (o365_store);

	if (o365_store->priv->summary) {
		o365_store_save_summary (o365_store->priv->summary, G_STRFUNC);
		g_clear_object (&o365_store->priv->summary);
	}

	g_clear_object (&o365_store->priv->cnc);

	UNLOCK (o365_store);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_store_parent_class)->dispose (object);
}

static void
o365_store_finalize (GObject *object)
{
	CamelO365Store *o365_store;

	o365_store = CAMEL_O365_STORE (object);

	g_rec_mutex_clear (&o365_store->priv->property_lock);
	g_hash_table_destroy (o365_store->priv->default_folders);
	g_free (o365_store->priv->storage_path);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_store_parent_class)->finalize (object);
}

static void
camel_o365_store_class_init (CamelO365StoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = o365_store_set_property;
	object_class->get_property = o365_store_get_property;
	object_class->dispose = o365_store_dispose;
	object_class->finalize = o365_store_finalize;

	/* Inherited from CamelNetworkService */
	g_object_class_override_property (
		object_class,
		PROP_CONNECTABLE,
		"connectable");

	/* Inherited from CamelNetworkService */
	g_object_class_override_property (
		object_class,
		PROP_HOST_REACHABLE,
		"host-reachable");

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_O365_SETTINGS;
	service_class->query_auth_types_sync = o365_store_query_auth_types_sync;
	service_class->get_name = o365_store_get_name;
	service_class->connect_sync = o365_store_connect_sync;
	service_class->disconnect_sync = o365_store_disconnect_sync;
	service_class->authenticate_sync = o365_store_authenticate_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder_sync = o365_store_get_folder_sync;
	store_class->create_folder_sync = o365_store_create_folder_sync;
	store_class->delete_folder_sync = o365_store_delete_folder_sync;
	store_class->rename_folder_sync = o365_store_rename_folder_sync;
	store_class->get_folder_info_sync = o365_store_get_folder_info_sync;
	store_class->initial_setup_sync = o365_store_initial_setup_sync;
	store_class->get_trash_folder_sync = o365_store_get_trash_folder_sync;
	store_class->get_junk_folder_sync = o365_store_get_junk_folder_sync;
	store_class->can_refresh_folder = o365_store_can_refresh_folder;
}

static void
camel_o365_store_initable_init (GInitableIface *iface)
{
	parent_initable_interface = g_type_interface_peek_parent (iface);

	iface->init = o365_store_initable_init;
}

static void
camel_o365_subscribable_init (CamelSubscribableInterface *iface)
{
	iface->folder_is_subscribed = o365_store_folder_is_subscribed;
	iface->subscribe_folder_sync = o365_store_subscribe_folder_sync;
	iface->unsubscribe_folder_sync = o365_store_unsubscribe_folder_sync;
}

static void
camel_o365_store_init (CamelO365Store *o365_store)
{
	o365_store->priv = camel_o365_store_get_instance_private (o365_store);

	g_rec_mutex_init (&o365_store->priv->property_lock);
	o365_store->priv->default_folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

CamelO365StoreSummary *
camel_o365_store_ref_store_summary (CamelO365Store *o365_store)
{
	CamelO365StoreSummary *summary;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (o365_store), NULL);

	LOCK (o365_store);

	summary = o365_store->priv->summary;

	if (summary)
		g_object_ref (summary);

	UNLOCK (o365_store);

	return summary;
}

EO365Connection *
camel_o365_store_ref_connection (CamelO365Store *o365_store)
{
	EO365Connection *cnc = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_STORE (o365_store), NULL);

	LOCK (o365_store);

	if (o365_store->priv->cnc)
		cnc = g_object_ref (o365_store->priv->cnc);

	UNLOCK (o365_store);

	return cnc;
}

gboolean
camel_o365_store_ensure_connected (CamelO365Store *o365_store,
				   EO365Connection **out_cnc, /* out, nullable, transfer full */
				   GCancellable *cancellable,
				   GError **error)
{
	g_return_val_if_fail (CAMEL_IS_O365_STORE (o365_store), FALSE);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (o365_store))) {
		g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));

		return FALSE;
	}

	if (!camel_service_connect_sync ((CamelService *) o365_store, cancellable, error))
		return FALSE;

	if (out_cnc) {
		*out_cnc = camel_o365_store_ref_connection (o365_store);

		if (!*out_cnc) {
			g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("You must be working online to complete this operation"));

			return FALSE;
		}
	}

	return TRUE;
}

void
camel_o365_store_maybe_disconnect (CamelO365Store *o365_store,
				   const GError *error)
{
	CamelService *service;

	g_return_if_fail (CAMEL_IS_O365_STORE (o365_store));

	if (!error)
		return;

	service = CAMEL_SERVICE (o365_store);

	if (camel_service_get_connection_status (service) != CAMEL_SERVICE_CONNECTED)
		return;

#if 0
	if (g_error_matches (error, O365_CONNECTION_ERROR, O365_CONNECTION_ERROR_NORESPONSE) ||
	    g_error_matches (error, O365_CONNECTION_ERROR, O365_CONNECTION_ERROR_AUTHENTICATION_FAILED))
		camel_service_disconnect_sync (service, FALSE, NULL, NULL);
#endif
}

void
camel_o365_store_connect_folder_summary (CamelO365Store *o365_store,
					 CamelFolderSummary *folder_summary)
{
	g_return_if_fail (CAMEL_IS_O365_STORE (o365_store));
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (folder_summary));

	LOCK (o365_store);

	if (o365_store->priv->summary)
		camel_o365_store_summary_connect_folder_summary (o365_store->priv->summary, folder_summary);

	UNLOCK (o365_store);
}
