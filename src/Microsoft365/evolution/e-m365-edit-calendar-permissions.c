/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>
#include <e-util/e-util.h>
#include <shell/e-shell.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"

#include "e-m365-edit-calendar-permissions.h"

#define E_M365_PERM_DLG_DATA "EM365PermDlgData"

typedef struct _DailogData {
	gint ref_count;

	GtkWidget *dialog;
	GtkWidget *progress_box;
	GtkLabel *progress_label;
	GtkWidget *progress_cancel_button;
	GtkWidget *progress_close_button;
	GtkEntry *name_entry;
	GtkComboBox *perms_combo;
	GtkWidget *share_button;
	GtkWidget *perms_scrolled;
	GtkWidget *in_org_label;
	GtkGrid *in_org_grid;
	GtkWidget *out_org_label;
	GtkGrid *out_org_grid;

	EDestinationStore *dest_store;
	EM365Connection *cnc;
	gchar *group_id;
	gchar *calendar_id;
	GCancellable *cancellable;
	GtkCssProvider *css_provider;
	GHashTable *perms;
	guint n_in_org_rows;
	guint n_out_org_rows;
} DialogData;

static DialogData *
dialog_data_ref (DialogData *dlg_data)
{
	if (dlg_data)
		g_atomic_int_inc (&dlg_data->ref_count);

	return dlg_data;
}

static void
dialog_data_unref (gpointer ptr)
{
	DialogData *dlg_data = ptr;

	if (dlg_data && g_atomic_int_dec_and_test (&dlg_data->ref_count)) {
		if (dlg_data->cancellable)
			g_cancellable_cancel (dlg_data->cancellable);
		g_clear_object (&dlg_data->cnc);
		g_clear_object (&dlg_data->cancellable);
		g_clear_object (&dlg_data->dest_store);
		g_clear_object (&dlg_data->css_provider);
		g_clear_pointer (&dlg_data->perms, g_hash_table_unref);
		g_free (dlg_data->group_id);
		g_free (dlg_data->calendar_id);
		g_free (dlg_data);
	}
}

typedef void (* ThreadFunc)	(DialogData *dlg_data,
				 gpointer user_data,
				 gpointer *out_result,
				 GDestroyNotify *out_result_free_func,
				 GCancellable *cancellable,
				 GError **error);
typedef void (* IdleFunc)	(DialogData *dlg_data,
				 gpointer result,
				 gpointer user_data);

typedef struct _OpData {
	DialogData *dlg_data;
	gchar *failure_prefix;
	ThreadFunc thread_func;
	IdleFunc idle_func;
	gpointer user_data;
	GDestroyNotify user_data_free_func;
	GCancellable *cancellable;
	gpointer result;
	GDestroyNotify result_free_func;
	GError *error;
} OpData;

static void
op_data_free (gpointer ptr)
{
	OpData *op_data = ptr;

	if (op_data) {
		if (op_data->user_data && op_data->user_data_free_func)
			op_data->user_data_free_func (op_data->user_data);
		if (op_data->result && op_data->result_free_func)
			op_data->result_free_func (op_data->result);
		g_clear_object (&op_data->cancellable);
		g_clear_error (&op_data->error);
		dialog_data_unref (op_data->dlg_data);
		g_free (op_data->failure_prefix);
		g_free (op_data);
	}
}

static gboolean
op_finish_idle_cb (gpointer user_data)
{
	OpData *op_data = user_data;

	if (!g_cancellable_is_cancelled (op_data->cancellable)) {
		if (op_data->error) {
			g_prefix_error (&op_data->error, "%s", op_data->failure_prefix);
			gtk_label_set_text (op_data->dlg_data->progress_label, op_data->error->message);
			gtk_widget_set_visible (op_data->dlg_data->progress_cancel_button, FALSE);
			gtk_widget_set_visible (op_data->dlg_data->progress_close_button, TRUE);
		} else {
			gtk_widget_set_visible (op_data->dlg_data->progress_box, FALSE);

			if (op_data->idle_func)
				op_data->idle_func (op_data->dlg_data, op_data->result, op_data->user_data);
		}

		gtk_widget_set_sensitive (GTK_WIDGET (op_data->dlg_data->name_entry), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (op_data->dlg_data->perms_combo), TRUE);
		gtk_widget_set_sensitive (op_data->dlg_data->share_button, !e_str_is_empty (gtk_entry_get_text (op_data->dlg_data->name_entry)));
		gtk_widget_set_sensitive (op_data->dlg_data->perms_scrolled, TRUE);
	}

	return G_SOURCE_REMOVE;
}

