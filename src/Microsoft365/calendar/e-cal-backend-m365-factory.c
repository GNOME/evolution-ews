/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#include <libedata-cal/libedata-cal.h>

#include "common/e-oauth2-service-microsoft365.h"
#include "common/e-source-m365-folder.h"

#include "e-cal-backend-m365.h"

#define FACTORY_NAME "microsoft365"

typedef ECalBackendFactory ECalBackendM365EventsFactory;
typedef ECalBackendFactoryClass ECalBackendM365EventsFactoryClass;

typedef ECalBackendFactory ECalBackendM365JournalFactory;
typedef ECalBackendFactoryClass ECalBackendM365JournalFactoryClass;

typedef ECalBackendFactory ECalBackendM365TodosFactory;
typedef ECalBackendFactoryClass ECalBackendM365TodosFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_m365_events_factory_get_type (void);
GType e_cal_backend_m365_journal_factory_get_type (void);
GType e_cal_backend_m365_todos_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (ECalBackendM365EventsFactory, e_cal_backend_m365_events_factory, E_TYPE_CAL_BACKEND_FACTORY)
G_DEFINE_DYNAMIC_TYPE (ECalBackendM365JournalFactory, e_cal_backend_m365_journal_factory, E_TYPE_CAL_BACKEND_FACTORY)
G_DEFINE_DYNAMIC_TYPE (ECalBackendM365TodosFactory, e_cal_backend_m365_todos_factory, E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_m365_events_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_M365;
}

static void
e_cal_backend_m365_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_m365_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_m365_journal_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_M365;
}

static void
e_cal_backend_m365_journal_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_m365_journal_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_m365_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_M365;
}

static void
e_cal_backend_m365_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_m365_todos_factory_init (ECalBackendFactory *factory)
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
	e_cal_backend_m365_events_factory_register_type (type_module);
	e_cal_backend_m365_journal_factory_register_type (type_module);
	e_cal_backend_m365_todos_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
