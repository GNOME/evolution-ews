/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-cal-config-m365.h"

G_DEFINE_DYNAMIC_TYPE (ECalConfigM365, e_cal_config_m365, E_TYPE_SOURCE_CONFIG_BACKEND)

static gboolean
cal_config_m365_allow_creation (ESourceConfigBackend *backend)
{
#if 0
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
#endif
	return FALSE;
}

static void
cal_config_m365_insert_widgets (ESourceConfigBackend *backend,
				ESource *scratch_source)
{
	ESourceConfig *config;

	if (!scratch_source)
		return;

	config = e_source_config_backend_get_config (backend);

	e_source_config_add_refresh_interval (config, scratch_source);
	e_source_config_add_refresh_on_metered_network (config, scratch_source);
}

static void
e_cal_config_m365_class_init (ECalConfigM365Class *class)
{
	EExtensionClass *extension_class;
	ESourceConfigBackendClass *backend_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_CAL_SOURCE_CONFIG;

	backend_class = E_SOURCE_CONFIG_BACKEND_CLASS (class);
	backend_class->backend_name = "microsoft365";
	backend_class->allow_creation = cal_config_m365_allow_creation;
	backend_class->insert_widgets = cal_config_m365_insert_widgets;
}

static void
e_cal_config_m365_class_finalize (ECalConfigM365Class *class)
{
}

static void
e_cal_config_m365_init (ECalConfigM365 *backend)
{
}

void
e_cal_config_m365_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_cal_config_m365_register_type (type_module);
}
