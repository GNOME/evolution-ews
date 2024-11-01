/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <e-util/e-util.h>
#include <mail/e-mail-backend.h>

#include "common/camel-ews-settings.h"
#include "common/e-ews-connection.h"
#include "common/e-ews-connection-utils.h"
#include "common/e-ews-oof-settings.h"
#include "camel/camel-ews-store.h"

#include "e-ews-config-utils.h"
#include "e-ews-search-user.h"
#include "e-mail-config-ews-delegates-page.h"

typedef struct _AsyncContext AsyncContext;

struct _EMailConfigEwsDelegatesPagePrivate {
	ESourceRegistry *registry;
	ESource *account_source;
	ESource *identity_source;
	ESource *collection_source;
	EEwsConnection *connection;

	/* The try_password() method deposits results here.
	 * This avoids calling GTK+ functions from multiple threads. */
	GSList *orig_delegates; /* EwsDelegateInfo * */
	GSList *new_delegates; /* EwsDelegateInfo * */
	EwsDelegateDeliver deliver_to;
	GMutex delegates_lock;

	GCancellable *refresh_cancellable;

	GtkWidget *users_tree_view;			/* not referenced */
	GtkWidget *add_button;				/* not referenced */
	GtkWidget *remove_button;			/* not referenced */
	GtkWidget *properties_button;			/* not referenced */
	GtkWidget *deliver_copy_me_radio;		/* not referenced */
	GtkWidget *deliver_delegates_only_radio;	/* not referenced */
	GtkWidget *deliver_delegates_and_me_radio;	/* not referenced */
};

struct _AsyncContext {
	EMailConfigEwsDelegatesPage *page;
	EActivity *activity;
	ESource *source;
	GObject *settings;
};

enum {
	PROP_0,
	PROP_ACCOUNT_SOURCE,
	PROP_COLLECTION_SOURCE,
	PROP_IDENTITY_SOURCE,
	PROP_REGISTRY
};

/* Forward Declarations */
static void	e_mail_config_ews_delegates_page_interface_init
					(EMailConfigPageInterface *iface);
static void	add_to_tree_view	(EMailConfigEwsDelegatesPage *page,
					 EwsDelegateInfo *di,
					 gboolean select);
static gboolean	page_contains_user	(EMailConfigEwsDelegatesPage *page,
					 const gchar *primary_smtp,
					 GtkTreeIter *piter);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailConfigEwsDelegatesPage, e_mail_config_ews_delegates_page, E_TYPE_MAIL_CONFIG_ACTIVITY_PAGE, 0,
	G_ADD_PRIVATE_DYNAMIC (EMailConfigEwsDelegatesPage)
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_MAIL_CONFIG_PAGE, e_mail_config_ews_delegates_page_interface_init))

static void
async_context_free (gpointer ptr)
{
	AsyncContext *async_context = ptr;

	if (!async_context)
		return;

	if (async_context->settings)
		g_object_thaw_notify (async_context->settings);

	g_clear_object (&async_context->page);
	g_clear_object (&async_context->activity);
	g_clear_object (&async_context->source);
	g_clear_object (&async_context->settings);

	g_slice_free (AsyncContext, async_context);
}

static CamelSettings *
mail_config_ews_delegates_page_get_settings (EMailConfigEwsDelegatesPage *page)
{
	ESource *source;
	ESourceCamel *extension;
	const gchar *extension_name;

	source = e_mail_config_ews_delegates_page_get_collection_source (page);

	extension_name = e_source_camel_get_extension_name ("ews");
	extension = e_source_get_extension (source, extension_name);

	return e_source_camel_get_settings (extension);
}

static const gchar *
mail_config_ews_delegates_page_get_mailbox (EMailConfigEwsDelegatesPage *page)
{
	ESourceRegistry *registry;
	ESource *collection_source;
	GList *list, *link;
	const gchar *collection_uid;
	const gchar *extension_name;
	const gchar *mailbox = NULL;

	/* Find the mail identity source that belongs to
	 * our collection and return its email address. */

	collection_source =
		e_mail_config_ews_delegates_page_get_collection_source (page);
	collection_uid = e_source_get_uid (collection_source);

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	registry = e_mail_config_ews_delegates_page_get_registry (page);
	list = e_source_registry_list_sources (registry, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source = E_SOURCE (link->data);
		ESourceMailIdentity *extension;
		const gchar *parent_uid;

		parent_uid = e_source_get_parent (source);
		if (g_strcmp0 (parent_uid, collection_uid) != 0)
			continue;

		extension = e_source_get_extension (source, extension_name);
		mailbox = e_source_mail_identity_get_address (extension);
		break;
	}

	g_list_free_full (list, g_object_unref);

	return mailbox;
}

static void
enable_delegates_page_widgets (EMailConfigEwsDelegatesPage *page,
                               gboolean enable)
{
	gboolean has_users = FALSE;

	g_return_if_fail (page != NULL);

	if (enable)
		has_users = page->priv->new_delegates != NULL;

	gtk_widget_set_sensitive (page->priv->users_tree_view, enable && has_users);
	gtk_widget_set_sensitive (page->priv->add_button, enable);
	gtk_widget_set_sensitive (page->priv->deliver_copy_me_radio, enable && has_users);
	gtk_widget_set_sensitive (page->priv->deliver_delegates_only_radio, enable && has_users);
	gtk_widget_set_sensitive (page->priv->deliver_delegates_and_me_radio, enable && has_users);

	if (enable) {
		GtkTreeSelection *selection;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (page->priv->users_tree_view));
		enable = gtk_tree_selection_count_selected_rows (selection) == 1;
	}

	gtk_widget_set_sensitive (page->priv->remove_button, enable);
	gtk_widget_set_sensitive (page->priv->properties_button, enable);
}

static EwsDelegateInfo *
copy_delegate_info (const EwsDelegateInfo *src)
{
	EwsDelegateInfo *di;

	g_return_val_if_fail (src != NULL, NULL);

	di = g_new0 (EwsDelegateInfo, 1);
	di->user_id = g_new0 (EwsUserId, 1);
	di->user_id->sid = g_strdup (src->user_id->sid);
	di->user_id->primary_smtp = g_strdup (src->user_id->primary_smtp);
	di->user_id->display_name = g_strdup (src->user_id->display_name);
	di->user_id->distinguished_user = g_strdup (src->user_id->distinguished_user);
	di->user_id->external_user = g_strdup (src->user_id->external_user);
	di->calendar = src->calendar;
	di->tasks = src->tasks;
	di->inbox = src->inbox;
	di->contacts = src->contacts;
	di->notes = src->notes;
	di->journal = src->journal;
	di->meetingcopies = src->meetingcopies;
	di->view_priv_items = src->view_priv_items;

	return di;
}

