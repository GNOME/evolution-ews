/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_BOOK_CONFIG_M365_H
#define E_BOOK_CONFIG_M365_H

#include <e-util/e-util.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_CONFIG_M365 \
	(e_book_config_m365_get_type ())
#define E_BOOK_CONFIG_M365(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_CONFIG_M365, EBookConfigM365))
#define E_BOOK_CONFIG_M365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_CONFIG_M365, EBookConfigM365Class))
#define E_IS_BOOK_CONFIG_M365(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_CONFIG_M365))
#define E_IS_BOOK_CONFIG_M365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_CONFIG_M365))
#define E_BOOK_CONFIG_M365_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_CONFIG_M365, EBookConfigM365Class))

G_BEGIN_DECLS

typedef struct _EBookConfigM365 EBookConfigM365;
typedef struct _EBookConfigM365Class EBookConfigM365Class;
typedef struct _EBookConfigM365Private EBookConfigM365Private;

struct _EBookConfigM365 {
	ESourceConfigBackend parent;
	EBookConfigM365Private *priv;
};

struct _EBookConfigM365Class {
	ESourceConfigBackendClass parent_class;
};

GType		e_book_config_m365_get_type	(void) G_GNUC_CONST;
void		e_book_config_m365_type_register(GTypeModule *type_module);

G_END_DECLS

#endif /* E_BOOK_CONFIG_M365_H */
