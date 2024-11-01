/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "e-ews-config-utils.h"
#include "e-ews-edit-folder-permissions.h"
#include "e-ews-search-user.h"

#define E_EWS_PERM_DLG_WIDGETS "e-ews-perm-dlg-widgets"

enum {
	COL_NAME = 0,
	COL_PERMISSION_LEVEL,
	COL_E_EWS_PERMISSION,
	COL_E_EWS_PERMISSION_USER_TYPE,
	COL_IS_NEW
};

struct EEwsPermissionsDialogWidgets
{
	ESourceRegistry *registry;
	ESource *source;
	CamelEwsSettings *ews_settings;
	EwsFolderId *folder_id;
	EEwsFolderType folder_type;

	EEwsConnection *conn;

	guint updating;

	GtkWidget *dialog;
	GtkWidget *tree_view;

	GtkWidget *add_button;
	GtkWidget *remove_button;
	GtkWidget *level_combo;

	GtkWidget *read_none_radio;
	GtkWidget *read_full_radio;
	GtkWidget *read_fb_time_radio;
	GtkWidget *read_fb_detail_radio;

	GtkWidget *write_create_items_check;
	GtkWidget *write_create_subfolders_check;
	GtkWidget *write_edit_own_check;
	GtkWidget *write_edit_all_check;

	GtkWidget *delete_none_radio;
	GtkWidget *delete_own_radio;
	GtkWidget *delete_all_radio;

	GtkWidget *other_folder_owner_check;
	GtkWidget *other_folder_contact_check;
	GtkWidget *other_folder_visible_check;
};

static const struct EEwsPredefinedLevels {
	const gchar *name;
	uint32_t rights;
} predefined_levels[] = {
	{ NC_("PermissionsLevel", "None"), 0 },
	{ NC_("PermissionsLevel", "Owner"),	E_EWS_PERMISSION_BIT_READ_ANY |
						E_EWS_PERMISSION_BIT_CREATE |
						E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER |
						E_EWS_PERMISSION_BIT_EDIT_OWNED |
						E_EWS_PERMISSION_BIT_EDIT_ANY |
						E_EWS_PERMISSION_BIT_DELETE_OWNED |
						E_EWS_PERMISSION_BIT_DELETE_ANY |
						E_EWS_PERMISSION_BIT_FOLDER_OWNER |
						E_EWS_PERMISSION_BIT_FOLDER_CONTACT |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Publishing Editor"),
						E_EWS_PERMISSION_BIT_READ_ANY |
						E_EWS_PERMISSION_BIT_CREATE |
						E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER |
						E_EWS_PERMISSION_BIT_EDIT_OWNED |
						E_EWS_PERMISSION_BIT_EDIT_ANY |
						E_EWS_PERMISSION_BIT_DELETE_OWNED |
						E_EWS_PERMISSION_BIT_DELETE_ANY |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Editor"),
						E_EWS_PERMISSION_BIT_READ_ANY |
						E_EWS_PERMISSION_BIT_CREATE |
						E_EWS_PERMISSION_BIT_EDIT_OWNED |
						E_EWS_PERMISSION_BIT_EDIT_ANY |
						E_EWS_PERMISSION_BIT_DELETE_OWNED |
						E_EWS_PERMISSION_BIT_DELETE_ANY |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Publishing Author"),
						E_EWS_PERMISSION_BIT_READ_ANY |
						E_EWS_PERMISSION_BIT_CREATE |
						E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER |
						E_EWS_PERMISSION_BIT_EDIT_OWNED |
						E_EWS_PERMISSION_BIT_DELETE_OWNED |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Author"),
						E_EWS_PERMISSION_BIT_READ_ANY |
						E_EWS_PERMISSION_BIT_CREATE |
						E_EWS_PERMISSION_BIT_EDIT_OWNED |
						E_EWS_PERMISSION_BIT_DELETE_OWNED |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Nonediting Author"),
						E_EWS_PERMISSION_BIT_READ_ANY |
						E_EWS_PERMISSION_BIT_CREATE |
						E_EWS_PERMISSION_BIT_DELETE_OWNED |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Reviewer"),
						E_EWS_PERMISSION_BIT_READ_ANY |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Contributor"),
						E_EWS_PERMISSION_BIT_CREATE |
						E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
	{ NC_("PermissionsLevel", "Free/Busy time"),
						E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE },
	{ NC_("PermissionsLevel", "Free/Busy time, subject, location"),
						E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED },
	{ NC_("PermissionsLevel", "Custom"), ~0 } /* make sure 'Custom' is always the last */
};

static void
edit_permissions_widgets_free (gpointer ptr)
{
	struct EEwsPermissionsDialogWidgets *widgets = ptr;

	if (!widgets)
		return;

	g_object_unref (widgets->registry);
	g_object_unref (widgets->source);
	g_object_unref (widgets->ews_settings);
	e_ews_folder_id_free (widgets->folder_id);
	if (widgets->conn)
		e_ews_config_utils_unref_in_thread (G_OBJECT (widgets->conn));
	g_free (widgets);
}

static void
folder_permissions_clear_all_permissions (GObject *dialog)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (dialog != NULL);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);
	g_return_if_fail (widgets->tree_view != NULL);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widgets->tree_view));
	g_return_if_fail (model != NULL);

	if (!gtk_tree_model_get_iter_first (model, &iter))
		return;

	do {
		EEwsPermission *perm = NULL;

		gtk_tree_model_get (model, &iter, COL_E_EWS_PERMISSION, &perm, -1);

		e_ews_permission_free (perm);
	} while (gtk_tree_model_iter_next (model, &iter));

	gtk_list_store_clear (GTK_LIST_STORE (model));
}

