/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_BACKEND_M365_H
#define E_CAL_BACKEND_M365_H

#include <libedata-cal/libedata-cal.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_M365            (e_cal_backend_m365_get_type ())
#define E_CAL_BACKEND_M365(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_M365,	ECalBackendM365))
#define E_CAL_BACKEND_M365_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_M365,	ECalBackendM365Class))
#define E_IS_CAL_BACKEND_M365(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_M365))
#define E_IS_CAL_BACKEND_M365_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_M365))

typedef struct _ECalBackendM365        ECalBackendM365;
typedef struct _ECalBackendM365Class   ECalBackendM365Class;
typedef struct _ECalBackendM365Private ECalBackendM365Private;

struct _ECalBackendM365 {
	ECalMetaBackend parent_object;
	ECalBackendM365Private *priv;
};

struct _ECalBackendM365Class {
	ECalMetaBackendClass parent_class;
};

GType   e_cal_backend_m365_get_type (void);

G_END_DECLS

#endif /* E_CAL_BACKEND_M365_H */
