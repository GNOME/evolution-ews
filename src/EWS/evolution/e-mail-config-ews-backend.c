/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-mail-config-ews-backend.h"

#include <glib/gi18n-lib.h>

#include <camel/camel.h>
#include <libebackend/libebackend.h>

#include <mail/e-mail-config-auth-check.h>
#include <mail/e-mail-config-receiving-page.h>

#include "common/camel-ews-settings.h"
#include "common/e-oauth2-service-office365.h"

#include "e-mail-config-ews-autodiscover.h"
#include "e-ews-config-utils.h"
#include "e-ews-search-user.h"

struct _EMailConfigEwsBackendPrivate {
	GtkWidget *user_entry;
	GtkWidget *host_entry;
	GtkWidget *url_button;
	GtkWidget *oab_entry;
	GtkWidget *auth_check;
	GtkWidget *impersonate_user_entry;
	GtkGrid *oauth2_settings_grid;
	GtkWidget *oauth2_override_check;
	GtkWidget *oauth2_v2_check;
	GtkWidget *oauth2_tenant_entry;
	GtkWidget *oauth2_client_id_entry;
	GtkWidget *oauth2_redirect_uri_entry;
	GtkWidget *oauth2_resource_uri_entry;
	GtkWidget *oauth2_endpoint_host_entry;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailConfigEwsBackend, e_mail_config_ews_backend, E_TYPE_MAIL_CONFIG_SERVICE_BACKEND, 0,
	G_ADD_PRIVATE_DYNAMIC (EMailConfigEwsBackend))

static ESource *
mail_config_ews_backend_new_collection (EMailConfigServiceBackend *backend)
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
	 * needed to avoid creating an [Ews Backend] extension in the
	 * mail transport source. */

	class = E_MAIL_CONFIG_SERVICE_BACKEND_GET_CLASS (backend);

	source = e_source_new (NULL, NULL, NULL);
	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, class->backend_name);

	return source;
}

static void
search_for_impersonate_user_clicked_cb (GtkButton *button,
					EMailConfigServiceBackend *backend)
{
	EMailConfigEwsBackend *ews_backend;
	ESource *source;
	CamelSettings *settings;
	EEwsConnection *conn;
	GtkWindow *parent;
	gchar *email = NULL;

	g_return_if_fail (E_IS_MAIL_CONFIG_SERVICE_BACKEND (backend));

	source = e_mail_config_service_backend_get_collection (backend);
	if (!source)
		source = e_mail_config_service_backend_get_source (backend);

	ews_backend = E_MAIL_CONFIG_EWS_BACKEND (backend);
	settings = e_mail_config_service_backend_get_settings (backend);
	conn = e_ews_connection_new (source, gtk_entry_get_text (GTK_ENTRY (ews_backend->priv->host_entry)), CAMEL_EWS_SETTINGS (settings));
	parent = e_ews_config_utils_get_widget_toplevel_window (GTK_WIDGET (button));

	if (e_ews_search_user_modal (parent, conn, NULL, NULL, &email)) {
		gtk_entry_set_text (GTK_ENTRY (ews_backend->priv->impersonate_user_entry), email);
	}

	g_object_unref (conn);
	g_free (email);
}

static gboolean
mail_config_ews_backend_auth_mech_is_oauth2 (GBinding *binding,
					     const GValue *from_value,
					     GValue *to_value,
					     gpointer user_data)
{
	gboolean is_office365;
	const gchar *active_mechanism;

	active_mechanism = g_value_get_string (from_value);
	is_office365 = g_strcmp0 (active_mechanism, "Office365") == 0;

	g_value_set_boolean (to_value, is_office365);

	return TRUE;
}

static gboolean
mail_config_ews_active_mech_to_auth_mech (GBinding *binding,
					  const GValue *from_value,
					  GValue *to_value,
					  gpointer user_data)
{
	const gchar *active_mechanism, *use_mechanism;

	active_mechanism = g_value_get_string (from_value);

	if (!active_mechanism || (
	    g_ascii_strcasecmp (active_mechanism, "NTLM") != 0 &&
	    g_ascii_strcasecmp (active_mechanism, "PLAIN") != 0 &&
	    g_ascii_strcasecmp (active_mechanism, "GSSAPI") != 0 &&
	    g_ascii_strcasecmp (active_mechanism, "Office365") != 0))
		use_mechanism = "NTLM";
	else
		use_mechanism = active_mechanism;

	g_value_set_string (to_value, use_mechanism);

	return TRUE;
}