static gpointer
op_thread (gpointer user_data)
{
	OpData *op_data = user_data;

	op_data->thread_func (op_data->dlg_data, op_data->user_data, &op_data->result, &op_data->result_free_func,
		op_data->cancellable, &op_data->error);

	g_idle_add_full (G_PRIORITY_HIGH_IDLE, op_finish_idle_cb, op_data, op_data_free);

	return NULL;
}

static void
run_op_in_thread (DialogData *dlg_data,
		  const gchar *description,
		  const gchar *failure_prefix,
		  ThreadFunc thread_func,
		  IdleFunc idle_func,
		  gpointer user_data,
		  GDestroyNotify user_data_free_func)
{
	OpData *op_data;

	g_return_if_fail (description != NULL);
	g_return_if_fail (thread_func != NULL);

	if (dlg_data->cancellable)
		g_cancellable_cancel (dlg_data->cancellable);
	g_clear_object (&dlg_data->cancellable);

	dlg_data->cancellable = g_cancellable_new ();

	op_data = g_new0 (OpData, 1);
	op_data->dlg_data = dialog_data_ref (dlg_data);
	op_data->failure_prefix = g_strdup (failure_prefix);
	op_data->thread_func = thread_func;
	op_data->idle_func = idle_func;
	op_data->user_data = user_data;
	op_data->user_data_free_func = user_data_free_func;
	op_data->cancellable = g_object_ref (dlg_data->cancellable);

	gtk_widget_set_sensitive (GTK_WIDGET (dlg_data->name_entry), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (dlg_data->perms_combo), FALSE);
	gtk_widget_set_sensitive (dlg_data->share_button, FALSE);
	gtk_widget_set_sensitive (dlg_data->perms_scrolled, FALSE);

	gtk_label_set_text (dlg_data->progress_label, description);
	gtk_widget_set_visible (dlg_data->progress_cancel_button, TRUE);
	gtk_widget_set_visible (dlg_data->progress_close_button, FALSE);
	gtk_widget_set_visible (dlg_data->progress_box, TRUE);

	g_thread_unref (g_thread_new ("m365-perm-op-thread", op_thread, op_data));
}

static const struct _PermAndId {
	EM365CalendarPermissionType perm;
	const gchar *id;
} perm_and_id[] = {
	{ E_M365_CALENDAR_PERMISSION_NOT_SET, "not-set" },
	{ E_M365_CALENDAR_PERMISSION_UNKNOWN, "unknown" },
	{ E_M365_CALENDAR_PERMISSION_NONE, "none" },
	{ E_M365_CALENDAR_PERMISSION_FREE_BUSY_READ, "free-busy" },
	{ E_M365_CALENDAR_PERMISSION_LIMITED_READ, "limited-read" },
	{ E_M365_CALENDAR_PERMISSION_READ, "read" },
	{ E_M365_CALENDAR_PERMISSION_WRITE, "write" },
	{ E_M365_CALENDAR_PERMISSION_DELEGATE_WITHOUT_PRIVATE_EVENT_ACCESS, "delete-with" },
	{ E_M365_CALENDAR_PERMISSION_DELEGATE_WITH_PRIVATE_EVENT_ACCESS, "delegate-without" },
	{ E_M365_CALENDAR_PERMISSION_CUSTOM, "custom" }
};

static const gchar *
perm_to_id (EM365CalendarPermissionType perm)
{
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (perm_and_id); ii++) {
		if (perm == perm_and_id[ii].perm)
			return perm_and_id[ii].id;
	}

	return "???";
}

static EM365CalendarPermissionType
id_to_perm (const gchar *id)
{
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (perm_and_id); ii++) {
		if (g_strcmp0 (id, perm_and_id[ii].id) == 0)
			return perm_and_id[ii].perm;
	}

	return E_M365_CALENDAR_PERMISSION_UNKNOWN;
}

