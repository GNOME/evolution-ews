/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews-factory.c - Ews contact backend factory.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Chris Toshok <toshok@ximian.com>
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <libedata-book/libedata-book.h>

#include "server/e-oauth2-service-office365.h"
#include "server/e-source-ews-folder.h"

#include "e-book-backend-ews.h"

typedef EBookBackendFactory EBookBackendEwsFactory;
typedef EBookBackendFactoryClass EBookBackendEwsFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_ews_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendEwsFactory,
	e_book_backend_ews_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_ews_factory_class_init (EBookBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = "ews";
	class->backend_type = E_TYPE_BOOK_BACKEND_EWS;
}

static void
e_book_backend_ews_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_ews_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, EXCHANGE_EWS_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_module = E_MODULE (type_module);

	e_source_ews_folder_type_register (type_module);
	e_oauth2_service_office365_type_register (type_module);

	e_book_backend_ews_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
