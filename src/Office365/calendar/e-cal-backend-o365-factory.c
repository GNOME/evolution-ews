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

#include <string.h>
#include <glib/gi18n-lib.h>

#include <libedata-cal/libedata-cal.h>

#include "common/e-oauth2-service-office365.h"
#include "common/e-source-o365-folder.h"

#include "e-cal-backend-o365.h"

#define FACTORY_NAME "office365"

typedef ECalBackendFactory ECalBackendO365EventsFactory;
typedef ECalBackendFactoryClass ECalBackendO365EventsFactoryClass;

typedef ECalBackendFactory ECalBackendO365JournalFactory;
typedef ECalBackendFactoryClass ECalBackendO365JournalFactoryClass;

typedef ECalBackendFactory ECalBackendO365TodosFactory;
typedef ECalBackendFactoryClass ECalBackendO365TodosFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_o365_events_factory_get_type (void);
GType e_cal_backend_o365_journal_factory_get_type (void);
GType e_cal_backend_o365_todos_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendO365EventsFactory,
	e_cal_backend_o365_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendO365JournalFactory,
	e_cal_backend_o365_journal_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendO365TodosFactory,
	e_cal_backend_o365_todos_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_o365_events_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_O365;
}

static void
e_cal_backend_o365_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_o365_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_o365_journal_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VJOURNAL_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_O365;
}

static void
e_cal_backend_o365_journal_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_o365_journal_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_o365_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->component_kind = I_CAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_O365;
}

static void
e_cal_backend_o365_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_o365_todos_factory_init (ECalBackendFactory *factory)
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

	e_cal_backend_o365_events_factory_register_type (type_module);
	e_cal_backend_o365_journal_factory_register_type (type_module);
	e_cal_backend_o365_todos_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
