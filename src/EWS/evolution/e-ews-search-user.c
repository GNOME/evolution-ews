/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "e-ews-config-utils.h"
#include "e-ews-search-user.h"

#define E_EWS_SEARCH_DLG_DATA "e-ews-search-dlg-data"

enum {
	COL_DISPLAY_NAME = 0,
	COL_EMAIL
};

struct EEwsSearchUserData
{
	EEwsConnection *conn;
	GCancellable *cancellable;
	gchar *search_text;
	GtkWidget *tree_view;
	GtkWidget *info_label;
	guint schedule_search_id;
};

static void
e_ews_search_user_data_free (gpointer ptr)
{
	struct EEwsSearchUserData *pgu = ptr;

	if (!pgu)
		return;

	if (pgu->schedule_search_id) {
		g_source_remove (pgu->schedule_search_id);
		pgu->schedule_search_id = 0;
	}
	if (pgu->cancellable) {
		g_cancellable_cancel (pgu->cancellable);
		g_object_unref (pgu->cancellable);
		pgu->cancellable = NULL;
	}
	g_object_unref (pgu->conn);
	g_free (pgu->search_text);
	g_slice_free (struct EEwsSearchUserData, pgu);
}

struct EEwsSearchUser
{
	gchar *display_name;
	gchar *email;
};

static struct EEwsSearchUser *
e_ews_search_user_new (const gchar *display_name,
                       const gchar *email)
{
	struct EEwsSearchUser *user;

	if (!display_name)
		display_name = email;

	user = g_new0 (struct EEwsSearchUser, 1);
	user->display_name = g_strdup (display_name);
	user->email = g_strdup (email);

	return user;
}

static void
e_ews_search_user_free (gpointer ptr)
{
	struct EEwsSearchUser *user = ptr;

	if (!user)
		return;

	g_free (user->display_name);
	g_free (user->email);
	g_free (user);
}

struct EEwsSearchIdleData
{
	gint ref_count;
	EEwsConnection *conn;
	gchar *search_text;
	GCancellable *cancellable;

	GObject *dialog;
	GSList *found_users; /* struct EEwsSearchUser *, for 'Mailbox' MailboxType */
	guint found_contacts; /* how many other than 'Mailbox' were found */
	gboolean includes_last_item;
};

static void
e_ews_search_idle_data_ref (struct EEwsSearchIdleData *sid)
{
	g_atomic_int_inc (&sid->ref_count);
}

static void
e_ews_search_idle_data_unref (gpointer ptr)
{
	struct EEwsSearchIdleData *sid = ptr;

	if (!sid)
		return;

	if (g_atomic_int_dec_and_test (&sid->ref_count)) {
		g_clear_object (&sid->conn);
		g_clear_object (&sid->cancellable);
		g_free (sid->search_text);
		g_slist_free_full (sid->found_users, e_ews_search_user_free);
		g_slice_free (struct EEwsSearchIdleData, sid);
	}
}

static void
empty_search_tree_view (GtkWidget *tree_view)
{
	GtkListStore *store;
	GtkTreeModel *model;

	g_return_if_fail (tree_view != NULL);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));
	g_return_if_fail (model != NULL);

	store = GTK_LIST_STORE (model);
	g_return_if_fail (store != NULL);

	gtk_list_store_clear (store);
}

static void
search_add_user (GtkListStore *store,
                 const gchar *display_name,
                 const gchar *email)
{
	GtkTreeIter iter;

	g_return_if_fail (store != NULL);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (
		store, &iter,
		COL_DISPLAY_NAME, display_name,
		COL_EMAIL, email,
		-1);
}

