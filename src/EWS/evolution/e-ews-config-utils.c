/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <unistd.h>
#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>
#include <libedataserverui/libedataserverui.h>

#include <e-util/e-util.h>
#include <mail/em-folder-tree.h>
#include <mail/em-folder-utils.h>
#include <libemail-engine/libemail-engine.h>
#include <shell/e-shell.h>
#include <shell/e-shell-sidebar.h>
#include <shell/e-shell-view.h>
#include <shell/e-shell-window.h>

#include "common/e-ews-connection.h"
#include "common/e-ews-connection-utils.h"
#include "common/e-source-ews-folder.h"

#include "e-ews-edit-folder-permissions.h"

#include "camel/camel-ews-store.h"
#include "camel/camel-ews-store-summary.h"

#include "e-ews-config-utils.h"
#include "e-ews-search-user.h"
#include "e-ews-subscribe-foreign-folder.h"

struct RunWithFeedbackData
{
	GtkWindow *parent;
	GtkWidget *dialog;
	GCancellable *cancellable;
	GObject *with_object;
	EEwsSetupFunc thread_func;
	EEwsSetupFunc idle_func;
	EEwsSetupFunc finish_idle_func;
	gpointer user_data;
	GDestroyNotify free_user_data;
	GError *error;
	gboolean run_modal;
};

static void
free_run_with_feedback_data (gpointer ptr)
{
	struct RunWithFeedbackData *rfd = ptr;

	if (!rfd)
		return;

	if (rfd->dialog)
		gtk_widget_destroy (rfd->dialog);

	g_object_unref (rfd->cancellable);
	g_object_unref (rfd->with_object);

	if (rfd->free_user_data)
		rfd->free_user_data (rfd->user_data);

	g_clear_error (&rfd->error);

	g_slice_free (struct RunWithFeedbackData, rfd);
}

static gboolean
run_with_feedback_idle (gpointer user_data)
{
	struct RunWithFeedbackData *rfd = user_data;
	gboolean was_cancelled = FALSE;

	g_return_val_if_fail (rfd != NULL, FALSE);

	if (!g_cancellable_is_cancelled (rfd->cancellable)) {
		if (rfd->idle_func && !rfd->error)
			rfd->idle_func (rfd->with_object, rfd->user_data, rfd->cancellable, &rfd->error);

		was_cancelled = g_cancellable_is_cancelled (rfd->cancellable);

		if (rfd->dialog) {
			gtk_widget_destroy (rfd->dialog);
			rfd->dialog = NULL;
		}
	} else {
		was_cancelled = TRUE;
	}

	if (rfd->finish_idle_func)
		rfd->finish_idle_func (rfd->with_object, rfd->user_data, rfd->cancellable, &rfd->error);

	if (!was_cancelled) {
		if (rfd->error) {
			g_dbus_error_strip_remote_error (rfd->error);

			e_notice (rfd->parent, GTK_MESSAGE_ERROR, "%s", rfd->error->message);
		}
	}

	free_run_with_feedback_data (rfd);

	return FALSE;
}

static gpointer
run_with_feedback_thread (gpointer user_data)
{
	struct RunWithFeedbackData *rfd = user_data;

	g_return_val_if_fail (rfd != NULL, NULL);
	g_return_val_if_fail (rfd->thread_func != NULL, NULL);

	if (!g_cancellable_is_cancelled (rfd->cancellable))
		rfd->thread_func (rfd->with_object, rfd->user_data, rfd->cancellable, &rfd->error);

	g_idle_add (run_with_feedback_idle, rfd);

	return NULL;
}

static void
run_with_feedback_response_cb (GtkWidget *dialog,
                               gint resonse_id,
                               struct RunWithFeedbackData *rfd)
{
	g_return_if_fail (rfd != NULL);

	rfd->dialog = NULL;

	g_cancellable_cancel (rfd->cancellable);

	gtk_widget_destroy (dialog);
}

