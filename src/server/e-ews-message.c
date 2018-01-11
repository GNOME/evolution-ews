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

#include "evolution-ews-config.h"

#include <string.h>
#include <libsoup/soup-uri.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include "e-ews-message.h"

static SoupBuffer *
ews_soup_chunk_allocator (SoupMessage *message,
			  gsize max_len,
			  gpointer user_data)
{
	gsize len = 32768;
	guchar *data;

	data = g_new0 (guchar, len);

	return soup_buffer_new_take (data, len);
}

void
e_ews_message_attach_chunk_allocator (SoupMessage *message)
{
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	soup_message_set_chunk_allocator (message, ews_soup_chunk_allocator, NULL, NULL);
}

static const gchar *
convert_server_version_to_string (EEwsServerVersion version)
{
	/* server info */
	switch (version) {
		/*
		 * If we don't know the server version, let's use the safest possible
		 */
		case E_EWS_EXCHANGE_UNKNOWN:
			return "Exchange2007_SP1";
		case E_EWS_EXCHANGE_2007:
			return "Exchange2007";
		case E_EWS_EXCHANGE_2007_SP1:
			return "Exchange2007_SP1";
		case E_EWS_EXCHANGE_2010:
			return "Exchange2010";
		case E_EWS_EXCHANGE_2010_SP1:
			return "Exchange2010_SP1";
		case E_EWS_EXCHANGE_2010_SP2:
			return "Exchange2010_SP2";
		/*
		 * If we don't have support for the latest version, let's use the latest possible
		 */
		case E_EWS_EXCHANGE_FUTURE:
		case E_EWS_EXCHANGE_2013:
			return "Exchange2013";
	}

	return "Exchange2007";
}

void
e_ews_message_set_user_agent_header (SoupMessage *message,
				     CamelEwsSettings *settings)
{
	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if (camel_ews_settings_get_override_user_agent (settings)) {
		gchar *user_agent;

		user_agent = camel_ews_settings_dup_user_agent (settings);

		if (user_agent && *user_agent) {
			soup_message_headers_append (message->request_headers, "User-Agent", user_agent);
		}

		g_free (user_agent);
	} else {
		soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
	}
}

ESoapMessage *
e_ews_message_new_with_header (CamelEwsSettings *settings,
			       const gchar *uri,
                               const gchar *impersonate_user,
                               const gchar *method_name,
                               const gchar *attribute_name,
                               const gchar *attribute_value,
			       EEwsServerVersion server_version,
                               EEwsServerVersion minimum_version,
			       gboolean force_minimum_version,
			       gboolean standard_handlers)
{
	ESoapMessage *msg;
	const gchar *server_ver;
	EEwsServerVersion version;

	msg = e_soap_message_new (
		SOUP_METHOD_POST, uri, FALSE, NULL, NULL, NULL, standard_handlers);
	if (msg == NULL) {
		g_warning ("%s: Could not build SOAP message for uri '%s'", G_STRFUNC, uri);
		return NULL;
	}

	e_ews_message_attach_chunk_allocator (SOUP_MESSAGE (msg));

	soup_message_headers_append (
		SOUP_MESSAGE (msg)->request_headers,
		"Content-Type", "text/xml; charset=utf-8");
	e_ews_message_set_user_agent_header (SOUP_MESSAGE (msg), settings);
	soup_message_headers_append (
		SOUP_MESSAGE (msg)->request_headers,
		"Connection", "Keep-Alive");

	e_soap_message_start_envelope (msg);

	if (force_minimum_version)
		version = minimum_version;
	else
		version = server_version >= minimum_version ? server_version : minimum_version;

	server_ver = convert_server_version_to_string (version);

	e_soap_message_start_header (msg);

	e_soap_message_start_element (
		msg, "RequestServerVersion", "types",
		"http://schemas.microsoft.com/exchange/services/2006/types");
	e_soap_message_add_attribute (msg, "Version", server_ver, NULL, NULL);
	e_soap_message_end_element (msg);

	if (impersonate_user && *impersonate_user) {
		e_soap_message_start_element (msg, "ExchangeImpersonation", "types",
			"http://schemas.microsoft.com/exchange/services/2006/types");

		e_soap_message_start_element (msg, "ConnectingSID", "types", NULL);

		if (strchr (impersonate_user, '@') != 0)
			e_soap_message_start_element (msg, "PrimarySmtpAddress", "types", NULL);
		else
			e_soap_message_start_element (msg, "PrincipalName", "types", NULL);
		e_soap_message_write_string (msg, impersonate_user);
		e_soap_message_end_element (msg); /* PrimarySmtpAddress or PrincipalName */

		e_soap_message_end_element (msg); /* ConnectingSID */

		e_soap_message_end_element (msg); /* ExchangeImpersonation */
	}

	e_soap_message_end_header (msg);

	e_soap_message_start_body (msg);
	e_soap_message_add_namespace (
		msg, "messages",
		"http://schemas.microsoft.com/exchange/services/2006/messages");
	e_soap_message_start_element (msg, method_name, "messages", NULL);
	e_soap_message_set_default_namespace (
		msg,
		"http://schemas.microsoft.com/exchange/services/2006/types");
	if (attribute_name != NULL)
		e_soap_message_add_attribute (
			msg, attribute_name, attribute_value, NULL, NULL);

	return msg;
}

void
e_ews_message_write_string_parameter (ESoapMessage *msg,
                                      const gchar *name,
                                      const gchar *prefix,
                                      const gchar *value)
{
	e_soap_message_start_element (msg, name, prefix, NULL);
	e_soap_message_write_string (msg, value);
	e_soap_message_end_element (msg);
}

