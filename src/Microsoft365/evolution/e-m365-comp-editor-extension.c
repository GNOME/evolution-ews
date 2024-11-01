/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include <calendar/gui/e-comp-editor.h>
#include <calendar/gui/e-comp-editor-event.h>
#include <calendar/gui/e-comp-editor-page-general.h>

#include "e-m365-comp-editor-extension.h"

#define E_TYPE_M365_COMP_EDITOR_EXTENSION (e_m365_comp_editor_extension_get_type ())

GType e_m365_comp_editor_extension_get_type (void);

typedef struct _EM365CompEditorExtension {
	EExtension parent;
} EM365CompEditorExtension;

typedef struct _EM365CompEditorExtensionClass {
	EExtensionClass parent_class;
} EM365CompEditorExtensionClass;

G_DEFINE_DYNAMIC_TYPE (EM365CompEditorExtension, e_m365_comp_editor_extension, E_TYPE_EXTENSION)

static void
e_m365_comp_editor_extension_target_client_changed_cb (ECompEditor *comp_editor)
{
	ECalClient *target_client;
	ECompEditorPropertyPart *part;
	gboolean can_use;

	target_client = e_comp_editor_get_target_client (comp_editor);

	can_use = target_client != NULL;

	if (can_use) {
		ESource *source;

		source = e_client_get_source (E_CLIENT (target_client));

		#define check_is_m365_backend(ext) (e_source_has_extension (source, ext) && \
			g_strcmp0 (e_source_backend_get_backend_name (e_source_get_extension (source, ext)), "microsoft365") == 0)

		can_use = source && (
			check_is_m365_backend (E_SOURCE_EXTENSION_CALENDAR) ||
			check_is_m365_backend (E_SOURCE_EXTENSION_MEMO_LIST) ||
			check_is_m365_backend (E_SOURCE_EXTENSION_TASK_LIST));

		#undef check_is_m365_backend
	}

	/* The Summary has limited length on the Exchange server */
	part = e_comp_editor_get_property_part (comp_editor, I_CAL_SUMMARY_PROPERTY);
	if (part) {
		GtkWidget *edit_widget;

		edit_widget = e_comp_editor_property_part_get_edit_widget (part);
		if (GTK_IS_ENTRY (edit_widget))
			gtk_entry_set_max_length (GTK_ENTRY (edit_widget), can_use ? 255 : 0);
	}
}

static void
e_m365_comp_editor_extension_update_actions (ECompEditor *comp_editor)
{
	EUIAction *action;
	gboolean can_use = FALSE;

	action = e_comp_editor_get_action (comp_editor, "m365-online-meeting");

	g_return_if_fail (action != NULL);

	/* Requirements to see the action:
	 *  - it's a new component (the option cannot be changed, it can be only set on create)
	 *  - it is a meeting
	 *  - the target calendar is M365
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
				g_strcmp0 (e_source_backend_get_backend_name (e_source_get_extension (source, E_SOURCE_EXTENSION_CALENDAR)), "microsoft365") == 0;
		}
	}

	e_ui_action_set_visible (action, can_use);
}

static void
e_m365_comp_editor_extension_fill_widgets_cb (ECompEditor *comp_editor,
					      ICalComponent *component)
{
	EUIAction *action;

	action = e_comp_editor_get_action (comp_editor, "m365-online-meeting");

	if (action)
		e_ui_action_set_active (action, FALSE);

	e_m365_comp_editor_extension_update_actions (comp_editor);
}

static gboolean
e_m365_comp_editor_extension_fill_component_cb (ECompEditor *comp_editor,
						ICalComponent *component)
{
	EUIAction *action;

	action = e_comp_editor_get_action (comp_editor, "m365-online-meeting");

	if (action && e_ui_action_get_visible (action) &&
	    e_ui_action_get_active (action)) {
		e_cal_util_component_set_x_property (component, "X-M365-ONLINE-MEETING", "1");
	} else {
		e_cal_util_component_remove_x_property (component, "X-M365-ONLINE-MEETING");
	}

	return TRUE;
}

static void
e_m365_comp_editor_extension_handle_map_unmap (ECompEditor *comp_editor,
					       gboolean is_map)
{
	ECompEditorPage *page_general;

	/* Cannot do this in the 'constructed' method, because the extensions are loaded
	   before the Event editor fills the comp editor with the pages. */
	page_general = e_comp_editor_get_page (comp_editor, E_TYPE_COMP_EDITOR_PAGE_GENERAL);

	if (page_general) {
		if (is_map) {
			g_signal_connect_object (page_general, "notify::show-attendees",
				G_CALLBACK (e_m365_comp_editor_extension_update_actions), comp_editor,
				G_CONNECT_SWAPPED);
		} else {
			g_signal_handlers_disconnect_by_func (page_general,
				G_CALLBACK (e_m365_comp_editor_extension_update_actions), comp_editor);
		}
	}
}

