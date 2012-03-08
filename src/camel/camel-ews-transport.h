/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-transport.h : class for an ews transport */

/*
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_EWS_TRANSPORT_H
#define CAMEL_EWS_TRANSPORT_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_TRANSPORT \
	(camel_ews_transport_get_type ())
#define CAMEL_EWS_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_TRANSPORT, CamelEwsTransport))
#define CAMEL_EWS_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_TRANSPORT, CamelEwsTransportClass))
#define CAMEL_IS_EWS_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_TRANSPORT))
#define CAMEL_IS_EWS_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_TRANSPORT))
#define CAMEL_EWS_TRANSPORT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_TRANSPORT, CamelEwsTransportClass))

G_BEGIN_DECLS

typedef struct _CamelEwsTransport CamelEwsTransport;
typedef struct _CamelEwsTransportClass CamelEwsTransportClass;

struct _CamelEwsTransport {
	CamelTransport parent;
	gboolean connected;
};

struct _CamelEwsTransportClass {
	CamelTransportClass parent_class;
};

GType camel_ews_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_EWS_TRANSPORT_H */
