/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"
#include "common/e-m365-enumtypes.h"
#include "camel-m365-folder.h"
#include "camel-m365-store-summary.h"
#include "camel-m365-utils.h"
#include "e-ews-common-utils.h"

#include "camel-m365-store.h"

#define LOCK(_store) g_rec_mutex_lock (&(_store->priv->property_lock))
#define UNLOCK(_store) g_rec_mutex_unlock (&(_store->priv->property_lock))

struct _CamelM365StorePrivate {
	GRecMutex property_lock;
	gchar *storage_path;
	CamelM365StoreSummary *summary;
	EM365Connection *cnc;
	GHashTable *default_folders;
	gboolean did_folder_list_refresh;
	gboolean has_ooo_set;
	CamelM365StoreOooAlertState ooo_alert_state;
};

static void camel_m365_store_initable_init (GInitableIface *iface);
static void camel_m365_subscribable_init (CamelSubscribableInterface *iface);
static GInitableIface *parent_initable_interface;

enum {
	PROP_0,
	PROP_CONNECTABLE,
	PROP_HOST_REACHABLE,
	PROP_HAS_OOO_SET,
	PROP_OOO_ALERT_STATE
};

G_DEFINE_TYPE_WITH_CODE (CamelM365Store, camel_m365_store, CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, camel_m365_store_initable_init)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_NETWORK_SERVICE, NULL)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_SUBSCRIBABLE, camel_m365_subscribable_init)
	G_ADD_PRIVATE (CamelM365Store))

static gboolean
m365_store_construct (CamelService *service,
		      CamelSession *session,
		      CamelProvider *provider,
		      GError **error)
{
	CamelM365Store *m365_store;
	gchar *summary_file, *session_storage_path;
	guint32 store_flags;
	GError *local_error = NULL;

	m365_store = (CamelM365Store *) service;

	store_flags = camel_store_get_flags (CAMEL_STORE (m365_store));

	/* Disable virtual trash and junk folders. Microsoft365 has real folders for that */
	store_flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
	store_flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	camel_store_set_flags (CAMEL_STORE (m365_store), store_flags);

	session_storage_path = g_strdup (camel_service_get_user_cache_dir (service));

	if (!session_storage_path) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Session has no storage path"));
		return FALSE;
	}

	m365_store->priv->storage_path = session_storage_path;

	g_mkdir_with_parents (m365_store->priv->storage_path, 0700);

	summary_file = g_build_filename (m365_store->priv->storage_path, "folder-tree", NULL);
	m365_store->priv->summary = camel_m365_store_summary_new (summary_file);

	if (!camel_m365_store_summary_load (m365_store->priv->summary, &local_error))
		g_warning ("%s: Failed to load store summary '%s': %s", G_STRFUNC, summary_file, local_error ? local_error->message : "Unknown error");

	g_clear_error (&local_error);
	g_free (summary_file);

	return TRUE;
}

static gboolean
m365_store_initable_init (GInitable *initable,
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

	ret = m365_store_construct (service, session, NULL, error);

	g_object_unref (session);

	return ret;
}

static GList *
m365_store_query_auth_types_sync (CamelService *service,
				  GCancellable *cancellable,
				  GError **error)
{
	g_return_val_if_fail (CAMEL_IS_M365_STORE (service), NULL);

	return NULL;
}

static gchar *
m365_store_get_name (CamelService *service,
		     gboolean brief)
{
	gchar *name;

	if (brief)
		name = g_strdup (_("Microsoft 365 server"));
	else
		name = g_strdup (_("Mail receive via Microsoft 365"));

	return name;
}

static gboolean
m365_store_read_default_folders (CamelM365Store *m365_store,
				 EM365Connection *cnc,
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

	g_return_val_if_fail (CAMEL_IS_M365_STORE (m365_store), FALSE);
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	LOCK (m365_store);

	if (g_hash_table_size (m365_store->priv->default_folders)) {
		UNLOCK (m365_store);
		return TRUE;
	}

	UNLOCK (m365_store);

	requests = g_ptr_array_new_full (G_N_ELEMENTS (default_folders), g_object_unref);

	for (ii = 0; ii < G_N_ELEMENTS (default_folders); ii++) {
		SoupMessage *message;
		gchar *uri;

		uri = e_m365_connection_construct_uri (cnc, TRUE, NULL, E_M365_API_V1_0, NULL,
			"mailFolders",
			NULL,
			default_folders[ii].name,
			"$select", "id",
			NULL);

		message = soup_message_new (SOUP_METHOD_GET, uri);

		if (!message) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Malformed URI: “%s”"), uri);

			g_ptr_array_unref (requests);
			g_free (uri);

			return FALSE;
		}

		g_free (uri);

		g_ptr_array_add (requests, message);
	}

	success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);

	if (success) {
		g_warn_if_fail (requests->len == G_N_ELEMENTS (default_folders));

		LOCK (m365_store);

		for (ii = 0; ii < requests->len; ii++) {
			SoupMessage *message = g_ptr_array_index (requests, ii);
			JsonNode *node = NULL;

			if (e_m365_connection_util_get_message_status_code (message) > 0 &&
			    SOUP_STATUS_IS_SUCCESSFUL (e_m365_connection_util_get_message_status_code (message)) &&
			    e_m365_connection_json_node_from_message (message, NULL, &node, cancellable, NULL) &&
			    node && JSON_NODE_HOLDS_OBJECT (node)) {
				JsonObject *object = json_node_get_object (node);

				if (object) {
					const gchar *id;

					id = e_m365_json_get_string_member (object, "id", NULL);

					if (id && *id) {
						g_hash_table_insert (m365_store->priv->default_folders, g_strdup (id),
							GUINT_TO_POINTER (default_folders[ii].flags));
					}
				}
			}

			if (node)
				json_node_unref (node);
		}

		UNLOCK (m365_store);
	}

	g_ptr_array_unref (requests);

	return success;
}

