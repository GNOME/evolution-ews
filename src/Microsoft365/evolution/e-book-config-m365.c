/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "common/camel-m365-settings.h"
#include "common/e-m365-connection.h"
#include "common/e-source-m365-folder.h"

#include "e-book-config-m365.h"

G_DEFINE_DYNAMIC_TYPE (EBookConfigM365, e_book_config_m365, E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
book_config_m365_allow_creation (ESourceConfigBackend *backend)
{
	return FALSE;
}

static void
book_config_m365_insert_widgets (ESourceConfigBackend *backend,
				 ESource *scratch_source)
{
	ESourceConfig *config;

	if (!scratch_source)
		return;

	config = e_source_config_backend_get_config (backend);

	if (e_source_has_extension (scratch_source, E_SOURCE_EXTENSION_M365_FOLDER)) {
		ESourceM365Folder *m365_folder_ext;
		const gchar *id;

		m365_folder_ext = e_source_get_extension (scratch_source, E_SOURCE_EXTENSION_M365_FOLDER);
		id = e_source_m365_folder_get_id (m365_folder_ext);

		if (g_strcmp0 (id, E_M365_ARTIFICIAL_FOLDER_ID_ORG_CONTACTS) == 0 ||
		    g_strcmp0 (id, E_M365_ARTIFICIAL_FOLDER_ID_USERS) == 0) {
			e_book_source_config_add_offline_toggle (E_BOOK_SOURCE_CONFIG (config), scratch_source);
		} else if (g_strcmp0 (id, E_M365_ARTIFICIAL_FOLDER_ID_PEOPLE) == 0) {
			GtkWidget *widget;
			GtkWidget *container;
			ESourceExtension *extension;

			extension = e_source_get_extension (scratch_source, E_SOURCE_EXTENSION_M365_FOLDER);

			widget = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
			e_source_config_insert_widget (config, scratch_source, NULL, widget);
			gtk_widget_show (widget);

			container = widget;

			widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
			gtk_container_add (GTK_CONTAINER (container), widget);
			gtk_widget_show (widget);

			container = widget;

			widget = gtk_label_new (_("Maximum contacts to download"));
			gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
			gtk_widget_show (widget);

			widget = gtk_spin_button_new_with_range (0, G_MAXUINT, 1);
			gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (widget), TRUE);
			gtk_spin_button_set_update_policy (GTK_SPIN_BUTTON (widget), GTK_UPDATE_IF_VALID);
			gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
			gtk_widget_show (widget);

			e_binding_bind_property (
				extension, "max-people",
				widget, "value",
				G_BINDING_BIDIRECTIONAL |
				G_BINDING_SYNC_CREATE);
		}
	}

	e_source_config_add_refresh_interval (config, scratch_source);
	e_source_config_add_refresh_on_metered_network (config, scratch_source);
}

static void
e_book_config_m365_class_init (EBookConfigM365Class *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_BOOK_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "microsoft365";
	backend_class->allow_creation = book_config_m365_allow_creation;
	backend_class->insert_widgets = book_config_m365_insert_widgets;
}

static void
e_book_config_m365_class_finalize (EBookConfigM365Class *class)
{
}

static void
e_book_config_m365_init (EBookConfigM365 *backend)
{
}

void
e_book_config_m365_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_book_config_m365_register_type (type_module);
}
