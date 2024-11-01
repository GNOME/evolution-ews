/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Sivaiah Nallagatla <snallagatla@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
typedef struct _CamelEwsTransportPrivate CamelEwsTransportPrivate;

struct _CamelEwsTransport {
	CamelTransport parent;

	CamelEwsTransportPrivate *priv;
};

struct _CamelEwsTransportClass {
	CamelTransportClass parent_class;
};

GType camel_ews_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_EWS_TRANSPORT_H */
