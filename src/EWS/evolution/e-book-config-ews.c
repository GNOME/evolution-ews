/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "common/camel-ews-settings.h"
#include "common/e-source-ews-folder.h"

#include "e-book-config-ews.h"

G_DEFINE_DYNAMIC_TYPE (
	EBookConfigEws,
	e_book_config_ews,
	E_TYPE_SOURCE_CONFIG_BACKEND)

static CamelEwsSettings *
book_config_ews_get_ews_settings (ESourceConfigBackend *backend,
				  ESource *source)
{
	ESource *collection;
	ESourceCamel *extension;
	ESourceRegistry *registry;
	CamelSettings *settings;
	const gchar *extension_name;

	if (!source)
		return NULL;

	registry = e_source_config_get_registry (e_source_config_backend_get_config (backend));

	extension_name = e_source_camel_get_extension_name ("ews");
	e_source_camel_generate_subtype ("ews", CAMEL_TYPE_EWS_SETTINGS);

	/* The collection settings live in our parent data source. */
	collection = e_source_registry_find_extension (registry, source, extension_name);
	if (!collection)
		return NULL;

	extension = e_source_get_extension (collection, extension_name);
	settings = e_source_camel_get_settings (extension);

	g_object_unref (collection);

	return CAMEL_EWS_SETTINGS (settings);
}

static gboolean
book_config_ews_allow_creation (ESourceConfigBackend *backend)
{
	return TRUE;
}

static void
book_config_ews_maybe_insert_gal_options (ESourceConfigBackend *backend,
					  ESource *scratch_source)
{
	CamelEwsSettings *ews_settings;
	ESource *source;
	gchar *gal_uid;
	gboolean is_gal;

	if (!scratch_source || !e_source_has_extension (scratch_source, E_SOURCE_EXTENSION_EWS_FOLDER))
		return;

	source = e_source_config_get_original_source (e_source_config_backend_get_config (backend));
	if (!source)
		return;

	ews_settings = book_config_ews_get_ews_settings (backend, source);
	if (!ews_settings)
		return;

	gal_uid = camel_ews_settings_dup_gal_uid (ews_settings);
	is_gal = g_strcmp0 (e_source_get_uid (source), gal_uid) == 0;
	g_free (gal_uid);

	if (is_gal) {
		ESourceEwsFolder *ews_folder;
		GtkWidget *checkbox;

		ews_folder = e_source_get_extension (scratch_source, E_SOURCE_EXTENSION_EWS_FOLDER);

		checkbox = gtk_check_button_new_with_mnemonic (_("Use only _primary contact email address"));
		gtk_widget_set_tooltip_text (checkbox, _("When checked, the contacts looked up in the online Global Address List will contain only the primary email address"));
		gtk_widget_show (checkbox);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), e_source_ews_folder_get_use_primary_address (ews_folder));

		e_binding_bind_property (
			checkbox, "active",
			ews_folder, "use-primary-address",
			G_BINDING_DEFAULT);

		e_source_config_insert_widget (e_source_config_backend_get_config (backend), scratch_source, NULL, checkbox);

		checkbox = gtk_check_button_new_with_mnemonic (_("_Fetch contact photos"));
		gtk_widget_set_tooltip_text (checkbox, _("Tries to look up for user photo"));
		gtk_widget_show (checkbox);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbox), e_source_ews_folder_get_fetch_gal_photos (ews_folder));

		e_binding_bind_property (
			checkbox, "active",
			ews_folder, "fetch-gal-photos",
			G_BINDING_DEFAULT);

		e_source_config_insert_widget (e_source_config_backend_get_config (backend), scratch_source, NULL, checkbox);
	}
}

static void
book_config_ews_insert_widgets (ESourceConfigBackend *backend,
				ESource *scratch_source)
{
	ESourceConfig *config;

	if (!scratch_source)
		return;

	book_config_ews_maybe_insert_gal_options (backend, scratch_source);

	config = e_source_config_backend_get_config (backend);

	e_source_config_add_refresh_interval (config, scratch_source);
	e_source_config_add_refresh_on_metered_network (config, scratch_source);
}

static void
e_book_config_ews_class_init (EBookConfigEwsClass *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_BOOK_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "ews";
	backend_class->allow_creation = book_config_ews_allow_creation;
	backend_class->insert_widgets = book_config_ews_insert_widgets;
}

static void
e_book_config_ews_class_finalize (EBookConfigEwsClass *class)
{
}

static void
e_book_config_ews_init (EBookConfigEws *backend)
{
}

void
e_book_config_ews_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_book_config_ews_register_type (type_module);
}