static gboolean
m365_store_equal_label_tag_cb (gconstpointer ptr1,
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
m365_store_find_in_ptr_array (GPtrArray *haystack,
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
m365_store_save_category_changes (GHashTable *old_categories, /* gchar *id ~> CamelM365Category * */
				  GHashTable *new_categories) /* gchar *id ~> CamelM365Category * */
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
		CamelM365Category *new_cat = value, *old_cat;
		gchar *tag = NULL;

		if (!new_cat)
			continue;

		old_cat = g_hash_table_lookup (old_categories, new_cat->id);
		if (old_cat) {
			if (g_strcmp0 (old_cat->display_name, new_cat->display_name) != 0 ||
			    g_strcmp0 (old_cat->color, new_cat->color) != 0) {
				/* Old category changed name or color */
				tag = camel_m365_utils_encode_category_name (new_cat->display_name);
			}
		} else {
			/* This is a new category */
			tag = camel_m365_utils_encode_category_name (new_cat->display_name);
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
				gchar *old_tag = camel_m365_utils_encode_category_name (old_cat->display_name);

				if (old_tag && *old_tag) {
					if (!m365_store_find_in_ptr_array (evo_labels, old_tag, m365_store_equal_label_tag_cb, &index))
						index = (guint) -1;
				}

				g_free (old_tag);
			}

			for (ii = 0; new_cat->display_name[ii]; ii++) {
				if (new_cat->display_name[ii] == '|')
					new_cat->display_name[ii] = '-';
			}

			if (index == (guint) -1 &&
			    !m365_store_find_in_ptr_array (evo_labels, tag, m365_store_equal_label_tag_cb, &index))
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
			CamelM365Category *old_cat = value;
			gchar *old_tag;
			guint index;

			if (!old_cat)
				continue;

			old_tag = camel_m365_utils_encode_category_name (old_cat->display_name);

			for (ii = 0; old_tag && old_tag[ii]; ii++) {
				if (old_tag[ii] == '|')
					old_tag[ii] = '-';
			}

			if (old_tag &&
			    m365_store_find_in_ptr_array (evo_labels, old_tag, m365_store_equal_label_tag_cb, &index))
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
m365_store_get_categories_cb (CamelSession *session,
			      GCancellable *cancellable,
			      gpointer user_data,
			      GError **error)
{
	CamelM365Store *m365_store = user_data;
	EM365Connection *cnc;
	GSList *categories = NULL;

	g_return_if_fail (CAMEL_IS_M365_STORE (m365_store));

	cnc = camel_m365_store_ref_connection (m365_store);

	if (!cnc)
		return;

	if (e_m365_connection_get_categories_sync (cnc, NULL, &categories, cancellable, error)) {
		GHashTable *old_categories, *new_categories;
		GSList *link;

		new_categories = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, camel_m365_category_free);

		for (link = categories; link; link = g_slist_next (link)) {
			EM365Category *category = link->data;
			CamelM365Category *cat;
			const gchar *id, *display_name, *color;

			if (!category)
				continue;

			id = e_m365_category_get_id (category);
			display_name = e_m365_category_get_display_name (category);
			color = e_m365_category_get_color (category);

			if (!id || !display_name)
				continue;

			if (display_name != camel_m365_utils_rename_label (display_name, TRUE))
				continue;

			cat = camel_m365_category_new (id, display_name, color);

			if (cat)
				g_hash_table_insert (new_categories, cat->id, cat);
		}

		g_slist_free_full (categories, (GDestroyNotify) json_object_unref);

		old_categories = camel_m365_store_summary_get_categories (m365_store->priv->summary);

		if (m365_store_save_category_changes (old_categories, new_categories)) {
			camel_m365_store_summary_set_categories (m365_store->priv->summary, new_categories);
			camel_m365_store_summary_save (m365_store->priv->summary, NULL);
		}

		g_hash_table_destroy (new_categories);
		g_hash_table_destroy (old_categories);
	}

	g_object_unref (cnc);
}

static void
m365_update_has_ooo_set (CamelSession *session,
			 GCancellable *cancellable,
			 gpointer user_data,
			 GError **error)
{
	CamelM365Store *self = user_data;
	EM365AutomaticRepliesSetting *setting = NULL;
	EM365Connection *cnc;

	cnc = camel_m365_store_ref_connection (self);
	if (!cnc)
		return;

	camel_operation_push_message (cancellable, _("Checking “Out of Office” settings"));

	if (e_m365_connection_get_automatic_replies_setting_sync (cnc, NULL,  &setting, cancellable, error) && setting) {
		camel_m365_store_set_has_ooo_set (self, e_m365_automatic_replies_setting_get_status (setting) == E_M365_AUTOMATIC_REPLIES_STATUS_ALWAYS_ENABLED);
		g_clear_pointer (&setting, json_object_unref);
	}

	camel_operation_pop_message (cancellable);
	g_clear_object (&cnc);
}

static gboolean
m365_store_connect_sync (CamelService *service,
			 GCancellable *cancellable,
			 GError **error)
{
	CamelM365Store *m365_store;
	EM365Connection *cnc;
	gboolean success = FALSE;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_m365_store_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	m365_store = CAMEL_M365_STORE (service);
	cnc = camel_m365_store_ref_connection (m365_store);

	if (!cnc) {
		cnc = camel_m365_utils_new_connection (service, NULL);

		if (cnc) {
			LOCK (m365_store);

			m365_store->priv->cnc = g_object_ref (cnc);

			UNLOCK (m365_store);
		}
	}

	if (cnc) {
		CamelSession *session;

		session = camel_service_ref_session (service);

		success = camel_session_authenticate_sync (session, service, "Microsoft365", cancellable, error);

		if (success) {
			CamelM365StoreOooAlertState state;

			state = camel_m365_store_get_ooo_alert_state (m365_store);

			if (state == CAMEL_M365_STORE_OOO_ALERT_STATE_UNKNOWN) {
				camel_session_submit_job (
					session, _("Checking “Out of Office” settings"),
					m365_update_has_ooo_set,
					g_object_ref (m365_store),
					g_object_unref);
			}

			camel_session_submit_job (
				session, _("Look up Microsoft 365 categories"),
				m365_store_get_categories_cb,
				g_object_ref (m365_store),
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
m365_store_disconnect_sync (CamelService *service,
			    gboolean clean,
			    GCancellable *cancellable,
			    GError **error)
{
	CamelM365Store *m365_store = CAMEL_M365_STORE (service);
	EM365Connection *cnc;
	gboolean success = TRUE;

	cnc = camel_m365_store_ref_connection (m365_store);

	if (cnc) {
		success = e_m365_connection_disconnect_sync (cnc, cancellable, error);

		g_clear_object (&cnc);
	}

	if (!success)
		return FALSE;

	/* Chain up to parent's method. */
	return CAMEL_SERVICE_CLASS (camel_m365_store_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
m365_store_authenticate_sync (CamelService *service,
			      const gchar *mechanism,
			      GCancellable *cancellable,
			      GError **error)
{
	CamelAuthenticationResult result;
	CamelM365Store *m365_store;
	EM365Connection *cnc;

	m365_store = CAMEL_M365_STORE (service);
	cnc = camel_m365_store_ref_connection (m365_store);

	if (!cnc)
		return CAMEL_AUTHENTICATION_ERROR;

	switch (e_m365_connection_authenticate_sync (cnc, NULL, E_M365_FOLDER_KIND_MAIL, NULL, NULL, NULL, NULL, cancellable, error)) {
	case E_SOURCE_AUTHENTICATION_ERROR:
	case E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED:
	default:
		result = CAMEL_AUTHENTICATION_ERROR;
		break;
	case E_SOURCE_AUTHENTICATION_ACCEPTED:
		result = CAMEL_AUTHENTICATION_ACCEPTED;

		m365_store_read_default_folders (m365_store, cnc, cancellable, NULL);
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
m365_store_get_folder_sync (CamelStore *store,
			    const gchar *folder_name,
			    guint32 flags,
			    GCancellable *cancellable,
			    GError **error)
{
	CamelM365Store *m365_store;
	CamelFolder *folder = NULL;
	gchar *fid, *folder_dir, *display_name;

	m365_store = CAMEL_M365_STORE (store);

	fid = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store->priv->summary, folder_name);

	if (!fid) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return NULL;
	}

	display_name = camel_m365_store_summary_dup_folder_display_name (m365_store->priv->summary, fid);
	folder_dir = g_build_filename (m365_store->priv->storage_path, "folders", folder_name, NULL);

	folder = camel_m365_folder_new (store, display_name, folder_name, folder_dir, cancellable, error);

	g_free (display_name);
	g_free (folder_dir);
	g_free (fid);

	if (folder && (flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0)
		camel_folder_prepare_content_refresh (folder);

	return folder;
}

static void
m365_store_save_summary (CamelM365StoreSummary *summary,
			 const gchar *where)
{
	GError *error = NULL;

	if (!camel_m365_store_summary_save (summary, &error))
		g_warning ("%s: Failed to save store summary: %s", where, error ? error->message : "Unknown error");

	g_clear_error (&error);
}

static CamelFolderInfo *
m365_store_create_folder_sync (CamelStore *store,
			       const gchar *parent_name,
			       const gchar *folder_name,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelM365Store *m365_store;
	EM365MailFolder *mail_folder = NULL;
	gchar *fid = NULL;
	gchar *full_name;
	EM365Connection *cnc;
	CamelFolderInfo *fi = NULL;
	guint32 flags;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (store), NULL);

	m365_store = CAMEL_M365_STORE (store);

	if (parent_name && *parent_name)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	fid = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store->priv->summary, full_name);

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
		fid = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store->priv->summary, parent_name);

		if (!fid) {
			g_set_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Parent folder “%s” does not exist"),
				parent_name);
			return NULL;
		}
	}

	if (!camel_m365_store_ensure_connected (m365_store, &cnc, cancellable, error)) {
		g_free (fid);

		return NULL;
	}

	success = e_m365_connection_create_mail_folder_sync (cnc, NULL, fid, folder_name, &mail_folder, cancellable, &local_error);

	g_object_unref (cnc);
	g_free (fid);

	if (!success) {
		camel_m365_store_maybe_disconnect (m365_store, local_error);
		g_propagate_error (error, local_error);

		return NULL;
	}

	flags = e_m365_mail_folder_get_child_folder_count (mail_folder) ? CAMEL_STORE_INFO_FOLDER_CHILDREN : CAMEL_STORE_INFO_FOLDER_NOCHILDREN;

	camel_m365_store_summary_set_folder (m365_store->priv->summary, TRUE,
		e_m365_folder_get_id (mail_folder),
		e_m365_folder_get_parent_folder_id (mail_folder),
		e_m365_folder_get_display_name (mail_folder),
		e_m365_mail_folder_get_total_item_count (mail_folder),
		e_m365_mail_folder_get_unread_item_count (mail_folder),
		flags, E_M365_FOLDER_KIND_MAIL, FALSE, FALSE);

	fi = camel_m365_store_summary_build_folder_info_for_id (m365_store->priv->summary, e_m365_folder_get_id (mail_folder));

	camel_store_folder_created (store, fi);
	camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (m365_store), fi);

	json_object_unref (mail_folder);

	m365_store_save_summary (m365_store->priv->summary, G_STRFUNC);

	return fi;
}

static void
m365_store_notify_created_recursive (CamelStore *store,
				     CamelFolderInfo *folder_info)
{
	while (folder_info) {
		camel_store_folder_created (store, folder_info);
		camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (store), folder_info);

		if (folder_info->child)
			m365_store_notify_created_recursive (store, folder_info->child);

		folder_info = folder_info->next;
	}
}

static gboolean
m365_store_move_mail_folder (CamelM365Store *m365_store,
			     EM365Connection *cnc,
			     const gchar *folder_id,
			     const gchar *des_folder_id,
			     GCancellable *cancellable,
			     GError **error)
{
	EM365MailFolder *moved_mail_folder = NULL;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (m365_store), FALSE);
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (des_folder_id != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (folder_id, des_folder_id) != 0, FALSE);

	success = e_m365_connection_copy_move_mail_folder_sync (cnc, NULL, folder_id, des_folder_id, FALSE, &moved_mail_folder, cancellable, error);

	if (success && moved_mail_folder) {
		CamelFolderInfo *fi;
		gchar *new_full_name;

		fi = camel_m365_store_summary_build_folder_info_for_id (m365_store->priv->summary, folder_id);

		camel_m365_store_summary_set_folder_parent_id (m365_store->priv->summary, folder_id, e_m365_folder_get_parent_folder_id (moved_mail_folder));
		camel_m365_store_summary_rebuild_hashes (m365_store->priv->summary);

		camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (m365_store), fi);
		camel_store_folder_deleted (CAMEL_STORE (m365_store), fi);

		camel_folder_info_free (fi);

		new_full_name = camel_m365_store_summary_dup_folder_full_name (m365_store->priv->summary, folder_id);
		g_warn_if_fail (new_full_name != NULL);

		fi = camel_m365_store_summary_build_folder_info (m365_store->priv->summary, new_full_name, TRUE);

		m365_store_notify_created_recursive (CAMEL_STORE (m365_store), fi);

		json_object_unref (moved_mail_folder);
		camel_folder_info_free (fi);
		g_free (new_full_name);
	}

	return success;
}

