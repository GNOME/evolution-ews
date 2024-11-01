/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include <calendar/gui/e-comp-editor.h>
#include <calendar/gui/e-comp-editor-event.h>
#include <calendar/gui/e-comp-editor-page-general.h>

#include "e-ews-comp-editor-extension.h"

#define E_TYPE_EWS_COMP_EDITOR_EXTENSION (e_ews_comp_editor_extension_get_type ())

GType e_ews_comp_editor_extension_get_type (void);

typedef struct _EEwsCompEditorExtension {
	EExtension parent;
} EEwsCompEditorExtension;

typedef struct _EEwsCompEditorExtensionClass {
	EExtensionClass parent_class;
} EEwsCompEditorExtensionClass;

G_DEFINE_DYNAMIC_TYPE (EEwsCompEditorExtension, e_ews_comp_editor_extension, E_TYPE_EXTENSION)

static void
e_ews_comp_editor_extension_target_client_changed_cb (ECompEditor *comp_editor)
{
	ECalClient *target_client;
	ECompEditorPropertyPart *part;
	gboolean can_use;

	target_client = e_comp_editor_get_target_client (comp_editor);

	can_use = target_client != NULL;

	if (can_use) {
		ESource *source;

		source = e_client_get_source (E_CLIENT (target_client));

		#define check_is_ews_backend(ext) (e_source_has_extension (source, ext) && \
			g_strcmp0 (e_source_backend_get_backend_name (e_source_get_extension (source, ext)), "ews") == 0)

		can_use = source && (
			check_is_ews_backend (E_SOURCE_EXTENSION_CALENDAR) ||
			check_is_ews_backend (E_SOURCE_EXTENSION_MEMO_LIST) ||
			check_is_ews_backend (E_SOURCE_EXTENSION_TASK_LIST));

		#undef check_is_ews_backend
	}

	/* These two have limited length on the EWS server */
	part = e_comp_editor_get_property_part (comp_editor, I_CAL_SUMMARY_PROPERTY);
	if (part) {
		GtkWidget *edit_widget;

		edit_widget = e_comp_editor_property_part_get_edit_widget (part);
		if (GTK_IS_ENTRY (edit_widget))
			gtk_entry_set_max_length (GTK_ENTRY (edit_widget), can_use ? 255 : 0);
	}

	part = e_comp_editor_get_property_part (comp_editor, I_CAL_LOCATION_PROPERTY);
	if (part) {
		GtkWidget *edit_widget;

		edit_widget = e_comp_editor_property_part_get_edit_widget (part);
		if (GTK_IS_ENTRY (edit_widget))
			gtk_entry_set_max_length (GTK_ENTRY (edit_widget), can_use ? 255 : 0);
	}
}

static void
e_ews_comp_editor_extension_update_actions (ECompEditor *comp_editor)
{
	EUIAction *action;
	gboolean can_use = FALSE;

	action = e_comp_editor_get_action (comp_editor, "ews-online-meeting");

	g_return_if_fail (action != NULL);

	/* Requirements to see the action:
	 *  - it's a new component (the option cannot be changed, it can be only set on create)
	 *  - it is a meeting
	 *  - the target calendar is EWS
	 *  - the authentication method is OAuth2
	 *
	 * It would check the host this connects to, but there are different
	 * end points, not only outlook.office365.com, thus check whether the auth
	 * method is OAuth2. It is also not accurate, because on-premise Exchange
	 * servers can have configured OAuth2, without using the Microsoft servers,
	 * but it might be good enough test.
	 */
	can_use = (e_comp_editor_get_flags (comp_editor) & E_COMP_EDITOR_FLAG_IS_NEW) != 0;

	if (can_use) {
		ECompEditorPage *page_general;

		page_general = e_comp_editor_get_page (comp_editor, E_TYPE_COMP_EDITOR_PAGE_GENERAL);

		can_use = page_general && e_comp_editor_page_general_get_show_attendees (E_COMP_EDITOR_PAGE_GENERAL (page_general));
	}

	if (can_use) {
		ECalClient *target_client;

		target_client = e_comp_editor_get_target_client (comp_editor);

		can_use = target_client != NULL;

		if (can_use) {
			ESource *source;

			source = e_client_get_source (E_CLIENT (target_client));

			can_use = source && e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR) &&
				g_strcmp0 (e_source_backend_get_backend_name (e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR)), "ews") == 0;

			if (can_use) {
				ESourceRegistry *registry;
				ESource *collection_source;

				registry = e_shell_get_registry (e_comp_editor_get_shell (comp_editor));
				collection_source = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);
				can_use = collection_source && e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

				if (can_use) {
					EOAuth2Services *oauth2_services;
					const gchar *method;

					oauth2_services = e_source_registry_get_oauth2_services (registry);
					method = e_source_authentication_get_method (e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION));

					can_use = method && e_oauth2_services_is_oauth2_alias (oauth2_services, method);
				}

				g_clear_object (&collection_source);
			}
		}
	}

	e_ui_action_set_visible (action, can_use);
}

static void
e_ews_comp_editor_extension_fill_widgets_cb (ECompEditor *comp_editor,
					     ICalComponent *component)
{
	EUIAction *action;

	action = e_comp_editor_get_action (comp_editor, "ews-online-meeting");

	if (action)
		e_ui_action_set_active (action, FALSE);

	e_ews_comp_editor_extension_update_actions (comp_editor);
}

