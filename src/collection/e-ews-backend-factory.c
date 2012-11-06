/*
 * e-ews-backend-factory.c
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

#include "e-ews-backend-factory.h"

#include "e-ews-backend.h"

G_DEFINE_DYNAMIC_TYPE (
	EEwsBackendFactory,
	e_ews_backend_factory,
	E_TYPE_COLLECTION_BACKEND_FACTORY)

static void
ews_backend_prepare_mail_account_source (ESource *source)
{
	ESourceBackend *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, "ews");
}

static void
ews_backend_prepare_mail_transport_source (ESource *source)
{
	ESourceBackend *extension;
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	extension = e_source_get_extension (source, extension_name);
	e_source_backend_set_backend_name (extension, "ews");
}

static void
ews_backend_factory_prepare_mail (ECollectionBackendFactory *factory,
                                  ESource *mail_account_source,
                                  ESource *mail_identity_source,
                                  ESource *mail_transport_source)
{
	ECollectionBackendFactoryClass *parent_class;

	/* Chain up to parent's prepare_mail() method. */
	parent_class =
		E_COLLECTION_BACKEND_FACTORY_CLASS (
		e_ews_backend_factory_parent_class);
	parent_class->prepare_mail (
		factory,
		mail_account_source,
		mail_identity_source,
		mail_transport_source);

	ews_backend_prepare_mail_account_source (mail_account_source);
	ews_backend_prepare_mail_transport_source (mail_transport_source);
}

static void
e_ews_backend_factory_class_init (EEwsBackendFactoryClass *class)
{
	ECollectionBackendFactoryClass *factory_class;

	factory_class = E_COLLECTION_BACKEND_FACTORY_CLASS (class);
	factory_class->factory_name = "ews";
	factory_class->backend_type = E_TYPE_EWS_BACKEND;
	factory_class->prepare_mail = ews_backend_factory_prepare_mail;
}

static void
e_ews_backend_factory_class_finalize (EEwsBackendFactoryClass *class)
{
}

static void
e_ews_backend_factory_init (EEwsBackendFactory *factory)
{
}

void
e_ews_backend_factory_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_backend_factory_register_type (type_module);
}