static void
m365_store_delete_folders_from_summary_recursive (CamelM365Store *m365_store,
						  CamelFolderInfo *fi,
						  gboolean send_signals)
{
	CamelStore *store = send_signals ? CAMEL_STORE (m365_store) : NULL;
	CamelSubscribable *subscribable = send_signals ? CAMEL_SUBSCRIBABLE (m365_store) : NULL;

	while (fi) {
		gchar *folder_id;

		if (fi->child)
			m365_store_delete_folders_from_summary_recursive (m365_store, fi->child, send_signals);

		folder_id = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store->priv->summary, fi->full_name);
		if (folder_id) {
			camel_m365_store_summary_remove_folder (m365_store->priv->summary, folder_id);
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
m365_store_delete_folder_sync (CamelStore *store,
			       const gchar *folder_name,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelM365Store *m365_store;
	CamelFolderInfo *folder_info;
	EM365Connection *cnc = NULL;
	gchar *folder_id;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (store), FALSE);

	m365_store = CAMEL_M365_STORE (store);

	folder_info = camel_store_get_folder_info_sync (store, folder_name,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
		cancellable, &local_error);

	if (!folder_info) {
		if (local_error)
			g_propagate_error (error, local_error);

		return FALSE;
	}

	folder_id = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store->priv->summary, folder_name);

	if (!folder_id) {
		camel_folder_info_free (folder_info);

		g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Folder does not exist"));

		return FALSE;
	}

	if (!camel_m365_store_ensure_connected (m365_store, &cnc, cancellable, error)) {
		camel_folder_info_free (folder_info);
		g_free (folder_id);

		return FALSE;
	}

	if (camel_m365_store_summary_get_folder_is_foreign (m365_store->priv->summary, folder_id) ||
	    camel_m365_store_summary_get_folder_is_public (m365_store->priv->summary, folder_id)) {
		/* do not delete foreign or public folders,
		 * only remove them from the local cache */
		success = TRUE;
	} else {
		success = e_m365_connection_delete_mail_folder_sync (cnc, NULL, folder_id, cancellable, &local_error);
	}

	g_clear_object (&cnc);

	if (!success) {
		camel_folder_info_free (folder_info);
		g_free (folder_id);

		camel_m365_store_maybe_disconnect (m365_store, local_error);
		g_propagate_error (error, local_error);

		return FALSE;
	}

	m365_store_delete_folders_from_summary_recursive (m365_store, folder_info, FALSE);

	camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (m365_store), folder_info);
	camel_store_folder_deleted (store, folder_info);

	camel_folder_info_free (folder_info);

	m365_store_save_summary (m365_store->priv->summary, G_STRFUNC);

	g_free (folder_id);

	return TRUE;
}

