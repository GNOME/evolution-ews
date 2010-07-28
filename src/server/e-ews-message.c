/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
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

#include <config.h>
#include <string.h>
#include <libsoup/soup-uri.h>
#include "e-ews-message.h"

SoupSoapMessage *
e_ews_message_new_with_header (const gchar *uri, const gchar *method_name)
{
	SoupSoapMessage *msg;

	msg = soup_soap_message_new (SOUP_METHOD_POST, uri, FALSE, NULL, NULL, NULL);
	if (!msg) {
		g_warning (G_STRLOC ": Could not build SOAP message");
		return NULL;
	}

	soup_message_headers_append (SOUP_MESSAGE (msg)->request_headers, "Content-Type", "text/xml");
	soup_message_headers_append (SOUP_MESSAGE (msg)->request_headers, "User-Agent",
				     "Evolution/" VERSION);
	soup_message_headers_append (SOUP_MESSAGE (msg)->request_headers,"Connection",  "Keep-Alive");

	soup_soap_message_start_envelope (msg);
	
	soup_soap_message_start_body(msg);
	soup_soap_message_add_namespace(msg, "types", 
				       "http://schemas.microsoft.com/exchange/services/2006/types");
	soup_soap_message_start_element(msg, method_name, NULL, NULL);
	soup_soap_message_set_default_namespace(msg,
						"http://schemas.microsoft.com/exchange/services/2006/messages");
	return msg;
}

void
e_ews_message_write_string_parameter (SoupSoapMessage *msg, const gchar *name, const gchar *prefix, const gchar *value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_write_string (msg, value);
	soup_soap_message_end_element (msg);
}

void
e_ews_message_write_string_parameter_with_attribute (SoupSoapMessage *msg,
						    const gchar *name,
						    const gchar *prefix,
						    const gchar *value,
						    const gchar *attribute_name,
						    const gchar *attribute_value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_add_attribute (msg, attribute_name, attribute_value, NULL, NULL);
	soup_soap_message_write_string (msg, value);
	soup_soap_message_end_element (msg);
}

void
e_ews_message_write_base64_parameter (SoupSoapMessage *msg, const gchar *name, const gchar *prefix, const gchar *value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_write_base64 (msg, value, strlen (value));
	soup_soap_message_end_element (msg);
}

void
e_ews_message_write_int_parameter (SoupSoapMessage *msg, const gchar *name, const gchar *prefix, glong value)
{
	soup_soap_message_start_element (msg, name, prefix, NULL);
	soup_soap_message_write_int (msg, value);
	soup_soap_message_end_element (msg);
}

void
e_ews_message_write_footer (SoupSoapMessage *msg)
{
	soup_soap_message_end_element (msg);
	soup_soap_message_end_body (msg);
	soup_soap_message_end_envelope (msg);

	soup_soap_message_persist (msg);

	if (g_getenv ("EWS_DEBUG") && (atoi (g_getenv ("EWS_DEBUG")) == 1)) {
		soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (msg)->request_body));
		/* print request's body */
		fputc ('\n', stdout);
		fputs (SOUP_MESSAGE (msg)->request_body->data, stdout);
		fputc ('\n', stdout);
	}
}
