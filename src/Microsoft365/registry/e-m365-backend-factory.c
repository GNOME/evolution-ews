/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-m365-backend.h"

#include "e-m365-backend-factory.h"

G_DEFINE_DYNAMIC_TYPE (EM365BackendFactory, e_m365_backend_factory, E_TYPE_COLLECTION_BACKEND_FACTORY)

static void
m365_backend_prepare_mail_account_source (ESource *source)
{
	ESourceBackend *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, "microsoft365");
}

static void
m365_backend_prepare_mail_transport_source (ESource *source)
{
	ESourceBackend *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, "microsoft365");
}

static void
m365_backend_factory_prepare_mail (ECollectionBackendFactory *factory,
				   ESource *mail_account_source,
				   ESource *mail_identity_source,
				   ESource *mail_transport_source)
{
	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_FACTORY_CLASS (e_m365_backend_factory_parent_class)->prepare_mail (factory, mail_account_source, mail_identity_source, mail_transport_source);

	m365_backend_prepare_mail_account_source (mail_account_source);
	m365_backend_prepare_mail_transport_source (mail_transport_source);
}

static void
e_m365_backend_factory_class_init (EM365BackendFactoryClass *class)
{
	ECollectionBackendFactoryClass *factory_class;

	factory_class = E_COLLECTION_BACKEND_FACTORY_CLASS (class);
	factory_class->factory_name = "microsoft365";
	factory_class->backend_type = E_TYPE_M365_BACKEND;
	factory_class->prepare_mail = m365_backend_factory_prepare_mail;
}

static void
e_m365_backend_factory_class_finalize (EM365BackendFactoryClass *class)
{
}

static void
e_m365_backend_factory_init (EM365BackendFactory *factory)
{
}

void
e_m365_backend_factory_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_m365_backend_factory_register_type (type_module);
}