static gboolean
m365_store_rename_folder_sync (CamelStore *store,
			       const gchar *old_name,
			       const gchar *new_name,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelM365Store *m365_store;
	EM365Connection *cnc;
	const gchar *old_slash, *new_slash;
	gint parent_len;
	gchar *folder_id;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (store), FALSE);

	if (!g_strcmp0 (old_name, new_name))
		return TRUE;

	m365_store = CAMEL_M365_STORE (store);
	folder_id = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store->priv->summary, old_name);

	if (!folder_id) {
		g_set_error (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER,
			_("Folder “%s” does not exist"), old_name);

		return FALSE;
	}

	if (!camel_m365_store_ensure_connected (m365_store, &cnc, cancellable, error)) {
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
			new_parent_id = camel_m365_store_summary_dup_folder_id_for_full_name (m365_store->priv->summary, new_parent);

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

		success = m365_store_move_mail_folder (m365_store, cnc, folder_id, new_parent_id ? new_parent_id : "msgfolderroot", cancellable, &local_error);

		g_free (new_parent_id);
	}

	/* Then rename the folder, if needed */
	if (success && g_strcmp0 (old_slash, new_slash) != 0) {
		EM365MailFolder *mail_folder = NULL;

		success = e_m365_connection_rename_mail_folder_sync (cnc, NULL, folder_id, new_slash, &mail_folder, cancellable, &local_error);

		if (mail_folder) {
			camel_m365_store_summary_set_folder_display_name (m365_store->priv->summary, folder_id,
				e_m365_folder_get_display_name (mail_folder), TRUE);

			json_object_unref (mail_folder);
		}
	}

	if (success) {
		CamelFolderInfo *fi;

		fi = camel_m365_store_summary_build_folder_info_for_id (m365_store->priv->summary, folder_id);

		if (fi) {
			camel_store_folder_renamed (store, old_name, fi);
			camel_folder_info_free (fi);
		}
	}

	m365_store_save_summary (m365_store->priv->summary, G_STRFUNC);

	if (!success && local_error) {
		camel_m365_store_maybe_disconnect (m365_store, local_error);
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
	CamelM365Store *m365_store;
	GSList *added_ids; /* gchar *, folder ids */
	GSList *renamed_data; /* FolderRenamedData * */
	GSList *removed_fis; /* CamelFolderInfo * */
} FoldersDeltaData;

