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

#ifndef E_O365_BACKEND_FACTORY_H
#define E_O365_BACKEND_FACTORY_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_O365_BACKEND_FACTORY \
	(e_o365_backend_factory_get_type ())
#define E_O365_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_O365_BACKEND_FACTORY, EO365BackendFactory))
#define E_O365_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_O365_BACKEND_FACTORY, EO365BackendFactoryClass))
#define E_IS_O365_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_O365_BACKEND_FACTORY))
#define E_IS_O365_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_O365_BACKEND_FACTORY))
#define E_O365_BACKEND_FACTORY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_O365_BACKEND_FACTORY, EO365BackendFactoryClass))

G_BEGIN_DECLS

typedef struct _EO365BackendFactory EO365BackendFactory;
typedef struct _EO365BackendFactoryClass EO365BackendFactoryClass;
typedef struct _EO365BackendFactoryPrivate EO365BackendFactoryPrivate;

struct _EO365BackendFactory {
	ECollectionBackendFactory parent;
	EO365BackendFactoryPrivate *priv;
};

struct _EO365BackendFactoryClass {
	ECollectionBackendFactoryClass parent_class;
};

GType		e_o365_backend_factory_get_type	(void) G_GNUC_CONST;
void		e_o365_backend_factory_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_O365_BACKEND_FACTORY_H */
