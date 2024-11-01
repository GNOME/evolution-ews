/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-mail-config-ews-notebook.h"

#include <mail/e-mail-config-notebook.h>

#include "e-mail-config-ews-delegates-page.h"
#include "e-mail-config-ews-ooo-page.h"
#include "e-mail-config-ews-folder-sizes-page.h"

G_DEFINE_DYNAMIC_TYPE (
	EMailConfigEwsNotebook,
	e_mail_config_ews_notebook,
	E_TYPE_EXTENSION)

static EMailConfigNotebook *
mail_config_ews_notebook_get_extensible (EMailConfigEwsNotebook *extension)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	return E_MAIL_CONFIG_NOTEBOOK (extensible);
}

static void
mail_config_ews_notebook_constructed (GObject *object)
{
	EMailConfigEwsNotebook *extension;
	EMailConfigNotebook *notebook;
	EMailSession *session;
	ESource *account_source;
	ESource *identity_source;
	ESource *collection_source;
	ESourceBackend *backend_ext;
	ESourceRegistry *registry;
	const gchar *backend_name;
	const gchar *extension_name;

	extension = E_MAIL_CONFIG_EWS_NOTEBOOK (object);
	notebook = mail_config_ews_notebook_get_extensible (extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_config_ews_notebook_parent_class)->constructed (object);

	session = e_mail_config_notebook_get_session (notebook);
	registry = e_mail_session_get_registry (session);

	account_source =
		e_mail_config_notebook_get_account_source (notebook);
	identity_source =
		e_mail_config_notebook_get_identity_source (notebook);
	collection_source =
		e_mail_config_notebook_get_collection_source (notebook);

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	backend_ext = e_source_get_extension (account_source, extension_name);
	backend_name = e_source_backend_get_backend_name (backend_ext);

	if (g_strcmp0 (backend_name, "ews") == 0) {
		EMailConfigPage *page;

		page = e_mail_config_ews_ooo_page_new (
			registry,
			account_source,
			identity_source,
			collection_source);
		e_mail_config_notebook_add_page (notebook, page);

		page = e_mail_config_ews_delegates_page_new (
			registry,
			account_source,
			identity_source,
			collection_source);
		e_mail_config_notebook_add_page (notebook, page);

		page = e_mail_config_ews_folder_sizes_page_new (
			account_source,
			collection_source,
			registry);
		e_mail_config_notebook_add_page (notebook, page);
	}
}

static void
e_mail_config_ews_notebook_class_init (EMailConfigEwsNotebookClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = mail_config_ews_notebook_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_MAIL_CONFIG_NOTEBOOK;
}

static void
e_mail_config_ews_notebook_class_finalize (EMailConfigEwsNotebookClass *class)
{
}

static void
e_mail_config_ews_notebook_init (EMailConfigEwsNotebook *extension)
{
}

void
e_mail_config_ews_notebook_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_ews_notebook_register_type (type_module);
}