static void
fill_perms_combo (GtkComboBox *combo,
		  guint32 allowed, /* bit-or of EM365CalendarPermissionType */
		  EM365CalendarPermissionType to_set)
{
	struct _values {
		EM365CalendarPermissionType perm;
		const gchar *text;
	} values[] = {
		{ E_M365_CALENDAR_PERMISSION_NONE, N_("Not shared") },
		{ E_M365_CALENDAR_PERMISSION_FREE_BUSY_READ, N_("Can view when I’m busy") },
		{ E_M365_CALENDAR_PERMISSION_LIMITED_READ, N_("Can view titles and locations") },
		{ E_M365_CALENDAR_PERMISSION_READ, N_("Can view all details") },
		{ E_M365_CALENDAR_PERMISSION_WRITE, N_("Can edit") },
		{ E_M365_CALENDAR_PERMISSION_DELEGATE_WITHOUT_PRIVATE_EVENT_ACCESS, N_("Delegate without private events") },
		{ E_M365_CALENDAR_PERMISSION_DELEGATE_WITH_PRIVATE_EVENT_ACCESS, N_("Delegate with private events") },
		/* Translators: It's for custom permission setting, like some not known what it is precisely */
		{ E_M365_CALENDAR_PERMISSION_CUSTOM, N_("Custom") }
	};

	GtkComboBoxText *text_combo;
	guint ii;

	text_combo = GTK_COMBO_BOX_TEXT (combo);

	gtk_combo_box_text_remove_all (text_combo);

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		if ((allowed & (values[ii].perm)) != 0)
			gtk_combo_box_text_append (text_combo, perm_to_id (values[ii].perm), _(values[ii].text));
	}

	if (!gtk_combo_box_set_active_id (combo, perm_to_id (to_set))) {
		for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
			if ((to_set & (values[ii].perm)) != 0)
				gtk_combo_box_text_append (text_combo, perm_to_id (values[ii].perm), _(values[ii].text));
		}

		if (!gtk_combo_box_set_active_id (combo, perm_to_id (to_set))) {
			const gchar *id = perm_to_id (to_set);

			gtk_combo_box_text_append (text_combo, id, _("Unknown permission"));
			gtk_combo_box_set_active_id (combo, id);
		}
	}
}

typedef struct _PermissionData {
	gint ref_count;

	DialogData *dlg_data;
	gchar *id;
	GtkWidget *combo;
	GtkGrid *grid;
	gint grid_row;
	EM365CalendarPermissionType perm_type;
} PermissionData;

static PermissionData *
permission_data_new (DialogData *dlg_data,
		     const gchar *id,
		     GtkGrid *grid,
		     gint grid_row,
		     EM365CalendarPermissionType perm_type)
{
	PermissionData *perm_data;

	perm_data = g_new0 (PermissionData, 1);
	perm_data->ref_count = 1;
	perm_data->dlg_data = dialog_data_ref (dlg_data);
	perm_data->id = g_strdup (id);
	perm_data->grid = grid;
	perm_data->grid_row = grid_row;
	perm_data->perm_type = perm_type;

	return perm_data;
}

static PermissionData *
permission_data_ref (PermissionData *perm_data)
{
	if (perm_data)
		g_atomic_int_inc (&perm_data->ref_count);

	return perm_data;
}

static void
permission_data_unref (gpointer ptr)
{
	PermissionData *perm_data = ptr;

	if (perm_data && g_atomic_int_dec_and_test (&perm_data->ref_count)) {
		dialog_data_unref (perm_data->dlg_data);
		g_free (perm_data->id);
		g_free (perm_data);
	}
}

static void
update_permission_thread (DialogData *dlg_data,
			  gpointer user_data,
			  gpointer *out_result,
			  GDestroyNotify *out_result_free_func,
			  GCancellable *cancellable,
			  GError **error)
{
	PermissionData *perm_data = user_data;
	JsonBuilder *builder;

	if (!dlg_data->cnc) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Cannot create connection object."));
		return;
	}

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_calendar_permission_add_role (builder, perm_data->perm_type);
	e_m365_json_end_object_member (builder);

	e_m365_connection_update_calendar_permission_sync (dlg_data->cnc, NULL, dlg_data->group_id,
		dlg_data->calendar_id, perm_data->id, builder, cancellable, error);

	g_clear_object (&builder);
}

static void
permission_combo_changed_cb (GtkComboBox *combo,
			     gpointer user_data)
{
	PermissionData *perm_data = user_data;
	EM365CalendarPermissionType perm_type;

	perm_type = id_to_perm (gtk_combo_box_get_active_id (combo));

	if (perm_type != E_M365_CALENDAR_PERMISSION_NOT_SET &&
	    perm_type != E_M365_CALENDAR_PERMISSION_UNKNOWN) {
		perm_data->perm_type = perm_type;

		run_op_in_thread (perm_data->dlg_data, _("Updating permission, please wait…"),
			_("Failed to update permission: "),
			update_permission_thread,
			NULL,
			permission_data_ref (perm_data), permission_data_unref);
	}
}

static void
permission_grid_row_removed (DialogData *dlg_data,
			     GtkGrid *grid,
			     gint row)
{
	GHashTableIter iter;
	gpointer value = NULL;

	g_hash_table_iter_init (&iter, dlg_data->perms);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		PermissionData *perm_data = value;

		if (perm_data && perm_data->grid == grid && perm_data->grid_row >= row)
			perm_data->grid_row--;
	}
}

