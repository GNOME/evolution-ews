/*
 * e-ews-backend-factory.h
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

#ifndef E_EWS_BACKEND_FACTORY_H
#define E_EWS_BACKEND_FACTORY_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_EWS_BACKEND_FACTORY \
	(e_ews_backend_factory_get_type ())
#define E_EWS_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EWS_BACKEND_FACTORY, EEwsBackendFactory))
#define E_EWS_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_EWS_BACKEND_FACTORY, EEwsBackendFactoryClass))
#define E_IS_EWS_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_EWS_BACKEND_FACTORY))
#define E_IS_EWS_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_EWS_BACKEND_FACTORY))
#define E_EWS_BACKEND_FACTORY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_EWS_BACKEND_FACTORY, EEwsBackendFactoryClass))

G_BEGIN_DECLS

typedef struct _EEwsBackendFactory EEwsBackendFactory;
typedef struct _EEwsBackendFactoryClass EEwsBackendFactoryClass;
typedef struct _EEwsBackendFactoryPrivate EEwsBackendFactoryPrivate;

struct _EEwsBackendFactory {
	ECollectionBackendFactory parent;
	EEwsBackendFactoryPrivate *priv;
};

struct _EEwsBackendFactoryClass {
	ECollectionBackendFactoryClass parent_class;
};

GType		e_ews_backend_factory_get_type	(void) G_GNUC_CONST;
void		e_ews_backend_factory_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_EWS_BACKEND_FACTORY_H */