static void
mail_config_ews_delegates_page_set_account_source (EMailConfigEwsDelegatesPage *page,
                                                   ESource *account_source)
{
	g_return_if_fail (E_IS_SOURCE (account_source));
	g_return_if_fail (page->priv->account_source == NULL);

	page->priv->account_source = g_object_ref (account_source);
}

static void
mail_config_ews_delegates_page_set_collection_source (EMailConfigEwsDelegatesPage *page,
                                                      ESource *collection_source)
{
	g_return_if_fail (E_IS_SOURCE (collection_source));
	g_return_if_fail (page->priv->collection_source == NULL);

	page->priv->collection_source = g_object_ref (collection_source);
}

static void
mail_config_ews_delegates_page_set_identity_source (EMailConfigEwsDelegatesPage *page,
                                                    ESource *identity_source)
{
	g_return_if_fail (E_IS_SOURCE (identity_source));
	g_return_if_fail (page->priv->identity_source == NULL);

	page->priv->identity_source = g_object_ref (identity_source);
}

static void
mail_config_ews_delegates_page_set_registry (EMailConfigEwsDelegatesPage *page,
                                             ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (page->priv->registry == NULL);

	page->priv->registry = g_object_ref (registry);
}

static void
mail_config_ews_delegates_page_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACCOUNT_SOURCE:
			mail_config_ews_delegates_page_set_account_source (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object),
				g_value_get_object (value));
			return;

		case PROP_COLLECTION_SOURCE:
			mail_config_ews_delegates_page_set_collection_source (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object),
				g_value_get_object (value));
			return;

		case PROP_IDENTITY_SOURCE:
			mail_config_ews_delegates_page_set_identity_source (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object),
				g_value_get_object (value));
			return;

		case PROP_REGISTRY:
			mail_config_ews_delegates_page_set_registry (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_delegates_page_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACCOUNT_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_delegates_page_get_account_source (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object)));
			return;

		case PROP_COLLECTION_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_delegates_page_get_collection_source (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object)));
			return;

		case PROP_IDENTITY_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_delegates_page_get_identity_source (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object)));
			return;

		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_mail_config_ews_delegates_page_get_registry (
				E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_delegates_page_dispose (GObject *object)
{
	EMailConfigEwsDelegatesPage *page = E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object);

	if (page->priv->refresh_cancellable) {
		g_cancellable_cancel (page->priv->refresh_cancellable);
		g_clear_object (&page->priv->refresh_cancellable);
	}

	g_clear_object (&page->priv->registry);
	g_clear_object (&page->priv->account_source);
	g_clear_object (&page->priv->collection_source);
	g_clear_object (&page->priv->identity_source);
	g_clear_object (&page->priv->connection);

	g_slist_free_full (page->priv->orig_delegates, (GDestroyNotify) ews_delegate_info_free);
	page->priv->orig_delegates = NULL;

	g_slist_free_full (page->priv->new_delegates, (GDestroyNotify) ews_delegate_info_free);
	page->priv->new_delegates = NULL;

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_config_ews_delegates_page_parent_class)->dispose (object);
}

static void
mail_config_ews_delegates_page_finalize (GObject *object)
{
	EMailConfigEwsDelegatesPage *page = E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object);

	g_mutex_clear (&page->priv->delegates_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_mail_config_ews_delegates_page_parent_class)->finalize (object);
}

static void
delegates_tree_selection_changed_cb (GtkTreeSelection *selection,
                                     EMailConfigEwsDelegatesPage *page)
{
	g_return_if_fail (selection != NULL);
	g_return_if_fail (page != NULL);

	enable_delegates_page_widgets (page, page->priv->connection != NULL);
}

static GtkWidget *
add_permission_level_combo_row (GtkGrid *grid,
                                gint row,
                                const gchar *icon_name,
                                const gchar *label_text,
                                EwsPermissionLevel preselect)
{
	GtkWidget *image = NULL, *label;
	GtkComboBoxText *combo;
	gint index = 0;

	g_return_val_if_fail (grid != NULL, NULL);
	g_return_val_if_fail (label_text != NULL, NULL);
	g_return_val_if_fail (preselect != EwsPermissionLevel_Unknown, NULL);

	combo = GTK_COMBO_BOX_TEXT (gtk_combo_box_text_new ());
	gtk_combo_box_text_append_text (combo, C_("PermissionsLevel", "None"));
	gtk_combo_box_text_append_text (combo, C_("PermissionsLevel", "Reviewer (can read items)"));
	gtk_combo_box_text_append_text (combo, C_("PermissionsLevel", "Author (can read and create items)"));
	gtk_combo_box_text_append_text (combo, C_("PermissionsLevel", "Editor (can read, create and modify items)"));

	switch (preselect) {
	/* to make compiler happy */
	/* coverity[dead_error_begin] */
	case EwsPermissionLevel_Unknown:
		break;
	case EwsPermissionLevel_None:
		index = 0;
		break;
	case EwsPermissionLevel_Reviewer:
		index = 1;
		break;
	case EwsPermissionLevel_Author:
		index = 2;
		break;
	case EwsPermissionLevel_Editor:
		index = 3;
		break;
	case EwsPermissionLevel_Custom:
		gtk_combo_box_text_append_text (combo, C_("PermissionsLevel", "Custom"));
		index = 4;
		break;
	}

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), index);
	g_object_set (
		G_OBJECT (combo),
		"valign", GTK_ALIGN_CENTER,
		NULL);

	if (icon_name)
		image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);
	label = gtk_label_new_with_mnemonic (label_text);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (combo));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

	if (image)
		gtk_grid_attach (grid, image, 0, row, 1, 1);
	gtk_grid_attach (grid, label, 1, row, 1, 1);
	gtk_grid_attach (grid, GTK_WIDGET (combo), 2, row, 1, 1);

	return GTK_WIDGET (combo);
}