static void
e_ews_config_utils_run_in_thread_with_feedback_general (GtkWindow *parent,
                                                        GObject *with_object,
                                                        const gchar *description,
                                                        EEwsSetupFunc thread_func,
                                                        EEwsSetupFunc idle_func,
                                                        gpointer user_data,
                                                        GDestroyNotify free_user_data,
                                                        gboolean run_modal)
{
	GtkWidget *dialog, *label, *content, *spinner, *box;
	struct RunWithFeedbackData *rfd;

	g_return_if_fail (with_object != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (thread_func != NULL);

	dialog = gtk_dialog_new_with_buttons (
		"",
		parent,
		GTK_DIALOG_MODAL,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		NULL);

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

	spinner = e_spinner_new ();
	e_spinner_start (E_SPINNER (spinner));
	gtk_box_pack_start (GTK_BOX (box), spinner, FALSE, FALSE, 0);

	label = gtk_label_new (description);
	gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

	gtk_widget_show_all (box);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	gtk_container_add (GTK_CONTAINER (content), box);
	gtk_container_set_border_width (GTK_CONTAINER (content), 12);

	rfd = g_slice_new0 (struct RunWithFeedbackData);
	rfd->parent = parent;
	rfd->dialog = dialog;
	rfd->cancellable = g_cancellable_new ();
	rfd->with_object = g_object_ref (with_object);
	rfd->thread_func = thread_func;
	rfd->idle_func = idle_func;
	rfd->finish_idle_func = NULL;
	rfd->user_data = user_data;
	rfd->free_user_data = free_user_data;
	rfd->error = NULL;
	rfd->run_modal = run_modal;

	g_signal_connect (dialog, "response", G_CALLBACK (run_with_feedback_response_cb), rfd);

	if (run_modal) {
		GThread *thread;
		GCancellable *cancellable;

		cancellable = g_object_ref (rfd->cancellable);

		thread = g_thread_new (NULL, run_with_feedback_thread, rfd);
		g_thread_unref (thread);

		gtk_dialog_run (GTK_DIALOG (dialog));

		g_cancellable_cancel (cancellable);
		g_object_unref (cancellable);
	} else {
		GThread *thread;

		gtk_widget_show (dialog);

		thread = g_thread_new (NULL, run_with_feedback_thread, rfd);
		g_thread_unref (thread);
	}
}

void
e_ews_config_utils_run_in_thread_with_feedback (GtkWindow *parent,
                                                GObject *with_object,
                                                const gchar *description,
                                                EEwsSetupFunc thread_func,
                                                EEwsSetupFunc idle_func,
                                                gpointer user_data,
                                                GDestroyNotify free_user_data)
{
	e_ews_config_utils_run_in_thread_with_feedback_general (parent, with_object, description, thread_func, idle_func, user_data, free_user_data, FALSE);
}

void
e_ews_config_utils_run_in_thread_with_feedback_modal (GtkWindow *parent,
                                                      GObject *with_object,
                                                      const gchar *description,
                                                      EEwsSetupFunc thread_func,
                                                      EEwsSetupFunc idle_func,
                                                      gpointer user_data,
                                                      GDestroyNotify free_user_data)
{
	e_ews_config_utils_run_in_thread_with_feedback_general (parent, with_object, description, thread_func, idle_func, user_data, free_user_data, TRUE);
}

void
e_ews_config_utils_run_in_thread (GObject *with_object,
				  EEwsSetupFunc thread_func,
				  EEwsSetupFunc idle_func,
				  gpointer user_data,
				  GDestroyNotify free_user_data,
				  GCancellable *cancellable)
{
	struct RunWithFeedbackData *rfd;
	GThread *thread;

	g_return_if_fail (with_object != NULL);
	g_return_if_fail (thread_func != NULL);

	rfd = g_slice_new0 (struct RunWithFeedbackData);
	rfd->parent = NULL;
	rfd->dialog = NULL;
	rfd->cancellable = cancellable ? g_object_ref (cancellable) : g_cancellable_new ();
	rfd->with_object = g_object_ref (with_object);
	rfd->thread_func = thread_func;
	rfd->idle_func = NULL;
	rfd->finish_idle_func = idle_func;
	rfd->user_data = user_data;
	rfd->free_user_data = free_user_data;
	rfd->error = NULL;
	rfd->run_modal = FALSE;

	thread = g_thread_new (NULL, run_with_feedback_thread, rfd);
	g_thread_unref (thread);
}

typedef struct _TryCredentialsData {
	CamelEwsSettings *ews_settings;
	const gchar *connect_url;
	EEwsConfigUtilTryCredentialsFunc try_credentials_func;
	gpointer user_data;
	EEwsConnection *conn;
} TryCredentialsData;

static gboolean
ews_config_utils_try_credentials_sync (ECredentialsPrompter *prompter,
				       ESource *source,
				       const ENamedParameters *credentials,
				       gboolean *out_authenticated,
				       gpointer user_data,
				       GCancellable *cancellable,
				       GError **error)
{
	TryCredentialsData *data = user_data;
	ESourceAuthenticationResult auth_result;
	gchar *hosturl;
	gboolean res = TRUE;

	hosturl = camel_ews_settings_dup_hosturl (data->ews_settings);
	data->conn = e_ews_connection_new (source, data->connect_url ? data->connect_url : hosturl, data->ews_settings);
	g_free (hosturl);

	e_ews_connection_update_credentials (data->conn, credentials);

	if (data->try_credentials_func)
		auth_result = data->try_credentials_func (data->conn, credentials, data->user_data, cancellable, error);
	else
		auth_result = e_ews_connection_try_credentials_sync (data->conn, credentials, NULL, NULL, NULL, cancellable, error);

	if (auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		*out_authenticated = TRUE;
	} else if (auth_result == E_SOURCE_AUTHENTICATION_REJECTED) {
		*out_authenticated = FALSE;
		g_clear_object (&data->conn);
		g_clear_error (error);
	} else {
		res = FALSE;
		g_clear_object (&data->conn);
	}

	return res;
}

EEwsConnection	*
e_ews_config_utils_open_connection_for (ESource *source,
                                        CamelEwsSettings *ews_settings,
					const gchar *connect_url,
					EEwsConfigUtilTryCredentialsFunc try_credentials_func,
					gpointer user_data,
                                        GCancellable *cancellable,
                                        GError **perror)
{
	EEwsConnection *conn = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (ews_settings != NULL, NULL);

	/* use the one from mailer, if there, otherwise open new */
	conn = e_ews_connection_find (
		connect_url && *connect_url ? connect_url : camel_ews_settings_get_hosturl (ews_settings),
		ews_settings);
	if (conn) {
		if (try_credentials_func &&
		    try_credentials_func (conn, NULL, user_data, cancellable, perror) != E_SOURCE_AUTHENTICATION_ACCEPTED) {
			g_clear_object (&conn);
		}
		return conn;
	}

	while (!conn && !g_cancellable_is_cancelled (cancellable) && !local_error) {
		if (e_ews_connection_utils_get_without_password (ews_settings)) {
			ESourceAuthenticationResult result;
			gchar *hosturl;

			hosturl = camel_ews_settings_dup_hosturl (ews_settings);
			conn = e_ews_connection_new (source, connect_url && *connect_url ? connect_url : hosturl, ews_settings);
			g_free (hosturl);

			e_ews_connection_update_credentials (conn, NULL);

			if (try_credentials_func)
				result = try_credentials_func (conn, NULL, user_data, cancellable, &local_error);
			else
				result = e_ews_connection_try_credentials_sync (conn, NULL, NULL, NULL, NULL, cancellable, &local_error);

			if (result != E_SOURCE_AUTHENTICATION_ACCEPTED) {
				g_clear_object (&conn);
				if (result != E_SOURCE_AUTHENTICATION_REJECTED || local_error)
					break;
			}
		}

		if (!conn) {
			EShell *shell;
			TryCredentialsData data;

			e_ews_connection_utils_force_off_ntlm_auth_check ();
			g_clear_error (&local_error);

			shell = e_shell_get_default ();

			data.ews_settings = g_object_ref (ews_settings);
			data.connect_url = connect_url && *connect_url ? connect_url : NULL;
			data.try_credentials_func = try_credentials_func;
			data.user_data = user_data;
			data.conn = NULL;

			e_credentials_prompter_loop_prompt_sync (e_shell_get_credentials_prompter (shell),
				source, E_CREDENTIALS_PROMPTER_PROMPT_FLAG_ALLOW_SOURCE_SAVE,
				ews_config_utils_try_credentials_sync, &data, cancellable, &local_error);

			if (data.conn)
				conn = g_object_ref (data.conn);

			g_clear_object (&data.ews_settings);
			g_clear_object (&data.conn);
		}
	}

	if (local_error)
		g_propagate_error (perror, local_error);

	return conn;
}

enum {
	COL_FOLDER_ICON = 0,	/* G_TYPE_STRING */
	COL_FOLDER_NAME,	/* G_TYPE_STRING */
	COL_FOLDER_SIZE,	/* G_TYPE_STRING */
	COL_FOLDER_FLAGS,	/* G_TYPE_UINT */
	N_COLUMNS
};

typedef struct
{
	GtkDialog *dialog;
	GtkGrid *spinner_grid;

	ESourceRegistry *registry;
	ESource *source;
	CamelEwsSettings *ews_settings;
	CamelEwsStore *ews_store;

	GHashTable *folder_sizes;
	GCancellable *cancellable;
	GError *error;
} FolderSizeDialogData;

static gint
folder_tree_model_sort (GtkTreeModel *model,
			GtkTreeIter *a,
			GtkTreeIter *b,
			gpointer unused)
{
	gchar *aname, *bname;
	guint32 aflags, bflags;
	gint ret = -2;

	gtk_tree_model_get (
		model, a,
		COL_FOLDER_NAME, &aname,
		COL_FOLDER_FLAGS, &aflags,
		-1);

	gtk_tree_model_get (
		model, b,
		COL_FOLDER_NAME, &bname,
		COL_FOLDER_FLAGS, &bflags,
		-1);

	/* Inbox is always first. */
	if ((aflags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX)
		ret = -1;
	else if ((bflags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX)
		ret = 1;
	else {
		if (aname != NULL && bname != NULL)
			ret = g_utf8_collate (aname, bname);
		else if (aname == bname)
			ret = 0;
		else if (aname == NULL)
			ret = -1;
		else
			ret = 1;
	}

	g_free (aname);
	g_free (bname);

	return ret;
}

static void
folder_sizes_tree_populate (GtkTreeStore *store,
			    CamelFolderInfo *folder_info,
			    GtkTreeIter *parent,
			    FolderSizeDialogData *fsd)
{
	while (folder_info != NULL) {
		GtkTreeIter iter;
		const gchar *icon_name;
		const gchar *folder_size;

		icon_name = em_folder_utils_get_icon_name (folder_info->flags);
		if (g_strcmp0 (icon_name, "folder") == 0) {
			CamelFolder *folder;

			folder = camel_store_get_folder_sync (
				CAMEL_STORE (fsd->ews_store), folder_info->full_name, 0, NULL, NULL);

			if (folder != NULL) {
				if (em_utils_folder_is_drafts (fsd->registry, folder))
					icon_name = "accessories-text-editor";

				g_object_unref (folder);
			}
		}

		folder_size = g_hash_table_lookup (fsd->folder_sizes, folder_info->full_name);

		gtk_tree_store_append (store, &iter, parent);
		gtk_tree_store_set (store, &iter,
				COL_FOLDER_ICON, icon_name,
				COL_FOLDER_NAME, folder_info->display_name,
				COL_FOLDER_SIZE, folder_size,
				COL_FOLDER_FLAGS, folder_info->flags,
				-1);

		if (folder_info->child != NULL)
			folder_sizes_tree_populate (store, folder_info->child, &iter, fsd);

		folder_info = folder_info->next;
	}
}

static gboolean
ews_settings_get_folder_sizes_idle (gpointer user_data)
{
	GtkWidget *widget;
	GtkCellRenderer *renderer;
	GtkTreeStore *tree_store;
	GtkBox *content_area;
	FolderSizeDialogData *fsd = user_data;
	CamelFolderInfo *root;

	g_return_val_if_fail (fsd != NULL, FALSE);

	if (g_cancellable_is_cancelled (fsd->cancellable))
		goto cleanup;

	/* Hide progress bar. Set status */
	gtk_widget_destroy (GTK_WIDGET (fsd->spinner_grid));

	if (fsd->folder_sizes != NULL) {
		GtkWidget *scrolledwindow, *tree_view;

		scrolledwindow = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_widget_show (scrolledwindow);

		/* Tree View */
		tree_view =  gtk_tree_view_new ();
		renderer = gtk_cell_renderer_pixbuf_new ();
		gtk_tree_view_insert_column_with_attributes (
			GTK_TREE_VIEW (tree_view),
			-1,
			NULL,
			renderer,
			"icon-name",
			COL_FOLDER_ICON,
			NULL);

		renderer = gtk_cell_renderer_text_new ();
		gtk_tree_view_insert_column_with_attributes (
			GTK_TREE_VIEW (tree_view),
			-1,
			_("Folder"),
			renderer,
			"text",
			COL_FOLDER_NAME,
			NULL);

		renderer = gtk_cell_renderer_text_new ();
		gtk_tree_view_insert_column_with_attributes (
			GTK_TREE_VIEW (tree_view),
			-1,
			_("Size"),
			renderer,
			"text",
			COL_FOLDER_SIZE,
			NULL);

		/* Model for TreeView */
		tree_store = gtk_tree_store_new (
			N_COLUMNS,
			/* COL_FOLDER_ICON */ G_TYPE_STRING,
			/* COL_FOLDER_NAME */ G_TYPE_STRING,
			/* COL_FOLDER_SIZE */ G_TYPE_STRING,
			/* COL_FOLDER_FLAGS */ G_TYPE_UINT);

		gtk_tree_sortable_set_default_sort_func (
			GTK_TREE_SORTABLE (tree_store),
			folder_tree_model_sort, NULL, NULL);

		gtk_tree_sortable_set_sort_column_id (
			GTK_TREE_SORTABLE (tree_store),
			GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
			GTK_SORT_ASCENDING);

		gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), GTK_TREE_MODEL (tree_store));

		root = camel_store_get_folder_info_sync (
			CAMEL_STORE (fsd->ews_store), NULL,
			CAMEL_STORE_FOLDER_INFO_RECURSIVE,
			NULL, NULL);

		folder_sizes_tree_populate (tree_store, root, NULL, fsd);

		camel_folder_info_free (root);

		gtk_tree_view_expand_all (GTK_TREE_VIEW (tree_view));
		gtk_container_add (GTK_CONTAINER (scrolledwindow), tree_view);
		widget = scrolledwindow;
	} else if (fsd->error) {
		gchar *msg = g_strconcat (_("Unable to retrieve folder size information"), "\n",
				fsd->error->message, NULL);
		widget = gtk_label_new (msg);
		g_free (msg);
	} else {
		widget = gtk_label_new (_("Unable to retrieve folder size information"));
	}

	gtk_widget_show_all (widget);

	/* Pack into content_area */
	content_area = GTK_BOX (gtk_dialog_get_content_area (fsd->dialog));
	gtk_box_pack_start (content_area, widget, TRUE, TRUE, 6);

cleanup:
	g_hash_table_destroy (fsd->folder_sizes);
	g_object_unref (fsd->registry);
	g_object_unref (fsd->source);
	g_object_unref (fsd->ews_settings);
	g_object_unref (fsd->ews_store);
	g_object_unref (fsd->cancellable);
	g_clear_error (&fsd->error);
	g_slice_free (FolderSizeDialogData, fsd);

	return FALSE;
}

static gpointer
ews_settings_get_folder_sizes_thread (gpointer user_data)
{
	FolderSizeDialogData *fsd = user_data;
	EEwsConnection *cnc;

	g_return_val_if_fail (fsd != NULL, NULL);

	cnc = e_ews_config_utils_open_connection_for (
			fsd->source,
			fsd->ews_settings,
			NULL, NULL, NULL,
			fsd->cancellable,
			&fsd->error);

	if (cnc) {
		EEwsAdditionalProps *add_props;
		EEwsExtendedFieldURI *ext_uri;
		GSList *ids, *l, *folders_ids = NULL, *folders_list = NULL;

		fsd->folder_sizes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

		/* Use MAPI property to retrieve folder size */
		add_props = e_ews_additional_props_new ();

		/* PidTagMessageSizeExtended */
		ext_uri = e_ews_extended_field_uri_new ();
		ext_uri->prop_tag = g_strdup_printf ("%d", 0x0e08);
		ext_uri->prop_type = g_strdup ("Long");
		add_props->extended_furis = g_slist_prepend (add_props->extended_furis, ext_uri);

		ids = camel_ews_store_summary_get_folders (fsd->ews_store->summary, NULL, FALSE);
		for (l = ids; l != NULL; l = l->next) {
			EwsFolderId *fid;
			fid = e_ews_folder_id_new (l->data, NULL, FALSE);
			folders_ids = g_slist_prepend (folders_ids, fid);
		}
		folders_ids = g_slist_reverse (folders_ids);

		e_ews_connection_get_folder_sync (
				cnc, EWS_PRIORITY_MEDIUM, "Default",
				add_props, folders_ids, &folders_list,
				fsd->cancellable, &fsd->error);

		for (l = folders_list; l != NULL; l = l->next) {
			const EEwsFolder *folder = l->data;
			const EwsFolderId *folder_id;
			gchar *folder_full_name;
			gchar *folder_size;

			if (!folder || e_ews_folder_is_error (folder))
				continue;

			folder_id = e_ews_folder_get_id (folder);
			if (!folder_id)
				continue;

			folder_full_name = camel_ews_store_summary_get_folder_full_name (
				fsd->ews_store->summary, folder_id->id, NULL);
			folder_size = g_format_size_full (e_ews_folder_get_size (folder), G_FORMAT_SIZE_IEC_UNITS);

			g_hash_table_insert (fsd->folder_sizes, folder_full_name, folder_size);
		}

		g_slist_free_full (folders_list, g_object_unref);
		g_slist_free_full (folders_ids, (GDestroyNotify) e_ews_folder_id_free);
		g_slist_free_full (ids, g_free);
		e_ews_additional_props_free (add_props);
		g_object_unref (cnc);
	}

	g_idle_add (ews_settings_get_folder_sizes_idle, fsd);

	return NULL;
}

static void
folder_sizes_dialog_response_cb (GObject *dialog,
				 gint response_id,
				 gpointer data)
{
	GCancellable *cancellable = data;

	g_cancellable_cancel (cancellable);
	g_object_unref (cancellable);

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

void
e_ews_config_utils_run_folder_sizes_dialog (GtkWindow *parent,
					    ESourceRegistry *registry,
					    ESource *source,
					    CamelEwsStore *ews_store)
{
	GtkBox *content_area;
	GtkWidget *spinner, *alignment, *dialog;
	GtkWidget *spinner_label;
	GCancellable *cancellable;
	GThread *thread;
	FolderSizeDialogData *fsd;

	g_return_if_fail (ews_store != NULL);

	cancellable = g_cancellable_new ();

	dialog = gtk_dialog_new_with_buttons (
			_("Folder Sizes"),
			parent,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			_("_Close"), GTK_RESPONSE_ACCEPT,
			NULL);

	g_signal_connect (dialog, "response", G_CALLBACK (folder_sizes_dialog_response_cb), cancellable);

	fsd = g_slice_new0 (FolderSizeDialogData);
	fsd->dialog = GTK_DIALOG (dialog);

	gtk_window_set_default_size (GTK_WINDOW (fsd->dialog), 250, 300);

	content_area = GTK_BOX (gtk_dialog_get_content_area (fsd->dialog));

	spinner = e_spinner_new ();
	e_spinner_start (E_SPINNER (spinner));
	spinner_label = gtk_label_new (_("Fetching folder list…"));

	fsd->spinner_grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_column_spacing (fsd->spinner_grid, 6);
	gtk_grid_set_column_homogeneous (fsd->spinner_grid, FALSE);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (fsd->spinner_grid), GTK_ORIENTATION_HORIZONTAL);

	alignment = gtk_alignment_new (1.0, 0.5, 0.0, 1.0);
	gtk_container_add (GTK_CONTAINER (alignment), spinner);
	gtk_misc_set_alignment (GTK_MISC (spinner_label), 0.0, 0.5);

	gtk_container_add (GTK_CONTAINER (fsd->spinner_grid), alignment);
	gtk_container_add (GTK_CONTAINER (fsd->spinner_grid), spinner_label);

	/* Pack the TreeView into dialog's content area */
	gtk_box_pack_start (content_area, GTK_WIDGET (fsd->spinner_grid), TRUE, TRUE, 6);
	gtk_widget_show_all (GTK_WIDGET (fsd->dialog));

	fsd->registry = g_object_ref (registry);
	fsd->source = g_object_ref (source);
	fsd->ews_store = g_object_ref (ews_store);
	fsd->ews_settings = CAMEL_EWS_SETTINGS (camel_service_ref_settings (CAMEL_SERVICE (ews_store)));
	fsd->cancellable = g_object_ref (cancellable);

	thread = g_thread_new (NULL, ews_settings_get_folder_sizes_thread, fsd);
	g_thread_unref (thread);

	/* Start the dialog */
	gtk_widget_show (GTK_WIDGET (dialog));
}

static void
action_global_subscribe_foreign_folder_cb (EUIAction *action,
					   GVariant *parameter,
					   gpointer user_data)
{
	EShellView *shell_view = user_data;
	EShell *shell;
	EShellBackend *shell_backend;
	EShellWindow *shell_window;
	EClientCache *client_cache;
	CamelSession *session = NULL;

	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));

	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);
	shell_backend = e_shell_get_backend_by_name (shell, "mail");

	if (shell_backend)
		g_object_get (G_OBJECT (shell_backend), "session", &session, NULL);

	if (!session)
		return;

	client_cache = e_shell_get_client_cache (shell);

	e_ews_subscribe_foreign_folder (GTK_WINDOW (shell_window), session, NULL, client_cache);

	g_object_unref (session);
}

