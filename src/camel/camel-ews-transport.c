/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-transport.c : class for an groupwise transport */

/*
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *	    Parthasarathi Susarla <sparthasarathi@novell.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include <e-ews-compat.h>

#include "camel-ews-store.h"
#include "camel-ews-transport.h"
#include "camel-ews-utils.h"
#include <ews-camel-common.h>
#include "ews-camel-compat.h"

static gboolean
ews_transport_connect_sync (CamelService *service,
                            EVO3(GCancellable *cancellable,)
			    CamelException *ex)
{
	return TRUE;
}

static gchar *
ews_transport_get_name (CamelService *service,
                              gboolean brief)
{
	if (brief)
		return g_strdup_printf (
			_("Exchange server %s"),
			service->url->host);
	else
		return g_strdup_printf (
			_("Exchange mail delivery via %s"),
			service->url->host);
}

static gboolean
ews_send_to_sync (CamelTransport *transport,
		  CamelMimeMessage *message,
		  CamelAddress *from,
		  CamelAddress *recipients,
		  EVO3(GCancellable *cancellable,)
		  CamelException *ex)
{
	EVO2(GCancellable *cancellable = NULL;)
	CamelService *service;
	EEwsConnection *cnc;
	const gchar *host_url;
	gboolean res;
	GError *error = NULL;

	service = CAMEL_SERVICE (transport);
	host_url = camel_url_get_param (service->url, "hosturl");

	cnc = e_ews_connection_find (host_url, service->url->user);
	if (!cnc) {
		g_set_error (&error, CAMEL_SERVICE_ERROR,
			     CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			     _("Service not connected"));
		ews_compat_propagate_gerror_to_exception (error, ex);
		return FALSE;
	}

	res = camel_ews_utils_create_mime_message (cnc, "SendOnly", NULL,
						   message, 0, from,
						   NULL, NULL,
						   cancellable, &error);
	ews_compat_propagate_gerror_to_exception (error, ex);
	g_object_unref (cnc);
	return res;
}

static void
camel_ews_transport_class_init (CamelEwsTransportClass *class)
{
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->EVO3_sync(connect) = ews_transport_connect_sync;
	service_class->get_name = ews_transport_get_name;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->EVO3_sync(send_to) = ews_send_to_sync;
}

static void
camel_ews_transport_init (CamelEwsTransport *ews_transport)
{
}

CamelType
camel_ews_transport_get_type (void)
{
	static CamelType camel_ews_transport_type = CAMEL_INVALID_TYPE;

	if (camel_ews_transport_type == CAMEL_INVALID_TYPE) {
		camel_ews_transport_type =
			camel_type_register (CAMEL_TRANSPORT_TYPE,
					     "CamelEwsTransport",
					     sizeof (CamelEwsTransport),
					     sizeof (CamelEwsTransportClass),
					     (CamelObjectClassInitFunc) camel_ews_transport_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_ews_transport_init,
					     NULL);
	}

	return camel_ews_transport_type;
}