static void
write_folder_permissions_thread (GObject *dialog,
                                 gpointer user_data,
                                 GCancellable *cancellable,
                                 GError **perror)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	const GSList *permissions = user_data;

	g_return_if_fail (dialog != NULL);

	if (g_cancellable_is_cancelled (cancellable))
		return;

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);
	g_return_if_fail (widgets->conn != NULL);

	e_ews_connection_set_folder_permissions_sync (
		widgets->conn,
		G_PRIORITY_DEFAULT, widgets->folder_id, widgets->folder_type, permissions, cancellable, perror);
}

static void
write_folder_permissions_idle (GObject *dialog,
                               gpointer user_data,
                               GCancellable *cancellable,
                               GError **perror)
{
	/* does this only if no error was raised from the thread function */
	folder_permissions_clear_all_permissions (dialog);
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
edit_permissions_response_cb (GObject *dialog,
                              gint response_id)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	GSList *write_permissions = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (dialog != NULL);

	if (response_id != GTK_RESPONSE_OK) {
		folder_permissions_clear_all_permissions (dialog);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);
	g_return_if_fail (widgets->tree_view != NULL);
	g_return_if_fail (widgets->conn != NULL);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widgets->tree_view));
	g_return_if_fail (model != NULL);

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			EEwsPermission *perm = NULL;

			gtk_tree_model_get (model, &iter, COL_E_EWS_PERMISSION, &perm, -1);

			if (perm)
				write_permissions = g_slist_prepend (write_permissions, perm);
		} while (gtk_tree_model_iter_next (model, &iter));

		write_permissions = g_slist_reverse (write_permissions);
	}

	e_ews_config_utils_run_in_thread_with_feedback (
		GTK_WINDOW (dialog), dialog,
		_("Writing folder permissions, please wait…"),
		write_folder_permissions_thread,
		write_folder_permissions_idle,
		write_permissions, (GDestroyNotify) g_slist_free);
}

static void
enable_all_widgets (struct EEwsPermissionsDialogWidgets *widgets,
                    gboolean enabled)
{
	g_return_if_fail (widgets != NULL);

	gtk_widget_set_sensitive (widgets->add_button, enabled || gtk_widget_get_sensitive (widgets->tree_view));
	gtk_widget_set_sensitive (widgets->remove_button, enabled);
	gtk_widget_set_sensitive (widgets->level_combo, enabled);
	gtk_widget_set_sensitive (widgets->read_none_radio, enabled);
	gtk_widget_set_sensitive (widgets->read_full_radio, enabled);
	if (widgets->read_fb_time_radio)
		gtk_widget_set_sensitive (widgets->read_fb_time_radio, enabled);
	if (widgets->read_fb_detail_radio)
		gtk_widget_set_sensitive (widgets->read_fb_detail_radio, enabled);
	gtk_widget_set_sensitive (widgets->write_create_items_check, enabled);
	gtk_widget_set_sensitive (widgets->write_create_subfolders_check, enabled);
	gtk_widget_set_sensitive (widgets->write_edit_own_check, enabled);
	gtk_widget_set_sensitive (widgets->write_edit_all_check, enabled);
	gtk_widget_set_sensitive (widgets->delete_none_radio, enabled);
	gtk_widget_set_sensitive (widgets->delete_own_radio, enabled);
	gtk_widget_set_sensitive (widgets->delete_all_radio, enabled);
	gtk_widget_set_sensitive (widgets->other_folder_owner_check, enabled);
	gtk_widget_set_sensitive (widgets->other_folder_contact_check, enabled);
	gtk_widget_set_sensitive (widgets->other_folder_visible_check, enabled);
}

