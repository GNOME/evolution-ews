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

#include "e-cal-config-o365.h"

G_DEFINE_DYNAMIC_TYPE (ECalConfigO365, e_cal_config_o365, E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
cal_config_o365_allow_creation (ESourceConfigBackend *backend)
{
	ESourceConfig *config;
	ECalSourceConfig *cal_config;
	ECalClientSourceType source_type;
	gboolean allow_creation = FALSE;

	config = e_source_config_backend_get_config (backend);

	cal_config = E_CAL_SOURCE_CONFIG (config);
	source_type = e_cal_source_config_get_source_type (cal_config);

	switch (source_type) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			allow_creation = TRUE;
			break;

		default:
			break;
	}

	return allow_creation;
}

static void
cal_config_o365_insert_widgets (ESourceConfigBackend *backend,
				ESource *scratch_source)
{
	if (!scratch_source)
		return;

	e_source_config_add_refresh_interval (e_source_config_backend_get_config (backend), scratch_source);
}

static void
e_cal_config_o365_class_init (ECalConfigO365Class *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_CAL_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "office365";
	backend_class->allow_creation = cal_config_o365_allow_creation;
	backend_class->insert_widgets = cal_config_o365_insert_widgets;
}

static void
e_cal_config_o365_class_finalize (ECalConfigO365Class *class)
{
}

static void
e_cal_config_o365_init (ECalConfigO365 *backend)
{
}

void
e_cal_config_o365_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_cal_config_o365_register_type (type_module);
}
