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

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "common/camel-o365-settings.h"

#include "camel-o365-transport.h"

struct _CamelO365TransportPrivate
{
	GMutex property_lock;
	/*EO365Connection *connection;*/
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelO365Transport, camel_o365_transport, CAMEL_TYPE_TRANSPORT)

/*static EO365Connection *
o365_transport_ref_connection (CamelO365Transport *o365_transport)
{
	EO365Connection *connection = NULL;

	g_return_val_if_fail (CAMEL_IS_O365_TRANSPORT (o365_transport), NULL);

	g_mutex_lock (&o365_transport->priv->property_lock);

	if (o365_transport->priv->connection)
		connection = g_object_ref (o365_transport->priv->connection);

	g_mutex_unlock (&o365_transport->priv->property_lock);

	return connection;
}*/

static gboolean
o365_transport_connect_sync (CamelService *service,
			     GCancellable *cancellable,
			     GError **error)
{
	CamelSession *session;
	gboolean success;

	/* Chain up to parent's method. */
	if (!CAMEL_SERVICE_CLASS (camel_o365_transport_parent_class)->connect_sync (service, cancellable, error))
		return FALSE;

	if (camel_service_get_connection_status (service) == CAMEL_SERVICE_DISCONNECTED)
		return FALSE;

	/*connection = o365_transport_ref_connection (CAMEL_O365_TRANSPORT (service));
	if (connection) {
		g_object_unref (connection);
		return TRUE;
	}*/

	session = camel_service_ref_session (service);

	success = camel_session_authenticate_sync (session, service, "Office365", cancellable, error);

	g_object_unref (session);

	return success;
}

static gboolean
o365_transport_disconnect_sync (CamelService *service,
				gboolean clean,
				GCancellable *cancellable,
				GError **error)
{
	CamelO365Transport *o365_transport = CAMEL_O365_TRANSPORT (service);

	g_mutex_lock (&o365_transport->priv->property_lock);
	/*g_clear_object (&o365_transport->priv->connection);*/
	g_mutex_unlock (&o365_transport->priv->property_lock);

	return CAMEL_SERVICE_CLASS (camel_o365_transport_parent_class)->disconnect_sync (service, clean, cancellable, error);
}

static CamelAuthenticationResult
o365_transport_authenticate_sync (CamelService *service,
				  const gchar *mechanism,
				  GCancellable *cancellable,
				  GError **error)
{
	CamelAuthenticationResult result;

	result = CAMEL_AUTHENTICATION_ERROR;

	return result;
}

static gchar *
o365_transport_get_name (CamelService *service,
			 gboolean brief)
{
	gchar *name;

	if (brief)
		name = g_strdup (_("Office365 server"));
	else
		name = g_strdup (_("Mail delivery via Microsoft Office365"));

	return name;
}

static gboolean
o365_send_to_sync (CamelTransport *transport,
		   CamelMimeMessage *message,
		   CamelAddress *from,
		   CamelAddress *recipients,
		   gboolean *out_sent_message_saved,
		   GCancellable *cancellable,
		   GError **error)
{
	return FALSE;
}

static void
o365_transport_dispose (GObject *object)
{
	CamelO365Transport *o365_transport = CAMEL_O365_TRANSPORT (object);

	g_mutex_lock (&o365_transport->priv->property_lock);
	/*g_clear_object (&o365_transport->priv->connection);*/
	g_mutex_unlock (&o365_transport->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_transport_parent_class)->dispose (object);
}

static void
o365_transport_finalize (GObject *object)
{
	CamelO365Transport *o365_transport = CAMEL_O365_TRANSPORT (object);

	g_mutex_clear (&o365_transport->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_transport_parent_class)->finalize (object);
}

static void
camel_o365_transport_class_init (CamelO365TransportClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = o365_transport_dispose;
	object_class->finalize = o365_transport_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_O365_SETTINGS;
	service_class->get_name = o365_transport_get_name;
	service_class->connect_sync = o365_transport_connect_sync;
	service_class->disconnect_sync = o365_transport_disconnect_sync;
	service_class->authenticate_sync = o365_transport_authenticate_sync;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = o365_send_to_sync;
}

static void
camel_o365_transport_init (CamelO365Transport *o365_transport)
{
	o365_transport->priv = camel_o365_transport_get_instance_private (o365_transport);

	g_mutex_init (&o365_transport->priv->property_lock);
}