static EwsPermissionLevel
get_permission_level_from_combo (GtkWidget *combo)
{
	EwsPermissionLevel res = EwsPermissionLevel_Unknown;

	g_return_val_if_fail (GTK_IS_COMBO_BOX_TEXT (combo), res);

	switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo))) {
	case 0:
		res = EwsPermissionLevel_None;
		break;
	case 1:
		res = EwsPermissionLevel_Reviewer;
		break;
	case 2:
		res = EwsPermissionLevel_Author;
		break;
	case 3:
		res = EwsPermissionLevel_Editor;
		break;
	case 4:
		res = EwsPermissionLevel_Custom;
		break;
	default:
		g_warn_if_reached ();
		break;
	}

	return res;
}

static void
enable_cal_copies_by_combo_index (GtkComboBox *combo,
                                  GtkWidget *check)
{
	gtk_widget_set_sensitive (check, gtk_combo_box_get_active (combo) >= 3);
}

static void
show_delegate_properties_modal (EMailConfigEwsDelegatesPage *page,
                                EwsDelegateInfo *di)
{
	GtkWidget *dialog, *widget, *content_area;
	GtkGrid *grid;
	GtkWidget *lev_calendar, *lev_tasks, *lev_inbox, *lev_contacts, *lev_notes, *lev_journal;
	GtkWidget *check_cal_copies, *check_see_private_items;
	GtkWindow *parent;
	const gchar *name;
	gchar *text;
	gint row = 0;

	g_return_if_fail (page != NULL);
	g_return_if_fail (di != NULL);

	name = di->user_id->display_name;
	if (!name)
		name = di->user_id->primary_smtp;

	parent = e_ews_config_utils_get_widget_toplevel_window (GTK_WIDGET (page));
	dialog = gtk_dialog_new_with_buttons (
		_("Delegate permissions"), parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL);

	widget = gtk_grid_new ();
	grid = GTK_GRID (widget);
	g_object_set (
		G_OBJECT (grid),
		"row-spacing", 6,
		"column-spacing", 6,
		"orientation", GTK_ORIENTATION_VERTICAL,
		"border-width", 12,
		NULL);

	row = 0;

	lev_calendar = add_permission_level_combo_row (grid, row, "x-office-calendar", _("C_alendar"), di->calendar);
	row++;

	check_cal_copies = gtk_check_button_new_with_mnemonic (_("_Delegate receives copies of meeting-related messages sent to me"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_cal_copies), di->meetingcopies);
	gtk_grid_attach (grid, check_cal_copies, 1, row, 2, 1);
	row++;

	lev_tasks = add_permission_level_combo_row (grid, row, "evolution-tasks", _("_Tasks"), di->tasks);
	row++;

	lev_inbox = add_permission_level_combo_row (grid, row, "mail-inbox", _("_Inbox"), di->inbox);
	row++;

	lev_contacts = add_permission_level_combo_row (grid, row, "x-office-address-book", _("C_ontacts"), di->contacts);
	row++;

	lev_notes = add_permission_level_combo_row (grid, row, "evolution-memos", _("_Notes"), di->notes);
	row++;

	lev_journal = add_permission_level_combo_row (grid, row, NULL, _("_Journal"), di->journal);
	row++;

	text = g_strdup_printf (_("Delegate “%s” has the following permissions"), name);
	widget = gtk_frame_new (text);
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (grid));
	g_free (text);

	grid = GTK_GRID (gtk_grid_new ());
	g_object_set (
		G_OBJECT (grid),
		"row-spacing", 6,
		"orientation", GTK_ORIENTATION_VERTICAL,
		"border-width", 12,
		NULL);

	row = 0;

	gtk_grid_attach (grid, widget, 0, row, 1, 1);
	row++;

	check_see_private_items = gtk_check_button_new_with_mnemonic (_("Delegate can see my _private items"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_see_private_items), di->view_priv_items);
	gtk_grid_attach (grid, check_see_private_items, 0, row, 1, 1);
	row++;

	gtk_widget_show_all (GTK_WIDGET (grid));

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_add (GTK_CONTAINER (content_area), GTK_WIDGET (grid));

	g_signal_connect (lev_calendar, "changed", G_CALLBACK (enable_cal_copies_by_combo_index), check_cal_copies);
	enable_cal_copies_by_combo_index (GTK_COMBO_BOX (lev_calendar), check_cal_copies);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
		di->meetingcopies = gtk_widget_get_sensitive (check_cal_copies) &&
			gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_cal_copies));

		di->view_priv_items = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_see_private_items));

		#define update_perm_level(_member, _combo) {			\
			EwsPermissionLevel level;				\
										\
			level = get_permission_level_from_combo (_combo);	\
			if (di->_member != level) {				\
				di->_member = level;				\
			}							\
		}

		update_perm_level (calendar, lev_calendar);
		update_perm_level (tasks, lev_tasks);
		update_perm_level (inbox, lev_inbox);
		update_perm_level (contacts, lev_contacts);
		update_perm_level (notes, lev_notes);
		update_perm_level (journal, lev_journal);

		#undef update_perm_level

		if (!page_contains_user (page, di->user_id->primary_smtp, NULL))
			add_to_tree_view (page, copy_delegate_info (di), TRUE);
	}

	gtk_widget_destroy (dialog);
}

static void
retrieve_user_permissions_idle_cb (GObject *ppage,
                                   gpointer pdi,
                                   GCancellable *cancellable,
                                   GError **perror)
{
	EMailConfigEwsDelegatesPage *page = E_MAIL_CONFIG_EWS_DELEGATES_PAGE (ppage);
	EwsDelegateInfo *di = pdi;

	g_return_if_fail (page != NULL);
	g_return_if_fail (di != NULL);

	show_delegate_properties_modal (page, di);
}

static EwsPermissionLevel
get_level_from_permissions (const GSList *permissions,
                            const gchar *primary_smtp)
{
	EwsPermissionLevel level = EwsPermissionLevel_None;
	const GSList *iter;

	g_return_val_if_fail (primary_smtp != NULL, level);

	for (iter = permissions; iter; iter = iter->next) {
		EEwsPermission *perm = iter->data;

		if (!perm || !perm->primary_smtp)
			continue;

		if (g_ascii_strcasecmp (primary_smtp, perm->primary_smtp) == 0) {
			const gchar *level_name = e_ews_permission_rights_to_level_name (perm->rights);

			if (g_strcmp0 (level_name, "None") == 0)
				level = EwsPermissionLevel_None;
			else if (g_strcmp0 (level_name, "Reviewer") == 0)
				level = EwsPermissionLevel_Reviewer;
			else if (g_strcmp0 (level_name, "Author") == 0)
				level = EwsPermissionLevel_Author;
			else if (g_strcmp0 (level_name, "Editor") == 0)
				level = EwsPermissionLevel_Editor;
			else
				level = EwsPermissionLevel_Custom;
			break;
		}
	}

	return level;
}

