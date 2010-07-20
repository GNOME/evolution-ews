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

#include "camel-ews-store.h"
#include "camel-ews-transport.h"
#include "camel-ews-utils.h"

#define REPLY_VIEW "default message attachments threading"

G_DEFINE_TYPE (CamelEwsTransport, camel_ews_transport, CAMEL_TYPE_TRANSPORT)

static gboolean
ews_transport_connect (CamelService *service,
                             GError **error)
{
	return TRUE;
}

static gchar *
ews_transport_get_name (CamelService *service,
                              gboolean brief)
{
	if (brief)
		return g_strdup_printf (
			_("GroupWise server %s"),
			service->url->host);
	else
		return g_strdup_printf (
			_("GroupWise mail delivery via %s"),
			service->url->host);
}

static gboolean
ews_send_to (CamelTransport *transport,
                   CamelMimeMessage *message,
                   CamelAddress *from,
                   CamelAddress *recipients,
                   GError **error)
{
	CamelService *service;
	CamelStore *store =  NULL;
	CamelEwsStore *ews_store = NULL;
	CamelEwsStorePrivate *priv = NULL;
	EEwsItem *item ,*temp_item=NULL;
	EEwsConnection *cnc = NULL;
	EEwsConnectionStatus status = 0;
	GSList *sent_item_list = NULL;
	gchar *url = NULL;
	gchar *reply_request = NULL;
	EEwsItemLinkInfo *info = NULL;

	if (!transport) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Authentication failed"));
		return FALSE;
	}

	service = CAMEL_SERVICE(transport);
	url = camel_url_to_string (service->url,
				   (CAMEL_URL_HIDE_PASSWORD |
				    CAMEL_URL_HIDE_PARAMS   |
				    CAMEL_URL_HIDE_AUTH) );

	camel_operation_start (NULL, _("Sending Message") );

	/*camel groupwise store and cnc*/
	store = camel_session_get_store (service->session, url, NULL);
	g_free (url);
	if (!store) {
		g_warning ("ERROR: Could not get a pointer to the store");
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Cannot get folder: Invalid operation on this store"));
		return FALSE;
	}
	ews_store = CAMEL_EWS_STORE (store);
	priv = ews_store->priv;

	cnc = cnc_lookup (priv);
	if (!cnc) {
		g_warning ("||| Eh!!! Failure |||\n");
		camel_operation_end (NULL);
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("Authentication failed"));
		return FALSE;
	}

	item = camel_ews_util_item_from_message (cnc, message, from);

	reply_request = g_strdup (camel_medium_get_header (CAMEL_MEDIUM (message), "X-GW-ORIG-ITEM-ID"));
	if (reply_request) {
		g_strstrip (reply_request);
		status = e_ews_connection_reply_item (cnc, reply_request, REPLY_VIEW, &temp_item);
		if (status != E_EWS_CONNECTION_STATUS_OK)
			g_warning ("Could not send a replyRequest...continuing without!!\n");
		else {
			info = e_ews_item_get_link_info (temp_item);
			e_ews_item_set_link_info (item, info);
		}
		g_free (reply_request);
	}

	/*Send item*/
	status = e_ews_connection_send_item (cnc, item, &sent_item_list);
	if (status != E_EWS_CONNECTION_STATUS_OK) {
		g_warning (" Error Sending mail");
		camel_operation_end (NULL);
		e_ews_item_set_link_info (item, NULL);
		g_object_unref (item);
		if (temp_item)
			g_object_unref (temp_item);

		/* FIXME: 58652 should be changed with an enum.*/
		if (status == 58652)
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("You have exceeded this account's storage limit. Your messages are queued in your Outbox. Resend by pressing Send/Receive after deleting/archiving some of your mail.\n"));
		else
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Could not send message: %s"),
				_("Unknown error"));
		status = 0;
		return FALSE;
	}
	e_ews_item_set_link_info (item, NULL);

	e_ews_item_set_recipient_list (item, NULL);

	if (temp_item)
		g_object_unref (temp_item);
	g_object_unref (item);

	camel_operation_end (NULL);

	return TRUE;
}

static void
camel_ews_transport_class_init (CamelEwsTransportClass *class)
{
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->connect = ews_transport_connect;
	service_class->get_name = ews_transport_get_name;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to = ews_send_to;
}

static void
camel_ews_transport_init (CamelEwsTransport *ews_transport)
{
}