static uint32_t
folder_permissions_dialog_to_rights (GObject *dialog)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	uint32_t rights;

	g_return_val_if_fail (dialog != NULL, 0);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_val_if_fail (widgets != NULL, 0);

	#define set_bit_by_active(x, bt) G_STMT_START {					\
		if (widgets->x &&							\
		    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widgets->x)) &&	\
		    gtk_widget_get_sensitive (widgets->x)) {				\
			rights |= bt;							\
		} } G_STMT_END

	rights = 0;

	set_bit_by_active (read_none_radio, 0);
	set_bit_by_active (read_full_radio, E_EWS_PERMISSION_BIT_READ_ANY);
	set_bit_by_active (read_fb_time_radio, E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE);
	set_bit_by_active (read_fb_detail_radio, E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED);
	set_bit_by_active (write_create_items_check, E_EWS_PERMISSION_BIT_CREATE);
	set_bit_by_active (write_create_subfolders_check, E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER);
	set_bit_by_active (write_edit_own_check, E_EWS_PERMISSION_BIT_EDIT_OWNED);
	set_bit_by_active (write_edit_all_check, E_EWS_PERMISSION_BIT_EDIT_ANY | E_EWS_PERMISSION_BIT_EDIT_OWNED);
	set_bit_by_active (delete_none_radio, 0);
	set_bit_by_active (delete_own_radio, E_EWS_PERMISSION_BIT_DELETE_OWNED);
	set_bit_by_active (delete_all_radio, E_EWS_PERMISSION_BIT_DELETE_ANY | E_EWS_PERMISSION_BIT_DELETE_OWNED);
	set_bit_by_active (other_folder_owner_check, E_EWS_PERMISSION_BIT_FOLDER_OWNER);
	set_bit_by_active (other_folder_contact_check, E_EWS_PERMISSION_BIT_FOLDER_CONTACT);
	set_bit_by_active (other_folder_visible_check, E_EWS_PERMISSION_BIT_FOLDER_VISIBLE);

	#undef set_bit_by_active

	return rights;
}

static void
update_folder_permissions_sensitivity (GObject *dialog,
                                       gboolean member_valid,
                                       EEwsPermissionUserType user_type)
{
	struct EEwsPermissionsDialogWidgets *widgets;

	g_return_if_fail (dialog != NULL);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);

	enable_all_widgets (widgets, member_valid);

	if (user_type == E_EWS_PERMISSION_USER_TYPE_DEFAULT ||
	    user_type == E_EWS_PERMISSION_USER_TYPE_ANONYMOUS)
		gtk_widget_set_sensitive (widgets->other_folder_contact_check, FALSE);

	if (member_valid)
		gtk_widget_set_sensitive (
			widgets->remove_button,
			user_type != E_EWS_PERMISSION_USER_TYPE_DEFAULT &&
			user_type != E_EWS_PERMISSION_USER_TYPE_ANONYMOUS);

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widgets->write_edit_all_check))) {
		gtk_widget_set_sensitive (widgets->write_edit_own_check, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widgets->write_edit_own_check), TRUE);
	}
}

static void
update_folder_permissions_by_rights (GObject *dialog,
                                     uint32_t rights)
{
	struct EEwsPermissionsDialogWidgets *widgets;

	g_return_if_fail (dialog != NULL);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);

	#define set_active(x, act) G_STMT_START { if (widgets->x) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widgets->x), act); } G_STMT_END
	#define set_active_by_bit(x, bt) set_active (x, (rights & (bt)) != 0)

	widgets->updating++;

	set_active (read_none_radio, TRUE);
	set_active_by_bit (read_full_radio, E_EWS_PERMISSION_BIT_READ_ANY);
	set_active_by_bit (read_fb_time_radio, E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE);
	set_active_by_bit (read_fb_detail_radio, E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED);
	set_active_by_bit (write_create_items_check, E_EWS_PERMISSION_BIT_CREATE);
	set_active_by_bit (write_create_subfolders_check, E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER);
	set_active_by_bit (write_edit_own_check, E_EWS_PERMISSION_BIT_EDIT_OWNED | E_EWS_PERMISSION_BIT_EDIT_ANY);
	set_active_by_bit (write_edit_all_check, E_EWS_PERMISSION_BIT_EDIT_ANY);
	set_active (delete_none_radio, TRUE);
	set_active_by_bit (delete_own_radio, E_EWS_PERMISSION_BIT_DELETE_OWNED);
	set_active_by_bit (delete_all_radio, E_EWS_PERMISSION_BIT_DELETE_ANY);
	set_active_by_bit (other_folder_owner_check, E_EWS_PERMISSION_BIT_FOLDER_OWNER);
	set_active_by_bit (other_folder_contact_check, E_EWS_PERMISSION_BIT_FOLDER_CONTACT);
	set_active_by_bit (other_folder_visible_check, E_EWS_PERMISSION_BIT_FOLDER_VISIBLE);

	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widgets->write_edit_all_check)) &&
	    gtk_widget_get_sensitive (widgets->write_edit_all_check)) {
		gtk_widget_set_sensitive (widgets->write_edit_own_check, TRUE);
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widgets->write_edit_all_check))) {
		gtk_widget_set_sensitive (widgets->write_edit_own_check, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widgets->write_edit_own_check), TRUE);
	}

	widgets->updating--;

	#undef set_active_by_bit
	#undef set_active
}

static void
update_folder_permissions_tree_view (GObject *dialog,
                                     struct EEwsPermissionsDialogWidgets *widgets)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	uint32_t rights;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (widgets != NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widgets->tree_view));
	if (selection && gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gchar *combo_text;
		EEwsPermission *perm = NULL;

		combo_text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (widgets->level_combo));
		rights = folder_permissions_dialog_to_rights (dialog);

		gtk_tree_model_get (model, &iter, COL_E_EWS_PERMISSION, &perm, -1);

		if (perm) {
			if (!widgets->read_fb_time_radio)
				rights = rights | (perm->rights & (E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED | E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE));

			perm->rights = rights;

			gtk_list_store_set (GTK_LIST_STORE (model), &iter, COL_PERMISSION_LEVEL, combo_text, -1);
		}

		g_free (combo_text);
	}
}