static const EUIActionEntry global_ews_mail_entries[] = {
	{ "ews-mail-global-subscribe-foreign-folder",
	  NULL,
	  N_("Subscribe to folder of other EWS user…"),
	  NULL,
	  NULL,
	  action_global_subscribe_foreign_folder_cb, NULL, NULL, NULL }
};

static const EUIActionEntry global_ews_book_entries[] = {
	{ "ews-contact-global-subscribe-foreign-folder",
	  NULL,
	  N_("Subscribe to folder of other EWS user…"),
	  NULL,
	  NULL,
	  action_global_subscribe_foreign_folder_cb, NULL, NULL, NULL }
};

static const EUIActionEntry global_ews_cal_entries[] = {
	{ "ews-calendar-global-subscribe-foreign-folder",
	  NULL,
	  N_("Subscribe to folder of other EWS user…"),
	  NULL,
	  NULL,
	  action_global_subscribe_foreign_folder_cb, NULL, NULL, NULL }
};

static const EUIActionEntry global_ews_task_entries[] = {
	{ "ews-task-global-subscribe-foreign-folder",
	  NULL,
	  N_("Subscribe to folder of other EWS user…"),
	  NULL,
	  NULL,
	  action_global_subscribe_foreign_folder_cb, NULL, NULL, NULL }
};

