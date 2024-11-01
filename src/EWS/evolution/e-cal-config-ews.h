/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_CONFIG_EWS_H
#define E_CAL_CONFIG_EWS_H

#include <e-util/e-util.h>

/* Standard GObject macros */
#define E_TYPE_CAL_CONFIG_EWS \
	(e_cal_config_ews_get_type ())
#define E_CAL_CONFIG_EWS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_CONFIG_EWS, ECalConfigEws))
#define E_CAL_CONFIG_EWS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_CONFIG_EWS, ECalConfigEwsClass))
#define E_IS_CAL_CONFIG_EWS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_CONFIG_EWS))
#define E_IS_CAL_CONFIG_EWS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_CONFIG_EWS))
#define E_CAL_CONFIG_EWS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_CONFIG_EWS, ECalConfigEwsClass))

G_BEGIN_DECLS

typedef struct _ECalConfigEws ECalConfigEws;
typedef struct _ECalConfigEwsClass ECalConfigEwsClass;
typedef struct _ECalConfigEwsPrivate ECalConfigEwsPrivate;

struct _ECalConfigEws {
	ESourceConfigBackend parent;
	ECalConfigEwsPrivate *priv;
};

struct _ECalConfigEwsClass {
	ESourceConfigBackendClass parent_class;
};

GType		e_cal_config_ews_get_type	(void) G_GNUC_CONST;
void		e_cal_config_ews_type_register	(GTypeModule *type_module);

G_END_DECLS

#endif /* E_CAL_CONFIG_EWS_H */

