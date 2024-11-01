/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_CONFIG_M365_H
#define E_CAL_CONFIG_M365_H

#include <e-util/e-util.h>

/* Standard GObject macros */
#define E_TYPE_CAL_CONFIG_M365 \
	(e_cal_config_m365_get_type ())
#define E_CAL_CONFIG_M365(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_CONFIG_M365, ECalConfigM365))
#define E_CAL_CONFIG_M365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_CONFIG_M365, ECalConfigM365Class))
#define E_IS_CAL_CONFIG_M365(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_CONFIG_M365))
#define E_IS_CAL_CONFIG_M365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_CONFIG_M365))
#define E_CAL_CONFIG_M365_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_CONFIG_M365, ECalConfigM365Class))

G_BEGIN_DECLS

typedef struct _ECalConfigM365 ECalConfigM365;
typedef struct _ECalConfigM365Class ECalConfigM365Class;
typedef struct _ECalConfigM365Private ECalConfigM365Private;

struct _ECalConfigM365 {
	ESourceConfigBackend parent;
	ECalConfigM365Private *priv;
};

struct _ECalConfigM365Class {
	ESourceConfigBackendClass parent_class;
};

GType		e_cal_config_m365_get_type	(void) G_GNUC_CONST;
void		e_cal_config_m365_type_register	(GTypeModule *type_module);

G_END_DECLS

#endif /* E_CAL_CONFIG_M365_H */