void
e_ews_message_write_string_parameter_with_attribute (ESoapMessage *msg,
                                                     const gchar *name,
                                                     const gchar *prefix,
                                                     const gchar *value,
                                                     const gchar *attribute_name,
                                                     const gchar *attribute_value)
{
	e_soap_message_start_element (msg, name, prefix, NULL);
	e_soap_message_add_attribute (
		msg, attribute_name, attribute_value, NULL, NULL);
	e_soap_message_write_string (msg, value);
	e_soap_message_end_element (msg);
}

void
e_ews_message_write_base64_parameter (ESoapMessage *msg,
                                      const gchar *name,
                                      const gchar *prefix,
                                      const gchar *value)
{
	e_soap_message_start_element (msg, name, prefix, NULL);
	e_soap_message_write_base64 (msg, value, strlen (value));
	e_soap_message_end_element (msg);
}

void
e_ews_message_write_int_parameter (ESoapMessage *msg,
                                   const gchar *name,
                                   const gchar *prefix,
                                   glong value)
{
	e_soap_message_start_element (msg, name, prefix, NULL);
	e_soap_message_write_int (msg, value);
	e_soap_message_end_element (msg);
}

void
e_ews_message_write_double_parameter (ESoapMessage *msg,
				      const gchar *name,
				      const gchar *prefix,
				      gdouble value)
{
	e_soap_message_start_element (msg, name, prefix, NULL);
	e_soap_message_write_double (msg, value);
	e_soap_message_end_element (msg);
}

void
e_ews_message_write_time_parameter (ESoapMessage *msg,
				    const gchar *name,
				    const gchar *prefix,
				    time_t value)
{
	e_soap_message_start_element (msg, name, prefix, NULL);
	e_soap_message_write_time (msg, value);
	e_soap_message_end_element (msg);
}

void
e_ews_message_write_footer (ESoapMessage *msg)
{
	e_soap_message_end_element (msg);
	e_soap_message_end_body (msg);
	e_soap_message_end_envelope (msg);

	e_soap_message_persist (msg);
}

void
e_ews_message_write_extended_tag (ESoapMessage *msg,
				  guint32 prop_id,
				  const gchar *prop_type)
{
	gchar *num;

	num = g_strdup_printf ("%d", prop_id);

	e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyTag", num, NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyType", prop_type, NULL, NULL);
	e_soap_message_end_element (msg); /* ExtendedFieldURI */

	g_free (num);
}

void
e_ews_message_write_extended_distinguished_tag (ESoapMessage *msg,
						const gchar *set_id,
						guint32 prop_id,
						const gchar *prop_type)
{
	gchar *num;

	num = g_strdup_printf ("%d", prop_id);

	e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
	e_soap_message_add_attribute (msg, "DistinguishedPropertySetId", set_id, NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyId", num, NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyType", prop_type, NULL, NULL);
	e_soap_message_end_element (msg); /* ExtendedFieldURI */

	g_free (num);
}

void
e_ews_message_write_extended_name (ESoapMessage *msg,
				   const gchar *name,
				   const gchar *prop_type)
{
	e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyName", name, NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyType", prop_type, NULL, NULL);
	e_soap_message_end_element (msg); /* ExtendedFieldURI */
}

void
e_ews_message_write_extended_distinguished_name (ESoapMessage *msg,
						 const gchar *set_id,
						 const gchar *name,
						 const gchar *prop_type)
{
	e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
	e_soap_message_add_attribute (msg, "DistinguishedPropertySetId", set_id, NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyName", name, NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyType", prop_type, NULL, NULL);
	e_soap_message_end_element (msg); /* ExtendedFieldURI */
}

static xmlXPathObjectPtr
xpath_eval (xmlXPathContextPtr ctx,
	    const gchar *format,
	    ...)
{
	xmlXPathObjectPtr result;
	va_list args;
	gchar *expr;

	if (ctx == NULL)
		return NULL;

	va_start (args, format);
	expr = g_strdup_vprintf (format, args);
	va_end (args);

	result = xmlXPathEvalExpression (BAD_CAST expr, ctx);
	g_free (expr);

	if (result == NULL)
		return NULL;

	if (result->type == XPATH_NODESET && xmlXPathNodeSetIsEmpty (result->nodesetval)) {
		xmlXPathFreeObject (result);
		return NULL;
	}

	return result;
}

void
e_ews_message_replace_server_version (ESoapMessage *msg,
				      EEwsServerVersion version)
{
	xmlDocPtr doc;
	xmlXPathContextPtr xpctx;
	xmlXPathObjectPtr result;
	xmlNodeSetPtr nodeset;
	xmlNodePtr node;
	const gchar *server_ver;

	doc = e_soap_message_get_xml_doc (msg);
	xpctx = xmlXPathNewContext (doc);

	xmlXPathRegisterNs (
			xpctx,
			BAD_CAST "s",
			BAD_CAST "http://schemas.xmlsoap.org/soap/envelope/");

	xmlXPathRegisterNs (
			xpctx,
			BAD_CAST "t",
			BAD_CAST "http://schemas.microsoft.com/exchange/services/2006/types");

	result = xpath_eval (xpctx, "/s:Envelope/s:Header/t:RequestServerVersion");
	if (result != NULL) {
		server_ver = convert_server_version_to_string (version);

		nodeset = result->nodesetval;
		node = nodeset->nodeTab[0];
		xmlSetProp (node, BAD_CAST "Version", BAD_CAST server_ver);
	}

	xmlXPathFreeObject (result);
	xmlXPathFreeContext (xpctx);
}