static void
update_permission_level_combo_by_dialog (GObject *dialog)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	uint32_t rights;
	gint ii;

	g_return_if_fail (dialog != NULL);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);

	if (widgets->updating)
		return;

	rights = folder_permissions_dialog_to_rights (dialog);
	if (!widgets->read_fb_time_radio)
		rights = rights & ~(E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED |
			E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE);

	for (ii = 0; ii < G_N_ELEMENTS (predefined_levels) - 1; ii++) {
		if (predefined_levels[ii].rights == rights) {
			break;
		}
	}

	if (!widgets->read_fb_time_radio && ii > G_N_ELEMENTS (predefined_levels) - 3)
		ii = G_N_ELEMENTS (predefined_levels) - 3;

	/* ii points to the matched or the last item, which is 'Custom' */
	widgets->updating++;
	gtk_combo_box_set_active (GTK_COMBO_BOX (widgets->level_combo), ii);

	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widgets->write_edit_all_check)) &&
	    gtk_widget_get_sensitive (widgets->write_edit_all_check)) {
		gtk_widget_set_sensitive (widgets->write_edit_own_check, TRUE);
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widgets->write_edit_own_check))) {
			rights |= E_EWS_PERMISSION_BIT_EDIT_OWNED;

			for (ii = 0; ii < G_N_ELEMENTS (predefined_levels) - 1; ii++) {
				if (predefined_levels[ii].rights == rights) {
					break;
				}
			}

			if (!widgets->read_fb_time_radio && ii > G_N_ELEMENTS (predefined_levels) - 3)
				ii = G_N_ELEMENTS (predefined_levels) - 3;

			gtk_combo_box_set_active (GTK_COMBO_BOX (widgets->level_combo), ii);
		}
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widgets->write_edit_all_check))) {
		gtk_widget_set_sensitive (widgets->write_edit_own_check, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widgets->write_edit_own_check), TRUE);
	}

	update_folder_permissions_tree_view (dialog, widgets);

	widgets->updating--;
}

static void
update_permission_dialog_by_level_combo (GObject *dialog)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	uint32_t rights;
	gint ii;

	g_return_if_fail (dialog != NULL);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);

	if (widgets->updating)
		return;

	ii = gtk_combo_box_get_active (GTK_COMBO_BOX (widgets->level_combo));
	/* out of bounds or 'Customs' level, or 'Custom' without free/busy checks */
	if (ii < 0 || ii >= G_N_ELEMENTS (predefined_levels) - 1 ||
	    (!widgets->read_fb_time_radio && ii >= G_N_ELEMENTS (predefined_levels) - 3))
		return;

	if (!predefined_levels[ii].rights) {
		rights = predefined_levels[ii].rights;
	} else {
		rights = folder_permissions_dialog_to_rights (dialog);
		rights = predefined_levels[ii].rights | (rights & (E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED |
			E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE));
	}

	widgets->updating++;
	update_folder_permissions_by_rights (dialog, rights);
	update_folder_permissions_tree_view (dialog, widgets);
	widgets->updating--;
}

static void
add_button_clicked_cb (GObject *dialog)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	gchar *display_name = NULL;
	gchar *primary_smtp = NULL;
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (dialog != NULL);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);

	if (widgets->updating)
		return;

	g_return_if_fail (widgets->tree_view != NULL);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widgets->tree_view));
	g_return_if_fail (model != NULL);

	if (e_ews_search_user_modal (GTK_WINDOW (dialog), widgets->conn, NULL, &display_name, &primary_smtp)) {
		EEwsPermission *perm;
		GtkTreeSelection *selection;
		gboolean found = FALSE;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widgets->tree_view));
		g_return_if_fail (selection != NULL);

		if (gtk_tree_model_get_iter_first (model, &iter)) {
			do {
				EEwsPermissionUserType ut = E_EWS_PERMISSION_USER_TYPE_NONE;

				perm = NULL;

				gtk_tree_model_get (
					model, &iter,
					COL_E_EWS_PERMISSION, &perm,
					COL_E_EWS_PERMISSION_USER_TYPE, &ut,
					-1);

				if (ut == E_EWS_PERMISSION_USER_TYPE_REGULAR && perm && g_strcmp0 (perm->primary_smtp, primary_smtp) == 0) {
					gtk_tree_selection_select_iter (selection, &iter);
					found = TRUE;
					break;
				}
			} while (gtk_tree_model_iter_next (model, &iter));
		}

		if (!found) {
			GtkListStore *store = GTK_LIST_STORE (model);

			perm = e_ews_permission_new (
				E_EWS_PERMISSION_USER_TYPE_REGULAR,
				display_name, primary_smtp, NULL,
				widgets->read_fb_time_radio ? E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE : 0);

			gtk_list_store_append (store, &iter);
			gtk_list_store_set (
				store, &iter,
				COL_NAME, perm->display_name,
				COL_PERMISSION_LEVEL, g_dpgettext2 (GETTEXT_PACKAGE, "PermissionsLevel", predefined_levels[0].name),
				COL_E_EWS_PERMISSION, perm,
				COL_E_EWS_PERMISSION_USER_TYPE, E_EWS_PERMISSION_USER_TYPE_REGULAR,
				COL_IS_NEW, TRUE,
				-1);

			gtk_tree_selection_select_iter (selection, &iter);
		}
	}

	g_free (display_name);
	g_free (primary_smtp);
}