static void
remove_permission_thread (DialogData *dlg_data,
			  gpointer user_data,
			  gpointer *out_result,
			  GDestroyNotify *out_result_free_func,
			  GCancellable *cancellable,
			  GError **error)
{
	PermissionData *perm_data = user_data;

	if (!dlg_data->cnc) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Cannot create connection object."));
		return;
	}

	e_m365_connection_delete_calendar_permission_sync (dlg_data->cnc, NULL, dlg_data->group_id,
		dlg_data->calendar_id, perm_data->id, cancellable, error);
}

static void
remove_permission_idle (DialogData *dlg_data,
			gpointer result,
			gpointer user_data)
{
	PermissionData *perm_data = user_data;

	GtkWidget *label;
	guint *p_n_rows;

	if (!perm_data->grid)
		return;

	if (perm_data->grid == dlg_data->in_org_grid) {
		label = dlg_data->in_org_label;
		p_n_rows = &dlg_data->n_in_org_rows;
	} else if (perm_data->grid == dlg_data->out_org_grid) {
		label = dlg_data->out_org_label;
		p_n_rows = &dlg_data->n_out_org_rows;
	} else {
		g_warn_if_reached ();
		return;
	}

	if (*p_n_rows > 0) {
		GtkGrid *grid = perm_data->grid;
		gint row = perm_data->grid_row;

		g_hash_table_remove (dlg_data->perms, perm_data->id);
		gtk_grid_remove_row (grid, row);

		*p_n_rows = (*p_n_rows) - 1;

		if (!*p_n_rows) {
			gtk_widget_set_visible (label, FALSE);
			gtk_widget_set_visible (GTK_WIDGET (grid), FALSE);
		} else {
			permission_grid_row_removed (dlg_data, grid, row);
		}
	}
}

static void
permission_remove_clicked_cb (GtkButton *button,
			      gpointer user_data)
{
	PermissionData *perm_data = user_data;

	run_op_in_thread (perm_data->dlg_data, _("Removing permission, please wait…"),
		_("Failed to remove permission: "),
		remove_permission_thread,
		remove_permission_idle,
		permission_data_ref (perm_data), permission_data_unref);
}

static void
add_permission_to_dialog (DialogData *dlg_data,
			  EM365CalendarPermission *perm)
{
	EM365EmailAddress *addr;
	PermissionData *perm_data;
	const gchar *name, *address;
	GtkGrid *grid;
	guint *p_n_rows;
	GtkWidget *widget;

	if (!perm || !e_m365_calendar_permission_get_id (perm))
		return;

	addr = e_m365_calendar_permission_get_email_address (perm);
	if (!addr)
		return;

	name = e_m365_email_address_get_name (addr);
	address = e_m365_email_address_get_address (addr);

	if ((!name || !*name) && (!address || !*address))
		return;

	perm_data = g_hash_table_lookup (dlg_data->perms, e_m365_calendar_permission_get_id (perm));
	if (perm_data) {
		g_signal_handlers_block_by_func (perm_data->combo, permission_combo_changed_cb, perm_data);
		fill_perms_combo (GTK_COMBO_BOX (perm_data->combo),
			e_m365_calendar_permission_get_allowed_roles (perm),
			e_m365_calendar_permission_get_role (perm));
		g_signal_handlers_unblock_by_func (perm_data->combo, permission_combo_changed_cb, perm_data);

		return;
	}

	if (e_m365_calendar_permission_get_is_inside_organization (perm)) {
		gtk_widget_set_visible (dlg_data->in_org_label, TRUE);
		grid = dlg_data->in_org_grid;
		p_n_rows = &dlg_data->n_in_org_rows;
	} else {
		gtk_widget_set_visible (dlg_data->out_org_label, TRUE);
		grid = dlg_data->out_org_grid;
		p_n_rows = &dlg_data->n_out_org_rows;
	}

	perm_data = permission_data_new (dlg_data, e_m365_calendar_permission_get_id (perm), grid, *p_n_rows, E_M365_CALENDAR_PERMISSION_UNKNOWN);

	if (name && *name && address && *address) {
		GtkWidget *box;
		PangoAttrList *attrs;

		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);

		widget = gtk_label_new (name);
		g_object_set (
			G_OBJECT (widget),
			"hexpand", TRUE,
			"vexpand", FALSE,
			"use-underline", FALSE,
			"xalign", 0.0,
			"halign", GTK_ALIGN_START,
			"max-width-chars", 45,
			"width-chars", 25,
			"ellipsize", PANGO_ELLIPSIZE_END,
			NULL);

		gtk_box_pack_start (GTK_BOX (box), widget, TRUE, TRUE, 0);

		attrs = pango_attr_list_new ();
		pango_attr_list_insert (attrs, pango_attr_scale_new (0.8));

		widget = gtk_label_new (address);
		g_object_set (
			G_OBJECT (widget),
			"hexpand", TRUE,
			"vexpand", FALSE,
			"use-underline", FALSE,
			"attributes", attrs,
			"xalign", 0.0,
			"halign", GTK_ALIGN_START,
			"max-width-chars", 45,
			"width-chars", 25,
			"ellipsize", PANGO_ELLIPSIZE_END,
			NULL);

		gtk_box_pack_start (GTK_BOX (box), widget, TRUE, TRUE, 0);

		gtk_widget_show_all (box);

		pango_attr_list_unref (attrs);

		widget = box;
	} else {
		if (!address && name && g_ascii_strcasecmp (name, "My Organization") == 0)
			name = _("People in the Organization");

		widget = gtk_label_new (name && *name ? name : address);
		g_object_set (
			G_OBJECT (widget),
			"hexpand", TRUE,
			"vexpand", FALSE,
			"use-underline", FALSE,
			"xalign", 0.0,
			"halign", GTK_ALIGN_START,
			"valign", GTK_ALIGN_CENTER,
			"max-width-chars", 45,
			"width-chars", 25,
			"ellipsize", PANGO_ELLIPSIZE_END,
			"visible", TRUE,
			NULL);
	}

	gtk_grid_attach (grid, widget, 0, perm_data->grid_row, 1, 1);

	widget = gtk_combo_box_text_new ();
	gtk_widget_set_visible (widget, TRUE);
	fill_perms_combo (GTK_COMBO_BOX (widget),
		e_m365_calendar_permission_get_allowed_roles (perm),
		e_m365_calendar_permission_get_role (perm));
	g_signal_connect_data (widget, "changed",
		G_CALLBACK (permission_combo_changed_cb), permission_data_ref (perm_data),
		(GClosureNotify) permission_data_unref, 0);
	perm_data->combo = widget;

	gtk_grid_attach (grid, widget, 1, perm_data->grid_row, 1, 1);

	if (e_m365_calendar_permission_get_is_removable (perm)) {
		widget = gtk_button_new_from_icon_name ("user-trash", GTK_ICON_SIZE_BUTTON);
		gtk_widget_set_tooltip_text (widget, _("Remove permission"));
		gtk_widget_set_visible (widget, TRUE);
		gtk_grid_attach (grid, widget, 2, perm_data->grid_row, 1, 1);

		g_signal_connect_data (widget, "clicked",
			G_CALLBACK (permission_remove_clicked_cb), permission_data_ref (perm_data),
			(GClosureNotify) permission_data_unref, 0);
	}

	gtk_widget_set_visible (GTK_WIDGET (grid), TRUE);

	*p_n_rows = (*p_n_rows) + 1;

	g_hash_table_insert (dlg_data->perms, perm_data->id, perm_data);
}

