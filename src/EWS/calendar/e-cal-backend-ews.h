/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_BACKEND_EWS_H
#define E_CAL_BACKEND_EWS_H

#include <libedata-cal/libedata-cal.h>

#include "common/e-ews-connection.h"

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_EWS            (e_cal_backend_ews_get_type ())
#define E_CAL_BACKEND_EWS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_EWS,	ECalBackendEws))
#define E_CAL_BACKEND_EWS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_EWS,	ECalBackendEwsClass))
#define E_IS_CAL_BACKEND_EWS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_EWS))
#define E_IS_CAL_BACKEND_EWS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_EWS))

typedef struct _ECalBackendEws        ECalBackendEws;
typedef struct _ECalBackendEwsClass   ECalBackendEwsClass;
typedef struct _ECalBackendEwsPrivate ECalBackendEwsPrivate;

struct _ECalBackendEws {
	ECalMetaBackend parent_object;
	ECalBackendEwsPrivate *priv;
};

struct _ECalBackendEwsClass {
	ECalMetaBackendClass parent_class;
};

GType   e_cal_backend_ews_get_type (void);

G_END_DECLS

#endif /* E_CAL_BACKEND_EWS_H */