static void
remove_button_clicked_cb (GObject *dialog)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	GtkTreeSelection *selection;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;

	g_return_if_fail (dialog != NULL);

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);

	if (widgets->updating)
		return;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widgets->tree_view));
	if (selection && gtk_tree_selection_get_selected (selection, &model, &iter)) {
		GtkTreeIter select;
		gboolean can_select = FALSE;
		EEwsPermission *perm = NULL;

		select = iter;
		can_select = gtk_tree_model_iter_next (model, &select);
		if (!can_select) {
			select = iter;
			can_select = gtk_tree_model_iter_previous (model, &select);
		}

		if (can_select)
			gtk_tree_selection_select_iter (selection, &select);

		gtk_tree_model_get (model, &iter, COL_E_EWS_PERMISSION, &perm, -1);

		if (gtk_list_store_remove (GTK_LIST_STORE (model), &iter))
			e_ews_permission_free (perm);
	}
}

static void
folder_permissions_free_found_permissions (gpointer ptr)
{
	GSList **ppermissions = ptr;

	if (!ppermissions)
		return;

	e_ews_permissions_free (*ppermissions);
	*ppermissions = NULL;
	g_free (ppermissions);
}

static void
read_folder_permissions_thread (GObject *dialog,
                                gpointer user_data,
                                GCancellable *cancellable,
                                GError **perror)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	GSList **ppermissions = user_data;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (ppermissions != NULL);

	if (g_cancellable_is_cancelled (cancellable))
		return;

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);
	g_return_if_fail (widgets->registry != NULL);
	g_return_if_fail (widgets->source != NULL);
	g_return_if_fail (widgets->ews_settings != NULL);
	g_return_if_fail (widgets->folder_id != NULL);

	widgets->conn = e_ews_config_utils_open_connection_for (
		widgets->source,
		widgets->ews_settings,
		NULL, NULL, NULL,
		cancellable,
		perror);

	if (!widgets->conn)
		g_cancellable_cancel (cancellable);

	if (g_cancellable_is_cancelled (cancellable))
		return;

	if (e_ews_connection_get_folder_permissions_sync (
		widgets->conn,
		G_PRIORITY_DEFAULT, widgets->folder_id, ppermissions, cancellable, perror)) {
		EEwsFolder *folder = NULL;

		e_ews_connection_get_folder_info_sync (widgets->conn,
			G_PRIORITY_DEFAULT, NULL, widgets->folder_id,
			&folder, cancellable, NULL);

		if (folder) {
			const EwsFolderId *folder_id = e_ews_folder_get_id (folder);

			if (folder_id) {
				g_free (widgets->folder_id->change_key);
				widgets->folder_id->change_key = g_strdup (folder_id->change_key);
			}

			g_object_unref (folder);
		}
	}
}

static void
read_folder_permissions_idle (GObject *dialog,
                              gpointer user_data,
                              GCancellable *cancellable,
                              GError **perror)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	GSList **ppermissions = user_data;
	GSList *permiter;
	GtkListStore *store;
	GtkTreeIter iter;

	g_return_if_fail (dialog != NULL);
	g_return_if_fail (ppermissions != NULL);

	if (g_cancellable_is_cancelled (cancellable))
		return;

	widgets = g_object_get_data (dialog, E_EWS_PERM_DLG_WIDGETS);
	g_return_if_fail (widgets != NULL);
	g_return_if_fail (widgets->tree_view != NULL);

	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (widgets->tree_view)));
	g_return_if_fail (store != NULL);

	for (permiter = *ppermissions; permiter; permiter = permiter->next) {
		EEwsPermission *perm = permiter->data;
		const gchar *perm_level;
		uint32_t rights;
		gint ii;

		if (!perm)
			continue;

		/* steal the perm */
		permiter->data = NULL;

		rights = perm->rights;
		if (!widgets->read_fb_time_radio) {
			rights = rights & ~(E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED |
				E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE);
		}

		for (ii = 0; ii < G_N_ELEMENTS (predefined_levels) - 1; ii++) {
			if (predefined_levels[ii].rights == rights) {
				break;
			}
		}

		if (perm->user_type == E_EWS_PERMISSION_USER_TYPE_ANONYMOUS) {
			g_free (perm->display_name);
			perm->display_name = g_strdup (C_("User", "Anonymous"));
		} else if (perm->user_type == E_EWS_PERMISSION_USER_TYPE_DEFAULT) {
			g_free (perm->display_name);
			perm->display_name = g_strdup (C_("User", "Default"));
		} else if (!perm->display_name || !*perm->display_name) {
			const gchar *display_name;

			display_name = perm->primary_smtp;
			if (!display_name)
				display_name = C_("User", "Unknown");

			g_free (perm->display_name);
			perm->display_name = g_strdup (display_name);
		}

		perm_level = g_dpgettext2 (GETTEXT_PACKAGE, "PermissionsLevel", predefined_levels[ii].name);

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (
			store, &iter,
			COL_NAME, perm->display_name,
			COL_PERMISSION_LEVEL, perm_level,
			COL_E_EWS_PERMISSION, perm,
			COL_E_EWS_PERMISSION_USER_TYPE, perm->user_type,
			COL_IS_NEW, FALSE,
			-1);
	}

	gtk_widget_set_sensitive (widgets->add_button, TRUE);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (widgets->dialog), GTK_RESPONSE_OK, TRUE);
}

