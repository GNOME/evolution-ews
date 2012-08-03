/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-transport.c : class for an ews transport */

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

#include "server/camel-ews-settings.h"

#include "utils/ews-camel-common.h"

#include "camel-ews-store.h"
#include "camel-ews-transport.h"
#include "camel-ews-utils.h"

#define REPLY_VIEW "default message attachments threading"

G_DEFINE_TYPE (CamelEwsTransport, camel_ews_transport, CAMEL_TYPE_TRANSPORT)

static gboolean
ews_transport_connect_sync (CamelService *service,
                            GCancellable *cancellable,
                            GError **error)
{
	return TRUE;
}

static gchar *
ews_transport_get_name (CamelService *service,
                        gboolean brief)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	const gchar *host;

	settings = camel_service_get_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_get_host (network_settings);

	if (brief)
		return g_strdup_printf (
			_("Exchange server %s"), host);
	else
		return g_strdup_printf (
			_("Exchange mail delivery via %s"), host);
}

static gboolean
ews_send_to_sync (CamelTransport *transport,
                  CamelMimeMessage *message,
                  CamelAddress *from,
                  CamelAddress *recipients,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelEwsSettings *ews_settings;
	CamelInternetAddress *used_from;
	CamelSettings *settings;
	CamelService *service;
	EEwsConnection *cnc;
	const gchar *host_url;
	const gchar *user;
	gboolean res;

	service = CAMEL_SERVICE (transport);
	settings = camel_service_get_settings (service);

	ews_settings = CAMEL_EWS_SETTINGS (settings);
	host_url = camel_ews_settings_get_hosturl (ews_settings);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_get_user (network_settings);

	used_from = camel_mime_message_get_from (message);

	if (!used_from && CAMEL_IS_INTERNET_ADDRESS (from))
		used_from = CAMEL_INTERNET_ADDRESS (from);

	if (!used_from || camel_address_length (CAMEL_ADDRESS (used_from)) == 0) {
		g_set_error_literal (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot send message with no From address"));
		return FALSE;
	} else if (camel_address_length (CAMEL_ADDRESS (used_from)) > 1) {
		g_set_error_literal (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Exchange server cannot send message with multiple From addresses"));
		return FALSE;
	} else {
		const gchar *ews_email = NULL, *used_email = NULL;

		ews_email = camel_ews_settings_get_email (ews_settings);
		if (!camel_internet_address_get (used_from, 0, NULL, &used_email)) {
			g_set_error_literal (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Failed to read From address"));
			return FALSE;
		}

		if (!ews_email || !used_email || g_ascii_strcasecmp (ews_email, used_email) != 0) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Exchange server cannot send message as '%s', when the account was configured for address '%s'"),
				used_email ? used_email : "NULL",
				ews_email ? ews_email : "NULL");
			return FALSE;
		}
	}

	cnc = e_ews_connection_find (host_url, user);
	if (!cnc) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Service not connected"));
		return FALSE;
	}

	res = camel_ews_utils_create_mime_message (
		cnc, "SendOnly", NULL, message, 0,
		from, NULL, NULL, cancellable, error);
	g_object_unref (cnc);

	return res;
}

static void
camel_ews_transport_class_init (CamelEwsTransportClass *class)
{
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_EWS_SETTINGS;
	service_class->connect_sync = ews_transport_connect_sync;
	service_class->get_name = ews_transport_get_name;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = ews_send_to_sync;
}

static void
camel_ews_transport_init (CamelEwsTransport *ews_transport)
{
}
