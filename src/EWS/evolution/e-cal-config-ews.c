/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "common/e-source-ews-folder.h"

#include "e-cal-config-ews.h"

G_DEFINE_DYNAMIC_TYPE (
	ECalConfigEws,
	e_cal_config_ews,
	E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
cal_config_ews_allow_creation (ESourceConfigBackend *backend)
{
	ESourceConfig *config;
	ECalSourceConfig *cal_config;
	ECalClientSourceType source_type;
	gboolean allow_creation = FALSE;

	config = e_source_config_backend_get_config (backend);

	cal_config = E_CAL_SOURCE_CONFIG (config);
	source_type = e_cal_source_config_get_source_type (cal_config);

	switch (source_type) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			allow_creation = TRUE;
			break;

		default:
			break;
	}

	return allow_creation;
}

static void
cal_config_ews_insert_widgets (ESourceConfigBackend *backend,
			       ESource *scratch_source)
{
	ESourceConfig *config;

	if (!scratch_source)
		return;

	config = e_source_config_backend_get_config (backend);

	if (e_source_has_extension (scratch_source, E_SOURCE_EXTENSION_EWS_FOLDER)) {
		ESourceEwsFolder *ews_folder;
		const gchar *id;

		ews_folder = e_source_get_extension (scratch_source, E_SOURCE_EXTENSION_EWS_FOLDER);
		id = e_source_ews_folder_get_id (ews_folder);

		if (id && g_str_has_prefix (id, "freebusy-calendar::")) {
			GtkWidget *widget;
			GtkGrid *grid;

			widget = gtk_grid_new ();
			e_source_config_insert_widget (config, scratch_source, NULL, widget);

			grid = GTK_GRID (widget);

			widget = gtk_label_new (_("Show events in time before and after today, in weeks"));
			gtk_grid_attach (grid, widget, 0, 0, 4, 1);

			widget = gtk_label_new_with_mnemonic (_("_Before:"));
			g_object_set (widget,
				"halign", GTK_ALIGN_END,
				"margin-start", 12,
				"margin-end", 4,
				NULL);
			gtk_grid_attach (grid, widget, 0, 1, 1, 1);

			widget = gtk_spin_button_new_with_range (0, 5, 1);
			gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (widget), TRUE);
			gtk_spin_button_set_update_policy (GTK_SPIN_BUTTON (widget), GTK_UPDATE_IF_VALID);
			gtk_grid_attach (grid, widget, 1, 1, 1, 1);

			e_binding_bind_property (
				ews_folder, "freebusy-weeks-before",
				widget, "value",
				G_BINDING_BIDIRECTIONAL |
				G_BINDING_SYNC_CREATE);

			widget = gtk_label_new_with_mnemonic (_("_After:"));
			g_object_set (widget,
				"halign", GTK_ALIGN_END,
				"margin-start", 12,
				"margin-end", 4,
				NULL);
			gtk_grid_attach (grid, widget, 2, 1, 1, 1);

			widget = gtk_spin_button_new_with_range (0, 54, 1);
			gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (widget), TRUE);
			gtk_spin_button_set_update_policy (GTK_SPIN_BUTTON (widget), GTK_UPDATE_IF_VALID);
			gtk_grid_attach (grid, widget, 3, 1, 1, 1);

			e_binding_bind_property (
				ews_folder, "freebusy-weeks-after",
				widget, "value",
				G_BINDING_BIDIRECTIONAL |
				G_BINDING_SYNC_CREATE);

			gtk_widget_show_all (GTK_WIDGET (grid));
		}
	}

	e_source_config_add_refresh_interval (config, scratch_source);
	e_source_config_add_refresh_on_metered_network (config, scratch_source);
}

static void
e_cal_config_ews_class_init (ECalConfigEwsClass *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_CAL_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "ews";
	backend_class->allow_creation = cal_config_ews_allow_creation;
	backend_class->insert_widgets = cal_config_ews_insert_widgets;
}

static void
e_cal_config_ews_class_finalize (ECalConfigEwsClass *class)
{
}

static void
e_cal_config_ews_init (ECalConfigEws *backend)
{
}

void
e_cal_config_ews_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_cal_config_ews_register_type (type_module);
}