typedef struct _ShareData {
	EDestination *dest;
	EM365CalendarPermissionType perm_type;
} ShareData;

static void
share_data_free (gpointer ptr)
{
	ShareData *shr_data = ptr;

	if (shr_data) {
		g_clear_object (&shr_data->dest);
		g_free (shr_data);
	}
}

static void
create_permission_thread (DialogData *dlg_data,
			  gpointer user_data,
			  gpointer *out_result,
			  GDestroyNotify *out_result_free_func,
			  GCancellable *cancellable,
			  GError **error)
{
	ShareData *shr_data = user_data;
	EM365CalendarPermission *created_permission = NULL;
	JsonBuilder *builder;
	const gchar *name = NULL, *address = NULL;

	if (!dlg_data->cnc) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Cannot create connection object."));
		return;
	}

	name = e_destination_get_name (shr_data->dest);
	address = e_destination_get_email (shr_data->dest);

	if (name && !*name)
		name = NULL;
	if (address && !*address)
		address = NULL;

	if (!address) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("No valid email address found."));
		return;
	}

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_calendar_permission_add_email_address (builder, name, address);
	e_m365_calendar_permission_add_is_removable (builder, TRUE);
	e_m365_calendar_permission_add_role (builder, shr_data->perm_type);
	e_m365_json_end_object_member (builder);

	if (e_m365_connection_create_calendar_permission_sync (dlg_data->cnc, NULL, dlg_data->group_id,
		dlg_data->calendar_id, builder, &created_permission, cancellable, error)) {
		*out_result = created_permission;
		*out_result_free_func = (GDestroyNotify) json_object_unref;
	}

	g_clear_object (&builder);
}

static void
create_permission_idle (DialogData *dlg_data,
			gpointer result,
			gpointer user_data)
{
	EM365CalendarPermission *created_permission = result;

	add_permission_to_dialog (dlg_data, created_permission);
	gtk_entry_set_text (dlg_data->name_entry, "");
	gtk_widget_grab_focus (GTK_WIDGET (dlg_data->name_entry));
	e_util_ensure_scrolled_window_height (GTK_SCROLLED_WINDOW (dlg_data->perms_scrolled));
}

