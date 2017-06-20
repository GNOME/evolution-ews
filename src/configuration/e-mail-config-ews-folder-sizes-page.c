/*
 * e-mail-config-ews-folder-sizes-page.c
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

#include "evolution-ews-config.h"

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <mail/e-mail-backend.h>
#include <mail/e-mail-config-page.h>
#include <mail/e-mail-config-security-page.h>

#include "e-ews-config-utils.h"

#include "e-mail-config-ews-folder-sizes-page.h"

#define E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE, EMailConfigEwsFolderSizesPagePrivate))

#define E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_SORT_ORDER \
	(E_MAIL_CONFIG_SECURITY_PAGE_SORT_ORDER + 10)

struct _EMailConfigEwsFolderSizesPagePrivate {
	ESource *account_source;
	ESourceRegistry *registry;
};

enum {
	PROP_0,
	PROP_ACCOUNT_SOURCE,
	PROP_SOURCE_REGISTRY
};

static void e_mail_config_ews_folder_sizes_page_interface_init (EMailConfigPageInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailConfigEwsFolderSizesPage, e_mail_config_ews_folder_sizes_page, GTK_TYPE_SCROLLED_WINDOW, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_MAIL_CONFIG_PAGE, e_mail_config_ews_folder_sizes_page_interface_init))

static void
folder_sizes_clicked_cb (GtkWidget *button,
			 EMailConfigEwsFolderSizesPage *page)
{
	GtkWindow *parent;
	CamelService *service;
	CamelSession *session;
	EMailBackend *backend;
	EShell *shell;
	ESource *source;
	ESourceRegistry *registry;

	g_return_if_fail (page != NULL);

	parent = GTK_WINDOW (gtk_widget_get_toplevel (button));

	shell = e_shell_get_default ();
	backend = E_MAIL_BACKEND (e_shell_get_backend_by_name (shell, "mail"));
	g_return_if_fail (backend != NULL);

	session = CAMEL_SESSION (e_mail_backend_get_session (backend));
	source = e_mail_config_ews_folder_sizes_page_get_account_source (page);
	registry = e_mail_config_ews_folder_sizes_page_get_source_registry (page);
	service = camel_session_ref_service (session, e_source_get_uid (source));

	e_ews_config_utils_run_folder_sizes_dialog (parent, registry, source, CAMEL_EWS_STORE (service));

	g_object_unref (service);
}

static void
mail_config_ews_folder_sizes_page_set_account_source (EMailConfigEwsFolderSizesPage *page,
						      ESource *account_source)
{
	g_return_if_fail (E_IS_SOURCE (account_source));
	g_return_if_fail (page->priv->account_source == NULL);

	page->priv->account_source = g_object_ref (account_source);
}

static void
mail_config_ews_folder_sizes_page_set_source_registry (EMailConfigEwsFolderSizesPage *page,
						       ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (page->priv->registry == NULL);

	page->priv->registry = g_object_ref (registry);
}

static void
mail_config_ews_folder_sizes_page_set_property (GObject *object,
						guint property_id,
						const GValue *value,
						GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACCOUNT_SOURCE:
			mail_config_ews_folder_sizes_page_set_account_source (
				E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE (object),
				g_value_get_object (value));
			return;

		case PROP_SOURCE_REGISTRY:
			mail_config_ews_folder_sizes_page_set_source_registry (
				E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_folder_sizes_page_get_property (GObject *object,
						guint property_id,
						GValue *value,
						GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ACCOUNT_SOURCE:
			g_value_set_object (
				value,
				e_mail_config_ews_folder_sizes_page_get_account_source (
				E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE (object)));
			return;

		case PROP_SOURCE_REGISTRY:
			g_value_set_object (
				value,
				e_mail_config_ews_folder_sizes_page_get_source_registry (
				E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_config_ews_folder_sizes_page_dispose (GObject *object)
{
	EMailConfigEwsFolderSizesPagePrivate *priv;

	priv = E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_GET_PRIVATE (object);

	if (priv->account_source != NULL) {
		g_object_unref (priv->account_source);
		priv->account_source = NULL;
	}

	if (priv->registry != NULL) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_config_ews_folder_sizes_page_parent_class)->dispose (object);
}

static void
mail_config_ews_folder_sizes_page_constructed (GObject *object)
{
	EMailConfigEwsFolderSizesPage *page = E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE (object);
	GtkWidget *widget;
	GtkWidget *main_box;
	GtkGrid *content_grid;
	gchar *markup;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_folder_sizes_page_parent_class)->constructed (object);

	main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);

	content_grid = GTK_GRID (gtk_grid_new ());
	gtk_grid_set_row_spacing (content_grid, 6);
	gtk_grid_set_column_spacing (content_grid, 6);
	gtk_box_pack_start (GTK_BOX (main_box), GTK_WIDGET (content_grid), FALSE, FALSE, 0);

	markup = g_markup_printf_escaped ("<b>%s</b>", _("EWS Settings"));
	widget = gtk_label_new (markup);
	gtk_label_set_use_markup (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.5);
	gtk_grid_attach (content_grid, widget, 0, 0, 2, 1);
	g_free (markup);

	widget = gtk_label_new (_("View the size of all Exchange folders"));
	gtk_misc_set_alignment (GTK_MISC (widget), 0, 0.5);
	gtk_grid_attach (content_grid, widget, 0, 1, 1, 1);

	widget = gtk_button_new_with_mnemonic (_("Folder _Sizes"));
	g_signal_connect (widget, "clicked", G_CALLBACK (folder_sizes_clicked_cb), page);
	gtk_grid_attach (content_grid, widget, 1, 1, 1, 1);

	gtk_widget_show_all (GTK_WIDGET (main_box));

	e_mail_config_page_set_content (E_MAIL_CONFIG_PAGE (page), main_box);
}

static void
e_mail_config_ews_folder_sizes_page_class_init (EMailConfigEwsFolderSizesPageClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EMailConfigEwsFolderSizesPagePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_config_ews_folder_sizes_page_set_property;
	object_class->get_property = mail_config_ews_folder_sizes_page_get_property;
	object_class->dispose = mail_config_ews_folder_sizes_page_dispose;
	object_class->constructed = mail_config_ews_folder_sizes_page_constructed;

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
		PROP_SOURCE_REGISTRY,
		g_param_spec_object (
			"source-registry",
			"Source Registry",
			NULL,
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
e_mail_config_ews_folder_sizes_page_class_finalize (EMailConfigEwsFolderSizesPageClass *class)
{
}

static void
e_mail_config_ews_folder_sizes_page_interface_init (EMailConfigPageInterface *iface)
{
	iface->title = _("EWS Settings");
	iface->sort_order = E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_SORT_ORDER;
}

static void
e_mail_config_ews_folder_sizes_page_init (EMailConfigEwsFolderSizesPage *page)
{
	page->priv = E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_GET_PRIVATE (page);
}

void
e_mail_config_ews_folder_sizes_page_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_folder_sizes_page_register_type (type_module);
}

EMailConfigPage *
e_mail_config_ews_folder_sizes_page_new (ESource *account_source,
					 ESourceRegistry *registry)
{
	EMailConfigPage *page;

	g_return_val_if_fail (E_IS_SOURCE (account_source), NULL);

	page = g_object_new (E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE,
		"account-source", account_source,
		"source-registry", registry,
		NULL);

	return page;
}

ESource *
e_mail_config_ews_folder_sizes_page_get_account_source (EMailConfigEwsFolderSizesPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE (page), NULL);

	return page->priv->account_source;
}

ESourceRegistry *
e_mail_config_ews_folder_sizes_page_get_source_registry (EMailConfigEwsFolderSizesPage *page)
{
	g_return_val_if_fail (E_IS_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE (page), NULL);

	return page->priv->registry;
}
