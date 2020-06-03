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

#ifndef E_BOOK_CONFIG_O365_H
#define E_BOOK_CONFIG_O365_H

#include <e-util/e-util.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_CONFIG_O365 \
	(e_book_config_o365_get_type ())
#define E_BOOK_CONFIG_O365(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_CONFIG_O365, EBookConfigO365))
#define E_BOOK_CONFIG_O365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_CONFIG_O365, EBookConfigO365Class))
#define E_IS_BOOK_CONFIG_O365(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_CONFIG_O365))
#define E_IS_BOOK_CONFIG_O365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_CONFIG_O365))
#define E_BOOK_CONFIG_O365_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_CONFIG_O365, EBookConfigO365Class))

G_BEGIN_DECLS

typedef struct _EBookConfigO365 EBookConfigO365;
typedef struct _EBookConfigO365Class EBookConfigO365Class;
typedef struct _EBookConfigO365Private EBookConfigO365Private;

struct _EBookConfigO365 {
	ESourceConfigBackend parent;
	EBookConfigO365Private *priv;
};

struct _EBookConfigO365Class {
	ESourceConfigBackendClass parent_class;
};

GType		e_book_config_o365_get_type	(void) G_GNUC_CONST;
void		e_book_config_o365_type_register(GTypeModule *type_module);

G_END_DECLS

#endif /* E_BOOK_CONFIG_O365_H */