static void
e_m365_comp_editor_extension_map_cb (ECompEditor *comp_editor,
				     gpointer user_data)
{
	e_m365_comp_editor_extension_handle_map_unmap (comp_editor, TRUE);
}

static void
e_m365_comp_editor_extension_unmap_cb (ECompEditor *comp_editor,
				       gpointer user_data)
{
	e_m365_comp_editor_extension_handle_map_unmap (comp_editor, FALSE);
}

static void
e_m365_comp_editor_extension_constructed (GObject *object)
{
	static const gchar *eui =
		"<eui>"
		  "<menu id='main-menu'>"
		    "<submenu action='options-menu'>"
		      "<placeholder id='toggles'>"
			"<item action='m365-online-meeting'/>"
		      "</placeholder>"
		    "</submenu>"
		  "</menu>"
		  "<toolbar id='toolbar-with-headerbar'>"
		    "<placeholder id='content'>"
		      "<item action='m365-online-meeting'/>"
		    "</placeholder>"
		  "</toolbar>"
		  "<toolbar id='toolbar-without-headerbar'>"
		    "<placeholder id='content'>"
		      "<item action='m365-online-meeting'/>"
		    "</placeholder>"
		  "</toolbar>"
		"</eui>";

	static const EUIActionEntry entries[] = {
		{ "m365-online-meeting",
		  "stock_people",
		  N_("Online Meeting"),
		  NULL,
		  N_("Create the meeting as an online meeting in the main user calendar"),
		  NULL, NULL, "false", (EUIActionFunc) e_ui_action_set_state }
	};
	EExtensible *extensible;

	/* Chain up to parent's method */
	G_OBJECT_CLASS (e_m365_comp_editor_extension_parent_class)->constructed (object);

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
			G_CALLBACK (e_m365_comp_editor_extension_map_cb), NULL);

		g_signal_connect (comp_editor, "unmap",
			G_CALLBACK (e_m365_comp_editor_extension_unmap_cb), NULL);

		g_signal_connect (comp_editor, "notify::target-client",
			G_CALLBACK (e_m365_comp_editor_extension_update_actions), NULL);

		g_signal_connect (comp_editor, "notify::flags",
			G_CALLBACK (e_m365_comp_editor_extension_update_actions), NULL);

		g_signal_connect (comp_editor, "fill-widgets",
			G_CALLBACK (e_m365_comp_editor_extension_fill_widgets_cb), NULL);

		g_signal_connect (comp_editor, "fill-component",
			G_CALLBACK (e_m365_comp_editor_extension_fill_component_cb), NULL);
	}

	g_signal_connect (extensible, "notify::target-client",
		G_CALLBACK (e_m365_comp_editor_extension_target_client_changed_cb), NULL);
}

static void
e_m365_comp_editor_extension_class_init (EM365CompEditorExtensionClass *klass)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_m365_comp_editor_extension_constructed;

	extension_class = E_EXTENSION_CLASS (klass);
	extension_class->extensible_type = E_TYPE_COMP_EDITOR;
}

static void
e_m365_comp_editor_extension_class_finalize (EM365CompEditorExtensionClass *klass)
{
}

static void
e_m365_comp_editor_extension_init (EM365CompEditorExtension *extension)
{
}

void
e_m365_comp_editor_extension_type_register (GTypeModule *type_module)
{
	e_m365_comp_editor_extension_register_type (type_module);
}