static gboolean
get_folder_permissions_sync (EEwsConnection *conn,
                             const gchar *distinguished_folder_name,
                             CamelEwsStoreSummary *ews_summary,
                             GSList **permissions,
                             GCancellable *cancellable,
                             GError **error)
{
	gboolean res;
	EwsFolderId *folder_id;
	gchar *fid = NULL, *change_key;
	guint32 folder_type = ~0;

	if (g_strcmp0 (distinguished_folder_name, "calendar") == 0) {
		folder_type = CAMEL_FOLDER_TYPE_EVENTS;
	} else if (g_strcmp0 (distinguished_folder_name, "tasks") == 0) {
		folder_type = CAMEL_FOLDER_TYPE_TASKS;
	} else if (g_strcmp0 (distinguished_folder_name, "inbox") == 0) {
		folder_type = CAMEL_FOLDER_TYPE_INBOX;
	} else if (g_strcmp0 (distinguished_folder_name, "contacts") == 0) {
		folder_type = CAMEL_FOLDER_TYPE_CONTACTS;
	} else if (g_strcmp0 (distinguished_folder_name, "notes") == 0) {
		folder_type = CAMEL_FOLDER_TYPE_MEMOS;
	} else if (g_strcmp0 (distinguished_folder_name, "journal") == 0) {
		folder_type = CAMEL_EWS_FOLDER_TYPE_JOURNAL;
	} else {
		g_return_val_if_reached (FALSE);
	}

	fid = camel_ews_store_summary_get_folder_id_from_folder_type (ews_summary, folder_type);
	if (!fid)
		return FALSE;

	change_key = camel_ews_store_summary_get_change_key (ews_summary, fid, NULL);
	folder_id = e_ews_folder_id_new (fid, change_key, FALSE);

	res = e_ews_connection_get_folder_permissions_sync (
		conn, G_PRIORITY_DEFAULT, folder_id, permissions, cancellable, error);

	e_ews_folder_id_free (folder_id);
	g_free (change_key);
	g_free (fid);

	return res;
}

static void
retrieve_user_permissions_thread_cb (GObject *ppage,
                                     gpointer pdi,
                                     GCancellable *cancellable,
                                     GError **perror)
{
	EMailConfigEwsDelegatesPage *page = E_MAIL_CONFIG_EWS_DELEGATES_PAGE (ppage);
	EwsDelegateInfo *di = pdi;
	EShell *shell;
	EMailBackend *backend;
	ESource *source;
	CamelSession *session;
	CamelService *service;
	CamelEwsStoreSummary *ews_summary;
	EEwsConnection *conn;
	GSList *permissions;
	GError *local_error = NULL;

	g_return_if_fail (page != NULL);
	g_return_if_fail (di != NULL);

	shell = e_shell_get_default ();
	backend = E_MAIL_BACKEND (e_shell_get_backend_by_name (shell, "mail"));
	g_return_if_fail (backend != NULL);

	session = CAMEL_SESSION (e_mail_backend_get_session (backend));
	source = e_mail_config_ews_delegates_page_get_account_source (page);
	service = camel_session_ref_service (session, e_source_get_uid (source));

	g_return_if_fail (service != NULL);
	g_return_if_fail (CAMEL_IS_EWS_STORE (service));

	ews_summary = CAMEL_EWS_STORE (service)->summary;

	if (page->priv->connection)
		conn = g_object_ref (page->priv->connection);
	else
		conn = e_ews_config_utils_open_connection_for (
			e_mail_config_ews_delegates_page_get_collection_source (page),
			CAMEL_EWS_SETTINGS (mail_config_ews_delegates_page_get_settings (page)),
			NULL, NULL, NULL, cancellable, perror);

	g_object_unref (service);

	if (!conn)
		return;

	permissions = NULL;

	#define get_permissions_for(_member, _folder_name)							\
		if (!local_error && !g_cancellable_is_cancelled (cancellable) &&				\
		    get_folder_permissions_sync (conn, _folder_name, ews_summary,				\
			&permissions, cancellable, &local_error)) {						\
			di->_member = get_level_from_permissions (permissions, di->user_id->primary_smtp);	\
														\
			g_slist_free_full (permissions, (GDestroyNotify) e_ews_permission_free);		\
			permissions = NULL;									\
		} else {											\
			di->_member = EwsPermissionLevel_None;							\
		}

	get_permissions_for (calendar, "calendar");
	get_permissions_for (tasks, "tasks");
	get_permissions_for (inbox, "inbox");
	get_permissions_for (contacts, "contacts");
	get_permissions_for (notes, "notes");
	get_permissions_for (journal, "journal");

	#undef get_permissions_for

	if (local_error)
		g_propagate_error (perror, local_error);

	if (di->calendar == EwsPermissionLevel_None)
		di->calendar = EwsPermissionLevel_Editor;

	if (di->tasks == EwsPermissionLevel_None)
		di->tasks = EwsPermissionLevel_Editor;

	g_object_unref (conn);
}

enum {
	COL_NAME,
	COL_DELEGATE_INFO
};

static gboolean
page_contains_user (EMailConfigEwsDelegatesPage *page,
                    const gchar *primary_smtp,
                    GtkTreeIter *piter)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean found = FALSE;

	g_return_val_if_fail (page != NULL, FALSE);
	g_return_val_if_fail (primary_smtp != NULL, FALSE);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (page->priv->users_tree_view));
	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			EwsDelegateInfo *info = NULL;

			gtk_tree_model_get (model, &iter, COL_DELEGATE_INFO, &info, -1);

			found = info &&
				g_ascii_strcasecmp (info->user_id->primary_smtp, primary_smtp) == 0;
		} while (!found && gtk_tree_model_iter_next (model, &iter));
	}

	if (piter)
		*piter = iter;

	return found;
}