static const EUIActionEntry global_ews_memo_entries[] = {
	{ "ews-memo-global-subscribe-foreign-folder",
	  NULL,
	  N_("Subscribe to folder of other EWS user…"),
	  NULL,
	  NULL,
	  action_global_subscribe_foreign_folder_cb, NULL, NULL, NULL }
};

static gboolean
ews_ui_has_ews_account (EShellView *shell_view,
			CamelSession *in_session)
{
	CamelSession *session = in_session;
	EShell *shell;
	gboolean has_any = FALSE;

	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), FALSE);
	if (in_session)
		g_return_val_if_fail (CAMEL_IS_SESSION (in_session), FALSE);

	shell = e_shell_window_get_shell (e_shell_view_get_shell_window (shell_view));

	if (!session) {
		EShellBackend *shell_backend;

		shell_backend = e_shell_get_backend_by_name (shell, "mail");

		if (shell_backend) {
			g_object_get (G_OBJECT (shell_backend), "session", &session, NULL);
		}
	}

	if (session) {
		ESourceRegistry *registry;
		GList *services, *link;

		registry = e_shell_get_registry (shell);
		services = camel_session_list_services (session);

		for (link = services; link && !has_any; link = g_list_next (link)) {
			CamelService *service = link->data;

			if (CAMEL_IS_EWS_STORE (service)) {
				ESource *source;

				source = e_source_registry_ref_source (registry, camel_service_get_uid (service));
				has_any = source && e_source_registry_check_enabled (registry, source);

				g_clear_object (&source);
			}
		}

		g_list_free_full (services, g_object_unref);
	}

	if (session && session != in_session)
		g_object_unref (session);

	return has_any;
}

