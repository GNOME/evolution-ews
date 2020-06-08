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

#ifndef E_O365_MESSAGE_H
#define E_O365_MESSAGE_H

#include <glib-object.h>

#include <libsoup/soup.h>

/* Standard GObject macros */
#define E_TYPE_O365_MESSAGE \
	(e_o365_message_get_type ())
#define E_O365_MESSAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_O365_MESSAGE, EO365Message))
#define E_O365_MESSAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_O365_MESSAGE, EO365MessageClass))
#define E_IS_O365_MESSAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_O365_MESSAGE))
#define E_IS_O365_MESSAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_O365_MESSAGE))
#define E_O365_MESSAGE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_O365_MESSAGE))

G_BEGIN_DECLS

typedef struct _EO365Message EO365Message;
typedef struct _EO365MessageClass EO365MessageClass;
typedef struct _EO365MessagePrivate EO365MessagePrivate;

struct _EO365Message {
	SoupMessage parent;
	EO365MessagePrivate *priv;
};

struct _EO365MessageClass {
	SoupMessageClass parent_class;
};

GType		e_o365_message_get_type	(void) G_GNUC_CONST;

EO365Message *	e_o365_message_new	(void);

G_END_DECLS

#endif /* E_O365_MESSAGE_H */