static gboolean
search_finish_idle (gpointer user_data)
{
	struct EEwsSearchIdleData *sid = user_data;

	g_return_val_if_fail (sid != NULL, FALSE);
	g_return_val_if_fail (sid->dialog != NULL, FALSE);

	if (!g_cancellable_is_cancelled (sid->cancellable)) {
		struct EEwsSearchUserData *pgu;
		GtkListStore *store;
		guint added = 0;
		GSList *fu;

		pgu = g_object_get_data (sid->dialog, E_EWS_SEARCH_DLG_DATA);
		g_return_val_if_fail (pgu != NULL, FALSE);
		g_return_val_if_fail (pgu->tree_view != NULL, FALSE);
		g_return_val_if_fail (pgu->info_label != NULL, FALSE);

		empty_search_tree_view (pgu->tree_view);

		store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (pgu->tree_view)));
		g_return_val_if_fail (store != NULL, FALSE);

		for (fu = sid->found_users; fu; fu = fu->next) {
			struct EEwsSearchUser *user = fu->data;

			if (!user)
				continue;

			search_add_user (store, user->display_name, user->email);

			added++;
		}

		if (!added) {
			if (sid->found_contacts > 0) {
				gchar *str;
				str = g_strdup_printf (
					dngettext (GETTEXT_PACKAGE, "No users found, only one contact", "No users found, only %d contacts",
					sid->found_contacts), sid->found_contacts);
				gtk_label_set_text (GTK_LABEL (pgu->info_label), str);
				g_free (str);
			} else {
				gtk_label_set_text (GTK_LABEL (pgu->info_label), _("No users found"));
			}
		} else if (sid->includes_last_item) {
			gchar *str;
			str = g_strdup_printf (dngettext (GETTEXT_PACKAGE, "Found one user", "Found %d users", added), added);
			gtk_label_set_text (GTK_LABEL (pgu->info_label), str);
			g_free (str);
		} else {
			gchar *str;
			str = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
				"Found more than 100 users, but showing only first %d",
				"Found more than 100 users, but showing only first %d", added), added);
			gtk_label_set_text (GTK_LABEL (pgu->info_label), str);
			g_free (str);
		}
	}

	e_ews_search_idle_data_unref (sid);

	return FALSE;
}

static gpointer
search_thread (gpointer user_data)
{
	struct EEwsSearchIdleData *sid = user_data;

	g_return_val_if_fail (sid != NULL, NULL);

	if (!g_cancellable_is_cancelled (sid->cancellable)) {
		GSList *mailboxes = NULL;
		GError *error = NULL;

		if (e_ews_connection_resolve_names_sync (
			sid->conn, EWS_PRIORITY_MEDIUM, sid->search_text,
			EWS_SEARCH_AD, NULL, FALSE, &sid->includes_last_item, &mailboxes, NULL,
			sid->cancellable, &error)) {
			GSList *iter;

			sid->found_contacts = 0;

			for (iter = mailboxes; iter != NULL; iter = iter->next) {
				EwsMailbox *mb = iter->data;

				if (!mb || !mb->email || !*mb->email
				    || g_strcmp0 (mb->mailbox_type, "Mailbox") != 0) {
					sid->found_contacts++;
					continue;
				}

				sid->found_users = g_slist_prepend (
					sid->found_users,
					e_ews_search_user_new (mb->name, mb->email));
			}

			sid->found_users = g_slist_reverse (sid->found_users);
		}

		g_slist_free_full (mailboxes, (GDestroyNotify) e_ews_mailbox_free);

		if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
		    !g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_NAMERESOLUTIONNORESULTS))
			g_warning ("%s: Failed to search user: %s", G_STRFUNC, error->message);

		g_clear_error (&error);

		g_idle_add (search_finish_idle, sid);
	} else {
		e_ews_search_idle_data_unref (sid);
	}

	return NULL;
}