static gboolean
get_ews_store_from_folder_tree (EShellView *shell_view,
                                gchar **pfolder_path,
                                CamelStore **pstore)
{
	EShellSidebar *shell_sidebar;
	EMFolderTree *folder_tree;
	gchar *selected_path = NULL;
	CamelStore *selected_store = NULL;
	gboolean found = FALSE;

	/* Get hold of Folder Tree */
	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
	g_object_get (shell_sidebar, "folder-tree", &folder_tree, NULL);
	if (em_folder_tree_get_selected (folder_tree, &selected_store, &selected_path) ||
	    em_folder_tree_store_root_selected (folder_tree, &selected_store)) {
		if (selected_store) {
			CamelProvider *provider = camel_service_get_provider (CAMEL_SERVICE (selected_store));

			if (provider && g_ascii_strcasecmp (provider->protocol, "ews") == 0) {
				found = TRUE;

				if (pstore)
					*pstore = g_object_ref (selected_store);

				if (pfolder_path)
					*pfolder_path = selected_path;
				else
					g_free (selected_path);

				selected_path = NULL;
			}

			g_object_unref (selected_store);
		}

		g_free (selected_path);
	}

	g_object_unref (folder_tree);

	return found;
}

static void
action_folder_sizes_cb (EUIAction *action,
			GVariant *parameter,
			gpointer user_data)
{
	EShellView *shell_view = user_data;
	GtkWindow *parent;
	CamelSession *session;
	CamelStore *store = NULL;
	ESourceRegistry *registry;
	ESource *source;

	if (!get_ews_store_from_folder_tree (shell_view, NULL, &store))
		return;

	g_return_if_fail (store != NULL);

	parent = GTK_WINDOW (e_shell_view_get_shell_window (shell_view));

	session = camel_service_ref_session (CAMEL_SERVICE (store));
	registry = e_mail_session_get_registry (E_MAIL_SESSION (session));
	source = e_source_registry_ref_source (registry, camel_service_get_uid (CAMEL_SERVICE (store)));

	e_ews_config_utils_run_folder_sizes_dialog (parent, registry, source, CAMEL_EWS_STORE (store));

	g_object_unref (source);
	g_object_unref (session);
	g_object_unref (store);
}

static void
action_subscribe_foreign_folder_cb (EUIAction *action,
				    GVariant *parameter,
				    gpointer user_data)
{
	EShellView *shell_view = user_data;
	GtkWindow *parent;
	EShell *shell;
	EShellBackend *backend;
	EClientCache *client_cache;
	CamelSession *session = NULL;
	CamelStore *store = NULL;

	if (!get_ews_store_from_folder_tree (shell_view, NULL, &store))
		return;

	parent = GTK_WINDOW (e_shell_view_get_shell_window (shell_view));
	backend = e_shell_view_get_shell_backend (shell_view);
	g_object_get (G_OBJECT (backend), "session", &session, NULL);

	shell = e_shell_backend_get_shell (backend);
	client_cache = e_shell_get_client_cache (shell);

	e_ews_subscribe_foreign_folder (parent, session, store, client_cache);

	g_object_unref (session);
	g_object_unref (store);
}

