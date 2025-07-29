/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <mail/e-mail-config-notebook.h>

#include "e-mail-config-m365-ooo-page.h"
#include "e-mail-config-m365-notebook.h"

struct _EMailConfigM365Notebook {
	EExtension parent;
};

G_DEFINE_DYNAMIC_TYPE (EMailConfigM365Notebook, e_mail_config_m365_notebook, E_TYPE_EXTENSION)

static EMailConfigNotebook *
mail_config_m365_notebook_get_extensible (EMailConfigM365Notebook *self)
{
	return E_MAIL_CONFIG_NOTEBOOK (e_extension_get_extensible (E_EXTENSION (self)));
}

static void
mail_config_m365_notebook_constructed (GObject *object)
{
	EMailConfigM365Notebook *self;
	EMailConfigNotebook *notebook;
	EMailSession *session;
	ESource *account_source;
	ESource *identity_source;
	ESource *collection_source;
	ESourceBackend *backend_ext;
	ESourceRegistry *registry;
	const gchar *backend_name;
	const gchar *extension_name;

	self = E_MAIL_CONFIG_M365_NOTEBOOK (object);
	notebook = mail_config_m365_notebook_get_extensible (self);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_mail_config_m365_notebook_parent_class)->constructed (object);

	session = e_mail_config_notebook_get_session (notebook);
	registry = e_mail_session_get_registry (session);

	account_source = e_mail_config_notebook_get_account_source (notebook);
	identity_source = e_mail_config_notebook_get_identity_source (notebook);
	collection_source = e_mail_config_notebook_get_collection_source (notebook);

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	backend_ext = e_source_get_extension (account_source, extension_name);
	backend_name = e_source_backend_get_backend_name (backend_ext);

	if (g_strcmp0 (backend_name, "microsoft365") == 0) {
		EMailConfigPage *page;

		page = e_mail_config_m365_ooo_page_new (registry, account_source, identity_source, collection_source);
		e_mail_config_notebook_add_page (notebook, page);
	}
}

static void
e_mail_config_m365_notebook_class_init (EMailConfigM365NotebookClass *klass)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = mail_config_m365_notebook_constructed;

	extension_class = E_EXTENSION_CLASS (klass);
	extension_class->extensible_type = E_TYPE_MAIL_CONFIG_NOTEBOOK;
}

static void
e_mail_config_m365_notebook_class_finalize (EMailConfigM365NotebookClass *klass)
{
}

static void
e_mail_config_m365_notebook_init (EMailConfigM365Notebook *extension)
{
}

void
e_mail_config_m365_notebook_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_config_m365_notebook_register_type (type_module);
}