static void
permission_share_clicked_cb (GtkButton *button,
			     gpointer user_data)
{
	DialogData *dlg_data = user_data;
	GList *dests;
	EM365CalendarPermissionType perm_type;

	dests = e_destination_store_list_destinations (dlg_data->dest_store);
	if (!dests)
		return;

	perm_type = id_to_perm (gtk_combo_box_get_active_id (dlg_data->perms_combo));

	if (perm_type != E_M365_CALENDAR_PERMISSION_NOT_SET &&
	    perm_type != E_M365_CALENDAR_PERMISSION_UNKNOWN) {
		ShareData *shr_data;

		shr_data = g_new0 (ShareData, 1);
		shr_data->dest = e_destination_copy (dests->data);
		shr_data->perm_type = perm_type;

		run_op_in_thread (dlg_data, _("Saving new permission, please wait…"),
			_("Failed to save permission: "),
			create_permission_thread,
			create_permission_idle,
			shr_data, share_data_free);
	} else {
		g_warn_if_reached ();
	}

	g_list_free (dests);
}

static void
free_permissions_slist (gpointer ptr)
{
	GSList *list = ptr;

	g_slist_free_full (list, (GDestroyNotify) json_object_unref);
}

static void
read_calendar_permissions_thread (DialogData *dlg_data,
				  gpointer user_data,
				  gpointer *out_result,
				  GDestroyNotify *out_result_free_func,
				  GCancellable *cancellable,
				  GError **error)
{
	GSList *permissions = NULL;

	if (!dlg_data->cnc) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Cannot create connection object."));
		return;
	}

	e_m365_connection_list_calendar_permissions_sync (dlg_data->cnc, NULL, dlg_data->group_id, dlg_data->calendar_id, &permissions, cancellable, error);

	*out_result = permissions;
	*out_result_free_func = free_permissions_slist;
}

static void
read_calendar_permissions_idle (DialogData *dlg_data,
				gpointer result,
				gpointer user_data)
{
	GSList *permissions = result, *link;

	for (link = permissions; link; link = g_slist_next (link)) {
		EM365CalendarPermission *perm = link->data;

		add_permission_to_dialog (dlg_data, perm);
	}

	e_util_ensure_scrolled_window_height (GTK_SCROLLED_WINDOW (dlg_data->perms_scrolled));
}

static void
progress_cancel_cb (GtkButton *button,
		    gpointer user_data)
{
	DialogData *dlg_data = user_data;

	g_return_if_fail (dlg_data != NULL);

	g_cancellable_cancel (dlg_data->cancellable);
}

static void
progress_close_cb (GtkButton *button,
		    gpointer user_data)
{
	DialogData *dlg_data = user_data;

	g_return_if_fail (dlg_data != NULL);

	gtk_widget_set_visible (dlg_data->progress_box, FALSE);
}

static void
name_entry_changed_cb (GtkEntry *entry,
		       gpointer user_data)
{
	GtkWidget *share_button = user_data;

	gtk_widget_set_sensitive (share_button, !e_str_is_empty (gtk_entry_get_text (entry)));
}

static void
ensure_proxy_resolver (EM365Connection *cnc,
		       ESourceRegistry *registry,
		       ESource *account_source,
		       ESource *source)
{
	GProxyResolver *proxy_resolver;
	ESourceAuthentication *extension;
	ESource *proxy_source = NULL;
	gchar *uid;

	proxy_resolver = e_m365_connection_ref_proxy_resolver (cnc);
	if (proxy_resolver) {
		g_object_unref (proxy_resolver);
		return;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION))
		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	else
		extension = e_source_get_extension (account_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	uid = e_source_authentication_dup_proxy_uid (extension);
	if (uid != NULL) {
		proxy_source = e_source_registry_ref_source (registry, uid);
		g_free (uid);
	}

	if (proxy_source != NULL) {
		proxy_resolver = G_PROXY_RESOLVER (source);
		if (!g_proxy_resolver_is_supported (proxy_resolver))
			proxy_resolver = NULL;
	}

	e_m365_connection_set_proxy_resolver (cnc, proxy_resolver);

	g_clear_object (&proxy_source);
}