static void
action_folder_permissions_mail_cb (EUIAction *action,
				   GVariant *parameter,
				   gpointer user_data)
{
	EShellView *shell_view = user_data;
	EShell *shell;
	EShellWindow *shell_window;
	ESourceRegistry *registry;
	GtkWindow *parent;
	CamelStore *store = NULL;
	CamelEwsStore *ews_store;
	EwsFolderId *folder_id = NULL;
	gchar *folder_path = NULL;

	if (!get_ews_store_from_folder_tree (shell_view, &folder_path, &store))
		return;

	ews_store = CAMEL_EWS_STORE (store);
	g_return_if_fail (ews_store != NULL);

	shell_window = e_shell_view_get_shell_window (shell_view);
	parent = GTK_WINDOW (shell_window);
	shell = e_shell_window_get_shell (shell_window);
	registry = e_shell_get_registry (shell);

	if (folder_path && *folder_path) {
		gchar *str_folder_id = NULL;

		str_folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_path);
		if (!str_folder_id) {
			e_notice (parent, GTK_MESSAGE_ERROR, _("Cannot edit permissions of folder “%s”, choose other folder."), folder_path);
		} else {
			gchar *str_change_key;

			str_change_key = camel_ews_store_summary_get_change_key (
				ews_store->summary, str_folder_id, NULL);

			folder_id = e_ews_folder_id_new (str_folder_id, str_change_key, FALSE);

			g_free (str_change_key);
		}

		g_free (str_folder_id);
	} else {
		g_clear_pointer (&folder_path, g_free);

		folder_id = e_ews_folder_id_new ("msgfolderroot", NULL, TRUE);
	}

	if (folder_id) {
		ESource *source;
		CamelService *service;
		CamelSettings *settings;
		const gchar *uid;

		service = CAMEL_SERVICE (store);
		uid = camel_service_get_uid (service);
		source = e_source_registry_ref_source (registry, uid);
		if (source) {
			settings = camel_service_ref_settings (service);

			e_ews_edit_folder_permissions (
				parent,
				registry,
				source,
				CAMEL_EWS_SETTINGS (settings),
				camel_service_get_display_name (service),
				folder_path ? folder_path : camel_service_get_display_name (service),
				folder_id,
				E_EWS_FOLDER_TYPE_MAILBOX);

			g_object_unref (settings);
			g_object_unref (source);
		} else {
			g_warn_if_reached ();
		}
	}

	g_object_unref (store);
	g_free (folder_path);
	e_ews_folder_id_free (folder_id);
}

static void
ews_ui_enable_actions (EUIActionGroup *action_group,
                       const EUIActionEntry *entries,
                       guint n_entries,
                       gboolean can_show,
                       gboolean is_online)
{
	gint ii;

	g_return_if_fail (action_group != NULL);
	g_return_if_fail (entries != NULL);

	for (ii = 0; ii < n_entries; ii++) {
		EUIAction *action;

		action = e_ui_action_group_get_action (action_group, entries[ii].name);
		if (!action)
			continue;

		e_ui_action_set_visible (action, can_show);
		if (can_show)
			e_ui_action_set_sensitive (action, is_online);
	}
}

static const EUIActionEntry mail_account_context_entries[] = {
	{ "mail-ews-folder-sizes",
	  NULL,
	  N_("Folder Sizes…"),
	  NULL,
	  NULL,
	  action_folder_sizes_cb, NULL, NULL, NULL },

	{ "mail-ews-subscribe-foreign-folder",
	  NULL,
	  N_("Subscribe to folder of other user…"),
	  NULL,
	  NULL,
	  action_subscribe_foreign_folder_cb, NULL, NULL, NULL }
};

static const EUIActionEntry mail_folder_context_entries[] = {
	{ "mail-ews-folder-permissions",
	  "folder-new",
	  N_("Permissions…"),
	  NULL,
	  N_("Edit EWS folder permissions"),
	  action_folder_permissions_mail_cb, NULL, NULL, NULL }
};

static void
ews_ui_update_actions_mail_cb (EShellView *shell_view,
			       gpointer user_data)
{
	EShellBackend *backend;
	CamelSession *session = NULL;
	EUIActionGroup *action_group;
	EUIManager *ui_manager;
	EShellSidebar *shell_sidebar;
	EMFolderTree *folder_tree;
	CamelStore *selected_store = NULL;
	gchar *selected_path = NULL;
	gboolean account_node = FALSE, folder_node = FALSE;
	gboolean online, has_ews_account;

	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
	g_object_get (shell_sidebar, "folder-tree", &folder_tree, NULL);
	if (em_folder_tree_get_selected (folder_tree, &selected_store, &selected_path) ||
	    em_folder_tree_store_root_selected (folder_tree, &selected_store)) {
		if (selected_store) {
			CamelProvider *provider = camel_service_get_provider (CAMEL_SERVICE (selected_store));

			if (provider && g_ascii_strcasecmp (provider->protocol, "ews") == 0) {
				account_node = !selected_path || !*selected_path;
				folder_node = !account_node;
			}

			g_object_unref (selected_store);
		}
	}
	g_object_unref (folder_tree);

	g_free (selected_path);

	ui_manager = e_shell_view_get_ui_manager (shell_view);
	action_group = e_ui_manager_get_action_group (ui_manager, "mail");

	backend = e_shell_view_get_shell_backend (shell_view);
	g_object_get (G_OBJECT (backend), "session", &session, NULL);

	online = session && camel_session_get_online (session);

	has_ews_account = account_node || folder_node || ews_ui_has_ews_account (shell_view, session);

	if (session)
		g_object_unref (session);

	ews_ui_enable_actions (action_group, mail_account_context_entries, G_N_ELEMENTS (mail_account_context_entries), account_node, online);
	ews_ui_enable_actions (action_group, mail_folder_context_entries, G_N_ELEMENTS (mail_folder_context_entries), account_node || folder_node, online);
	ews_ui_enable_actions (action_group, global_ews_mail_entries, G_N_ELEMENTS (global_ews_mail_entries), has_ews_account, online);
}

static void
ews_ui_init_mail (EShellView *shell_view)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='main-menu'>"
		    "<submenu action='file-menu'>"
		      "<placeholder id='long-running-actions'>"
			"<item action='ews-mail-global-subscribe-foreign-folder'/>"
		      "</placeholder>"
		    "</submenu>"
		  "</menu>"
		  "<menu id='mail-folder-popup'>"
		    "<placeholder id='mail-folder-popup-actions'>"
		      "<item action='mail-ews-folder-sizes'/>"
		      "<item action='mail-ews-subscribe-foreign-folder'/>"
		      "<item action='mail-ews-folder-permissions'/>"
		    "</placeholder>"
		  "</menu>"
		"</eui>";

	EUIManager *ui_manager;

	ui_manager = e_shell_view_get_ui_manager (shell_view);

	e_ui_manager_add_actions (ui_manager, "mail", GETTEXT_PACKAGE,
		mail_account_context_entries, G_N_ELEMENTS (mail_account_context_entries), shell_view);
	e_ui_manager_add_actions (ui_manager, "mail", GETTEXT_PACKAGE,
		mail_folder_context_entries, G_N_ELEMENTS (mail_folder_context_entries), shell_view);
	e_ui_manager_add_actions_with_eui_data (ui_manager, "mail", GETTEXT_PACKAGE,
		global_ews_mail_entries, G_N_ELEMENTS (global_ews_mail_entries), shell_view, eui);

	/* Decide whether we want this option to be visible or not */
	g_signal_connect (shell_view, "update-actions", G_CALLBACK (ews_ui_update_actions_mail_cb), NULL);
}

