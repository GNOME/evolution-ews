/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <camel/camel.h>
#include <libebackend/libebackend.h>

#include <mail/e-mail-config-auth-check.h>
#include <mail/e-mail-config-receiving-page.h>

#include "common/camel-o365-settings.h"
#include "common/e-o365-connection.h"

#include "e-mail-config-o365-backend.h"

struct _EMailConfigO365BackendPrivate {
	GtkWidget *user_entry;
	GtkWidget *impersonate_user_entry;
	GtkGrid *oauth2_settings_grid;
	GtkWidget *oauth2_override_check;
	GtkWidget *oauth2_tenant_entry;
	GtkWidget *oauth2_client_id_entry;
	GtkWidget *oauth2_redirect_uri_entry;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailConfigO365Backend, e_mail_config_o365_backend, E_TYPE_MAIL_CONFIG_SERVICE_BACKEND, 0,
	G_ADD_PRIVATE_DYNAMIC (EMailConfigO365Backend))

static ESource *
mail_config_o365_backend_new_collection (EMailConfigServiceBackend *backend)
{
	EMailConfigServiceBackendClass *class;
	ESourceBackend *extension;
	ESource *source;
	const gchar *extension_name;

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  We return a new collection
	 * for both the Receiving Page and Sending Page.  Although the
	 * Sending Page instance ultimately gets discarded, it's still
	 * needed to avoid creating an [Office365 Backend] extension
	 * in the mail transport source. */

	class = E_MAIL_CONFIG_SERVICE_BACKEND_GET_CLASS (backend);

	source = e_source_new (NULL, NULL, NULL);
	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, class->backend_name);

	return source;
}

static void
mail_config_o365_backend_set_oauth2_tooltip (GtkWidget *widget,
					     const gchar *value,
					     const gchar *when_value_empty,
					     gchar *when_value_filled) /* takes ownership */
{
	g_return_if_fail (GTK_IS_WIDGET (widget));

	gtk_widget_set_tooltip_text (widget, value && *value ? when_value_filled : when_value_empty);

	g_free (when_value_filled);
}

