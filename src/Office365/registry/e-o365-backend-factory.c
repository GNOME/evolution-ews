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

#include "e-o365-backend.h"

#include "e-o365-backend-factory.h"

G_DEFINE_DYNAMIC_TYPE (EO365BackendFactory, e_o365_backend_factory, E_TYPE_COLLECTION_BACKEND_FACTORY)

static void
o365_backend_prepare_mail_account_source (ESource *source)
{
	ESourceBackend *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, "office365");
}

static void
o365_backend_prepare_mail_transport_source (ESource *source)
{
	ESourceBackend *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, "office365");
}

static void
o365_backend_factory_prepare_mail (ECollectionBackendFactory *factory,
				   ESource *mail_account_source,
				   ESource *mail_identity_source,
				   ESource *mail_transport_source)
{
	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_FACTORY_CLASS (e_o365_backend_factory_parent_class)->prepare_mail (factory, mail_account_source, mail_identity_source, mail_transport_source);

	o365_backend_prepare_mail_account_source (mail_account_source);
	o365_backend_prepare_mail_transport_source (mail_transport_source);
}

static void
e_o365_backend_factory_class_init (EO365BackendFactoryClass *class)
{
	ECollectionBackendFactoryClass *factory_class;

	factory_class = E_COLLECTION_BACKEND_FACTORY_CLASS (class);
	factory_class->factory_name = "office365";
	factory_class->backend_type = E_TYPE_O365_BACKEND;
	factory_class->prepare_mail = o365_backend_factory_prepare_mail;
}

static void
e_o365_backend_factory_class_finalize (EO365BackendFactoryClass *class)
{
}

static void
e_o365_backend_factory_init (EO365BackendFactory *factory)
{
}

void
e_o365_backend_factory_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_o365_backend_factory_register_type (type_module);
}