static gboolean
camel_m365_got_folders_delta_cb (EM365Connection *cnc,
				 const GSList *results, /* JsonObject * - the returned objects from the server */
				 gpointer user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	FoldersDeltaData *fdd = user_data;
	GSList *link;

	g_return_val_if_fail (fdd, FALSE);

	LOCK (fdd->m365_store);

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		JsonObject *object = link->data;
		const gchar *id = e_m365_folder_get_id (object);

		if (e_m365_delta_is_removed_object (object)) {
			CamelFolderInfo *info;

			info = camel_m365_store_summary_build_folder_info_for_id (fdd->m365_store->priv->summary, id);

			if (info)
				fdd->removed_fis = g_slist_prepend (fdd->removed_fis, info);

			camel_m365_store_summary_remove_folder (fdd->m365_store->priv->summary, id);
		} else {
			gchar *old_full_name = NULL;
			guint32 flags;

			if (camel_m365_store_summary_has_folder (fdd->m365_store->priv->summary, id))
				old_full_name = camel_m365_store_summary_dup_folder_full_name (fdd->m365_store->priv->summary, id);

			flags = e_m365_mail_folder_get_child_folder_count (object) ? CAMEL_STORE_INFO_FOLDER_CHILDREN : CAMEL_STORE_INFO_FOLDER_NOCHILDREN;

			flags |= GPOINTER_TO_UINT (g_hash_table_lookup (fdd->m365_store->priv->default_folders, id));

			camel_m365_store_summary_set_folder (fdd->m365_store->priv->summary, FALSE, id,
				e_m365_folder_get_parent_folder_id (object),
				e_m365_folder_get_display_name (object),
				e_m365_mail_folder_get_total_item_count (object),
				e_m365_mail_folder_get_unread_item_count (object),
				flags, E_M365_FOLDER_KIND_MAIL, FALSE, FALSE);

			if (old_full_name)
				fdd->renamed_data = g_slist_prepend (fdd->renamed_data, folder_renamed_data_new (g_strdup (id), old_full_name));
			else
				fdd->added_ids = g_slist_prepend (fdd->added_ids, g_strdup (id));
		}
	}

	UNLOCK (fdd->m365_store);

	return TRUE;
}

static void
m365_store_forget_all_folders (CamelM365Store *m365_store)
{
	CamelStore *store;
	CamelSubscribable *subscribable;
	GSList *ids, *link;

	g_return_if_fail (CAMEL_IS_M365_STORE (m365_store));

	store = CAMEL_STORE (m365_store);
	subscribable = CAMEL_SUBSCRIBABLE (m365_store);
	ids = camel_m365_store_summary_list_folder_ids (m365_store->priv->summary);

	if (!ids)
		return;

	for (link = ids; link; link = g_slist_next (link)) {
		const gchar *id = link->data;
		CamelFolderInfo *fi;

		fi = camel_m365_store_summary_build_folder_info_for_id (m365_store->priv->summary, id);
		camel_subscribable_folder_unsubscribed (subscribable, fi);
		camel_store_folder_deleted (store, fi);
		camel_folder_info_free (fi);
	}

	g_slist_free_full (ids, g_free);

	camel_m365_store_summary_set_delta_link (m365_store->priv->summary, "");
	camel_m365_store_summary_clear (m365_store->priv->summary);
}