static void
mail_config_o365_backend_insert_widgets (EMailConfigServiceBackend *backend,
					 GtkBox *parent)
{
	EMailConfigO365Backend *o365_backend;
	EMailConfigServicePage *page;
	ESource *source;
	ESourceExtension *extension;
	ESourceAuthentication *auth_extension;
	CamelSettings *settings;
	GtkLabel *label;
	GtkWidget *widget;
	GtkWidget *container;
	const gchar *extension_name;
	const gchar *text;
	gchar *markup;

	o365_backend = E_MAIL_CONFIG_O365_BACKEND (backend);
	page = e_mail_config_service_backend_get_page (backend);

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  This method only applies to
	 * the Receiving Page. */
	if (!E_IS_MAIL_CONFIG_RECEIVING_PAGE (page))
		return;

	/* This needs to come _after_ the page type check so we don't
	 * introduce a backend extension in the mail transport source. */
	settings = e_mail_config_service_backend_get_settings (backend);

	text = _("Configuration");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_grid_new ();
	gtk_widget_set_margin_left (widget, 12);
	gtk_grid_set_row_spacing (GTK_GRID (widget), 6);
	gtk_grid_set_column_spacing (GTK_GRID (widget), 6);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	widget = gtk_label_new_with_mnemonic (_("User_name:"));
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 0, 1, 1);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 0, 2, 1);
	o365_backend->priv->user_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_check_button_new_with_mnemonic (_("Open _Mailbox of other user"));
	gtk_grid_attach (GTK_GRID (container), widget, 1, 1, 1, 1);
	gtk_widget_show (widget);

	if (camel_o365_settings_get_use_impersonation (CAMEL_O365_SETTINGS (settings))) {
		const gchar *impersonate_user = camel_o365_settings_get_impersonate_user (CAMEL_O365_SETTINGS (settings));

		if (impersonate_user && !*impersonate_user) {
			camel_o365_settings_set_impersonate_user (CAMEL_O365_SETTINGS (settings), NULL);
			camel_o365_settings_set_use_impersonation (CAMEL_O365_SETTINGS (settings), FALSE);
		}
	}

	e_binding_bind_property (
		settings, "use-impersonation",
		widget, "active",
		G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 2, 1, 1);
	gtk_widget_show (widget);
	o365_backend->priv->impersonate_user_entry = widget;  /* do not reference */

	e_binding_bind_object_text_property (
		settings, "impersonate-user",
		widget, "text",
		G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		settings, "use-impersonation",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	text = _("Authentication");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_widget_set_margin_top (widget, 6);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = gtk_grid_new ();
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	o365_backend->priv->oauth2_settings_grid = GTK_GRID (widget);

	gtk_grid_set_column_spacing (o365_backend->priv->oauth2_settings_grid, 4);
	gtk_grid_set_row_spacing (o365_backend->priv->oauth2_settings_grid, 4);

	container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_grid_attach (o365_backend->priv->oauth2_settings_grid, container, 0, 0, 2, 1);

	widget = gtk_check_button_new_with_mnemonic (_("_Override Office365 OAuth2 settings"));
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	o365_backend->priv->oauth2_override_check = widget;

	markup = g_markup_printf_escaped ("(<a href=\"https://wiki.gnome.org/Apps/Evolution/EWS/OAuth2\">%s</a>)", _("Help…"));
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	g_free (markup);

	/* Translators: 'Tenant' here means a term used by Microsoft to identify a company or organization in an Office 365 world.
	   You probably do not want to translate it. More for example here: https://powerbi.microsoft.com/en-us/blog/what-is-a-tenant/ */
	widget = gtk_label_new_with_mnemonic (_("_Tenant:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (o365_backend->priv->oauth2_settings_grid, widget, 0, 1, 1, 1);
	label = GTK_LABEL (widget);

	e_binding_bind_property (
		o365_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (o365_backend->priv->oauth2_settings_grid, widget, 1, 1, 1, 1);
	o365_backend->priv->oauth2_tenant_entry = widget;

	e_binding_bind_property (
		o365_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	mail_config_o365_backend_set_oauth2_tooltip (widget, OFFICE365_TENANT,
		/* Translators: 'Tenant' here means a term used by Microsoft to identify a company or organization in an Office 365 world. Same for 'common', it's a default URL path.
		   You probably do not want to translate it. More for example here: https://powerbi.microsoft.com/en-us/blog/what-is-a-tenant/ */
		_("Default tenant is “common“"),
		/* Translators: 'Tenant' here means a term used by Microsoft to identify a company or organization in an Office 365 world.
		   You probably do not want to translate it. More for example here: https://powerbi.microsoft.com/en-us/blog/what-is-a-tenant/ */
		g_strdup_printf (_("Default tenant is “%s”"), OFFICE365_TENANT));

	widget = gtk_label_new_with_mnemonic (_("Application I_D:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (o365_backend->priv->oauth2_settings_grid, widget, 0, 2, 1, 1);
	label = GTK_LABEL (widget);

	e_binding_bind_property (
		o365_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (o365_backend->priv->oauth2_settings_grid, widget, 1, 2, 1, 1);
	o365_backend->priv->oauth2_client_id_entry = widget;

	e_binding_bind_property (
		o365_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	mail_config_o365_backend_set_oauth2_tooltip (widget, OFFICE365_CLIENT_ID,
		_("There is not set any default application ID"),
		g_strdup_printf (_("Default application ID is “%s”"), OFFICE365_CLIENT_ID));

	widget = gtk_label_new_with_mnemonic (_("_Redirect URI:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (o365_backend->priv->oauth2_settings_grid, widget, 0, 3, 1, 1);
	label = GTK_LABEL (widget);

	e_binding_bind_property (
		o365_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (o365_backend->priv->oauth2_settings_grid, widget, 1, 3, 1, 1);
	o365_backend->priv->oauth2_redirect_uri_entry = widget;

	e_binding_bind_property (
		o365_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	markup = g_strdup_printf (_("Default redirect URI is “%s”"), "https://login.microsoftonline.com/common/oauth2/nativeclient");
	mail_config_o365_backend_set_oauth2_tooltip (widget, OFFICE365_REDIRECT_URI,
		markup,
		g_strdup_printf (_("Default redirect URI is “%s”"), OFFICE365_REDIRECT_URI));
	g_free (markup);

	gtk_widget_show_all (GTK_WIDGET (o365_backend->priv->oauth2_settings_grid));

	e_binding_bind_object_text_property (
		settings, "user",
		o365_backend->priv->user_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		settings, "override-oauth2",
		o365_backend->priv->oauth2_override_check, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-tenant",
		o365_backend->priv->oauth2_tenant_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-client-id",
		o365_backend->priv->oauth2_client_id_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-redirect_uri",
		o365_backend->priv->oauth2_redirect_uri_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	source = e_mail_config_service_backend_get_collection (backend);
	extension = e_source_get_extension (source, extension_name);

	/* The collection identity is the user name. */
	e_binding_bind_property (
		settings, "user",
		extension, "identity",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	e_source_authentication_set_host (auth_extension, "graph.microsoft.com");
	e_source_authentication_set_port (auth_extension, 442);
	e_source_authentication_set_method (auth_extension, "Office365");
}

static void
mail_config_o365_backend_setup_defaults (EMailConfigServiceBackend *backend)
{
	CamelSettings *settings;
	EMailConfigServicePage *page;
	const gchar *email_address;

	page = e_mail_config_service_backend_get_page (backend);

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  This method only applies to
	 * the Receiving Page. */
	if (!E_IS_MAIL_CONFIG_RECEIVING_PAGE (page))
		return;

	/* This needs to come _after_ the page type check so we don't
	 * introduce a backend extension in the mail transport source. */
	email_address = e_mail_config_service_page_get_email_address (page);
	settings = e_mail_config_service_backend_get_settings (backend);

	camel_o365_settings_set_email (CAMEL_O365_SETTINGS (settings), email_address);
	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (settings), email_address);
}

static gboolean
mail_config_o365_backend_auto_configure (EMailConfigServiceBackend *backend,
					 EConfigLookup *config_lookup,
					 gint *out_priority,
					 gboolean *out_is_complete)
{
	return e_mail_config_service_backend_auto_configure_for_kind (backend, config_lookup,
		E_CONFIG_LOOKUP_RESULT_COLLECTION, NULL,
		e_mail_config_service_backend_get_collection (backend),
		out_priority, out_is_complete);
}

static gboolean
mail_config_o365_backend_check_complete (EMailConfigServiceBackend *backend)
{
	EMailConfigServicePage *page;
	EMailConfigO365Backend *o365_backend;
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	const gchar *user;
	gboolean correct, complete = TRUE;

	o365_backend = E_MAIL_CONFIG_O365_BACKEND (backend);
	page = e_mail_config_service_backend_get_page (backend);

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  This method only applies to
	 * the Receiving Page. */
	if (!E_IS_MAIL_CONFIG_RECEIVING_PAGE (page))
		return TRUE;

	/* This needs to come _after_ the page type check so we don't
	 * introduce a backend extension in the mail transport source. */
	settings = e_mail_config_service_backend_get_settings (backend);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_get_user (network_settings);

	correct = user != NULL && *user != '\0';
	complete = complete && correct;

	e_util_set_entry_issue_hint (o365_backend->priv->user_entry, correct ? NULL : _("User name cannot be empty"));

	if (correct) {
		CamelO365Settings *o365_settings = CAMEL_O365_SETTINGS (settings);
		const gchar *client_id;

		if (camel_o365_settings_get_override_oauth2 (o365_settings)) {
			client_id = camel_o365_settings_get_oauth2_client_id (o365_settings);
		} else {
			client_id = OFFICE365_CLIENT_ID;
		}

		correct = client_id && *client_id;
		complete = complete && correct;

		e_util_set_entry_issue_hint (o365_backend->priv->oauth2_client_id_entry, correct ? NULL : _("Application ID cannot be empty"));
	}

	return complete;
}

static void
mail_config_o365_backend_commit_changes (EMailConfigServiceBackend *backend)
{
	CamelSettings *settings;
	CamelO365Settings *o365_settings;
	EMailConfigServicePage *page;
	const gchar *email_address;

	page = e_mail_config_service_backend_get_page (backend);

	/* This backend serves double duty.  One instance holds the
	 * mail account source, another holds the mail transport source.
	 * We can differentiate by examining the EMailConfigServicePage
	 * the backend is associated with.  This method only applies to
	 * the Receiving Page. */
	if (!E_IS_MAIL_CONFIG_RECEIVING_PAGE (page))
		return;

	/* This needs to come _after_ the page type check so we don't
	 * introduce a backend extension in the mail transport source. */
	settings = e_mail_config_service_backend_get_settings (backend);

	email_address = e_mail_config_service_page_get_email_address (page);
	if (email_address != NULL) {
		o365_settings = CAMEL_O365_SETTINGS (settings);
		camel_o365_settings_set_email (o365_settings, email_address);
	}
}

static void
e_mail_config_o365_backend_class_init (EMailConfigO365BackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "office365";
	backend_class->new_collection = mail_config_o365_backend_new_collection;
	backend_class->insert_widgets = mail_config_o365_backend_insert_widgets;
	backend_class->setup_defaults = mail_config_o365_backend_setup_defaults;
	backend_class->auto_configure = mail_config_o365_backend_auto_configure;
	backend_class->check_complete = mail_config_o365_backend_check_complete;
	backend_class->commit_changes = mail_config_o365_backend_commit_changes;
}

static void
e_mail_config_o365_backend_class_finalize (EMailConfigO365BackendClass *class)
{
}

static void
e_mail_config_o365_backend_init (EMailConfigO365Backend *backend)
{
	backend->priv = e_mail_config_o365_backend_get_instance_private (backend);
}

void
e_mail_config_o365_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_o365_backend_register_type (type_module);
}