static void
add_to_tree_view (EMailConfigEwsDelegatesPage *page,
                  EwsDelegateInfo *di,
                  gboolean select)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (page != NULL);
	g_return_if_fail (di != NULL);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (page->priv->users_tree_view));
	if (page_contains_user (page, di->user_id->primary_smtp, &iter)) {
		ews_delegate_info_free (di);
	} else {
		const gchar *name;

		name = di->user_id->display_name;
		if (!name)
			name = di->user_id->primary_smtp;

		gtk_list_store_append (GTK_LIST_STORE (model), &iter);
		gtk_list_store_set (
			GTK_LIST_STORE (model), &iter,
			COL_NAME, name,
			COL_DELEGATE_INFO, di,
			-1);

		page->priv->new_delegates = g_slist_append (page->priv->new_delegates, di);
	}

	if (select) {
		GtkTreeSelection *selection;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (page->priv->users_tree_view));
		gtk_tree_selection_select_iter (selection, &iter);
	}
}

static void
add_button_clicked_cb (EMailConfigEwsDelegatesPage *page)
{
	GtkWindow *parent;
	gchar *display_name = NULL, *email = NULL;

	g_return_if_fail (page != NULL);

	parent = e_ews_config_utils_get_widget_toplevel_window (GTK_WIDGET (page));

	if (e_ews_search_user_modal (parent, page->priv->connection, NULL, &display_name, &email)) {
		if (email && *email) {
			EwsDelegateInfo *di;

			di = g_new0 (EwsDelegateInfo, 1);
			di->user_id = g_new0 (EwsUserId, 1);
			di->user_id->display_name = display_name;
			di->user_id->primary_smtp = email;
			di->meetingcopies = TRUE;
			di->view_priv_items = FALSE;

			if (page_contains_user (page, email, NULL))
				add_to_tree_view (page, di, TRUE);
			else
				e_ews_config_utils_run_in_thread_with_feedback_modal (
					parent, G_OBJECT (page),
					_("Retrieving current user permissions, please wait…"),
					retrieve_user_permissions_thread_cb,
					retrieve_user_permissions_idle_cb,
					di, (GDestroyNotify) ews_delegate_info_free);

			display_name = NULL;
			email = NULL;
		}
	}

	g_free (display_name);
	g_free (email);
}

static void
remove_button_clicked_cb (EMailConfigEwsDelegatesPage *page)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (page != NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (page->priv->users_tree_view));
	if (selection && gtk_tree_selection_get_selected (selection, &model, &iter)) {
		GtkTreeIter select;
		gboolean can_select = FALSE;
		EwsDelegateInfo *di = NULL;

		select = iter;
		can_select = gtk_tree_model_iter_next (model, &select);
		if (!can_select) {
			select = iter;
			can_select = gtk_tree_model_iter_previous (model, &select);
		}

		if (can_select)
			gtk_tree_selection_select_iter (selection, &select);

		gtk_tree_model_get (model, &iter, COL_DELEGATE_INFO, &di, -1);
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

		page->priv->new_delegates = g_slist_remove (page->priv->new_delegates, di);
		ews_delegate_info_free (di);

		enable_delegates_page_widgets (page, page->priv->connection != NULL);
	}
}

static void
properties_button_clicked_cb (EMailConfigEwsDelegatesPage *page)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (page != NULL);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (page->priv->users_tree_view));
	if (selection && gtk_tree_selection_get_selected (selection, &model, &iter)) {
		EwsDelegateInfo *di = NULL;

		gtk_tree_model_get (model, &iter, COL_DELEGATE_INFO, &di, -1);

		if (di) {
			g_return_if_fail (di->calendar != EwsPermissionLevel_Unknown);

			show_delegate_properties_modal (page, di);
		}
	}
}

static GtkWidget *
create_users_tree_view (EMailConfigEwsDelegatesPage *page)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	gint pos;

	g_return_val_if_fail (page != NULL, NULL);

	model = GTK_TREE_MODEL (gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_POINTER));
	tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));
	g_object_unref (model);
	gtk_tree_view_set_headers_visible (tree_view, FALSE);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", FALSE, NULL);
	pos = gtk_tree_view_insert_column_with_attributes (tree_view, -1, _("Name"), renderer, "text", COL_NAME, NULL);
	column = gtk_tree_view_get_column (tree_view, pos - 1);
	gtk_tree_view_column_set_expand (column, TRUE);

	selection = gtk_tree_view_get_selection (tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed", G_CALLBACK (delegates_tree_selection_changed_cb), page);

	g_signal_connect_swapped (tree_view, "row-activated", G_CALLBACK (properties_button_clicked_cb), page);

	page->priv->users_tree_view = GTK_WIDGET (tree_view);

	return page->priv->users_tree_view;
}

