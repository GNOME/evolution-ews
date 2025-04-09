/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <shell/e-shell-view.h>

#include "common/e-source-m365-folder.h"
#include "e-m365-edit-calendar-permissions.h"

#include "e-m365-config-ui-extension.h"

G_DEFINE_DYNAMIC_TYPE (EM365ConfigUIExtension, e_m365_config_ui_extension, E_TYPE_EXTENSION)

static gboolean
get_selected_m365_source (EShellView *shell_view,
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
		    g_strcmp0 (e_source_backend_get_backend_name (backend_ext), "microsoft365") != 0) {
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

static void
action_m365_calendar_permissions_cb (EUIAction *action,
				     GVariant *parameter,
				     gpointer user_data)
{
	EShellView *shell_view = user_data;
	ESourceRegistry *registry = NULL;
	ESource *source = NULL, *parent_source;
	ESourceM365Folder *folder_ext;
	ESourceCamel *extension;
	GtkWidget *parent_window;
	CamelSettings *settings;
	const gchar *extension_name;
	gchar *folder_id, *group_id;

	g_return_if_fail (action != NULL);
	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (get_selected_m365_source (shell_view, &source, &registry));
	g_return_if_fail (source != NULL);
	g_return_if_fail (e_source_has_extension (source, E_SOURCE_EXTENSION_M365_FOLDER));
	g_return_if_fail (g_action_get_name (G_ACTION (action)) != NULL);

	folder_ext = e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER);
	folder_id = e_source_m365_folder_dup_id (folder_ext);
	g_return_if_fail (folder_id != NULL);
	group_id = e_source_m365_folder_dup_group_id (folder_ext);

	parent_source = e_source_registry_ref_source (registry, e_source_get_parent (source));

	extension_name = e_source_camel_get_extension_name ("microsoft365");
	extension = e_source_get_extension (parent_source, extension_name);
	settings = e_source_camel_get_settings (extension);

	parent_window = gtk_widget_get_ancestor (GTK_WIDGET (shell_view), GTK_TYPE_WINDOW);

	e_m365_edit_calendar_permissions (parent_window ? GTK_WINDOW (parent_window) : NULL,
		registry, parent_source, source, CAMEL_M365_SETTINGS (settings),
		group_id, folder_id);

	g_free (folder_id);
	g_free (group_id);
	g_object_unref (source);
	g_object_unref (parent_source);
	g_object_unref (registry);
}

static void
update_m365_calendar_entries_cb (EShellView *shell_view,
				 gpointer user_data)
{
	EUIActionGroup *action_group;
	EUIAction *action;
	EShell *shell;
	EShellWindow *shell_window;
	ESource *source = NULL;
	gboolean is_m365_source, is_online;

	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));

	is_m365_source = get_selected_m365_source (shell_view, &source, NULL);

	if (is_m365_source) {
		if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_M365_FOLDER))
			is_m365_source = FALSE;

		if (is_m365_source) {
			ESource *clicked_source = NULL;

			g_object_get (G_OBJECT (shell_view), "clicked-source", &clicked_source, NULL);

			if (clicked_source && clicked_source != source)
				is_m365_source = FALSE;

			g_clear_object (&clicked_source);
		}

		if (is_m365_source) {
			ESourceM365Folder *m365_folder = e_source_get_extension (source, E_SOURCE_EXTENSION_M365_FOLDER);

			if (e_util_strcmp0 (e_source_m365_folder_get_id (m365_folder), "") == 0)
				is_m365_source = FALSE;
		}
	}

	g_clear_object (&source);

	shell_window = e_shell_view_get_shell_window (shell_view);
	shell = e_shell_window_get_shell (shell_window);

	is_online = shell && e_shell_get_online (shell);
	action_group = e_ui_manager_get_action_group (e_shell_view_get_ui_manager (shell_view), "calendar");
	action = e_ui_action_group_get_action (action_group, "m365-calendar-permissions");

	if (action) {
		e_ui_action_set_visible (action, is_m365_source);
		if (is_m365_source)
			e_ui_action_set_sensitive (action, is_online);
	}
}

static void
e_m365_config_ui_calendars (EShellView *shell_view)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='calendar-popup'>"
		    "<placeholder id='calendar-popup-actions'>"
		      "<item action='m365-calendar-permissions'/>"
		    "</placeholder>"
		  "</menu>"
		"</eui>";

	static const EUIActionEntry entries[] = {
		{ "m365-calendar-permissions",
		  "folder-new",
		  N_("Permissions…"),
		  NULL,
		  N_("Edit Microsoft 365 calendar permissions"),
		  action_m365_calendar_permissions_cb, NULL, NULL, NULL }
	};

	EUIManager *ui_manager;

	ui_manager = e_shell_view_get_ui_manager (shell_view);
	e_ui_manager_add_actions_with_eui_data (ui_manager, "calendar", GETTEXT_PACKAGE, entries, G_N_ELEMENTS (entries), shell_view, eui);

	g_signal_connect (shell_view, "update-actions", G_CALLBACK (update_m365_calendar_entries_cb), NULL);
}

static void
e_m365_config_ui_extension_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;
	EShellViewClass *shell_view_class;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_m365_config_ui_extension_parent_class)->constructed (object);

	shell_view_class = E_SHELL_VIEW_GET_CLASS (extensible);
	g_return_if_fail (shell_view_class != NULL);

	if (g_strcmp0 (shell_view_class->ui_manager_id, "org.gnome.evolution.calendars") == 0)
		e_m365_config_ui_calendars (E_SHELL_VIEW (extensible));
}

static void
e_m365_config_ui_extension_class_init (EM365ConfigUIExtensionClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = e_m365_config_ui_extension_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SHELL_VIEW;
}

static void
e_m365_config_ui_extension_class_finalize (EM365ConfigUIExtensionClass *class)
{
}

static void
e_m365_config_ui_extension_init (EM365ConfigUIExtension *extension)
{
}

void
e_m365_config_ui_extension_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_m365_config_ui_extension_register_type (type_module);
}