static void
mail_config_ews_backend_set_oauth2_tooltip (GtkWidget *widget,
					    const gchar *value,
					    const gchar *when_value_empty,
					    gchar *when_value_filled) /* takes ownership */
{
	g_return_if_fail (GTK_IS_WIDGET (widget));

	gtk_widget_set_tooltip_text (widget, value && *value ? when_value_filled : when_value_empty);

	g_free (when_value_filled);
}

static void
mail_config_ews_backend_insert_widgets (EMailConfigServiceBackend *backend,
                                        GtkBox *parent)
{
	EMailConfigEwsBackend *ews_backend;
	EMailConfigServicePage *page;
	ESource *source;
	ESourceExtension *extension;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	GtkLabel *label;
	GtkWidget *widget;
	GtkWidget *container;
	GtkWidget *expander;
	GtkWidget *advanced_help;
	GtkWidget *endpoint_host_label;
	GtkWidget *redirect_uri_label;
	GtkWidget *resource_uri_label;
	const gchar *extension_name;
	const gchar *text;
	gchar *markup;

	ews_backend = E_MAIL_CONFIG_EWS_BACKEND (backend);
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
	ews_backend->priv->user_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_label_new_with_mnemonic (_("_Host URL:"));
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 1, 1, 1);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 1, 1, 1);
	ews_backend->priv->host_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = e_mail_config_ews_autodiscover_new (backend);
	gtk_grid_attach (GTK_GRID (container), widget, 2, 1, 1, 1);
	ews_backend->priv->url_button = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_label_new_with_mnemonic (_("OAB U_RL:"));
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (GTK_GRID (container), widget, 0, 2, 1, 1);
	gtk_widget_show (widget);

	label = GTK_LABEL (widget);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 2, 2, 1);
	ews_backend->priv->oab_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_check_button_new_with_mnemonic (_("Open _Mailbox of other user"));
	gtk_grid_attach (GTK_GRID (container), widget, 1, 3, 1, 1);
	gtk_widget_show (widget);

	ews_settings = CAMEL_EWS_SETTINGS (settings);

	camel_ews_settings_lock (ews_settings);

	if (camel_ews_settings_get_use_impersonation (ews_settings)) {
		const gchar *impersonate_user = camel_ews_settings_get_impersonate_user (ews_settings);

		if (impersonate_user && !*impersonate_user) {
			camel_ews_settings_unlock (ews_settings);

			camel_ews_settings_set_impersonate_user (ews_settings, NULL);
			camel_ews_settings_set_use_impersonation (ews_settings, FALSE);

			camel_ews_settings_lock (ews_settings);
		}
	}

	camel_ews_settings_unlock (ews_settings);

	e_binding_bind_property (
		settings, "use-impersonation",
		widget, "active",
		G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 4, 1, 1);
	gtk_widget_show (widget);
	ews_backend->priv->impersonate_user_entry = widget;  /* do not reference */

	e_binding_bind_object_text_property (
		settings, "impersonate-user",
		widget, "text",
		G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		settings, "use-impersonation",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_button_new_with_mnemonic (_("S_earch…"));
	gtk_grid_attach (GTK_GRID (container), widget, 2, 4, 1, 1);
	gtk_widget_show (widget);

	e_binding_bind_property (
		ews_backend->priv->impersonate_user_entry, "sensitive",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	g_signal_connect (widget, "clicked", G_CALLBACK (search_for_impersonate_user_clicked_cb), backend);

	text = _("Authentication");
	markup = g_markup_printf_escaped ("<b>%s</b>", text);
	widget = gtk_label_new (markup);
	gtk_widget_set_margin_top (widget, 6);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);
	g_free (markup);

	widget = e_mail_config_auth_check_new (backend);
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	ews_backend->priv->auth_check = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_grid_new ();
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_pack_start (GTK_BOX (parent), widget, FALSE, FALSE, 0);
	ews_backend->priv->oauth2_settings_grid = GTK_GRID (widget);

	gtk_grid_set_column_spacing (ews_backend->priv->oauth2_settings_grid, 4);
	gtk_grid_set_row_spacing (ews_backend->priv->oauth2_settings_grid, 4);

	widget = gtk_check_button_new_with_mnemonic (_("Use _protocol version 2.0 (for school and organization accounts)"));
	ews_backend->priv->oauth2_v2_check = widget;
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 0, 0, 2, 1);

	container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, container, 0, 1, 2, 1);

	widget = gtk_check_button_new_with_mnemonic (_("_Override Office365 OAuth2 settings"));
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	ews_backend->priv->oauth2_override_check = widget;

	markup = g_markup_printf_escaped ("(<a href=\"https://gitlab.gnome.org/GNOME/evolution/-/wikis/EWS-OAuth2\">%s</a>)", _("Help…"));
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	g_free (markup);

	widget = gtk_label_new_with_mnemonic (_("Application I_D:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 0, 2, 1, 1);
	label = GTK_LABEL (widget);

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 1, 2, 1, 1);
	ews_backend->priv->oauth2_client_id_entry = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	mail_config_ews_backend_set_oauth2_tooltip (widget, OFFICE365_CLIENT_ID,
		_("There is not set any default application ID"),
		g_strdup_printf (_("Default application ID is “%s”"), OFFICE365_CLIENT_ID));

	/* Translators: 'Tenant ID' here means a term used by Microsoft to identify a company or organization in an Office 365 world.
	   You probably do not want to translate it. More for example here: https://powerbi.microsoft.com/en-us/blog/what-is-a-tenant/ */
	widget = gtk_label_new_with_mnemonic (_("_Tenant ID:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 0, 3, 1, 1);
	label = GTK_LABEL (widget);

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 1, 3, 1, 1);
	ews_backend->priv->oauth2_tenant_entry = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	mail_config_ews_backend_set_oauth2_tooltip (widget, OFFICE365_TENANT,
		/* Translators: 'Tenant' here means a term used by Microsoft to identify a company or organization in an Office 365 world. Same for 'common', it's a default URL path.
		   You probably do not want to translate it. More for example here: https://powerbi.microsoft.com/en-us/blog/what-is-a-tenant/ */
		_("Default tenant ID is “common“"),
		/* Translators: 'Tenant' here means a term used by Microsoft to identify a company or organization in an Office 365 world.
		   You probably do not want to translate it. More for example here: https://powerbi.microsoft.com/en-us/blog/what-is-a-tenant/ */
		g_strdup_printf (_("Default tenant ID is “%s”"), OFFICE365_TENANT));

	container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, container, 0, 4, 2, 1);

	widget = gtk_expander_new_with_mnemonic (_("_Advanced Settings"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	expander = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	markup = g_markup_printf_escaped ("(<a href=\"https://gitlab.gnome.org/GNOME/evolution/-/wikis/EWS-OAuth2#alternative-endpoints\">%s</a>)", _("Help…"));
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	g_free (markup);
	advanced_help = widget;

	widget = gtk_label_new_with_mnemonic (_("_Endpoint host:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 0, 5, 1, 1);
	label = GTK_LABEL (widget);
	endpoint_host_label = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 1, 5, 1, 1);
	ews_backend->priv->oauth2_endpoint_host_entry = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	markup = g_strdup_printf (_("Default endpoint host is “%s”"), "login.microsoftonline.com");
	mail_config_ews_backend_set_oauth2_tooltip (widget, OFFICE365_ENDPOINT_HOST,
		markup,
		g_strdup_printf (_("Default endpoint host is “%s”"), OFFICE365_ENDPOINT_HOST));
	g_free (markup);

	widget = gtk_label_new_with_mnemonic (_("Red_irect URI:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 0, 6, 1, 1);
	label = GTK_LABEL (widget);
	redirect_uri_label = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 1, 6, 1, 1);
	ews_backend->priv->oauth2_redirect_uri_entry = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	markup = g_strdup_printf (_("Default redirect URI is “%s”"), "https://login.microsoftonline.com/common/oauth2/nativeclient");
	mail_config_ews_backend_set_oauth2_tooltip (widget, OFFICE365_REDIRECT_URI,
		markup,
		g_strdup_printf (_("Default redirect URI is “%s”"), OFFICE365_REDIRECT_URI));
	g_free (markup);

	widget = gtk_label_new_with_mnemonic (_("Re_source URI:"));
	gtk_widget_set_margin_left (widget, 12);
	gtk_misc_set_alignment (GTK_MISC (widget), 1.0, 0.5);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 0, 7, 1, 1);
	label = GTK_LABEL (widget);
	resource_uri_label = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_label_set_mnemonic_widget (label, widget);
	gtk_grid_attach (ews_backend->priv->oauth2_settings_grid, widget, 1, 7, 1, 1);
	ews_backend->priv->oauth2_resource_uri_entry = widget;

	e_binding_bind_property (
		ews_backend->priv->oauth2_override_check, "active",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	markup = g_strdup_printf (_("Default resource URI is derived from the Host URL, or it can eventually fall back to “%s”."), OFFICE365_FALLBACK_RESOURCE_URI);
	mail_config_ews_backend_set_oauth2_tooltip (widget, NULL,
		markup,
		NULL);
	g_free (markup);

	gtk_widget_show_all (GTK_WIDGET (ews_backend->priv->oauth2_settings_grid));

	camel_ews_settings_lock (ews_settings);

	gtk_expander_set_expanded (GTK_EXPANDER (expander),
		(e_util_strcmp0 (camel_ews_settings_get_oauth2_endpoint_host (ews_settings), NULL) != 0 &&
		 e_util_strcmp0 (camel_ews_settings_get_oauth2_endpoint_host (ews_settings), OFFICE365_ENDPOINT_HOST) != 0) ||
		(e_util_strcmp0 (camel_ews_settings_get_oauth2_redirect_uri (ews_settings), NULL) != 0 &&
		 e_util_strcmp0 (camel_ews_settings_get_oauth2_redirect_uri (ews_settings), OFFICE365_REDIRECT_URI) != 0) ||
		e_util_strcmp0 (camel_ews_settings_get_oauth2_resource_uri (ews_settings), NULL) != 0);

	camel_ews_settings_unlock (ews_settings);

	e_binding_bind_property (
		expander, "expanded",
		advanced_help, "visible",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		expander, "expanded",
		endpoint_host_label, "visible",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		expander, "expanded",
		ews_backend->priv->oauth2_endpoint_host_entry, "visible",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		expander, "expanded",
		redirect_uri_label, "visible",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		expander, "expanded",
		ews_backend->priv->oauth2_redirect_uri_entry, "visible",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		expander, "expanded",
		resource_uri_label, "visible",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		expander, "expanded",
		ews_backend->priv->oauth2_resource_uri_entry, "visible",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property_full (
		ews_backend->priv->auth_check, "active-mechanism",
		ews_backend->priv->oauth2_settings_grid, "visible",
		G_BINDING_SYNC_CREATE,
		mail_config_ews_backend_auth_mech_is_oauth2,
		NULL, NULL, NULL);

	e_binding_bind_object_text_property (
		settings, "user",
		ews_backend->priv->user_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "hosturl",
		ews_backend->priv->host_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oaburl",
		ews_backend->priv->oab_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	/* Don't use G_BINDING_SYNC_CREATE here since the widget
	 * chooses its initial mechanism more intelligently than
	 * a simple property binding would. */
	e_binding_bind_property_full (
		settings, "auth-mechanism",
		ews_backend->priv->auth_check, "active-mechanism",
		G_BINDING_BIDIRECTIONAL,
		NULL,
		mail_config_ews_active_mech_to_auth_mech,
		NULL, NULL);

	e_binding_bind_property (
		settings, "use-oauth2-v2",
		ews_backend->priv->oauth2_v2_check, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		settings, "override-oauth2",
		ews_backend->priv->oauth2_override_check, "active",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-tenant",
		ews_backend->priv->oauth2_tenant_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-client-id",
		ews_backend->priv->oauth2_client_id_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-redirect-uri",
		ews_backend->priv->oauth2_redirect_uri_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-resource-uri",
		ews_backend->priv->oauth2_resource_uri_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oauth2-endpoint-host",
		ews_backend->priv->oauth2_endpoint_host_entry, "text",
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
}

static void
mail_config_ews_backend_setup_defaults (EMailConfigServiceBackend *backend)
{
	CamelSettings *settings;
	EMailConfigServicePage *page;
	const gchar *email_address;
	gchar **parts = NULL;

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
	if (email_address != NULL)
		parts = g_strsplit (email_address, "@", 2);

	if (parts != NULL && g_strv_length (parts) >= 2) {
		CamelEwsSettings *ews_settings;
		CamelNetworkSettings *network_settings;
		gchar *hosturl;

		g_strstrip (parts[0]);  /* user name */
		g_strstrip (parts[1]);  /* domain name */

		hosturl = g_strdup_printf (
			"https://exchange.%s/EWS/Exchange.asmx", parts[1]);

		ews_settings = CAMEL_EWS_SETTINGS (settings);
		camel_ews_settings_set_hosturl (ews_settings, hosturl);
		camel_ews_settings_set_email (ews_settings, email_address);

		/* Prefill email address as the user name, it's needed for office365.com
		   server, but also on-premise servers support it. */
		network_settings = CAMEL_NETWORK_SETTINGS (settings);
		camel_network_settings_set_user (network_settings, email_address);

		g_free (hosturl);
	}

	g_strfreev (parts);
}

static gboolean
mail_config_ews_backend_auto_configure (EMailConfigServiceBackend *backend,
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
mail_config_ews_backend_check_complete (EMailConfigServiceBackend *backend)
{
	EMailConfigServicePage *page;
	EMailConfigEwsBackend *ews_backend;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	CamelNetworkSettings *network_settings;
	const gchar *hosturl, *oaburl;
	const gchar *user;
	gboolean correct, complete = TRUE;

	ews_backend = E_MAIL_CONFIG_EWS_BACKEND (backend);
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

	ews_settings = CAMEL_EWS_SETTINGS (settings);

	camel_ews_settings_lock (ews_settings);

	hosturl = camel_ews_settings_get_hosturl (ews_settings);
	oaburl = camel_ews_settings_get_oaburl (ews_settings);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_get_user (network_settings);

	correct = hosturl != NULL && *hosturl != '\0';
	complete = complete && correct;

	if (correct) {
		GUri *uri;

		uri = g_uri_parse (hosturl, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
		if (uri) {
			g_uri_unref (uri);
			e_util_set_entry_issue_hint (ews_backend->priv->host_entry, NULL);
		} else {
			e_util_set_entry_issue_hint (ews_backend->priv->host_entry, _("Host URL is not valid"));
			complete = FALSE;
		}
	} else {
		e_util_set_entry_issue_hint (ews_backend->priv->host_entry, _("Host URL cannot be empty"));
	}

	if (oaburl && *oaburl) {
		GUri *uri;

		uri = g_uri_parse (oaburl, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
		if (uri) {
			g_uri_unref (uri);
			e_util_set_entry_issue_hint (ews_backend->priv->oab_entry, NULL);
		} else {
			e_util_set_entry_issue_hint (ews_backend->priv->oab_entry, _("OAB URL is not valid"));
			complete = FALSE;
		}
	} else {
		e_util_set_entry_issue_hint (ews_backend->priv->oab_entry, NULL);
	}

	correct = user != NULL && *user != '\0';
	complete = complete && correct;

	e_util_set_entry_issue_hint (ews_backend->priv->user_entry, correct ? NULL : _("User name cannot be empty"));

	if (correct && camel_ews_settings_get_auth_mechanism (ews_settings) == EWS_AUTH_TYPE_OAUTH2) {
		const gchar *client_id;

		if (camel_ews_settings_get_override_oauth2 (ews_settings)) {
			client_id = camel_ews_settings_get_oauth2_client_id (ews_settings);
		} else {
			client_id = OFFICE365_CLIENT_ID;
		}

		correct = e_util_strcmp0 (client_id, NULL) != 0;
		complete = complete && correct;

		e_util_set_entry_issue_hint (ews_backend->priv->oauth2_client_id_entry, correct ? NULL : _("Application ID cannot be empty"));
	}

	camel_ews_settings_unlock (ews_settings);

	return complete;
}

static void
mail_config_ews_backend_commit_changes (EMailConfigServiceBackend *backend)
{
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
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
		ews_settings = CAMEL_EWS_SETTINGS (settings);
		camel_ews_settings_set_email (ews_settings, email_address);
	}
}

static void
e_mail_config_ews_backend_class_init (EMailConfigEwsBackendClass *class)
{
	EMailConfigServiceBackendClass *backend_class;

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "ews";
	backend_class->new_collection = mail_config_ews_backend_new_collection;
	backend_class->insert_widgets = mail_config_ews_backend_insert_widgets;
	backend_class->setup_defaults = mail_config_ews_backend_setup_defaults;
	backend_class->auto_configure = mail_config_ews_backend_auto_configure;
	backend_class->check_complete = mail_config_ews_backend_check_complete;
	backend_class->commit_changes = mail_config_ews_backend_commit_changes;
}

static void
e_mail_config_ews_backend_class_finalize (EMailConfigEwsBackendClass *class)
{
}

static void
e_mail_config_ews_backend_init (EMailConfigEwsBackend *backend)
{
	backend->priv = e_mail_config_ews_backend_get_instance_private (backend);
}

void
e_mail_config_ews_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_backend_register_type (type_module);
}