static void
mail_config_ews_delegates_page_constructed (GObject *object)
{
	EMailConfigEwsDelegatesPage *page;
	GtkGrid *grid;
	GtkWidget *widget, *button, *hvgrid, *main_box;
	GSList *radio_group;
	gchar *markup;
	gint row = 0;

	page = E_MAIL_CONFIG_EWS_DELEGATES_PAGE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_delegates_page_parent_class)->constructed (object);

	main_box = e_mail_config_activity_page_get_internal_box (E_MAIL_CONFIG_ACTIVITY_PAGE (page));
	gtk_box_set_spacing (GTK_BOX (main_box), 12);

	markup = g_markup_printf_escaped ("<b>%s</b>", _("Delegates"));
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (main_box), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_grid_new ();
	g_object_set (
		G_OBJECT (widget),
		"row-homogeneous", FALSE,
		"row-spacing", 6,
		"column-homogeneous", FALSE,
		"column-spacing", 6,
		"margin-left", 12,
		"vexpand", FALSE,
		"valign", GTK_ALIGN_START,
		NULL);
	gtk_box_pack_start (GTK_BOX (main_box), widget, FALSE, FALSE, 0);

	grid = GTK_GRID (widget);

	widget = gtk_label_new (
		_("Delegates can send items on your behalf, including creating and "
		"responding to meeting requests. If you want to grant folder "
		"permissions without giving send-on-behalf-of permissions, close this "
		"dialog box, right-click the folder, click Permissions and change the options there."));
	gtk_label_set_width_chars (GTK_LABEL (widget), 20);
	gtk_label_set_max_width_chars (GTK_LABEL (widget), 80);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	row++;

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER (widget), create_users_tree_view (page));
	g_object_set (
		G_OBJECT (widget),
		"hexpand", TRUE,
		"vexpand", TRUE,
		"shadow-type", GTK_SHADOW_IN,
		NULL);

	gtk_grid_attach (grid, widget, 0, row, 1, 1);

	hvgrid = gtk_grid_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (hvgrid), GTK_ORIENTATION_VERTICAL);
	gtk_grid_set_row_spacing (GTK_GRID (hvgrid), 6);
	gtk_grid_set_column_homogeneous (GTK_GRID (hvgrid), TRUE);
	g_object_set (
		G_OBJECT (hvgrid),
		"hexpand", FALSE,
		"vexpand", FALSE,
		"halign", GTK_ALIGN_END,
		NULL);

	button = gtk_button_new_from_stock (GTK_STOCK_ADD);
	page->priv->add_button = button;
	gtk_container_add (GTK_CONTAINER (hvgrid), button);

	button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
	page->priv->remove_button = button;
	gtk_container_add (GTK_CONTAINER (hvgrid), button);

	button = gtk_button_new_from_stock (GTK_STOCK_PROPERTIES);
	page->priv->properties_button = button;
	gtk_container_add (GTK_CONTAINER (hvgrid), button);

	gtk_grid_attach (grid, hvgrid, 1, row, 1, 1);

	row++;

	widget = gtk_label_new (
		_("Deliver meeting requests addressed to me and responses to meeting "
		"requests where I am the organizer to:"));
	gtk_label_set_width_chars (GTK_LABEL (widget), 20);
	gtk_label_set_max_width_chars (GTK_LABEL (widget), 80);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	row++;

	/* new-line break, because GtkRadioButton doesn't allow wrapping of the inner label */
	widget = gtk_radio_button_new_with_mnemonic (NULL, _("My delegates only, but _send a copy of meeting requests\nand responses to me (recommended)"));
	page->priv->deliver_copy_me_radio = widget;
	radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	row++;

	widget = gtk_radio_button_new_with_mnemonic (radio_group, _("My d_elegates only"));
	page->priv->deliver_delegates_only_radio = widget;
	radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	row++;

	widget = gtk_radio_button_new_with_mnemonic (radio_group, _("My delegates a_nd me"));
	page->priv->deliver_delegates_and_me_radio = widget;
	gtk_grid_attach (grid, widget, 0, row, 2, 1);

	g_signal_connect_swapped (
		page->priv->add_button,
		"clicked", G_CALLBACK (add_button_clicked_cb), page);
	g_signal_connect_swapped (
		page->priv->remove_button,
		"clicked", G_CALLBACK (remove_button_clicked_cb), page);
	g_signal_connect_swapped (
		page->priv->properties_button,
		"clicked", G_CALLBACK (properties_button_clicked_cb), page);

	enable_delegates_page_widgets (page, FALSE);

	gtk_widget_show_all (GTK_WIDGET (grid));

	e_mail_config_page_set_content (E_MAIL_CONFIG_PAGE (page), main_box);

	e_mail_config_ews_delegates_page_refresh (page);
}

typedef struct _SubmitData {
	EEwsConnection *cnc;
	gboolean deliver_to_changed;
	EwsDelegateDeliver deliver_to;
	GSList *added; /* const EwsDelegateInfo * */
	GSList *updated; /* const EwsDelegateInfo * */
	GSList *removed; /* const gchar * */
} SubmitData;

static void
submit_data_free (gpointer ptr)
{
	SubmitData *sd = ptr;

	if (sd) {
		g_clear_object (&sd->cnc);
		g_slist_free (sd->added);
		g_slist_free (sd->updated);
		g_slist_free (sd->removed);
		g_slice_free (SubmitData, sd);
	}
}

static void
mail_config_ews_delegates_page_submit_thread (GTask *task,
					      gpointer source_object,
					      gpointer task_data,
					      GCancellable *cancellable)
{
	SubmitData *sd = task_data;
	gboolean success = TRUE;
	GError *local_error = NULL;
	
	if (sd->deliver_to_changed || sd->updated) {
		success = e_ews_connection_update_delegate_sync (sd->cnc, G_PRIORITY_DEFAULT, NULL, sd->deliver_to, sd->updated,
			cancellable, &local_error);
	}

	if (success && sd->removed) {
		success = e_ews_connection_remove_delegate_sync (sd->cnc, G_PRIORITY_DEFAULT, NULL, sd->removed,
			cancellable, &local_error);
	}

	if (success && sd->added) {
		success = e_ews_connection_add_delegate_sync (sd->cnc, G_PRIORITY_DEFAULT, NULL, sd->added,
			cancellable, &local_error);
	}

	if (local_error)
		g_task_return_error (task, local_error);
	else
		g_task_return_boolean (task, success);
}

static gboolean
delegate_infos_equal (const EwsDelegateInfo *di1,
                       const EwsDelegateInfo *di2)
{
	if (!di1 || !di2)
		return di1 == di2;

	return g_strcmp0 (di1->user_id->primary_smtp, di2->user_id->primary_smtp) == 0 &&
		di1->calendar == di2->calendar &&
		di1->tasks == di2->tasks &&
		di1->inbox == di2->inbox &&
		di1->contacts == di2->contacts &&
		di1->notes == di2->notes &&
		di1->journal == di2->journal &&
		(di1->meetingcopies ? 1 : 0) == (di2->meetingcopies ? 1 : 0) &&
		(di1->view_priv_items ? 1 : 0) == (di2->view_priv_items ? 1 : 0);
}