static void
folder_permissions_tree_selection_changed_cb (GtkTreeSelection *selection,
                                              struct EEwsPermissionsDialogWidgets *widgets)
{
	GObject *dialog;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	gboolean has_selected;

	g_return_if_fail (selection != NULL);
	g_return_if_fail (widgets != NULL);
	g_return_if_fail (widgets->dialog != NULL);
	g_return_if_fail (widgets->add_button != NULL);
	g_return_if_fail (widgets->remove_button != NULL);

	dialog = G_OBJECT (widgets->dialog);
	has_selected = gtk_tree_selection_get_selected (selection, &model, &iter);

	gtk_widget_set_sensitive (widgets->add_button, TRUE);
	gtk_widget_set_sensitive (widgets->remove_button, has_selected);

	if (has_selected) {
		EEwsPermissionUserType user_type = E_EWS_PERMISSION_USER_TYPE_NONE;
		EEwsPermission *perm = NULL;

		gtk_tree_model_get (
			model, &iter,
			COL_E_EWS_PERMISSION, &perm,
			COL_E_EWS_PERMISSION_USER_TYPE, &user_type,
			-1);

		update_folder_permissions_sensitivity (dialog, perm != NULL, user_type);
		update_folder_permissions_by_rights (dialog, perm ? perm->rights : 0);
	} else {
		update_folder_permissions_sensitivity (dialog, FALSE, E_EWS_PERMISSION_USER_TYPE_NONE);
		update_folder_permissions_by_rights (dialog, 0);
	}

	update_permission_level_combo_by_dialog (dialog);
}

static GtkWidget *
create_permissions_tree_view (GObject *dialog,
                              struct EEwsPermissionsDialogWidgets *widgets)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	gint pos;

	g_return_val_if_fail (widgets != NULL, NULL);

	model = GTK_TREE_MODEL (gtk_list_store_new (5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_BOOLEAN));
	tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));
	g_object_unref (model);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", FALSE, NULL);
	pos = gtk_tree_view_insert_column_with_attributes (tree_view, -1, _("Name"), renderer, "text", COL_NAME, NULL);
	column = gtk_tree_view_get_column (tree_view, pos - 1);
	gtk_tree_view_column_set_expand (column, TRUE);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", FALSE, NULL);
	gtk_tree_view_insert_column_with_attributes (tree_view, -1, _("Permission level"), renderer, "text", COL_PERMISSION_LEVEL, NULL);

	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed", G_CALLBACK (folder_permissions_tree_selection_changed_cb), widgets);

	widgets->tree_view = GTK_WIDGET (tree_view);

	return widgets->tree_view;
}

/* Opens dialog to subscribe to folders of other
 * users in the given store */