static gboolean
get_selected_ews_source (EShellView *shell_view,
                         ESource **selected_source,
                         ESourceRegistry **registry)
{
	ESource *source;
	EShellSidebar *shell_sidebar;
	ESourceSelector *selector = NULL;

	g_return_val_if_fail (shell_view != NULL, FALSE);

	shell_sidebar = e_shell_view_get_shell_sidebar (shell_view);
	g_return_val_if_fail (shell_sidebar != NULL, FALSE);

	g_object_get (shell_sidebar, "selector", &selector, NULL);
	g_return_val_if_fail (selector != NULL, FALSE);

	source = e_source_selector_ref_primary_selection (selector);
	if (source) {
		ESourceBackend *backend_ext = NULL;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK))
			backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_ADDRESS_BOOK);
		else if (e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR))
			backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR);
		else if (e_source_has_extension (source, E_SOURCE_EXTENSION_MEMO_LIST))
			backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_MEMO_LIST);
		else if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
			backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST);
		else if (e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT))
			backend_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);

		if (!backend_ext ||
		    g_strcmp0 (e_source_backend_get_backend_name (backend_ext), "ews") != 0) {
			g_object_unref (source);
			source = NULL;
		}
	}

	if (source && registry)
		*registry = g_object_ref (e_source_selector_get_registry (selector));

	g_object_unref (selector);

	if (selected_source)
		*selected_source = source;
	else if (source)
		g_object_unref (source);

	return source != NULL;
}

/* how many menu entries are defined; all calendar/tasks/memos/contacts
 * actions should have same count */
#define EWS_ESOURCE_NUM_ENTRIES 1

static void
update_ews_source_entries_cb (EShellView *shell_view,
                              const EUIActionEntry *entries)
{
	EUIActionGroup *action_group;
	EShell *shell;
	EShellWindow *shell_window;
	ESource *source = NULL;
	const EUIActionEntry *global_entries = NULL;
	guint n_global_entries = 0;
	const gchar *group;
	gboolean is_ews_source, is_online;

	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));
	g_return_if_fail (entries != NULL);

	if (strstr (entries->name, "calendar")) {
		group = "calendar";
		global_entries = global_ews_cal_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_cal_entries);
	} else if (strstr (entries->name, "tasks")) {
		group = "tasks";
		global_entries = global_ews_task_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_task_entries);
	} else if (strstr (entries->name, "memos")) {
		group = "memos";
		global_entries = global_ews_memo_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_memo_entries);
	} else if (strstr (entries->name, "contacts")) {
		group = "contacts";
		global_entries = global_ews_book_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_book_entries);
	} else {
		g_return_if_reached ();
	}

	is_ews_source = get_selected_ews_source (shell_view, &source, NULL);

	if (is_ews_source) {
		if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER))
			is_ews_source = FALSE;

		if (is_ews_source) {
			ESource *clicked_source = NULL;

			g_object_get (G_OBJECT (shell_view), "clicked-source", &clicked_source, NULL);

			if (clicked_source && clicked_source != source)
				is_ews_source = FALSE;

			g_clear_object (&clicked_source);
		}

		if (is_ews_source) {
			ESourceEwsFolder *ews_folder = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);

			/* Require both ChangeKey and folder's Id, but GAL can have a ':' in the Id,
			   which should be ignored, because it's not a valid folder Id. */
			if (!e_source_ews_folder_get_id (ews_folder) ||
			    g_strcmp0 (e_source_ews_folder_get_id (ews_folder), "") == 0 ||
			    !e_source_ews_folder_get_change_key (ews_folder) ||
			    g_strcmp0 (e_source_ews_folder_get_change_key (ews_folder), "") == 0 ||
			    strchr (e_source_ews_folder_get_id (ews_folder), ':') != NULL)
				is_ews_source = FALSE;
		}
	}

	g_clear_object (&source);

	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);

	is_online = shell && e_shell_get_online (shell);
	action_group = e_ui_manager_get_action_group (e_shell_view_get_ui_manager (shell_view), group);

	ews_ui_enable_actions (action_group, entries, EWS_ESOURCE_NUM_ENTRIES, is_ews_source, is_online);
	ews_ui_enable_actions (action_group, global_entries, n_global_entries,
		ews_ui_has_ews_account (shell_view, NULL), is_online);
}

static void
setup_ews_source_actions (EShellView *shell_view,
			  const EUIActionEntry *entries,
			  guint n_entries,
			  const gchar *eui)
{
	EUIManager *ui_manager;
	const EUIActionEntry *global_entries = NULL;
	guint n_global_entries = 0;
	const gchar *group;

	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (entries != NULL);
	g_return_if_fail (n_entries > 0);
	g_return_if_fail (n_entries == EWS_ESOURCE_NUM_ENTRIES);

	if (strstr (entries->name, "calendar")) {
		group = "calendar";
		global_entries = global_ews_cal_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_cal_entries);
	} else if (strstr (entries->name, "tasks")) {
		group = "tasks";
		global_entries = global_ews_task_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_task_entries);
	} else if (strstr (entries->name, "memos")) {
		group = "memos";
		global_entries = global_ews_memo_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_memo_entries);
	} else if (strstr (entries->name, "contacts")) {
		group = "contacts";
		global_entries = global_ews_book_entries;
		n_global_entries = G_N_ELEMENTS (global_ews_book_entries);
	} else {
		g_return_if_reached ();
	}

	ui_manager = e_shell_view_get_ui_manager (shell_view);
	e_ui_manager_add_actions (ui_manager, group, GETTEXT_PACKAGE, entries, n_entries, shell_view);
	e_ui_manager_add_actions_with_eui_data (ui_manager, group, GETTEXT_PACKAGE, global_entries, n_global_entries, shell_view, eui);

	g_signal_connect (shell_view, "update-actions", G_CALLBACK (update_ews_source_entries_cb), (gpointer) entries);
}

static void
action_folder_permissions_source_cb (EUIAction *action,
				     GVariant *parameter,
				     gpointer user_data)
{
	EShellView *shell_view = user_data;
	ESourceRegistry *registry = NULL;
	ESource *source = NULL, *parent_source;
	ESourceEwsFolder *folder_ext;
	ESourceCamel *extension;
	CamelSettings *settings;
	const gchar *extension_name;
	const gchar *action_name;
	EwsFolderId *folder_id;
	EEwsFolderType folder_type;

	g_return_if_fail (action != NULL);
	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (get_selected_ews_source (shell_view, &source, &registry));
	g_return_if_fail (source != NULL);
	g_return_if_fail (e_source_has_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER));
	g_return_if_fail (g_action_get_name (G_ACTION (action)) != NULL);

	folder_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_EWS_FOLDER);
	folder_id = e_source_ews_folder_dup_folder_id (folder_ext);
	g_return_if_fail (folder_id != NULL);

	parent_source = e_source_registry_ref_source (registry, e_source_get_parent (source));

	extension_name = e_source_camel_get_extension_name ("ews");
	extension = e_source_get_extension (parent_source, extension_name);
	settings = e_source_camel_get_settings (extension);

	action_name = g_action_get_name (G_ACTION (action));
	folder_type = E_EWS_FOLDER_TYPE_MAILBOX;
	if (strstr (action_name, "calendar") != NULL)
		folder_type = E_EWS_FOLDER_TYPE_CALENDAR;
	else if (strstr (action_name, "contacts") != NULL)
		folder_type = E_EWS_FOLDER_TYPE_CONTACTS;
	else if (strstr (action_name, "tasks") != NULL)
		folder_type = E_EWS_FOLDER_TYPE_TASKS;

	e_ews_edit_folder_permissions (
		NULL,
		registry,
		source,
		CAMEL_EWS_SETTINGS (settings),
		e_source_get_display_name (parent_source),
		e_source_get_display_name (source),
		folder_id,
		folder_type);

	g_object_unref (source);
	g_object_unref (parent_source);
	g_object_unref (registry);
	e_ews_folder_id_free (folder_id);
}

