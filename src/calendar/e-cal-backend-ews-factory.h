/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-cal-backend-ews-factory.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef _E_CAL_BACKEND_EWS_FACTORY_H_
#define _E_CAL_BACKEND_EWS_FACTORY_H_

#include <glib-object.h>
#include "libedata-cal/e-cal-backend-factory.h"

G_BEGIN_DECLS

void                 eds_module_initialize (GTypeModule *module);
void                 eds_module_shutdown   (void);
void                 eds_module_list_types (const GType **types, gint *num_types);

G_END_DECLS

#endif /* _E_CAL_BACKEND_EWS_FACTORY_H_ */
