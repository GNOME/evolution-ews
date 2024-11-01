/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "common/e-oauth2-service-microsoft365.h"
#include "common/e-source-m365-folder.h"

#include "e-m365-backend.h"
#include "e-m365-backend-factory.h"
#include "e-source-m365-deltas.h"

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, M365_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_oauth2_service_microsoft365_type_register (type_module);
	e_source_m365_folder_type_register (type_module);
	e_source_m365_deltas_type_register (type_module);
	e_m365_backend_type_register (type_module);
	e_m365_backend_factory_type_register (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}
