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

#include "common/camel-o365-settings.h"
#include "common/e-source-o365-folder.h"

#include "e-book-config-o365.h"

G_DEFINE_DYNAMIC_TYPE (EBookConfigO365, e_book_config_o365, E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
book_config_o365_allow_creation (ESourceConfigBackend *backend)
{
	return TRUE;
}

static void
book_config_o365_insert_widgets (ESourceConfigBackend *backend,
				 ESource *scratch_source)
{
	if (!scratch_source)
		return;

	e_source_config_add_refresh_interval (e_source_config_backend_get_config (backend), scratch_source);
}

static void
e_book_config_o365_class_init (EBookConfigO365Class *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_BOOK_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "office365";
	backend_class->allow_creation = book_config_o365_allow_creation;
	backend_class->insert_widgets = book_config_o365_insert_widgets;
}

static void
e_book_config_o365_class_finalize (EBookConfigO365Class *class)
{
}

static void
e_book_config_o365_init (EBookConfigO365 *backend)
{
}

void
e_book_config_o365_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_book_config_o365_register_type (type_module);
}
