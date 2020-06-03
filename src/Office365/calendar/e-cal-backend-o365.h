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

#ifndef E_CAL_BACKEND_O365_H
#define E_CAL_BACKEND_O365_H

#include <libedata-cal/libedata-cal.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_O365            (e_cal_backend_o365_get_type ())
#define E_CAL_BACKEND_O365(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_O365,	ECalBackendO365))
#define E_CAL_BACKEND_O365_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_O365,	ECalBackendO365Class))
#define E_IS_CAL_BACKEND_O365(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_O365))
#define E_IS_CAL_BACKEND_O365_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_O365))

typedef struct _ECalBackendO365        ECalBackendO365;
typedef struct _ECalBackendO365Class   ECalBackendO365Class;
typedef struct _ECalBackendO365Private ECalBackendO365Private;

struct _ECalBackendO365 {
	ECalMetaBackend parent_object;
	ECalBackendO365Private *priv;
};

struct _ECalBackendO365Class {
	ECalMetaBackendClass parent_class;
};

GType   e_cal_backend_o365_get_type (void);

G_END_DECLS

#endif /* E_CAL_BACKEND_O365_H */
