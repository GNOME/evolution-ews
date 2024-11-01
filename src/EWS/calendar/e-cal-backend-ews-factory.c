/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#include <libedata-cal/libedata-cal.h>

#include "common/e-oauth2-service-office365.h"
#include "common/e-source-ews-folder.h"

#include "e-cal-backend-ews.h"

#define FACTORY_NAME "ews"

typedef ECalBackendFactory ECalBackendEwsEventsFactory;
typedef ECalBackendFactoryClass ECalBackendEwsEventsFactoryClass;

typedef ECalBackendFactory ECalBackendEwsJournalFactory;
typedef ECalBackendFactoryClass ECalBackendEwsJournalFactoryClass;

typedef ECalBackendFactory ECalBackendEwsTodosFactory;
typedef ECalBackendFactoryClass ECalBackendEwsTodosFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_ews_events_factory_get_type (void);
GType e_cal_backend_ews_journal_factory_get_type (void);
GType e_cal_backend_ews_todos_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendEwsEventsFactory,
	e_cal_backend_ews_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendEwsJournalFactory,
	e_cal_backend_ews_journal_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendEwsTodosFactory,
	e_cal_backend_ews_todos_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_ews_events_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_EWS;
}

static void
e_cal_backend_ews_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_ews_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_ews_journal_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_EWS;
}

static void
e_cal_backend_ews_journal_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_ews_journal_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_ews_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_EWS;
}

static void
e_cal_backend_ews_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_ews_todos_factory_init (ECalBackendFactory *factory)
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

	e_cal_backend_ews_events_factory_register_type (type_module);
	e_cal_backend_ews_journal_factory_register_type (type_module);
	e_cal_backend_ews_todos_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