static gboolean
schedule_search_cb (gpointer user_data)
{
	struct EEwsSearchIdleData *sid = user_data;

	g_return_val_if_fail (sid != NULL, FALSE);
	g_return_val_if_fail (sid->dialog != NULL, FALSE);

	if (!g_cancellable_is_cancelled (sid->cancellable)) {
		struct EEwsSearchUserData *pgu;
		GThread *thread;
		GError *error = NULL;

		pgu = g_object_get_data (sid->dialog, E_EWS_SEARCH_DLG_DATA);
		g_return_val_if_fail (pgu != NULL, FALSE);
		g_return_val_if_fail (pgu->tree_view != NULL, FALSE);

		pgu->schedule_search_id = 0;
		sid->conn = g_object_ref (pgu->conn);
		sid->search_text = g_strdup (pgu->search_text);

		e_ews_search_idle_data_ref (sid);

		thread = g_thread_try_new (NULL, search_thread, sid, &error);
		if (thread) {
			sid = NULL;
			g_thread_unref (thread);
		} else {
			g_object_unref (sid->conn);
			g_warning ("%s: Failed to create search thread: %s", G_STRFUNC, error ? error->message : "Unknown error");
		}

		g_clear_error (&error);
	}

	return FALSE;
}

static void
search_term_changed_cb (GtkEntry *entry,
                        GObject *dialog)
{
	struct EEwsSearchUserData *pgu;

	g_return_if_fail (dialog != NULL);

	pgu = g_object_get_data (dialog, E_EWS_SEARCH_DLG_DATA);
	g_return_if_fail (pgu != NULL);
	g_return_if_fail (pgu->tree_view != NULL);

	if (pgu->schedule_search_id) {
		g_source_remove (pgu->schedule_search_id);
		pgu->schedule_search_id = 0;
	}

	if (pgu->cancellable) {
		g_cancellable_cancel (pgu->cancellable);
		g_object_unref (pgu->cancellable);
	}

	pgu->cancellable = g_cancellable_new ();

	if (entry) {
		g_free (pgu->search_text);
		pgu->search_text = g_strdup (gtk_entry_get_text (entry));
	}

	empty_search_tree_view (pgu->tree_view);

	if (!pgu->search_text || !*pgu->search_text) {
		gtk_label_set_text (GTK_LABEL (pgu->info_label), _("Search for a user"));
	} else {
		struct EEwsSearchIdleData *sid;

		sid = g_slice_new0 (struct EEwsSearchIdleData);
		sid->ref_count = 1;
		sid->cancellable = g_object_ref (pgu->cancellable);
		sid->dialog = dialog;

		gtk_label_set_text (GTK_LABEL (pgu->info_label), _("Searching…"));
		pgu->schedule_search_id = e_named_timeout_add_full (G_PRIORITY_DEFAULT,
			333, schedule_search_cb, sid, e_ews_search_idle_data_unref);
	}
}

static void
dialog_realized_cb (GObject *dialog)
{
	struct EEwsSearchUserData *pgu;

	g_return_if_fail (dialog != NULL);

	pgu = g_object_get_data (dialog, E_EWS_SEARCH_DLG_DATA);
	g_return_if_fail (pgu != NULL);
	g_return_if_fail (pgu->tree_view != NULL);

	if (pgu->cancellable)
		return;

	search_term_changed_cb (NULL, dialog);
}

static void
search_user_selection_changed_cb (GtkTreeSelection *selection,
                                  GtkDialog *dialog)
{
	g_return_if_fail (selection != NULL);
	g_return_if_fail (dialog != NULL);

	gtk_dialog_set_response_sensitive (
		dialog,
		GTK_RESPONSE_OK,
		gtk_tree_selection_get_selected (selection, NULL, NULL));
}

