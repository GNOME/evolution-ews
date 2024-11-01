/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_BACKEND_FACTORY_H
#define E_M365_BACKEND_FACTORY_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_M365_BACKEND_FACTORY \
	(e_m365_backend_factory_get_type ())
#define E_M365_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_M365_BACKEND_FACTORY, EM365BackendFactory))
#define E_M365_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_M365_BACKEND_FACTORY, EM365BackendFactoryClass))
#define E_IS_M365_BACKEND_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_M365_BACKEND_FACTORY))
#define E_IS_M365_BACKEND_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_M365_BACKEND_FACTORY))
#define E_M365_BACKEND_FACTORY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_M365_BACKEND_FACTORY, EM365BackendFactoryClass))

G_BEGIN_DECLS

typedef struct _EM365BackendFactory EM365BackendFactory;
typedef struct _EM365BackendFactoryClass EM365BackendFactoryClass;
typedef struct _EM365BackendFactoryPrivate EM365BackendFactoryPrivate;

struct _EM365BackendFactory {
	ECollectionBackendFactory parent;
	EM365BackendFactoryPrivate *priv;
};

struct _EM365BackendFactoryClass {
	ECollectionBackendFactoryClass parent_class;
};

GType		e_m365_backend_factory_get_type	(void) G_GNUC_CONST;
void		e_m365_backend_factory_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_M365_BACKEND_FACTORY_H */
