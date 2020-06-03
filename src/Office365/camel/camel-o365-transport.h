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

#ifndef CAMEL_O365_TRANSPORT_H
#define CAMEL_O365_TRANSPORT_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_O365_TRANSPORT \
	(camel_o365_transport_get_type ())
#define CAMEL_O365_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_O365_TRANSPORT, CamelO365Transport))
#define CAMEL_O365_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_O365_TRANSPORT, CamelO365TransportClass))
#define CAMEL_IS_O365_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_O365_TRANSPORT))
#define CAMEL_IS_O365_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_O365_TRANSPORT))
#define CAMEL_O365_TRANSPORT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_O365_TRANSPORT, CamelO365TransportClass))

G_BEGIN_DECLS

typedef struct _CamelO365Transport CamelO365Transport;
typedef struct _CamelO365TransportClass CamelO365TransportClass;
typedef struct _CamelO365TransportPrivate CamelO365TransportPrivate;

struct _CamelO365Transport {
	CamelTransport parent;
	CamelO365TransportPrivate *priv;
};

struct _CamelO365TransportClass {
	CamelTransportClass parent_class;
};

GType camel_o365_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_O365_TRANSPORT_H */
