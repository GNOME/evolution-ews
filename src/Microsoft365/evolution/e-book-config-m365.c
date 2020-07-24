/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "common/camel-m365-settings.h"
#include "common/e-source-m365-folder.h"

#include "e-book-config-m365.h"

G_DEFINE_DYNAMIC_TYPE (EBookConfigM365, e_book_config_m365, E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
book_config_m365_allow_creation (ESourceConfigBackend *backend)
{
	return TRUE;
}

static void
book_config_m365_insert_widgets (ESourceConfigBackend *backend,
				 ESource *scratch_source)
{
	if (!scratch_source)
		return;

	e_source_config_add_refresh_interval (e_source_config_backend_get_config (backend), scratch_source);
}

static void
e_book_config_m365_class_init (EBookConfigM365Class *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_BOOK_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "microsoft365";
	backend_class->allow_creation = book_config_m365_allow_creation;
	backend_class->insert_widgets = book_config_m365_insert_widgets;
}

static void
e_book_config_m365_class_finalize (EBookConfigM365Class *class)
{
}

static void
e_book_config_m365_init (EBookConfigM365 *backend)
{
}

void
e_book_config_m365_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_book_config_m365_register_type (type_module);
}