static CamelFolderInfo *
m365_store_get_folder_info_sync (CamelStore *store,
				 const gchar *top,
				 guint32 flags,
				 GCancellable *cancellable,
				 GError **error)
{
	CamelM365Store *m365_store;
	CamelFolderInfo *fi;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (store), NULL);

	m365_store = CAMEL_M365_STORE (store);

	if (camel_offline_store_get_online (CAMEL_OFFLINE_STORE (m365_store))) {
		gboolean refresh_online;

		refresh_online = !(flags & CAMEL_STORE_FOLDER_INFO_FAST) ||
				  (flags & CAMEL_STORE_FOLDER_INFO_REFRESH) != 0 ||
				 !m365_store->priv->did_folder_list_refresh;

		if (!refresh_online) {
			gchar *delta_link;

			LOCK (m365_store);

			delta_link = camel_m365_store_summary_dup_delta_link (m365_store->priv->summary);
			refresh_online = !delta_link || !*delta_link;
			g_free (delta_link);

			UNLOCK (m365_store);
		}

		if (refresh_online) {
			EM365Connection *cnc;

			cnc = camel_m365_store_ref_connection (m365_store);

			if (cnc) {
				FoldersDeltaData fdd;
				gchar *old_delta_link, *new_delta_link = NULL;
				GError *local_error = NULL;

				m365_store->priv->did_folder_list_refresh = TRUE;

				LOCK (m365_store);

				old_delta_link = camel_m365_store_summary_dup_delta_link (m365_store->priv->summary);

				UNLOCK (m365_store);

				fdd.m365_store = m365_store;
				fdd.added_ids = NULL;
				fdd.renamed_data = NULL;
				fdd.removed_fis = NULL;

				success = e_m365_connection_get_folders_delta_sync (cnc, NULL, E_M365_FOLDER_KIND_MAIL, NULL, old_delta_link, 0,
					camel_m365_got_folders_delta_cb, &fdd, &new_delta_link, cancellable, &local_error);

				if (old_delta_link && *old_delta_link && e_m365_connection_util_delta_token_failed (local_error)) {
					g_clear_pointer (&old_delta_link, g_free);
					g_clear_error (&local_error);

					m365_store_forget_all_folders (m365_store);

					success = e_m365_connection_get_folders_delta_sync (cnc, NULL, E_M365_FOLDER_KIND_MAIL, NULL, NULL, 0,
						camel_m365_got_folders_delta_cb, &fdd, &new_delta_link, cancellable, error);
				}

				if (local_error)
					g_propagate_error (error, local_error);

				if (success) {
					CamelSubscribable *subscribable = CAMEL_SUBSCRIBABLE (m365_store);
					CamelFolderInfo *info;
					GSList *link;

					LOCK (m365_store);

					camel_m365_store_summary_set_delta_link (m365_store->priv->summary, new_delta_link);
					m365_store_save_summary (m365_store->priv->summary, G_STRFUNC);

					fdd.added_ids = g_slist_reverse (fdd.added_ids);
					fdd.renamed_data = g_slist_reverse (fdd.renamed_data);
					fdd.removed_fis = g_slist_reverse (fdd.removed_fis);

					if (fdd.added_ids || fdd.renamed_data || fdd.removed_fis)
						camel_m365_store_summary_rebuild_hashes (m365_store->priv->summary);

					for (link = fdd.removed_fis; link; link = g_slist_next (link)) {
						info = link->data;

						camel_subscribable_folder_unsubscribed (subscribable, info);
						camel_store_folder_deleted (store, info);
					}

					for (link = fdd.added_ids; link; link = g_slist_next (link)) {
						const gchar *id = link->data;

						info = camel_m365_store_summary_build_folder_info_for_id (m365_store->priv->summary, id);

						if (info) {
							camel_store_folder_created (store, info);
							camel_subscribable_folder_subscribed (subscribable, info);
							camel_folder_info_free (info);
						}
					}

					for (link = fdd.renamed_data; link; link = g_slist_next (link)) {
						const FolderRenamedData *frd = link->data;

						info = camel_m365_store_summary_build_folder_info_for_id (m365_store->priv->summary, frd->id);

						if (info) {
							camel_store_folder_renamed (store, frd->old_name, info);
							camel_folder_info_free (info);
						}
					}

					UNLOCK (m365_store);
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
		LOCK (m365_store);

		fi = camel_m365_store_summary_build_folder_info (m365_store->priv->summary, top, (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) != 0);

		UNLOCK (m365_store);
	} else {
		fi = NULL;
	}

	return fi;
}

/* Hold the property lock before calling this function */
static void
m365_store_save_setup_folder_locked (CamelM365Store *m365_store,
				     GHashTable *save_setup,
				     guint32 folder_type, /* one of TYPE constants from CamelFolderInfoFlags */
				     const gchar *property_name)
{
	gchar *folder_id;

	g_return_if_fail (CAMEL_IS_M365_STORE (m365_store));
	g_return_if_fail (save_setup != NULL);
	g_return_if_fail (folder_type != 0);
	g_return_if_fail (property_name != NULL);

	folder_id = camel_m365_store_summary_dup_folder_id_for_type (m365_store->priv->summary, folder_type);

	if (folder_id) {
		gchar *fullname;

		fullname = camel_m365_store_summary_dup_folder_full_name (m365_store->priv->summary, folder_id);

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
m365_store_initial_setup_with_connection_sync (CamelStore *store,
					       GHashTable *save_setup,
					       EM365Connection *cnc,
					       GCancellable *cancellable,
					       GError **error)
{
	CamelM365Store *m365_store;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (store), FALSE);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	m365_store = CAMEL_M365_STORE (store);

	if (cnc) {
		g_object_ref (cnc);
	} else {
		if (!camel_m365_store_ensure_connected (m365_store, &cnc, cancellable, error))
			return FALSE;

		g_return_val_if_fail (cnc != NULL, FALSE);
	}

	if (!m365_store_read_default_folders (m365_store, cnc, cancellable, error)) {
		g_clear_object (&cnc);
		return FALSE;
	}

	if (save_setup) {
		LOCK (m365_store);

		m365_store_save_setup_folder_locked (m365_store, save_setup, CAMEL_FOLDER_TYPE_SENT, CAMEL_STORE_SETUP_SENT_FOLDER);
		m365_store_save_setup_folder_locked (m365_store, save_setup, CAMEL_FOLDER_TYPE_DRAFTS, CAMEL_STORE_SETUP_DRAFTS_FOLDER);
		m365_store_save_setup_folder_locked (m365_store, save_setup, CAMEL_FOLDER_TYPE_ARCHIVE, CAMEL_STORE_SETUP_ARCHIVE_FOLDER);

		UNLOCK (m365_store);
	}

	g_clear_object (&cnc);

	return TRUE;
}

static gboolean
m365_store_initial_setup_sync (CamelStore *store,
			       GHashTable *save_setup,
			       GCancellable *cancellable,
			       GError **error)
{
	return m365_store_initial_setup_with_connection_sync (store, save_setup, NULL, cancellable, error);
}

static CamelFolder *
m365_store_get_trash_folder_sync (CamelStore *store,
				  GCancellable *cancellable,
				  GError **error)
{
	CamelM365Store *m365_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (store), NULL);

	m365_store = CAMEL_M365_STORE (store);

	LOCK (m365_store);

	folder_id = camel_m365_store_summary_dup_folder_id_for_type (m365_store->priv->summary, CAMEL_FOLDER_TYPE_TRASH);

	if (!folder_id) {
		UNLOCK (m365_store);
		g_set_error_literal (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER, _("Could not locate Trash folder"));
		return NULL;
	}

	folder_name = camel_m365_store_summary_dup_folder_full_name (m365_store->priv->summary, folder_id);

	UNLOCK (m365_store);

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
m365_store_get_junk_folder_sync (CamelStore *store,
				 GCancellable *cancellable,
				 GError **error)
{
	CamelM365Store *m365_store;
	CamelFolder *folder = NULL;
	gchar *folder_id, *folder_name;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (store), NULL);

	m365_store = CAMEL_M365_STORE (store);

	folder_id = camel_m365_store_summary_dup_folder_id_for_type (m365_store->priv->summary, CAMEL_FOLDER_TYPE_JUNK);

	if (!folder_id) {
		g_set_error_literal (error, CAMEL_STORE_ERROR, CAMEL_STORE_ERROR_NO_FOLDER, _("Could not locate Junk folder"));
		return NULL;
	}

	folder_name = camel_m365_store_summary_dup_folder_full_name (m365_store->priv->summary, folder_id);

	folder = camel_store_get_folder_sync (store, folder_name, 0, cancellable, error);

	g_free (folder_name);
	g_free (folder_id);

	return folder;
}

static gboolean
m365_store_can_refresh_folder (CamelStore *store,
			       CamelFolderInfo *info,
			       GError **error)
{
	CamelFolder *folder;
	CamelSettings *settings;
	CamelM365Settings *m365_settings;
	gboolean check_all, res = FALSE;
	GError *local_error = NULL;

	/* Skip unselectable folders from automatic refresh */
	if (info && (info->flags & CAMEL_FOLDER_NOSELECT) != 0)
		return FALSE;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	m365_settings = CAMEL_M365_SETTINGS (settings);
	check_all = camel_m365_settings_get_check_all (m365_settings);

	g_object_unref (settings);

	if (check_all || CAMEL_STORE_CLASS (camel_m365_store_parent_class)->can_refresh_folder (store, info, &local_error))
		return TRUE;

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	folder = camel_store_get_folder_sync (store, info->full_name, 0, NULL, &local_error);
	if (folder && CAMEL_IS_M365_FOLDER (folder))
		res = camel_m365_folder_get_check_folder (CAMEL_M365_FOLDER (folder));

	g_clear_object (&folder);

	if (local_error)
		g_propagate_error (error, local_error);

	return res;
}

static gboolean
m365_store_folder_is_subscribed (CamelSubscribable *subscribable,
				 const gchar *folder_name)
{
	CamelM365Store *m365_store = CAMEL_M365_STORE (subscribable);

	return camel_m365_store_summary_has_full_name (m365_store->priv->summary, folder_name);
}

static gboolean
m365_store_subscribe_folder_sync (CamelSubscribable *subscribable,
				  const gchar *folder_name,
				  GCancellable *cancellable,
				  GError **error)
{
	return TRUE;
}

static gboolean
m365_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
				    const gchar *folder_name,
				    GCancellable *cancellable,
				    GError **error)
{
	return TRUE;
}

static void
m365_store_set_property (GObject *object,
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
		case PROP_HAS_OOO_SET:
			camel_m365_store_set_has_ooo_set (
				CAMEL_M365_STORE (object),
				g_value_get_boolean (value));
			return;
		case PROP_OOO_ALERT_STATE:
			camel_m365_store_set_ooo_alert_state (
				CAMEL_M365_STORE (object),
				g_value_get_enum (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_store_get_property (GObject *object,
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
		case PROP_HAS_OOO_SET:
			g_value_set_boolean (
				value,
				camel_m365_store_get_has_ooo_set (
				CAMEL_M365_STORE (object)));
			return;
		case PROP_OOO_ALERT_STATE:
			g_value_set_enum (
				value,
				camel_m365_store_get_ooo_alert_state (
				CAMEL_M365_STORE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_store_dispose (GObject *object)
{
	CamelM365Store *m365_store = CAMEL_M365_STORE (object);

	LOCK (m365_store);

	if (m365_store->priv->summary) {
		m365_store_save_summary (m365_store->priv->summary, G_STRFUNC);
		g_clear_object (&m365_store->priv->summary);
	}

	g_clear_object (&m365_store->priv->cnc);

	UNLOCK (m365_store);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_store_parent_class)->dispose (object);
}

static void
m365_store_finalize (GObject *object)
{
	CamelM365Store *m365_store;

	m365_store = CAMEL_M365_STORE (object);

	g_rec_mutex_clear (&m365_store->priv->property_lock);
	g_hash_table_destroy (m365_store->priv->default_folders);
	g_free (m365_store->priv->storage_path);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_store_parent_class)->finalize (object);
}

static void
camel_m365_store_class_init (CamelM365StoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = m365_store_set_property;
	object_class->get_property = m365_store_get_property;
	object_class->dispose = m365_store_dispose;
	object_class->finalize = m365_store_finalize;


	g_object_class_install_property (
		object_class,
		PROP_HAS_OOO_SET,
		g_param_spec_boolean (
			"has-ooo-set", NULL, NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OOO_ALERT_STATE,
		g_param_spec_enum (
			"ooo-alert-state", NULL, NULL,
			CAMEL_TYPE_M365_STORE_OOO_ALERT_STATE,
			CAMEL_M365_STORE_OOO_ALERT_STATE_UNKNOWN,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

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
	service_class->settings_type = CAMEL_TYPE_M365_SETTINGS;
	service_class->query_auth_types_sync = m365_store_query_auth_types_sync;
	service_class->get_name = m365_store_get_name;
	service_class->connect_sync = m365_store_connect_sync;
	service_class->disconnect_sync = m365_store_disconnect_sync;
	service_class->authenticate_sync = m365_store_authenticate_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder_sync = m365_store_get_folder_sync;
	store_class->create_folder_sync = m365_store_create_folder_sync;
	store_class->delete_folder_sync = m365_store_delete_folder_sync;
	store_class->rename_folder_sync = m365_store_rename_folder_sync;
	store_class->get_folder_info_sync = m365_store_get_folder_info_sync;
	store_class->initial_setup_sync = m365_store_initial_setup_sync;
	store_class->get_trash_folder_sync = m365_store_get_trash_folder_sync;
	store_class->get_junk_folder_sync = m365_store_get_junk_folder_sync;
	store_class->can_refresh_folder = m365_store_can_refresh_folder;
}

static void
camel_m365_store_initable_init (GInitableIface *iface)
{
	parent_initable_interface = g_type_interface_peek_parent (iface);

	iface->init = m365_store_initable_init;
}

static void
camel_m365_subscribable_init (CamelSubscribableInterface *iface)
{
	iface->folder_is_subscribed = m365_store_folder_is_subscribed;
	iface->subscribe_folder_sync = m365_store_subscribe_folder_sync;
	iface->unsubscribe_folder_sync = m365_store_unsubscribe_folder_sync;
}

static void
camel_m365_store_init (CamelM365Store *m365_store)
{
	m365_store->priv = camel_m365_store_get_instance_private (m365_store);

	g_rec_mutex_init (&m365_store->priv->property_lock);
	m365_store->priv->default_folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

CamelM365StoreSummary *
camel_m365_store_ref_store_summary (CamelM365Store *m365_store)
{
	CamelM365StoreSummary *summary;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (m365_store), NULL);

	LOCK (m365_store);

	summary = m365_store->priv->summary;

	if (summary)
		g_object_ref (summary);

	UNLOCK (m365_store);

	return summary;
}

EM365Connection *
camel_m365_store_ref_connection (CamelM365Store *m365_store)
{
	EM365Connection *cnc = NULL;

	g_return_val_if_fail (CAMEL_IS_M365_STORE (m365_store), NULL);

	LOCK (m365_store);

	if (m365_store->priv->cnc)
		cnc = g_object_ref (m365_store->priv->cnc);

	UNLOCK (m365_store);

	return cnc;
}

gboolean
camel_m365_store_ensure_connected (CamelM365Store *m365_store,
				   EM365Connection **out_cnc, /* out, nullable, transfer full */
				   GCancellable *cancellable,
				   GError **error)
{
	g_return_val_if_fail (CAMEL_IS_M365_STORE (m365_store), FALSE);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (m365_store))) {
		g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));

		return FALSE;
	}

	if (!camel_service_connect_sync ((CamelService *) m365_store, cancellable, error))
		return FALSE;

	if (out_cnc) {
		*out_cnc = camel_m365_store_ref_connection (m365_store);

		if (!*out_cnc) {
			g_set_error_literal (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("You must be working online to complete this operation"));

			return FALSE;
		}
	}

	return TRUE;
}

void
camel_m365_store_maybe_disconnect (CamelM365Store *m365_store,
				   GError *error)
{
	CamelService *service;

	g_return_if_fail (CAMEL_IS_M365_STORE (m365_store));

	if (!error)
		return;

	service = CAMEL_SERVICE (m365_store);

	if (camel_service_get_connection_status (service) != CAMEL_SERVICE_CONNECTED)
		return;

	if (g_error_matches (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		ESourceRegistry *registry = NULL;

		camel_service_disconnect_sync (service, FALSE, NULL, NULL);

		error->domain = CAMEL_SERVICE_ERROR;
		error->code = CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE;

		registry = e_source_registry_new_sync (NULL, NULL);

		if (registry) {
			ESource *source, *collection = NULL;

			source = e_source_registry_ref_source (registry, camel_service_get_uid (service));

			if (source) {
				collection = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

				if (collection)
					e_source_emit_credentials_required (collection, E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, error);
			}

			g_clear_object (&collection);
			g_clear_object (&source);
		}

		g_clear_object (&registry);
	}
}

void
camel_m365_store_connect_folder_summary (CamelM365Store *m365_store,
					 CamelFolderSummary *folder_summary)
{
	g_return_if_fail (CAMEL_IS_M365_STORE (m365_store));
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (folder_summary));

	LOCK (m365_store);

	if (m365_store->priv->summary)
		camel_m365_store_summary_connect_folder_summary (m365_store->priv->summary, folder_summary);

	UNLOCK (m365_store);
}

void
camel_m365_store_set_has_ooo_set (CamelM365Store *self,
				  gboolean has_ooo_set)
{
	g_return_if_fail (CAMEL_IS_M365_STORE (self));

	if ((self->priv->has_ooo_set ? 1 : 0) == (has_ooo_set ? 1 : 0))
		return;

	self->priv->has_ooo_set = has_ooo_set;
	g_object_notify (G_OBJECT (self), "has-ooo-set");
}

gboolean
camel_m365_store_get_has_ooo_set (const CamelM365Store *self)
{
	g_return_val_if_fail (CAMEL_IS_M365_STORE (self), FALSE);

	return self->priv->has_ooo_set;
}

void
camel_m365_store_set_ooo_alert_state (CamelM365Store *self,
				      CamelM365StoreOooAlertState state)
{
	g_return_if_fail (CAMEL_IS_M365_STORE (self));

	if (self->priv->ooo_alert_state == state)
		return;

	self->priv->ooo_alert_state = state;
	g_object_notify (G_OBJECT (self), "ooo-alert-state");
}

CamelM365StoreOooAlertState
camel_m365_store_get_ooo_alert_state (const CamelM365Store *self)
{
	g_return_val_if_fail (CAMEL_IS_M365_STORE (self), CAMEL_M365_STORE_OOO_ALERT_STATE_UNKNOWN);

	return self->priv->ooo_alert_state;
}

static void
m365_store_unset_oof_settings_state (CamelSession *session,
				     GCancellable *cancellable,
				     gpointer user_data,
				     GError **error)
{

	CamelM365Store *self = user_data;
	EM365Connection *cnc;
	EM365AutomaticRepliesSetting *setting = NULL;

	camel_operation_push_message (cancellable, _("Unsetting the “Out of Office” status"));

	cnc = camel_m365_store_ref_connection (self);

	if (e_m365_connection_get_automatic_replies_setting_sync (cnc, NULL, &setting, cancellable, error) && setting) {
		if (e_m365_automatic_replies_setting_get_status (setting) != E_M365_AUTOMATIC_REPLIES_STATUS_DISABLED) {
			JsonBuilder *builder;

			builder = json_builder_new_immutable ();

			e_m365_begin_mailbox_settings (builder);
			e_m365_begin_automatic_replies_setting (builder);
			e_m365_automatic_replies_setting_add_status (builder, E_M365_AUTOMATIC_REPLIES_STATUS_DISABLED);
			e_m365_end_automatic_replies_setting (builder);
			e_m365_end_mailbox_settings (builder);

			e_m365_connection_update_mailbox_settings_sync (cnc, NULL, builder, cancellable, error);

			g_clear_object (&builder);
		}

		g_clear_pointer (&setting, json_object_unref);
	}

	camel_operation_pop_message (cancellable);

	g_clear_object (&cnc);
}

void
camel_m365_store_unset_oof_settings_state (CamelM365Store *self)
{
	CamelService *service;
	CamelSession *session;

	g_return_if_fail (CAMEL_IS_M365_STORE (self));

	service = CAMEL_SERVICE (self);
	session = camel_service_ref_session (service);

	camel_session_submit_job (
		session, _("Unsetting the “Out of Office” status"),
		m365_store_unset_oof_settings_state,
		g_object_ref (self),
		g_object_unref);

	g_object_unref (session);
}