void
e_ews_edit_folder_permissions (GtkWindow *parent,
                               ESourceRegistry *registry,
                               ESource *source,
                               CamelEwsSettings *ews_settings,
                               const gchar *account_name,
                               const gchar *folder_name,
                               const EwsFolderId *folder_id,
                               EEwsFolderType folder_type)
{
	struct EEwsPermissionsDialogWidgets *widgets;
	PangoAttrList *attrs;
	GObject *dialog;
	GtkWidget *content;
	GtkWidget *label, *widget, *button, *frame, *hvbox;
	GtkScrolledWindow *scrolled_window;
	GtkComboBoxText *combo_text;
	GtkGrid *grid;
	GSList *radio_group, **found_permissions;
	gboolean with_freebusy;
	gint row, ii;

	g_return_if_fail (registry != NULL);
	g_return_if_fail (source != NULL);
	g_return_if_fail (ews_settings != NULL);
	g_return_if_fail (account_name != NULL);
	g_return_if_fail (folder_name != NULL);
	g_return_if_fail (folder_id != NULL);

	with_freebusy = folder_type == E_EWS_FOLDER_TYPE_CALENDAR;

	widgets = g_new0 (struct EEwsPermissionsDialogWidgets, 1);
	widgets->registry = g_object_ref (registry);
	widgets->source = g_object_ref (source);
	widgets->ews_settings = g_object_ref (ews_settings);
	widgets->folder_id = e_ews_folder_id_new (folder_id->id, folder_id->change_key, folder_id->is_distinguished_id);
	widgets->folder_type = folder_type;

	widgets->dialog = gtk_dialog_new_with_buttons (
		_("Edit EWS folder permissions…"),
		parent,
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL);

	dialog = G_OBJECT (widgets->dialog);
	g_signal_connect (dialog, "response", G_CALLBACK (edit_permissions_response_cb), NULL);
	g_object_set_data_full (dialog, E_EWS_PERM_DLG_WIDGETS, widgets, edit_permissions_widgets_free);

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

	label = gtk_label_new (_("Account:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		NULL);

	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	widget = gtk_label_new (account_name);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"use-underline", FALSE,
		"attributes", attrs,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		NULL);
	pango_attr_list_unref (attrs);

	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, widget, 1, row, 1, 1);

	row++;

	label = gtk_label_new (_("Folder name:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		NULL);

	widget = gtk_label_new (folder_name);
	gtk_label_set_ellipsize (GTK_LABEL (widget), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_tooltip_text (widget, folder_name);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"xalign", 0.0,
		NULL);

	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, widget, 1, row, 1, 1);

	row++;

	if (!folder_id->is_distinguished_id) {
		label = gtk_label_new (_("Folder ID:"));
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
			"xalign", 0.0,
			"has-frame", FALSE,
			"editable", FALSE,
			"can-focus", FALSE,
			"text", folder_id->id,
			NULL);

		gtk_grid_attach (grid, label, 0, row, 1, 1);
		gtk_grid_attach (grid, widget, 1, row, 1, 1);
	}

	row++;

	widget = gtk_scrolled_window_new (NULL, NULL);
	scrolled_window = GTK_SCROLLED_WINDOW (widget);
	gtk_scrolled_window_set_min_content_width (scrolled_window, 120);
	gtk_scrolled_window_set_min_content_height (scrolled_window, 120);
	gtk_container_add (GTK_CONTAINER (widget), create_permissions_tree_view (dialog, widgets));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"shadow-type", GTK_SHADOW_IN,
		NULL);

	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	row++;

	hvbox = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (hvbox), GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_set_column_spacing (GTK_GRID (hvbox), 6);
	gtk_grid_set_column_homogeneous (GTK_GRID (hvbox), TRUE);
	g_object_set (
		G_OBJECT (hvbox),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_END,
		NULL);

	button = gtk_button_new_from_stock (GTK_STOCK_ADD);
	widgets->add_button = button;
	gtk_container_add (GTK_CONTAINER (hvbox), button);

	button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
	widgets->remove_button = button;
	gtk_container_add (GTK_CONTAINER (hvbox), button);

	gtk_grid_attach (grid, hvbox, 0, row, 2, 1);

	row++;

	widget = gtk_frame_new (_("Permissions"));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", FALSE,
		NULL);
	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_row_homogeneous (grid, FALSE);
	gtk_grid_set_row_spacing (grid, 6);
	gtk_grid_set_column_homogeneous (grid, FALSE);
	gtk_grid_set_column_spacing (grid, 6);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (grid));

	row = 0;

	hvbox = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (hvbox), GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_set_column_spacing (GTK_GRID (hvbox), 6);

	label = gtk_label_new_with_mnemonic (_("Permi_ssion level:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		NULL);

	widget = GTK_WIDGET (
		g_object_new (gtk_combo_box_text_get_type (),
		"has-entry", FALSE,
		"entry-text-column", 0,
		"hexpand", TRUE,
		"vexpand", FALSE,
		NULL));
	widgets->level_combo = widget;

	combo_text = GTK_COMBO_BOX_TEXT (widget);
	for (ii = 0; ii < G_N_ELEMENTS (predefined_levels); ii++) {
		if (with_freebusy ||
		    (predefined_levels[ii].rights != E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE &&
		     predefined_levels[ii].rights != E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED))
			gtk_combo_box_text_append_text (combo_text, g_dpgettext2 (GETTEXT_PACKAGE, "PermissionsLevel", predefined_levels[ii].name));
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_text), 0);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
	gtk_container_add (GTK_CONTAINER (hvbox), label);
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	gtk_grid_attach (grid, hvbox, 0, row, 2, 1);

	row++;

	frame = gtk_frame_new (C_("Permissions", "Read"));
	g_object_set (
		G_OBJECT (frame),
		"hexpand", FALSE,
		"vexpand", FALSE,
		NULL);

	hvbox = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (hvbox), GTK_ORIENTATION_VERTICAL);
	gtk_grid_set_column_spacing (GTK_GRID (hvbox), 2);
	gtk_container_add (GTK_CONTAINER (frame), hvbox);

	widget = gtk_radio_button_new_with_label (NULL, C_("Permissions", "None"));
	widgets->read_none_radio = widget;
	radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	if (with_freebusy) {
		widget = gtk_radio_button_new_with_label (radio_group, C_("Permissions", "Free/Busy time"));
		widgets->read_fb_time_radio = widget;
		radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
		gtk_container_add (GTK_CONTAINER (hvbox), widget);

		widget = gtk_radio_button_new_with_label (radio_group, C_("Permissions", "Free/Busy time, subject, location"));
		widgets->read_fb_detail_radio = widget;
		radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
		gtk_container_add (GTK_CONTAINER (hvbox), widget);
	}

	widget = gtk_radio_button_new_with_label (radio_group, C_("Permissions", "Full Details"));
	widgets->read_full_radio = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	gtk_grid_attach (grid, frame, 0, row, 1, 1);

	frame = gtk_frame_new (C_("Permissions", "Write"));
	g_object_set (
		G_OBJECT (frame),
		"hexpand", FALSE,
		"vexpand", FALSE,
		NULL);

	hvbox = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (hvbox), GTK_ORIENTATION_VERTICAL);
	gtk_grid_set_column_spacing (GTK_GRID (hvbox), 2);
	gtk_container_add (GTK_CONTAINER (frame), hvbox);

	widget = gtk_check_button_new_with_label (C_("Permissions", "Create items"));
	widgets->write_create_items_check = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	widget = gtk_check_button_new_with_label (C_("Permissions", "Create subfolders"));
	widgets->write_create_subfolders_check = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	widget = gtk_check_button_new_with_label (C_("Permissions", "Edit own"));
	widgets->write_edit_own_check = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	widget = gtk_check_button_new_with_label (C_("Permissions", "Edit all"));
	widgets->write_edit_all_check = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	gtk_grid_attach (grid, frame, 1, row, 1, 1);

	row++;

	frame = gtk_frame_new (C_("Permissions", "Delete items"));
	g_object_set (
		G_OBJECT (frame),
		"hexpand", FALSE,
		"vexpand", FALSE,
		NULL);

	hvbox = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (hvbox), GTK_ORIENTATION_VERTICAL);
	gtk_grid_set_column_spacing (GTK_GRID (hvbox), 2);
	gtk_container_add (GTK_CONTAINER (frame), hvbox);

	widget = gtk_radio_button_new_with_label (NULL, C_("Permissions", "None"));
	widgets->delete_none_radio = widget;
	radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	widget = gtk_radio_button_new_with_label (radio_group, C_("Permissions", "Own"));
	widgets->delete_own_radio = widget;
	radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	widget = gtk_radio_button_new_with_label (radio_group, C_("Permissions", "All"));
	widgets->delete_all_radio = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	gtk_grid_attach (grid, frame, 0, row, 1, 1);

	frame = gtk_frame_new (C_("Permissions", "Other"));
	g_object_set (
		G_OBJECT (frame),
		"hexpand", FALSE,
		"vexpand", FALSE,
		NULL);

	hvbox = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (hvbox), GTK_ORIENTATION_VERTICAL);
	gtk_grid_set_column_spacing (GTK_GRID (hvbox), 2);
	gtk_container_add (GTK_CONTAINER (frame), hvbox);

	widget = gtk_check_button_new_with_label (C_("Permissions", "Folder owner"));
	widgets->other_folder_owner_check = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	widget = gtk_check_button_new_with_label (C_("Permissions", "Folder contact"));
	widgets->other_folder_contact_check = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	widget = gtk_check_button_new_with_label (C_("Permissions", "Folder visible"));
	widgets->other_folder_visible_check = widget;
	gtk_container_add (GTK_CONTAINER (hvbox), widget);

	gtk_grid_attach (grid, frame, 1, row, 1, 1);

	row++;

	g_signal_connect_swapped (
		widgets->add_button,
		"clicked", G_CALLBACK (add_button_clicked_cb), dialog);
	g_signal_connect_swapped (
		widgets->remove_button,
		"clicked", G_CALLBACK (remove_button_clicked_cb), dialog);
	g_signal_connect_swapped (
		widgets->level_combo,
		"changed", G_CALLBACK (update_permission_dialog_by_level_combo), dialog);
	g_signal_connect_swapped (
		widgets->read_none_radio,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->read_full_radio,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	if (widgets->read_fb_time_radio)
		g_signal_connect_swapped (
			widgets->read_fb_time_radio,
			"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	if (widgets->read_fb_detail_radio)
		g_signal_connect_swapped (
			widgets->read_fb_detail_radio,
			"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->write_create_items_check,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->write_create_subfolders_check,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->write_edit_own_check,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->write_edit_all_check,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->delete_none_radio,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->delete_own_radio,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->delete_all_radio,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->other_folder_owner_check,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->other_folder_contact_check,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);
	g_signal_connect_swapped (
		widgets->other_folder_visible_check,
		"toggled", G_CALLBACK (update_permission_level_combo_by_dialog), dialog);

	enable_all_widgets (widgets, FALSE);

	gtk_dialog_set_response_sensitive (GTK_DIALOG (widgets->dialog), GTK_RESPONSE_OK, FALSE);

	gtk_widget_show_all (content);
	gtk_widget_show (GTK_WIDGET (dialog));

	found_permissions = g_new0 (GSList *, 1);

	e_ews_config_utils_run_in_thread_with_feedback (
		GTK_WINDOW (dialog), dialog,
		_("Reading folder permissions, please wait…"),
		read_folder_permissions_thread,
		read_folder_permissions_idle,
		found_permissions, folder_permissions_free_found_permissions);
}
