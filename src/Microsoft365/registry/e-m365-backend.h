/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_BACKEND_H
#define E_M365_BACKEND_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_M365_BACKEND \
	(e_m365_backend_get_type ())
#define E_M365_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_M365_BACKEND, EM365Backend))
#define E_M365_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_M365_BACKEND, EM365BackendClass))
#define E_IS_M365_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_M365_BACKEND))
#define E_IS_M365_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_M365_BACKEND))
#define E_M365_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_M365_BACKEND, EM365BackendClass))

G_BEGIN_DECLS

typedef struct _EM365Backend EM365Backend;
typedef struct _EM365BackendClass EM365BackendClass;
typedef struct _EM365BackendPrivate EM365BackendPrivate;

struct _EM365Backend {
	ECollectionBackend parent;
	EM365BackendPrivate *priv;
};

struct _EM365BackendClass {
	ECollectionBackendClass parent_class;
};

GType		e_m365_backend_get_type		(void) G_GNUC_CONST;
void		e_m365_backend_type_register	(GTypeModule *type_module);

G_END_DECLS

#endif /* E_M365_BACKEND_H */