void
e_m365_edit_calendar_permissions (GtkWindow *parent,
				  ESourceRegistry *registry,
				  ESource *account_source,
				  ESource *source,
				  CamelM365Settings *m365_settings,
				  const gchar *group_id,
				  const gchar *calendar_id)
{
	DialogData *dlg_data;
	PangoAttrList *attrs;
	GObject *dialog;
	GtkWidget *content;
	GtkWidget *label, *widget, *button, *hvbox;
	GtkGrid *grid;
	gchar *text;
	GError *local_error = NULL;
	gint row;

	if (parent)
		g_return_if_fail (GTK_IS_WINDOW (parent));
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (E_IS_SOURCE (account_source));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (m365_settings));
	g_return_if_fail (calendar_id != NULL);

	dlg_data = g_new0 (DialogData, 1);
	dlg_data->ref_count = 1;
	dlg_data->cnc = e_m365_connection_new_full (source, m365_settings, TRUE);
	dlg_data->dest_store = e_destination_store_new ();
	dlg_data->group_id = g_strdup (group_id);
	dlg_data->calendar_id = g_strdup (calendar_id);
	dlg_data->css_provider = gtk_css_provider_new ();
	dlg_data->perms = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, permission_data_unref);

	ensure_proxy_resolver (dlg_data->cnc, registry, account_source, source);

	dlg_data->dialog = gtk_dialog_new_with_buttons (
		_("Edit calendar permissions…"),
		parent,
		GTK_DIALOG_DESTROY_WITH_PARENT | (e_util_get_use_header_bar () ? GTK_DIALOG_USE_HEADER_BAR : 0),
		_("Cl_ose"), GTK_RESPONSE_CANCEL,
		NULL);

	gtk_window_set_default_size (GTK_WINDOW (dlg_data->dialog), 580, 360);

	dialog = G_OBJECT (dlg_data->dialog);
	g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
	g_object_set_data_full (dialog, E_M365_PERM_DLG_DATA, dlg_data, dialog_data_unref);

	content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	widget = gtk_overlay_new ();
	gtk_container_add (GTK_CONTAINER (content), widget);

	grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_row_homogeneous (grid, FALSE);
	gtk_grid_set_row_spacing (grid, 6);
	gtk_grid_set_column_homogeneous (grid, FALSE);
	gtk_grid_set_column_spacing (grid, 6);
	gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (grid));

	hvbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	g_object_set (
		G_OBJECT (hvbox),
		"halign", GTK_ALIGN_CENTER,
		"valign", GTK_ALIGN_START,
		"name", "progress-box",
		NULL);
	dlg_data->progress_box = hvbox;

	gtk_overlay_add_overlay (GTK_OVERLAY (widget), hvbox);

	widget = gtk_label_new ("");
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"use-underline", FALSE,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		"max-width-chars", 60,
		"width-chars", 30,
		"wrap", TRUE,
		NULL);

	gtk_box_pack_start (GTK_BOX (hvbox), widget, TRUE, TRUE, 0);

	dlg_data->progress_label = GTK_LABEL (widget);

	button = gtk_button_new_with_mnemonic (_("_Cancel"));
	g_object_set (
		G_OBJECT (button),
		"halign", GTK_ALIGN_CENTER,
		"valign", GTK_ALIGN_CENTER,
		NULL);
	gtk_box_pack_start (GTK_BOX (hvbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked", G_CALLBACK (progress_cancel_cb), dlg_data);

	dlg_data->progress_cancel_button = button;

	button = gtk_button_new_from_icon_name ("window-close", GTK_ICON_SIZE_BUTTON);
	g_object_set (
		G_OBJECT (button),
		"halign", GTK_ALIGN_CENTER,
		"valign", GTK_ALIGN_CENTER,
		NULL);
	gtk_box_pack_start (GTK_BOX (hvbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked", G_CALLBACK (progress_close_cb), dlg_data);

	dlg_data->progress_close_button = button;

	if (!gtk_css_provider_load_from_data (dlg_data->css_provider,
		"box {"
			"padding:8px;"
			"border:1px solid @theme_unfocused_selected_fg_color;"
			"border-radius: 0 0 8px 8px;"
			"color: white;"
			"background-color:black;"
			"opacity:0.8;"
		"}", -1, &local_error)) {
		g_warning ("%s: Failed to parse CSS data: %s", G_STRFUNC, local_error ? local_error->message : "Unknown error");
		g_clear_error (&local_error);
	}

	gtk_style_context_add_provider (gtk_widget_get_style_context (hvbox),
		GTK_STYLE_PROVIDER (dlg_data->css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	row = 0;

	label = gtk_label_new (_("Calendar:"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		NULL);

	text = gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL ?
		g_strdup_printf ("%s : %s", e_source_get_display_name (source), e_source_get_display_name (account_source)) :
		g_strdup_printf ("%s : %s", e_source_get_display_name (account_source), e_source_get_display_name (source));
	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	widget = gtk_label_new (text);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"use-underline", FALSE,
		"attributes", attrs,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		"max-width-chars", 60,
		"width-chars", 30,
		"wrap", TRUE,
		NULL);
	g_free (text);

	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, widget, 1, row, 1, 1);

	row++;

	label = gtk_label_new (_("Choose sharing permissions for this calendar. You can add people from within the organization or outside of it. It can be changed any time."));
	g_object_set (
		G_OBJECT (label),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"xalign", 0.0,
		"max-width-chars", 60,
		"width-chars", 40,
		"wrap", TRUE,
		NULL);

	gtk_grid_attach (grid, label, 0, row, 2, 1);

	row++;

	hvbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_grid_attach (grid, hvbox, 0, row, 2, 1);

	row++;

	widget = e_name_selector_entry_new (e_shell_get_client_cache (e_shell_get_default ()));
	e_name_selector_entry_set_destination_store (E_NAME_SELECTOR_ENTRY (widget), dlg_data->dest_store);
	gtk_box_pack_start (GTK_BOX (hvbox), widget, TRUE, TRUE, 0);

	dlg_data->name_entry = GTK_ENTRY (widget);

	widget = gtk_combo_box_text_new ();
	gtk_box_pack_start (GTK_BOX (hvbox), widget, FALSE, FALSE, 0);

	dlg_data->perms_combo = GTK_COMBO_BOX (widget);
	fill_perms_combo (dlg_data->perms_combo,
		E_M365_CALENDAR_PERMISSION_FREE_BUSY_READ |
		E_M365_CALENDAR_PERMISSION_LIMITED_READ |
		E_M365_CALENDAR_PERMISSION_READ |
		E_M365_CALENDAR_PERMISSION_WRITE |
		E_M365_CALENDAR_PERMISSION_DELEGATE_WITHOUT_PRIVATE_EVENT_ACCESS |
		E_M365_CALENDAR_PERMISSION_DELEGATE_WITH_PRIVATE_EVENT_ACCESS,
		E_M365_CALENDAR_PERMISSION_FREE_BUSY_READ);

	widget = gtk_button_new_with_mnemonic (_("_Share"));
	gtk_widget_set_sensitive (widget, FALSE);
	gtk_box_pack_start (GTK_BOX (hvbox), widget, FALSE, FALSE, 0);

	g_signal_connect_data (widget, "clicked",
		G_CALLBACK (permission_share_clicked_cb), dialog_data_ref (dlg_data),
			(GClosureNotify) dialog_data_unref, 0);

	dlg_data->share_button = widget;

	g_signal_connect_object (dlg_data->name_entry, "changed",
		G_CALLBACK (name_entry_changed_cb), dlg_data->share_button, 0);

	hvbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (widget), hvbox);
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"min-content-width", 120,
		"min-content-height", 120,
		"shadow-type", GTK_SHADOW_NONE,
		NULL);

	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	row++;

	dlg_data->perms_scrolled = widget;

	gtk_widget_show_all (content);

	gtk_widget_set_visible (dlg_data->progress_box, FALSE);

	label = gtk_label_new (_("Inside the organization"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"use-underline", FALSE,
		"attributes", attrs,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		"visible", FALSE,
		NULL);
	gtk_box_pack_start (GTK_BOX (hvbox), label, FALSE, FALSE, 0);

	widget = gtk_grid_new ();
	g_object_set (
		G_OBJECT (widget),
		"column-homogeneous", FALSE,
		"row-homogeneous", TRUE,
		"row-spacing", 4,
		"visible", FALSE,
		NULL);
	gtk_box_pack_start (GTK_BOX (hvbox), widget, FALSE, FALSE, 0);
	dlg_data->in_org_label = label;
	dlg_data->in_org_grid = GTK_GRID (widget);

	label = gtk_label_new (_("Outside the organization"));
	g_object_set (
		G_OBJECT (label),
		"hexpand", TRUE,
		"vexpand", FALSE,
		"use-underline", FALSE,
		"attributes", attrs,
		"xalign", 0.0,
		"halign", GTK_ALIGN_START,
		"visible", FALSE,
		NULL);
	gtk_box_pack_start (GTK_BOX (hvbox), label, FALSE, FALSE, 12);

	widget = gtk_grid_new ();
	g_object_set (
		G_OBJECT (widget),
		"column-homogeneous", FALSE,
		"row-homogeneous", TRUE,
		"row-spacing", 4,
		"visible", FALSE,
		NULL);
	gtk_box_pack_start (GTK_BOX (hvbox), widget, FALSE, FALSE, 0);
	dlg_data->out_org_label = label;
	dlg_data->out_org_grid = GTK_GRID (widget);

	pango_attr_list_unref (attrs);

	gtk_widget_show (GTK_WIDGET (dialog));

	run_op_in_thread (dlg_data, _("Reading calendar permissions, please wait…"),
		_("Failed to read permissions: "),
		read_calendar_permissions_thread,
		read_calendar_permissions_idle,
		NULL, NULL);
}