static void
search_user_row_activated_cb (GtkTreeView *tree_view,
                              GtkTreePath *path,
                              GtkTreeViewColumn *column,
                              GtkDialog *dialog)
{
	g_return_if_fail (tree_view != NULL);
	g_return_if_fail (dialog != NULL);

	if (path && column)
		gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

static GtkWidget *
create_users_tree_view (GtkWidget *dialog,
                        struct EEwsSearchUserData *pgu)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	gint pos;

	g_return_val_if_fail (dialog != NULL, NULL);
	g_return_val_if_fail (pgu != NULL, NULL);

	model = GTK_TREE_MODEL (gtk_list_store_new (5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_UINT));
	tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));
	g_object_unref (model);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", FALSE, NULL);
	pos = gtk_tree_view_insert_column_with_attributes (tree_view, -1, _("Name"), renderer, "text", COL_DISPLAY_NAME, NULL);
	column = gtk_tree_view_get_column (tree_view, pos - 1);
	gtk_tree_view_column_set_expand (column, TRUE);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", FALSE, NULL);
	gtk_tree_view_insert_column_with_attributes (tree_view, -1, _("E-mail"), renderer, "text", COL_EMAIL, NULL);

	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	search_user_selection_changed_cb (selection, GTK_DIALOG (dialog));
	g_signal_connect (selection, "changed", G_CALLBACK (search_user_selection_changed_cb), dialog);

	g_signal_connect (tree_view, "row-activated", G_CALLBACK (search_user_row_activated_cb), dialog);

	pgu->tree_view = GTK_WIDGET (tree_view);

	return pgu->tree_view;
}

/* for non-NULL @info populates also permission levels for given user */
gboolean
e_ews_search_user_modal (GtkWindow *parent,
                         EEwsConnection *conn,
                         const gchar *search_this,
                         gchar **display_name,
                         gchar **email)
{
	gboolean res = FALSE;
	struct EEwsSearchUserData *pgu;
	GtkWidget *dialog;
	GtkWidget *content, *label, *widget;
	GtkGrid *grid;
	GtkScrolledWindow *scrolled_window;
	gint row;

	g_return_val_if_fail (conn != NULL, FALSE);
	g_return_val_if_fail (display_name || email, FALSE);

	pgu = g_slice_new0 (struct EEwsSearchUserData);
	pgu->conn = g_object_ref (conn);

	dialog = gtk_dialog_new_with_buttons (
		_("Choose EWS user…"),
		parent,
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL);

	g_object_set_data_full (G_OBJECT (dialog), E_EWS_SEARCH_DLG_DATA, pgu, e_ews_search_user_data_free);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_row_homogeneous (grid, FALSE);
	gtk_grid_set_row_spacing (grid, 6);
	gtk_grid_set_column_homogeneous (grid, FALSE);
	gtk_grid_set_column_spacing (grid, 6);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
	gtk_container_add (GTK_CONTAINER (content), GTK_WIDGET (grid));

	row = 0;

	label = gtk_label_new_with_mnemonic (_("_Search:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		NULL);

	widget = gtk_entry_new ();
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", FALSE,
		NULL);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
	if (search_this && *search_this) {
		gtk_entry_set_text (GTK_ENTRY (widget), search_this);
		pgu->search_text = g_strdup (search_this);
	}

	g_signal_connect (widget, "changed", G_CALLBACK (search_term_changed_cb), dialog);

	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, widget, 1, row, 1, 1);

	row++;

	widget = gtk_scrolled_window_new (NULL, NULL);
	scrolled_window = GTK_SCROLLED_WINDOW (widget);
	gtk_scrolled_window_set_min_content_width (scrolled_window, 120);
	gtk_scrolled_window_set_min_content_height (scrolled_window, 120);
	gtk_container_add (GTK_CONTAINER (widget), create_users_tree_view (dialog, pgu));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"shadow-type", GTK_SHADOW_IN,
		NULL);

	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	row++;

	label = gtk_label_new (_("Search for a user"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"xalign", 0.0,
		NULL);

	pgu->info_label = label;

	gtk_grid_attach (grid, label, 0, row, 2, 1);

	row++;

	gtk_widget_show_all (content);

	g_signal_connect (dialog, "realize", G_CALLBACK (dialog_realized_cb), NULL);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		GtkTreeSelection *selection;
		GtkTreeModel *model = NULL;
		GtkTreeIter iter;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (pgu->tree_view));
		if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
			if (display_name)
				gtk_tree_model_get (model, &iter, COL_DISPLAY_NAME, display_name, -1);
			if (email)
				gtk_tree_model_get (model, &iter, COL_EMAIL, email, -1);

			res = TRUE;
		}
	}

	gtk_widget_destroy (dialog);

	return res;
}
