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

#ifndef E_O365_CONNECTION_H
#define E_O365_CONNECTION_H

#include <glib-object.h>

#include <libebackend/libebackend.h>
#include <libsoup/soup.h>

#include "camel-o365-settings.h"

/* Standard GObject macros */
#define E_TYPE_O365_CONNECTION \
	(e_o365_connection_get_type ())
#define E_O365_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_O365_CONNECTION, EO365Connection))
#define E_O365_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_O365_CONNECTION, EO365ConnectionClass))
#define E_IS_O365_CONNECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_O365_CONNECTION))
#define E_IS_O365_CONNECTION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_O365_CONNECTION))
#define E_O365_CONNECTION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_O365_CONNECTION))

G_BEGIN_DECLS

typedef struct _EO365Connection EO365Connection;
typedef struct _EO365ConnectionClass EO365ConnectionClass;
typedef struct _EO365ConnectionPrivate EO365ConnectionPrivate;

struct _EO365Connection {
	GObject parent;
	EO365ConnectionPrivate *priv;
};

struct _EO365ConnectionClass {
	GObjectClass parent_class;
};

GType		e_o365_connection_get_type	(void) G_GNUC_CONST;

EO365Connection *
		e_o365_connection_new		(ESource *source,
						 CamelO365Settings *settings);
EO365Connection *
		e_o365_connection_new_for_backend
						(EBackend *backend,
						 ESourceRegistry *registry,
						 ESource *source,
						 CamelO365Settings *settings);
EO365Connection *
		e_o365_connection_new_full	(ESource *source,
						 CamelO365Settings *settings,
						 gboolean allow_reuse);
ESource *	e_o365_connection_get_source	(EO365Connection *cnc);
CamelO365Settings *
		e_o365_connection_get_settings	(EO365Connection *cnc);
guint		e_o365_connection_get_concurrent_connections
						(EO365Connection *cnc);
void		e_o365_connection_set_concurrent_connections
						(EO365Connection *cnc,
						 guint concurrent_connections);
GProxyResolver *e_o365_connection_ref_proxy_resolver
						(EO365Connection *cnc);
void		e_o365_connection_set_proxy_resolver
						(EO365Connection *cnc,
						 GProxyResolver *proxy_resolver);
ESoupAuthBearer *
		e_o365_connection_ref_bearer_auth
						(EO365Connection *cnc);
void		e_o365_connection_set_bearer_auth
						(EO365Connection *cnc,
						 ESoupAuthBearer *bearer_auth);

G_END_DECLS

#endif /* E_O365_CONNECTION_H */