static void
ews_ui_init_calendar (EShellView *shell_view)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='main-menu'>"
		    "<submenu action='file-menu'>"
		      "<placeholder id='long-running-actions'>"
			"<item action='ews-calendar-global-subscribe-foreign-folder'/>"
		      "</placeholder>"
		    "</submenu>"
		  "</menu>"
		  "<menu id='calendar-popup'>"
		    "<placeholder id='calendar-popup-actions'>"
		      "<item action='calendar-ews-folder-permissions'/>"
		    "</placeholder>"
		  "</menu>"
		"</eui>";

	static const EUIActionEntry entries[] = {
		{ "calendar-ews-folder-permissions",
		  "folder-new",
		  N_("Permissions…"),
		  NULL,
		  N_("Edit EWS calendar permissions"),
		  action_folder_permissions_source_cb, NULL, NULL, NULL }
	};

	setup_ews_source_actions (shell_view, entries, G_N_ELEMENTS (entries), eui);
}

static void
ews_ui_init_tasks (EShellView *shell_view)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='main-menu'>"
		    "<submenu action='file-menu'>"
		      "<placeholder id='long-running-actions'>"
			"<item action='ews-task-global-subscribe-foreign-folder'/>"
		      "</placeholder>"
		    "</submenu>"
		  "</menu>"
		  "<menu id='task-list-popup'>"
		    "<placeholder id='task-list-popup-actions'>"
		      "<item action='tasks-ews-folder-permissions'/>"
		    "</placeholder>"
		  "</menu>"
		"</eui>";

	static EUIActionEntry entries[] = {
		{ "tasks-ews-folder-permissions",
		  "folder-new",
		  N_("Permissions…"),
		  NULL,
		  N_("Edit EWS tasks permissions"),
		  action_folder_permissions_source_cb, NULL, NULL, NULL }
	};

	setup_ews_source_actions (shell_view, entries, G_N_ELEMENTS (entries), eui);
}

static void
ews_ui_init_memos (EShellView *shell_view)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='main-menu'>"
		    "<submenu action='file-menu'>"
		      "<placeholder id='long-running-actions'>"
			"<item action='ews-memo-global-subscribe-foreign-folder'/>"
		      "</placeholder>"
		    "</submenu>"
		  "</menu>"
		  "<menu id='memo-list-popup'>"
		    "<placeholder id='memo-list-popup-actions'>"
		      "<item action='memos-ews-folder-permissions'/>"
		    "</placeholder>"
		  "</menu>"
		"</eui>";

	static const EUIActionEntry entries[] = {
		{ "memos-ews-folder-permissions",
		  "folder-new",
		  N_("Permissions…"),
		  NULL,
		  N_("Edit EWS memos permissions"),
		  action_folder_permissions_source_cb, NULL, NULL, NULL }
	};

	setup_ews_source_actions (shell_view, entries, G_N_ELEMENTS (entries), eui);
}

static void
ews_ui_init_contacts (EShellView *shell_view)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='main-menu'>"
		    "<submenu action='file-menu'>"
		      "<placeholder id='long-running-actions'>"
			"<item action='ews-contact-global-subscribe-foreign-folder'/>"
		      "</placeholder>"
		    "</submenu>"
		  "</menu>"
		  "<menu id='address-book-popup'>"
		    "<placeholder id='address-book-popup-actions'>"
		      "<item action='contacts-ews-folder-permissions'/>"
		    "</placeholder>"
		  "</menu>"
		"</eui>";

	static const EUIActionEntry entries[] = {
		{ "contacts-ews-folder-permissions",
		  "folder-new",
		  N_("Permissions…"),
		  NULL,
		  N_("Edit EWS contacts permissions"),
		  action_folder_permissions_source_cb, NULL, NULL, NULL }
	};

	setup_ews_source_actions (shell_view, entries, G_N_ELEMENTS (entries), eui);
}

void
e_ews_config_utils_init_ui (EShellView *shell_view,
			    const gchar *ui_manager_id)
{
	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (ui_manager_id != NULL);

	if (g_strcmp0 (ui_manager_id, "org.gnome.evolution.mail") == 0)
		ews_ui_init_mail (shell_view);
	else if (g_strcmp0 (ui_manager_id, "org.gnome.evolution.calendars") == 0)
		ews_ui_init_calendar (shell_view);
	else if (g_strcmp0 (ui_manager_id, "org.gnome.evolution.tasks") == 0)
		ews_ui_init_tasks (shell_view);
	else if (g_strcmp0 (ui_manager_id, "org.gnome.evolution.memos") == 0)
		ews_ui_init_memos (shell_view);
	else if (g_strcmp0 (ui_manager_id, "org.gnome.evolution.contacts") == 0)
		ews_ui_init_contacts (shell_view);
}

gboolean
e_ews_config_utils_is_online (void)
{
	EShell *shell;

	shell = e_shell_get_default ();

	return shell && e_shell_get_online (shell);
}

GtkWindow *
e_ews_config_utils_get_widget_toplevel_window (GtkWidget *widget)
{
	if (!widget)
		return NULL;

	if (!GTK_IS_WINDOW (widget))
		widget = gtk_widget_get_toplevel (widget);

	if (GTK_IS_WINDOW (widget))
		return GTK_WINDOW (widget);

	return NULL;
}

static gpointer
ews_config_utils_unref_in_thread (gpointer user_data)
{
	g_object_unref (user_data);

	return NULL;
}

void
e_ews_config_utils_unref_in_thread (GObject *object)
{
	GThread *thread;

	g_return_if_fail (object != NULL);
	g_return_if_fail (G_IS_OBJECT (object));

	thread = g_thread_new (NULL, ews_config_utils_unref_in_thread, object);
	g_thread_unref (thread);
}
