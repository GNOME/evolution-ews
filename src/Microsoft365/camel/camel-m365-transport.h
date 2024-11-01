/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_TRANSPORT_H
#define CAMEL_M365_TRANSPORT_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_M365_TRANSPORT \
	(camel_m365_transport_get_type ())
#define CAMEL_M365_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_TRANSPORT, CamelM365Transport))
#define CAMEL_M365_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_TRANSPORT, CamelM365TransportClass))
#define CAMEL_IS_M365_TRANSPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_TRANSPORT))
#define CAMEL_IS_M365_TRANSPORT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_TRANSPORT))
#define CAMEL_M365_TRANSPORT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_TRANSPORT, CamelM365TransportClass))

G_BEGIN_DECLS

typedef struct _CamelM365Transport CamelM365Transport;
typedef struct _CamelM365TransportClass CamelM365TransportClass;
typedef struct _CamelM365TransportPrivate CamelM365TransportPrivate;

struct _CamelM365Transport {
	CamelTransport parent;
	CamelM365TransportPrivate *priv;
};

struct _CamelM365TransportClass {
	CamelTransportClass parent_class;
};

GType camel_m365_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_M365_TRANSPORT_H */
