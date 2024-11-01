/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: JP Rosevear <jpr@ximian.com>
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <libsoup/soup.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>

#include "e-ews-request.h"

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

ESoapRequest *
e_ews_request_new_with_header (const gchar *uri,
                               const gchar *impersonate_user,
                               const gchar *method_name,
                               const gchar *attribute_name,
                               const gchar *attribute_value,
			       EEwsServerVersion server_version,
                               EEwsServerVersion minimum_version,
			       gboolean force_minimum_version,
			       GError **error)
{
	ESoapRequest *req;
	const gchar *server_ver;
	EEwsServerVersion version;

	req = e_soap_request_new (SOUP_METHOD_POST, uri, FALSE, NULL, NULL, NULL, error);
	if (req == NULL) {
		g_warning ("%s: Could not build SOAP message for uri '%s'", G_STRFUNC, uri);
		return NULL;
	}

	e_soap_request_start_envelope (req);

	if (force_minimum_version)
		version = minimum_version;
	else
		version = server_version >= minimum_version ? server_version : minimum_version;

	server_ver = convert_server_version_to_string (version);

	e_soap_request_start_header (req);

	e_soap_request_start_element (
		req, "RequestServerVersion", "types",
		"http://schemas.microsoft.com/exchange/services/2006/types");
	e_soap_request_add_attribute (req, "Version", server_ver, NULL, NULL);
	e_soap_request_end_element (req);

	if (impersonate_user && *impersonate_user) {
		e_soap_request_start_element (req, "ExchangeImpersonation", "types",
			"http://schemas.microsoft.com/exchange/services/2006/types");

		e_soap_request_start_element (req, "ConnectingSID", "types", NULL);

		if (strchr (impersonate_user, '@') != 0)
			e_soap_request_start_element (req, "PrimarySmtpAddress", "types", NULL);
		else
			e_soap_request_start_element (req, "PrincipalName", "types", NULL);
		e_soap_request_write_string (req, impersonate_user);
		e_soap_request_end_element (req); /* PrimarySmtpAddress or PrincipalName */

		e_soap_request_end_element (req); /* ConnectingSID */

		e_soap_request_end_element (req); /* ExchangeImpersonation */
	}

	e_soap_request_end_header (req);

	e_soap_request_start_body (req);
	e_soap_request_add_namespace (
		req, "messages",
		"http://schemas.microsoft.com/exchange/services/2006/messages");
	e_soap_request_start_element (req, method_name, "messages", NULL);
	e_soap_request_set_default_namespace (
		req,
		"http://schemas.microsoft.com/exchange/services/2006/types");
	if (attribute_name != NULL)
		e_soap_request_add_attribute (
			req, attribute_name, attribute_value, NULL, NULL);

	return req;
}

void
e_ews_request_write_string_parameter (ESoapRequest *req,
                                      const gchar *name,
                                      const gchar *prefix,
                                      const gchar *value)
{
	e_soap_request_start_element (req, name, prefix, NULL);
	e_soap_request_write_string (req, value);
	e_soap_request_end_element (req);
}

void
e_ews_request_write_string_parameter_with_attribute (ESoapRequest *req,
                                                     const gchar *name,
                                                     const gchar *prefix,
                                                     const gchar *value,
                                                     const gchar *attribute_name,
                                                     const gchar *attribute_value)
{
	e_soap_request_start_element (req, name, prefix, NULL);
	e_soap_request_add_attribute (
		req, attribute_name, attribute_value, NULL, NULL);
	e_soap_request_write_string (req, value);
	e_soap_request_end_element (req);
}

void
e_ews_request_write_base64_parameter (ESoapRequest *req,
                                      const gchar *name,
                                      const gchar *prefix,
                                      const gchar *value)
{
	e_soap_request_start_element (req, name, prefix, NULL);
	e_soap_request_write_base64 (req, value, strlen (value));
	e_soap_request_end_element (req);
}

void
e_ews_request_write_int_parameter (ESoapRequest *req,
                                   const gchar *name,
                                   const gchar *prefix,
                                   glong value)
{
	e_soap_request_start_element (req, name, prefix, NULL);
	e_soap_request_write_int (req, value);
	e_soap_request_end_element (req);
}

void
e_ews_request_write_double_parameter (ESoapRequest *req,
				      const gchar *name,
				      const gchar *prefix,
				      gdouble value)
{
	e_soap_request_start_element (req, name, prefix, NULL);
	e_soap_request_write_double (req, value);
	e_soap_request_end_element (req);
}

void
e_ews_request_write_time_parameter (ESoapRequest *req,
				    const gchar *name,
				    const gchar *prefix,
				    time_t value)
{
	e_soap_request_start_element (req, name, prefix, NULL);
	e_soap_request_write_time (req, value);
	e_soap_request_end_element (req);
}

void
e_ews_request_write_footer (ESoapRequest *req)
{
	e_soap_request_end_element (req);
	e_soap_request_end_body (req);
	e_soap_request_end_envelope (req);
}

void
e_ews_request_write_extended_tag (ESoapRequest *req,
				  guint32 prop_id,
				  const gchar *prop_type)
{
	gchar *num;

	num = g_strdup_printf ("%d", prop_id);

	e_soap_request_start_element (req, "ExtendedFieldURI", NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyTag", num, NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyType", prop_type, NULL, NULL);
	e_soap_request_end_element (req); /* ExtendedFieldURI */

	g_free (num);
}

void
e_ews_request_write_extended_distinguished_tag (ESoapRequest *req,
						const gchar *set_id,
						guint32 prop_id,
						const gchar *prop_type)
{
	gchar *num;

	num = g_strdup_printf ("%d", prop_id);

	e_soap_request_start_element (req, "ExtendedFieldURI", NULL, NULL);
	e_soap_request_add_attribute (req, "DistinguishedPropertySetId", set_id, NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyId", num, NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyType", prop_type, NULL, NULL);
	e_soap_request_end_element (req); /* ExtendedFieldURI */

	g_free (num);
}

void
e_ews_request_write_extended_name (ESoapRequest *req,
				   const gchar *name,
				   const gchar *prop_type)
{
	e_soap_request_start_element (req, "ExtendedFieldURI", NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyName", name, NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyType", prop_type, NULL, NULL);
	e_soap_request_end_element (req); /* ExtendedFieldURI */
}

void
e_ews_request_write_extended_distinguished_name (ESoapRequest *req,
						 const gchar *set_id,
						 const gchar *name,
						 const gchar *prop_type)
{
	e_soap_request_start_element (req, "ExtendedFieldURI", NULL, NULL);
	e_soap_request_add_attribute (req, "DistinguishedPropertySetId", set_id, NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyName", name, NULL, NULL);
	e_soap_request_add_attribute (req, "PropertyType", prop_type, NULL, NULL);
	e_soap_request_end_element (req); /* ExtendedFieldURI */
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
e_ews_request_replace_server_version (ESoapRequest *req,
				      EEwsServerVersion version)
{
	xmlDocPtr doc;
	xmlXPathContextPtr xpctx;
	xmlXPathObjectPtr result;
	xmlNodeSetPtr nodeset;
	xmlNodePtr node;
	const gchar *server_ver;

	doc = e_soap_request_get_xml_doc (req);
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
