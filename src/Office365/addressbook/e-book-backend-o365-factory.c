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

#include <libedata-book/libedata-book.h>

#include "common/e-oauth2-service-office365.h"
#include "common/e-source-o365-folder.h"

#include "e-book-backend-o365.h"

typedef EBookBackendFactory EBookBackendO365Factory;
typedef EBookBackendFactoryClass EBookBackendO365FactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_o365_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (EBookBackendO365Factory, e_book_backend_o365_factory, E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_o365_factory_class_init (EBookBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = "office365";
	class->backend_type = E_TYPE_BOOK_BACKEND_O365;
}

static void
e_book_backend_o365_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_o365_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, O365_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_module = E_MODULE (type_module);

	e_oauth2_service_office365_type_register (type_module);
	e_source_o365_folder_type_register (type_module);

	e_book_backend_o365_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
