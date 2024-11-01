/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <libedata-book/libedata-book.h>

#include "common/e-oauth2-service-microsoft365.h"
#include "common/e-source-m365-folder.h"

#include "e-book-backend-m365.h"

typedef EBookBackendFactory EBookBackendM365Factory;
typedef EBookBackendFactoryClass EBookBackendM365FactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_m365_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (EBookBackendM365Factory, e_book_backend_m365_factory, E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_m365_factory_class_init (EBookBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = "microsoft365";
	class->backend_type = E_TYPE_BOOK_BACKEND_M365;
}

static void
e_book_backend_m365_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_m365_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, M365_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_module = E_MODULE (type_module);

	e_oauth2_service_microsoft365_type_register (type_module);
	e_source_m365_folder_type_register (type_module);
	e_book_backend_m365_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
