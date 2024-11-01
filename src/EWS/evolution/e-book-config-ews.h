/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_CONFIG_EWS_H
#define E_BOOK_CONFIG_EWS_H

#include <e-util/e-util.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_CONFIG_EWS \
	(e_book_config_ews_get_type ())
#define E_BOOK_CONFIG_EWS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_CONFIG_EWS, EBookConfigEws))
#define E_BOOK_CONFIG_EWS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_CONFIG_EWS, EBookConfigEwsClass))
#define E_IS_BOOK_CONFIG_EWS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_CONFIG_EWS))
#define E_IS_BOOK_CONFIG_EWS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_CONFIG_EWS))
#define E_BOOK_CONFIG_EWS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_CONFIG_EWS, EBookConfigEwsClass))

G_BEGIN_DECLS

typedef struct _EBookConfigEws EBookConfigEws;
typedef struct _EBookConfigEwsClass EBookConfigEwsClass;
typedef struct _EBookConfigEwsPrivate EBookConfigEwsPrivate;

struct _EBookConfigEws {
	ESourceConfigBackend parent;
	EBookConfigEwsPrivate *priv;
};

struct _EBookConfigEwsClass {
	ESourceConfigBackendClass parent_class;
};

GType		e_book_config_ews_get_type	(void) G_GNUC_CONST;
void		e_book_config_ews_type_register	(GTypeModule *type_module);

G_END_DECLS

#endif /* E_BOOK_CONFIG_EWS_H */