static void
mail_config_ews_delegates_page_submit (EMailConfigPage *page,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	EMailConfigEwsDelegatesPage *ews_page;
	GTask *task;
	SubmitData *sd;
	GSList *iter;
	GHashTable *oldies;
	GHashTableIter titer;
	gpointer key, value;

	ews_page = E_MAIL_CONFIG_EWS_DELEGATES_PAGE (page);

	sd = g_slice_new0 (SubmitData);

	task = g_task_new (ews_page, cancellable, callback, user_data);
	g_task_set_source_tag (task, mail_config_ews_delegates_page_submit);
	g_task_set_task_data (task, sd, submit_data_free);
	g_task_set_check_cancellable (task, TRUE);

	g_mutex_lock (&ews_page->priv->delegates_lock);

	if (!ews_page->priv->connection) {
		g_mutex_unlock (&ews_page->priv->delegates_lock);

		g_task_return_boolean (task, TRUE);
		g_object_unref (task);

		return;
	}

	oldies = g_hash_table_new (g_str_hash, g_str_equal);
	for (iter = ews_page->priv->orig_delegates; iter; iter = g_slist_next (iter)) {
		EwsDelegateInfo *di = iter->data;

		if (!di) {
			g_warn_if_reached ();
			continue;
		}

		g_hash_table_insert (oldies, di->user_id->primary_smtp, di);
	}

	for (iter = ews_page->priv->new_delegates; iter; iter = g_slist_next (iter)) {
		EwsDelegateInfo *di = iter->data;
		EwsDelegateInfo *orig_di;

		if (!di) {
			g_warn_if_reached ();
			continue;
		}

		orig_di = g_hash_table_lookup (oldies, di->user_id->primary_smtp);
		if (!orig_di) {
			sd->added = g_slist_prepend (sd->added, di);
		} else {
			if (!delegate_infos_equal (orig_di, di))
				sd->updated = g_slist_prepend (sd->updated, di);
			g_hash_table_remove (oldies, di->user_id->primary_smtp);
		}
	}

	g_hash_table_iter_init (&titer, oldies);
	while (g_hash_table_iter_next (&titer, &key, &value)) {
		EwsDelegateInfo *di = value;

		sd->removed = g_slist_prepend (sd->removed, di->user_id);
	}

	g_hash_table_destroy (oldies);

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ews_page->priv->deliver_copy_me_radio)))
		sd->deliver_to = EwsDelegateDeliver_DelegatesAndSendInformationToMe;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ews_page->priv->deliver_delegates_only_radio)))
		sd->deliver_to = EwsDelegateDeliver_DelegatesOnly;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ews_page->priv->deliver_delegates_and_me_radio)))
		sd->deliver_to = EwsDelegateDeliver_DelegatesAndMe;
	else
		sd->deliver_to = EwsDelegateDeliver_DelegatesAndSendInformationToMe;

	if (sd->deliver_to == ews_page->priv->deliver_to && !sd->added && !sd->updated && !sd->removed) {
		/* nothing changed, bye bye */
		g_mutex_unlock (&ews_page->priv->delegates_lock);

		g_task_return_boolean (task, TRUE);
		g_object_unref (task);

		return;
	}

	sd->cnc = g_object_ref (ews_page->priv->connection);
	sd->deliver_to_changed = sd->deliver_to != ews_page->priv->deliver_to;

	g_task_run_in_thread (task, mail_config_ews_delegates_page_submit_thread);

	g_object_unref (task);

	g_mutex_unlock (&ews_page->priv->delegates_lock);
}

static gboolean
mail_config_ews_delegates_page_submit_finish (EMailConfigPage *page,
                                              GAsyncResult *result,
                                              GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, page), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

static gint
sort_by_display_name_cb (gconstpointer a,
                         gconstpointer b)
{
	const EwsDelegateInfo *dia, *dib;
	const gchar *aname, *bname;

	if (!a || !b)
		return GPOINTER_TO_INT (a) - GPOINTER_TO_INT (b);

	dia = a;
	dib = b;

	aname = dia->user_id->display_name;
	if (!aname)
		aname = dia->user_id->primary_smtp;

	bname = dib->user_id->display_name;
	if (!bname)
		bname = dib->user_id->primary_smtp;

	if (!aname || !bname)
		return GPOINTER_TO_INT (aname) - GPOINTER_TO_INT (bname);

	return g_utf8_collate (aname, bname);
}

static ESourceAuthenticationResult
mail_config_ews_delegates_page_try_credentials_sync (EEwsConnection *connection,
						     const ENamedParameters *credentials,
						     gpointer user_data,
						     GCancellable *cancellable,
						     GError **error)
{
	AsyncContext *async_context = user_data;
	ESourceAuthenticationResult result;
	EwsDelegateDeliver deliver_to;
	GSList *delegates;
	const gchar *mailbox;
	GError *local_error = NULL;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return E_SOURCE_AUTHENTICATION_ERROR;

	mailbox = mail_config_ews_delegates_page_get_mailbox (async_context->page);

	g_clear_object (&async_context->page->priv->connection);
	e_ews_connection_set_mailbox (connection, mailbox);

	if (e_ews_connection_get_delegate_sync (connection, G_PRIORITY_DEFAULT, NULL, TRUE,
		&deliver_to, &delegates, cancellable, &local_error) ||
	    g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ITEMNOTFOUND)) {
		if (local_error) {
			deliver_to = EwsDelegateDeliver_DelegatesAndSendInformationToMe;
			delegates = NULL;
			g_clear_error (&local_error);
		}

		result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		async_context->page->priv->connection = g_object_ref (connection);

		/* The page takes ownership of the settings. */
		g_mutex_lock (&async_context->page->priv->delegates_lock);
		g_slist_free_full (async_context->page->priv->orig_delegates, (GDestroyNotify) ews_delegate_info_free);

		async_context->page->priv->deliver_to = deliver_to;
		async_context->page->priv->orig_delegates = g_slist_sort (delegates, sort_by_display_name_cb);
		g_mutex_unlock (&async_context->page->priv->delegates_lock);

	} else if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_clear_object (&async_context->page->priv->connection);
		g_error_free (local_error);

	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_clear_object (&async_context->page->priv->connection);
		g_propagate_error (error, local_error);
	}

	return result;
}

static void
mail_config_ews_delegates_page_refresh_thread_cb (GObject *with_object,
						  gpointer user_data,
						  GCancellable *cancellable,
						  GError **perror)
{
	AsyncContext *async_context = user_data;
	CamelEwsSettings *ews_settings;
	EEwsConnection *connection;

	if (g_cancellable_set_error_if_cancelled (cancellable, perror))
		return;

	ews_settings = CAMEL_EWS_SETTINGS (async_context->settings);
	connection = e_ews_config_utils_open_connection_for (async_context->source, ews_settings, NULL,
		mail_config_ews_delegates_page_try_credentials_sync, async_context, cancellable, perror);

	g_clear_object (&connection);
}