static gboolean
e_ews_comp_editor_extension_fill_component_cb (ECompEditor *comp_editor,
					       ICalComponent *component)
{
	EUIAction *action;

	action = e_comp_editor_get_action (comp_editor, "ews-online-meeting");

	if (action && e_ui_action_get_visible (action) &&
	    e_ui_action_get_active (action)) {
		e_cal_util_component_set_x_property (component, "X-M365-ONLINE-MEETING", "1");
	} else {
		e_cal_util_component_remove_x_property (component, "X-M365-ONLINE-MEETING");
	}

	return TRUE;
}

static void
e_ews_comp_editor_extension_handle_map_unmap (ECompEditor *comp_editor,
					      gboolean is_map)
{
	ECompEditorPage *page_general;

	/* Cannot do this in the 'constructed' method, because the extensions are loaded
	   before the Event editor fills the comp editor with the pages. */
	page_general = e_comp_editor_get_page (comp_editor, E_TYPE_COMP_EDITOR_PAGE_GENERAL);

	if (page_general) {
		if (is_map) {
			g_signal_connect_object (page_general, "notify::show-attendees",
				G_CALLBACK (e_ews_comp_editor_extension_update_actions), comp_editor,
				G_CONNECT_SWAPPED);
		} else {
			g_signal_handlers_disconnect_by_func (page_general,
				G_CALLBACK (e_ews_comp_editor_extension_update_actions), comp_editor);
		}
	}
}

static void
e_ews_comp_editor_extension_map_cb (ECompEditor *comp_editor,
				    gpointer user_data)
{
	e_ews_comp_editor_extension_handle_map_unmap (comp_editor, TRUE);
}

static void
e_ews_comp_editor_extension_unmap_cb (ECompEditor *comp_editor,
				      gpointer user_data)
{
	e_ews_comp_editor_extension_handle_map_unmap (comp_editor, FALSE);
}

static void
e_ews_comp_editor_extension_constructed (GObject *object)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='main-menu'>"
		    "<submenu action='options-menu'>"
		      "<placeholder id='toggles'>"
			"<item action='ews-online-meeting'/>"
		      "</placeholder>"
		    "</submenu>"
		  "</menu>"
		  "<toolbar id='toolbar-with-headerbar'>"
		    "<placeholder id='content'>"
		      "<item action='ews-online-meeting'/>"
		    "</placeholder>"
		  "</toolbar>"
		  "<toolbar id='toolbar-without-headerbar'>"
		    "<placeholder id='content'>"
		      "<item action='ews-online-meeting'/>"
		    "</placeholder>"
		  "</toolbar>"
		"</eui>";

	static const EUIActionEntry entries[] = {
		{ "ews-online-meeting",
		  "stock_people",
		  N_("Online Meeting"),
		  NULL,
		  N_("Create the meeting as an online meeting in the main user calendar"),
		  NULL, NULL, "false", (EUIActionFunc) e_ui_action_set_state }
	};
	EExtensible *extensible;

	/* Chain up to parent's method */
	G_OBJECT_CLASS (e_ews_comp_editor_extension_parent_class)->constructed (object);

	extensible = e_extension_get_extensible (E_EXTENSION (object));

	if (E_IS_COMP_EDITOR_EVENT (extensible)) {
		ECompEditor *comp_editor = E_COMP_EDITOR (extensible);
		EUIManager *ui_manager;
		EUIParser *ui_parser;
		GError *local_error = NULL;

		ui_manager = e_comp_editor_get_ui_manager (comp_editor);
		ui_parser = e_ui_manager_get_parser (ui_manager);

		e_ui_manager_add_actions (ui_manager, "individual", GETTEXT_PACKAGE,
			entries, G_N_ELEMENTS (entries), comp_editor);

		if (!e_ui_parser_merge_data (ui_parser, eui, -1, &local_error))
			g_critical ("%s: Failed to merge .eui data: %s", G_STRFUNC, local_error ? local_error->message : "Unknown error");

		g_clear_error (&local_error);

		g_signal_connect (comp_editor, "map",
			G_CALLBACK (e_ews_comp_editor_extension_map_cb), NULL);

		g_signal_connect (comp_editor, "unmap",
			G_CALLBACK (e_ews_comp_editor_extension_unmap_cb), NULL);

		g_signal_connect (comp_editor, "notify::target-client",
			G_CALLBACK (e_ews_comp_editor_extension_update_actions), NULL);

		g_signal_connect (comp_editor, "notify::flags",
			G_CALLBACK (e_ews_comp_editor_extension_update_actions), NULL);

		g_signal_connect (comp_editor, "fill-widgets",
			G_CALLBACK (e_ews_comp_editor_extension_fill_widgets_cb), NULL);

		g_signal_connect (comp_editor, "fill-component",
			G_CALLBACK (e_ews_comp_editor_extension_fill_component_cb), NULL);
	}

	g_signal_connect (extensible, "notify::target-client",
		G_CALLBACK (e_ews_comp_editor_extension_target_client_changed_cb), NULL);
}

static void
e_ews_comp_editor_extension_class_init (EEwsCompEditorExtensionClass *klass)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_ews_comp_editor_extension_constructed;

	extension_class = E_EXTENSION_CLASS (klass);
	extension_class->extensible_type = E_TYPE_COMP_EDITOR;
}

static void
e_ews_comp_editor_extension_class_finalize (EEwsCompEditorExtensionClass *klass)
{
}

static void
e_ews_comp_editor_extension_init (EEwsCompEditorExtension *extension)
{
}

void
e_ews_comp_editor_extension_type_register (GTypeModule *type_module)
{
	e_ews_comp_editor_extension_register_type (type_module);
}
