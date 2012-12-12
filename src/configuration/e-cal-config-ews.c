/*
 * e-cal-config-ews.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-cal-config-ews.h"

G_DEFINE_DYNAMIC_TYPE (
	ECalConfigEws,
	e_cal_config_ews,
	E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
cal_config_ews_allow_creation (ESourceConfigBackend *backend)
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
e_cal_config_ews_class_init (ECalConfigEwsClass *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_CAL_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "ews";
	backend_class->allow_creation = cal_config_ews_allow_creation;
}

static void
e_cal_config_ews_class_finalize (ECalConfigEwsClass *class)
{
}

static void
e_cal_config_ews_init (ECalConfigEws *backend)
{
}

void
e_cal_config_ews_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_cal_config_ews_register_type (type_module);
}

