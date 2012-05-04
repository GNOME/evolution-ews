/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EXCHANGE_EWS_ACCOUNT_LISTENER_H
#define EXCHANGE_EWS_ACCOUNT_LISTENER_H

#include <glib.h>
#include <glib-object.h>
#include <camel/camel.h>
G_BEGIN_DECLS

#define EXCHANGE_EWS_ACCOUNT_LISTENER_TYPE		(exchange_ews_account_listener_get_type ())
#define EXCHANGE_EWS_ACCOUNT_LISTENER(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_EWS_ACCOUNT_LISTENER_TYPE, ExchangeEWSAccountListener))
#define EXCHANGE_EWS_ACCOUNT_LISTENER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_EWS_ACCOUNT_LISTENER_TYPE,  ExchangeEWSAccountListenerClass))
#define EXCHANGE_EWS_IS_ACCOUNT_LISTENER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_EWS_ACCOUNT_LISTENER_TYPE))
#define EXCHANGE_EWS_IS_ACCOUNT_LISTENER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_EWS_ACCOUNT_LISTENER_TYPE))

typedef struct _ExchangeEWSAccountListener		ExchangeEWSAccountListener;
typedef struct _ExchangeEWSAccountListenerClass	ExchangeEWSAccountListenerClass;
typedef struct _ExchangeEWSAccountListenerPrivate	ExchangeEWSAccountListenerPrivate;

struct _ExchangeEWSAccountListener {
	GObject parent;
	ExchangeEWSAccountListenerPrivate *priv;
};

struct _ExchangeEWSAccountListenerClass {
	GObjectClass parent_class;
};

GType				exchange_ews_account_listener_get_type (void);
ExchangeEWSAccountListener *	exchange_ews_account_listener_new (void);

G_END_DECLS

#endif /* EXCHANGE_EWS_ACCOUNT_LISTENER_H */
