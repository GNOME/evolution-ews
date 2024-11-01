/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "common/e-oauth2-service-office365.h"
#include "common/e-source-ews-folder.h"

#include "e-ews-backend.h"
#include "e-ews-backend-factory.h"

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, EXCHANGE_EWS_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_ews_backend_type_register (type_module);
	e_ews_backend_factory_type_register (type_module);
	e_oauth2_service_office365_type_register (type_module);

	e_source_ews_folder_type_register (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