static void
mail_config_ews_delegates_page_refresh_idle_cb (GObject *with_object,
						gpointer user_data,
						GCancellable *cancellable,
						GError **perror)
{
	AsyncContext *async_context;
	EAlertSink *alert_sink;
	GError *error = NULL;

	async_context = (AsyncContext *) user_data;

	if (perror) {
		error = *perror;
		*perror = NULL;
	}

	alert_sink = e_activity_get_alert_sink (async_context->activity);

	if (e_activity_handle_cancellation (async_context->activity, error)) {
		g_error_free (error);

	} else if (error != NULL) {
		e_alert_submit (
			alert_sink,
			"ews:query-delegates-error",
			error->message, NULL);
		g_error_free (error);

	} else {
		EMailConfigEwsDelegatesPage *page = async_context->page;
		GtkWidget *radio;
		GtkTreeModel *model;
		const GSList *iter;

		g_mutex_lock (&page->priv->delegates_lock);

		switch (page->priv->deliver_to) {
		case EwsDelegateDeliver_DelegatesOnly:
			radio = page->priv->deliver_delegates_only_radio;
			break;
		case EwsDelegateDeliver_DelegatesAndMe:
			radio = page->priv->deliver_delegates_and_me_radio;
			break;
		default:
		case EwsDelegateDeliver_DelegatesAndSendInformationToMe:
			radio = page->priv->deliver_copy_me_radio;
			break;
		}

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);

		model = gtk_tree_view_get_model (GTK_TREE_VIEW (page->priv->users_tree_view));
		gtk_list_store_clear (GTK_LIST_STORE (model));

		for (iter = page->priv->orig_delegates; iter; iter = iter->next) {
			const EwsDelegateInfo *orig_di = iter->data;
			EwsDelegateInfo *di;

			if (!orig_di) {
				g_warn_if_reached ();
				continue;
			}

			di = copy_delegate_info (orig_di);

			add_to_tree_view (page, di, FALSE);
		}

		g_mutex_unlock (&page->priv->delegates_lock);

		enable_delegates_page_widgets (page, page->priv->connection != NULL);
	}
}

static void
e_mail_config_ews_delegates_page_class_init (EMailConfigEwsDelegatesPageClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_config_ews_delegates_page_set_property;
	object_class->get_property = mail_config_ews_delegates_page_get_property;
	object_class->dispose = mail_config_ews_delegates_page_dispose;
	object_class->finalize = mail_config_ews_delegates_page_finalize;
	object_class->constructed = mail_config_ews_delegates_page_constructed;

	g_object_class_install_property (
		object_class,
		PROP_ACCOUNT_SOURCE,
		g_param_spec_object (
			"account-source",
			"Account Source",
			"Mail account source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_COLLECTION_SOURCE,
		g_param_spec_object (
			"collection-source",
			"Collection Source",
			"Collection source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_IDENTITY_SOURCE,
		g_param_spec_object (
			"identity-source",
			"Identity Source",
			"Mail identity source being edited",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
e_mail_config_ews_delegates_page_interface_init (EMailConfigPageInterface *iface)
{
	iface->title = _("Delegates");
	iface->sort_order = E_MAIL_CONFIG_EWS_DELEGATES_PAGE_SORT_ORDER;
	iface->submit = mail_config_ews_delegates_page_submit;
	iface->submit_finish = mail_config_ews_delegates_page_submit_finish;
}

static void
e_mail_config_ews_delegates_page_class_finalize (EMailConfigEwsDelegatesPageClass *class)
{
}

static void
e_mail_config_ews_delegates_page_init (EMailConfigEwsDelegatesPage *page)
{
	page->priv = e_mail_config_ews_delegates_page_get_instance_private (page);

	g_mutex_init (&page->priv->delegates_lock);
}

void
e_mail_config_ews_delegates_page_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_delegates_page_register_type (type_module);
}

EMailConfigPage *
e_mail_config_ews_delegates_page_new (ESourceRegistry *registry,
                                      ESource *account_source,
                                      ESource *identity_source,
                                      ESource *collection_source)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (account_source), NULL);
	g_return_val_if_fail (E_IS_SOURCE (identity_source), NULL);
	g_return_val_if_fail (E_IS_SOURCE (collection_source), NULL);

	return g_object_new (
		E_TYPE_MAIL_CONFIG_EWS_DELEGATES_PAGE,
		"registry", registry,
		"account-source", account_source,
		"identity-source", identity_source,
		"collection-source", collection_source,
		NULL);
}

void
e_mail_config_ews_delegates_page_refresh (EMailConfigEwsDelegatesPage *page)
{
	ESource *source;
	EActivity *activity;
	GCancellable *cancellable;
	CamelSettings *settings;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_MAIL_CONFIG_EWS_DELEGATES_PAGE (page));

	source = e_mail_config_ews_delegates_page_get_collection_source (page);

	if (page->priv->refresh_cancellable) {
		g_cancellable_cancel (page->priv->refresh_cancellable);
		g_clear_object (&page->priv->refresh_cancellable);
	}

	activity = e_mail_config_activity_page_new_activity (
		E_MAIL_CONFIG_ACTIVITY_PAGE (page));
	cancellable = e_activity_get_cancellable (activity);
	page->priv->refresh_cancellable = g_object_ref (cancellable);

	e_activity_set_text (
		activity, _("Retrieving “Delegates” settings"));

	settings = mail_config_ews_delegates_page_get_settings (page);

	async_context = g_slice_new0 (AsyncContext);
	async_context->page = g_object_ref (page);
	async_context->activity = activity;  /* takes ownership */
	async_context->source = g_object_ref (source);
	async_context->settings = G_OBJECT (g_object_ref (settings));

	/* Property changes can cause update of the UI, but this runs in a thread,
	   thus freeze the notify till be back in UI thread */
	g_object_freeze_notify (async_context->settings);

	e_ews_config_utils_run_in_thread (G_OBJECT (page),
		mail_config_ews_delegates_page_refresh_thread_cb,
		mail_config_ews_delegates_page_refresh_idle_cb,
		async_context, async_context_free, cancellable);
}

ESourceRegistry *
e_mail_config_ews_delegates_page_get_registry (EMailConfigEwsDelegatesPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_DELEGATES_PAGE (page), NULL);

	return page->priv->registry;
}

ESource *
e_mail_config_ews_delegates_page_get_account_source (EMailConfigEwsDelegatesPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_DELEGATES_PAGE (page), NULL);

	return page->priv->account_source;
}

ESource *
e_mail_config_ews_delegates_page_get_identity_source (EMailConfigEwsDelegatesPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_DELEGATES_PAGE (page), NULL);

	return page->priv->identity_source;
}

ESource *
e_mail_config_ews_delegates_page_get_collection_source (EMailConfigEwsDelegatesPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_DELEGATES_PAGE (page), NULL);

	return page->priv->collection_source;
}
