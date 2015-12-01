/*
 * e-mail-config-ews-backend.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-mail-config-ews-backend.h"

#include <glib/gi18n-lib.h>

#include <camel/camel.h>
#include <libebackend/libebackend.h>

#include <mail/e-mail-config-auth-check.h>
#include <mail/e-mail-config-receiving-page.h>

#include "server/camel-ews-settings.h"

#include "e-mail-config-ews-autodiscover.h"
#include "e-ews-config-utils.h"
#include "e-ews-search-user.h"

#define E_MAIL_CONFIG_EWS_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_BACKEND, EMailConfigEwsBackendPrivate))

struct _EMailConfigEwsBackendPrivate {
	GtkWidget *user_entry;		/* not referenced */
	GtkWidget *host_entry;		/* not referenced */
	GtkWidget *url_button;		/* not referenced */
	GtkWidget *oab_entry;		/* not referenced */
	GtkWidget *auth_check;		/* not referenced */
	GtkWidget *impersonate_user_entry; /* not referenced */
};

G_DEFINE_DYNAMIC_TYPE (
	EMailConfigEwsBackend,
	e_mail_config_ews_backend,
	E_TYPE_MAIL_CONFIG_SERVICE_BACKEND)

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
	EMailConfigEwsBackendPrivate *priv;
	CamelSettings *settings;
	EEwsConnection *conn;
	GtkWindow *parent;
	gchar *email = NULL;

	g_return_if_fail (E_IS_MAIL_CONFIG_SERVICE_BACKEND (backend));

	priv = E_MAIL_CONFIG_EWS_BACKEND_GET_PRIVATE (backend);
	settings = e_mail_config_service_backend_get_settings (backend);
	conn = e_ews_connection_new (gtk_entry_get_text (GTK_ENTRY (priv->host_entry)), CAMEL_EWS_SETTINGS (settings));
	parent = e_ews_config_utils_get_widget_toplevel_window (GTK_WIDGET (button));

	if (e_ews_search_user_modal (parent, conn, NULL, NULL, &email)) {
		gtk_entry_set_text (GTK_ENTRY (priv->impersonate_user_entry), email);
	}

	g_object_unref (conn);
	g_free (email);
}

static void
mail_config_ews_backend_insert_widgets (EMailConfigServiceBackend *backend,
                                        GtkBox *parent)
{
	EMailConfigEwsBackendPrivate *priv;
	EMailConfigServicePage *page;
	ESource *source;
	ESourceExtension *extension;
	CamelSettings *settings;
	GtkLabel *label;
	GtkWidget *widget;
	GtkWidget *container;
	const gchar *extension_name;
	const gchar *text;
	gchar *markup;

	priv = E_MAIL_CONFIG_EWS_BACKEND_GET_PRIVATE (backend);
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
	priv->user_entry = widget;  /* do not reference */
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
	priv->host_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = e_mail_config_ews_autodiscover_new (backend);
	gtk_grid_attach (GTK_GRID (container), widget, 2, 1, 1, 1);
	priv->url_button = widget;  /* do not reference */
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
	priv->oab_entry = widget;  /* do not reference */
	gtk_widget_show (widget);

	widget = gtk_check_button_new_with_mnemonic (_("Open _Mailbox of other user"));
	gtk_grid_attach (GTK_GRID (container), widget, 1, 3, 1, 1);
	gtk_widget_show (widget);

	if (camel_ews_settings_get_use_impersonation (CAMEL_EWS_SETTINGS (settings))) {
		const gchar *impersonate_user = camel_ews_settings_get_impersonate_user (CAMEL_EWS_SETTINGS (settings));

		if (impersonate_user && !*impersonate_user) {
			camel_ews_settings_set_impersonate_user (CAMEL_EWS_SETTINGS (settings), NULL);
			camel_ews_settings_set_use_impersonation (CAMEL_EWS_SETTINGS (settings), FALSE);
		}
	}

	e_binding_bind_property (
		settings, "use-impersonation",
		widget, "active",
		G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	widget = gtk_entry_new ();
	gtk_widget_set_hexpand (widget, TRUE);
	gtk_grid_attach (GTK_GRID (container), widget, 1, 4, 1, 1);
	gtk_widget_show (widget);
	priv->impersonate_user_entry = widget;  /* do not reference */

	e_binding_bind_object_text_property (
		settings, "impersonate-user",
		widget, "text",
		G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		settings, "use-impersonation",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	widget = gtk_button_new_with_mnemonic (_("S_earch..."));
	gtk_grid_attach (GTK_GRID (container), widget, 2, 4, 1, 1);
	gtk_widget_show (widget);

	e_binding_bind_property (
		priv->impersonate_user_entry, "sensitive",
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
	priv->auth_check = widget;  /* do not reference */
	gtk_widget_show (widget);

	e_binding_bind_object_text_property (
		settings, "user",
		priv->user_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "hosturl",
		priv->host_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_object_text_property (
		settings, "oaburl",
		priv->oab_entry, "text",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE);

	/* Don't use G_BINDING_SYNC_CREATE here since the widget
	 * chooses its initial mechanism more intelligently than
	 * a simple property binding would. */
	e_binding_bind_property (
		settings, "auth-mechanism",
		priv->auth_check, "active-mechanism",
		G_BINDING_BIDIRECTIONAL);

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

		network_settings = CAMEL_NETWORK_SETTINGS (settings);
		camel_network_settings_set_user (network_settings, parts[0]);

		g_free (hosturl);
	}

	g_strfreev (parts);
}

static gboolean
mail_config_ews_backend_check_complete (EMailConfigServiceBackend *backend)
{
	EMailConfigServicePage *page;
	EMailConfigEwsBackendPrivate *priv;
	CamelSettings *settings;
	CamelEwsSettings *ews_settings;
	CamelNetworkSettings *network_settings;
	const gchar *hosturl;
	const gchar *user;
	gboolean correct, complete = TRUE;

	priv = E_MAIL_CONFIG_EWS_BACKEND_GET_PRIVATE (backend);
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
	hosturl = camel_ews_settings_get_hosturl (ews_settings);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_get_user (network_settings);

	correct = hosturl != NULL && *hosturl != '\0';
	complete = complete && correct;

	e_util_set_entry_issue_hint (priv->host_entry, correct ? NULL : _("Host URL cannot be empty"));

	correct = user != NULL && *user != '\0';
	complete = complete && correct;

	e_util_set_entry_issue_hint (priv->user_entry, correct ? NULL : _("User name cannot be empty"));

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

	g_type_class_add_private (
		class, sizeof (EMailConfigEwsBackendPrivate));

	backend_class = E_MAIL_CONFIG_SERVICE_BACKEND_CLASS (class);
	backend_class->backend_name = "ews";
	backend_class->new_collection = mail_config_ews_backend_new_collection;
	backend_class->insert_widgets = mail_config_ews_backend_insert_widgets;
	backend_class->setup_defaults = mail_config_ews_backend_setup_defaults;
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
	backend->priv = E_MAIL_CONFIG_EWS_BACKEND_GET_PRIVATE (backend);
}

void
e_mail_config_ews_backend_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_backend_register_type (type_module);
}

