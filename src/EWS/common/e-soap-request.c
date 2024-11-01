/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <libedataserver/libedataserver.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <libsoup/soup.h>

#include "e-ews-connection-utils.h"
#include "e-ews-debug.h"

#include "e-soap-request.h"

struct _ESoapRequestPrivate {
	gchar *method;
	GUri *uri;

	ESoapResponseProgressFn progress_fn;
	gpointer progress_data;

	ESoapRequestCustomProcessFn custom_process_fn;
	gpointer custom_process_data;

	gchar *custom_body_content_type;
	gpointer custom_body_data;
	gssize custom_body_data_len;

	gchar *etag;

	gchar *store_node_data_nodename;
	gchar *store_node_data_directory;
	gboolean store_node_data_base64;

	gchar *certificate_pem;
	GTlsCertificateFlags certificate_errors;

	xmlDocPtr doc;
	xmlNodePtr last_node;
	xmlNsPtr soap_ns;
	xmlNsPtr xsi_ns;
	xmlChar *env_prefix;
	xmlChar *env_uri;
	gboolean body_started;
	gchar *action;
};

G_DEFINE_TYPE_WITH_PRIVATE (ESoapRequest, e_soap_request, G_TYPE_OBJECT)

static void
soap_request_finalize (GObject *object)
{
	ESoapRequest *req = E_SOAP_REQUEST (object);

	g_clear_pointer (&req->priv->method, g_free);
	g_clear_pointer (&req->priv->uri, g_uri_unref);
	g_clear_pointer (&req->priv->custom_body_content_type, g_free);
	g_clear_pointer (&req->priv->custom_body_data, g_free);
	g_clear_pointer (&req->priv->etag, g_free);
	g_clear_pointer (&req->priv->certificate_pem, g_free);
	g_clear_pointer (&req->priv->store_node_data_nodename, g_free);
	g_clear_pointer (&req->priv->store_node_data_directory, g_free);

	g_clear_pointer (&req->priv->doc, xmlFreeDoc);
	g_clear_pointer (&req->priv->action, g_free);
	g_clear_pointer (&req->priv->env_uri, xmlFree);
	g_clear_pointer (&req->priv->env_prefix, xmlFree);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_soap_request_parent_class)->finalize (object);
}

static void
e_soap_request_class_init (ESoapRequestClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = soap_request_finalize;
}

static void
e_soap_request_init (ESoapRequest *req)
{
	req->priv = e_soap_request_get_instance_private (req);

	/* initialize XML structures */
	req->priv->doc = xmlNewDoc ((const xmlChar *) "1.0");
	req->priv->doc->standalone = FALSE;
	req->priv->doc->encoding = xmlCharStrdup ("UTF-8");
}

static xmlNsPtr
fetch_ns (ESoapRequest *req,
          const gchar *prefix,
          const gchar *ns_uri)
{
	xmlNsPtr ns = NULL;

	if (prefix && ns_uri)
		ns = xmlNewNs (
			req->priv->last_node,
			(const xmlChar *) ns_uri,
			(const xmlChar *) prefix);
	else if (prefix && !ns_uri) {
		ns = xmlSearchNs (
			req->priv->doc, req->priv->last_node,
			(const xmlChar *) prefix);
		if (!ns)
			ns = xmlNewNs (
				req->priv->last_node,
				(const xmlChar *) "",
				(const xmlChar *) prefix);
	}

	return ns;
}

/**
 * e_soap_request_new:
 * @method: the HTTP method for the created request.
 * @uri_string: the destination endpoint (as a string).
 * @standalone: ??? FIXME
 * @xml_encoding: ??? FIXME
 * @env_prefix: ??? FIXME
 * @env_uri: ??? FIXME
 * @error: a #GError, or %NULL
 *
 * Creates a new empty #ESoapRequest, which will connect to @uri_string.
 *
 * Returns: the new #ESoapRequest (or %NULL if @uri_string could not be
 * parsed).
 */
ESoapRequest *
e_soap_request_new (const gchar *method,
                    const gchar *uri_string,
                    gboolean standalone,
                    const gchar *xml_encoding,
                    const gchar *env_prefix,
                    const gchar *env_uri,
		    GError **error)
{
	ESoapRequest *req;
	GUri *uri;

	uri = g_uri_parse (uri_string, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, error);
	if (!uri)
		return NULL;

	req = e_soap_request_new_from_uri (
		method, uri, standalone,
		xml_encoding, env_prefix, env_uri);

	g_uri_unref (uri);

	return req;
}

/**
 * e_soap_request_new_from_uri:
 * @method: the HTTP method for the created request.
 * @uri: the destination endpoint (as a #GUri).
 * @standalone: ??? FIXME
 * @xml_encoding: ??? FIXME
 * @env_prefix: ??? FIXME
 * @env_uri: ??? FIXME
 *
 * Creates a new empty #ESoapRequest, which will connect to @uri
 *
 * Returns: the new #ESoapRequest
 */
ESoapRequest *
e_soap_request_new_from_uri (const gchar *method,
                             GUri *uri,
                             gboolean standalone,
                             const gchar *xml_encoding,
                             const gchar *env_prefix,
                             const gchar *env_uri)
{
	ESoapRequest *req;

	req = g_object_new (E_TYPE_SOAP_REQUEST, NULL);

	req->priv->method = g_strdup (method);
	req->priv->uri = g_uri_ref (uri);
	req->priv->doc->standalone = standalone;

	if (xml_encoding) {
		xmlFree ((xmlChar *) req->priv->doc->encoding);
		req->priv->doc->encoding = xmlCharStrdup (xml_encoding);
	}

	if (env_prefix != NULL)
		req->priv->env_prefix = xmlCharStrdup (env_prefix);
	if (env_uri != NULL)
		req->priv->env_uri = xmlCharStrdup (env_uri);

	return req;
}

/**
 * e_soap_request_start_envelope:
 * @req: the %ESoapRequest.
 *
 * Starts the top level SOAP Envelope element.
 */
void
e_soap_request_start_envelope (ESoapRequest *req)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->doc->xmlRootNode = xmlNewDocNode (
		req->priv->doc, NULL,
		(const xmlChar *) "Envelope",
		(const xmlChar *) NULL);
	req->priv->last_node = req->priv->doc->xmlRootNode;

	req->priv->soap_ns = xmlNewNs (
		req->priv->doc->xmlRootNode,
		req->priv->env_uri ? req->priv->env_uri :
		(const xmlChar *) "http://schemas.xmlsoap.org/soap/envelope/",
		req->priv->env_prefix ? req->priv->env_prefix :
		(const xmlChar *) "SOAP-ENV");

	if (req->priv->env_uri != NULL) {
		xmlFree (req->priv->env_uri);
		req->priv->env_uri = NULL;
	}

	if (req->priv->env_prefix) {
		xmlFree (req->priv->env_prefix);
		req->priv->env_prefix = NULL;
	}

	xmlSetNs (
		req->priv->doc->xmlRootNode,
		req->priv->soap_ns);
	xmlNewNs (
		req->priv->doc->xmlRootNode,
		(const xmlChar *) "http://schemas.xmlsoap.org/soap/encoding/",
		(const xmlChar *) "SOAP-ENC");
	xmlNewNs (
		req->priv->doc->xmlRootNode,
		(const xmlChar *) "http://www.w3.org/2001/XMLSchema",
		(const xmlChar *) "xsd");
	xmlNewNs (
		req->priv->doc->xmlRootNode,
		(const xmlChar *) "http://schemas.xmlsoap.org/soap/envelope/",
		(const xmlChar *) "SOAP-ENV");
	req->priv->xsi_ns = xmlNewNs (
		req->priv->doc->xmlRootNode,
		(const xmlChar *) "http://www.w3.org/2001/XMLSchema-instance",
		(const xmlChar *) "xsi");
}

/**
 * e_soap_request_end_envelope:
 * @req: the %ESoapRequest.
 *
 * Closes the top level SOAP Envelope element.
 */
void
e_soap_request_end_envelope (ESoapRequest *req)
{
	e_soap_request_end_element (req);
}

/**
 * e_soap_request_start_body:
 * @req: the %ESoapRequest.
 *
 * Starts the SOAP Body element.
 */
void
e_soap_request_start_body (ESoapRequest *req)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	if (req->priv->body_started)
		return;

	req->priv->last_node = xmlNewChild (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "Body", NULL);

	req->priv->body_started = TRUE;
}

/**
 * e_soap_request_end_body:
 * @req: the %ESoapRequest.
 *
 * Closes the SOAP Body element.
 */
void
e_soap_request_end_body (ESoapRequest *req)
{
	e_soap_request_end_element (req);
}

/**
 * e_soap_request_start_element:
 * @req: the #ESoapRequest.
 * @name: the element name.
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI
 *
 * Starts a new arbitrary request element, with @name as the element
 * name, @prefix as the XML Namespace prefix, and @ns_uri as the XML
 * Namespace uri for * the created element.
 *
 * Passing @prefix with no @ns_uri will cause a recursive search for
 * an existing namespace with the same prefix. Failing that a new ns
 * will be created with an empty uri.
 *
 * Passing both @prefix and @ns_uri always causes new namespace
 * attribute creation.
 *
 * Passing NULL for both @prefix and @ns_uri causes no prefix to be
 * used, and the element will be in the default namespace.
 */
void
e_soap_request_start_element (ESoapRequest *req,
                              const gchar *name,
                              const gchar *prefix,
                              const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->last_node = xmlNewChild (
		req->priv->last_node, NULL,
		(const xmlChar *) name, NULL);

	xmlSetNs (req->priv->last_node, fetch_ns (req, prefix, ns_uri));

	if (ns_uri == NULL)
		ns_uri = "";

	if (req->priv->body_started && req->priv->action == NULL)
		req->priv->action = g_strconcat (ns_uri, "#", name, NULL);
}

/**
 * e_soap_request_end_element:
 * @req: the #ESoapRequest.
 *
 * Closes the current request element.
 */
void
e_soap_request_end_element (ESoapRequest *req)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->last_node = req->priv->last_node->parent;
}

/**
 * e_soap_request_start_fault:
 * @req: the #ESoapRequest.
 * @faultcode: faultcode element value
 * @faultstring: faultstring element value
 * @faultfactor: faultfactor element value
 *
 * Starts a new SOAP Fault element, creating faultcode, faultstring,
 * and faultfactor child elements.
 *
 * If you wish to add the faultdetail element, use
 * e_soap_request_start_fault_detail(), and then
 * e_soap_request_start_element() to add arbitrary sub-elements.
 */
void
e_soap_request_start_fault (ESoapRequest *req,
                            const gchar *faultcode,
                            const gchar *faultstring,
                            const gchar *faultfactor)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->last_node = xmlNewChild (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "Fault", NULL);
	xmlNewChild (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "faultcode",
		(const xmlChar *) faultcode);
	xmlNewChild (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "faultstring",
		(const xmlChar *) faultstring);

	req->priv->last_node = xmlNewChild (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "faultfactor",
		(const xmlChar *) faultfactor);
	if (faultfactor == NULL)
		e_soap_request_set_null (req);

	e_soap_request_end_element (req);
}

/**
 * e_soap_request_end_fault:
 * @req: the #ESoapRequest.
 *
 * Closes the current SOAP Fault element.
 */
void
e_soap_request_end_fault (ESoapRequest *req)
{
	e_soap_request_end_element (req);
}

/**
 * e_soap_request_start_fault_detail:
 * @req: the #ESoapRequest.
 *
 * Start the faultdetail child element of the current SOAP Fault
 * element. The faultdetail element allows arbitrary data to be sent
 * in a returned fault.
 **/
void
e_soap_request_start_fault_detail (ESoapRequest *req)
{

	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->last_node = xmlNewChild (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "detail",
		(const xmlChar *) NULL);
}

/**
 * e_soap_request_end_fault_detail:
 * @req: the #ESoapRequest.
 *
 * Closes the current SOAP faultdetail element.
 */
void
e_soap_request_end_fault_detail (ESoapRequest *req)
{
	e_soap_request_end_element (req);
}

/**
 * e_soap_request_start_header:
 * @req: the #ESoapRequest.
 *
 * Creates a new SOAP Header element. You can call
 * e_soap_request_start_header_element() after this to add a new
 * header child element. SOAP Header elements allow out-of-band data
 * to be transferred while not interfering with the request body.
 *
 * This should be called after e_soap_request_start_envelope() and
 * before e_soap_request_start_body().
 */
void
e_soap_request_start_header (ESoapRequest *req)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->last_node = xmlNewChild (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "Header",
		(const xmlChar *) NULL);
}

/**
 * e_soap_request_end_header:
 * @req: the #ESoapRequest.
 *
 * Closes the current SOAP Header element.
 */
void
e_soap_request_end_header (ESoapRequest *req)
{
	e_soap_request_end_element (req);
}

/**
 * e_soap_request_start_header_element:
 * @req: the #ESoapRequest.
 * @name: name of the header element
 * @must_understand: whether the recipient must understand the header in order
 * to proceed with processing the request
 * @actor_uri: the URI which represents the destination actor for this header.
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI
 *
 * Starts a new SOAP arbitrary header element.
 */
void
e_soap_request_start_header_element (ESoapRequest *req,
                                     const gchar *name,
                                     gboolean must_understand,
                                     const gchar *actor_uri,
                                     const gchar *prefix,
                                     const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	e_soap_request_start_element (req, name, prefix, ns_uri);
	if (actor_uri != NULL)
		xmlNewNsProp (
			req->priv->last_node,
			req->priv->soap_ns,
			(const xmlChar *) "actorUri",
			(const xmlChar *) actor_uri);
	if (must_understand)
		xmlNewNsProp (
			req->priv->last_node,
			req->priv->soap_ns,
			(const xmlChar *) "mustUnderstand",
			(const xmlChar *) "1");
}

/**
 * e_soap_request_end_header_element:
 * @req: the #ESoapRequest.
 *
 * Closes the current SOAP header element.
 */
void
e_soap_request_end_header_element (ESoapRequest *req)
{
	e_soap_request_end_element (req);
}

/**
 * e_soap_request_write_int:
 * @req: the #ESoapRequest.
 * @i: the integer value to write.
 *
 * Writes the stringified value of @i as the current element's content.
 */
void
e_soap_request_write_int (ESoapRequest *req,
                          glong i)
{
	gchar *string;

	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	string = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) i);
	e_soap_request_write_string (req, string);
	g_free (string);
}

/**
 * e_soap_request_write_double:
 * @req: the #ESoapRequest.
 * @d: the double value to write.
 *
 * Writes the stringified value of @d as the current element's content.
 */
void
e_soap_request_write_double (ESoapRequest *req,
                             gdouble d)
{
	gchar buffer[G_ASCII_DTOSTR_BUF_SIZE + 1];

	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	g_ascii_dtostr (buffer, sizeof (buffer), d);
	e_soap_request_write_string (req, buffer);
}

/**
 * e_soap_request_write_base64:
 * @req: the #ESoapRequest
 * @string: the binary data buffer to encode
 * @len: the length of data to encode
 *
 * Writes the Base-64 encoded value of @string as the current
 * element's content.
 **/
void
e_soap_request_write_base64 (ESoapRequest *req,
                             const gchar *string,
                             gint len)
{
	gchar *encoded;

	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	encoded = g_base64_encode ((const guchar *) string, len);
	e_soap_request_write_string (req, encoded);
	g_free (encoded);
}

/**
 * e_soap_request_write_time:
 * @req: the #ESoapRequest.
 * @timeval: the time_t to encode
 *
 * Writes the iso8601 value of @timeval as the current element's
 * content.
 **/
void
e_soap_request_write_time (ESoapRequest *req,
                           time_t timeval)
{
	GTimeVal tv;
	gchar *iso_time;

	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	tv.tv_usec = 0;
	tv.tv_sec = timeval;

	iso_time = g_time_val_to_iso8601 (&tv);
	e_soap_request_write_string (req, iso_time);
	g_free (iso_time);
}

/**
 * e_soap_request_write_string:
 * @req: the #ESoapRequest.
 * @string: string to write.
 *
 * Writes the @string as the current element's content.
 */
void
e_soap_request_write_string (ESoapRequest *req,
                             const gchar *string)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	xmlNodeAddContent (
		req->priv->last_node,
		(const xmlChar *) string);
}

/**
 * e_soap_request_write_buffer:
 * @req: the #ESoapRequest.
 * @buffer: the string data buffer to write.
 * @len: length of @buffer.
 *
 * Writes the string buffer pointed to by @buffer as the current
 * element's content.
 */
void
e_soap_request_write_buffer (ESoapRequest *req,
                             const gchar *buffer,
                             gint len)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	xmlNodeAddContentLen (
		req->priv->last_node,
		(const xmlChar *) buffer, len);
}

/**
 * e_soap_request_set_element_type:
 * @req: the #ESoapRequest.
 * @xsi_type: the type name for the element.
 *
 * Sets the current element's XML schema xsi:type attribute, which
 * specifies the element's type name.
 */
void
e_soap_request_set_element_type (ESoapRequest *req,
                                 const gchar *xsi_type)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	xmlNewNsProp (
		req->priv->last_node,
		req->priv->xsi_ns,
		(const xmlChar *) "type",
		(const xmlChar *) xsi_type);
}

/**
 * e_soap_request_set_null:
 * @req: the #ESoapRequest.
 *
 * Sets the current element's XML Schema xsi:null attribute.
 */
void
e_soap_request_set_null (ESoapRequest *req)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	xmlNewNsProp (
		req->priv->last_node,
		req->priv->xsi_ns,
		(const xmlChar *) "null",
		(const xmlChar *) "1");
}

/**
 * e_soap_request_add_attribute:
 * @req: the #ESoapRequest.
 * @name: name of the attribute
 * @value: value of the attribute
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI
 *
 * Adds an XML attribute to the current element.
 */
void
e_soap_request_add_attribute (ESoapRequest *req,
                              const gchar *name,
                              const gchar *value,
                              const gchar *prefix,
                              const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	xmlNewNsProp (
		req->priv->last_node,
		fetch_ns (req, prefix, ns_uri),
		(const xmlChar *) name,
		(const xmlChar *) value);
}

/**
 * e_soap_request_add_namespace:
 * @req: the #ESoapRequest.
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI, or NULL for empty namespace
 *
 * Adds a new XML namespace to the current element.
 */
void
e_soap_request_add_namespace (ESoapRequest *req,
                              const gchar *prefix,
                              const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	if (ns_uri == NULL)
		ns_uri = "";

	xmlNewNs (
		req->priv->last_node,
		(const xmlChar *) ns_uri,
		(const xmlChar *) prefix);
}

/**
 * e_soap_request_set_default_namespace:
 * @req: the #ESoapRequest.
 * @ns_uri: the namespace URI.
 *
 * Sets the default namespace to the URI specified in @ns_uri. The
 * default namespace becomes the namespace all non-explicitly
 * namespaced child elements fall into.
 */
void
e_soap_request_set_default_namespace (ESoapRequest *req,
                                      const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	e_soap_request_add_namespace (req, NULL, ns_uri);
}

/**
 * e_soap_request_set_encoding_style:
 * @req: the #ESoapRequest.
 * @enc_style: the new encodingStyle value
 *
 * Sets the encodingStyle attribute on the current element to the
 * value of @enc_style.
 */
void
e_soap_request_set_encoding_style (ESoapRequest *req,
                                   const gchar *enc_style)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	xmlNewNsProp (
		req->priv->last_node,
		req->priv->soap_ns,
		(const xmlChar *) "encodingStyle",
		(const xmlChar *) enc_style);
}

/**
 * e_soap_request_reset:
 * @req: the #ESoapRequest.
 *
 * Resets the internal XML representation of the SOAP request.
 */
void
e_soap_request_reset (ESoapRequest *req)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	xmlFreeDoc (req->priv->doc);
	req->priv->doc = xmlNewDoc ((const xmlChar *) "1.0");
	req->priv->last_node = NULL;

	g_free (req->priv->action);
	req->priv->action = NULL;
	req->priv->body_started = FALSE;

	if (req->priv->env_uri != NULL) {
		xmlFree (req->priv->env_uri);
		req->priv->env_uri = NULL;
	}

	if (req->priv->env_prefix != NULL) {
		xmlFree (req->priv->env_prefix);
		req->priv->env_prefix = NULL;
	}
}

/**
 * e_soap_request_get_namespace_prefix:
 * @req: the #ESoapRequest.
 * @ns_uri: the namespace URI.
 *
 * Returns the namespace prefix for @ns_uri (or an empty string if
 * @ns_uri is set to the default namespace)
 *
 * Returns: The namespace prefix, or %NULL if no namespace exists
 * for the URI given.
 */
const gchar *
e_soap_request_get_namespace_prefix (ESoapRequest *req,
                                     const gchar *ns_uri)
{
	xmlNsPtr ns = NULL;

	g_return_val_if_fail (E_IS_SOAP_REQUEST (req), NULL);
	g_return_val_if_fail (ns_uri != NULL, NULL);

	ns = xmlSearchNsByHref (
		req->priv->doc,
		req->priv->last_node,
		(const xmlChar *) ns_uri);
	if (ns != NULL) {
		if (ns->prefix != NULL)
			return (const gchar *) ns->prefix;
		else
			return "";
	}

	return NULL;
}

/**
 * e_soap_request_get_xml_doc:
 * @req: the #ESoapRequest.
 *
 * Returns the internal XML representation tree of the
 * #ESoapRequest pointed to by @req.
 *
 * Returns: the #xmlDocPtr representing the SOAP request.
 */
xmlDocPtr
e_soap_request_get_xml_doc (ESoapRequest *req)
{
	g_return_val_if_fail (E_IS_SOAP_REQUEST (req), NULL);

	return req->priv->doc;
}

void
e_soap_request_set_progress_fn (ESoapRequest *req,
				ESoapResponseProgressFn fn,
				gpointer user_data)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->progress_fn = fn;
	req->priv->progress_data = user_data;
}

void
e_soap_request_get_progress_fn (ESoapRequest *req,
				ESoapResponseProgressFn *out_fn,
				gpointer *out_user_data)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));
	g_return_if_fail (out_fn != NULL);
	g_return_if_fail (out_user_data != NULL);

	*out_fn = req->priv->progress_fn;
	*out_user_data = req->priv->progress_data;
}

/**
 * e_soap_request_set_custom_body:
 * @req: the #ESoapRequest.
 * @content_type: (optional): content type of the custom body
 * @body: (optional): the custom message body, or %NULL; ignored when @content_type is %NULL
 * @body_len: length of the @body; ignored when @content_type is %NULL
 *
 * Stores custom body for the request, to be used instead of the SOAP @req content.
 * The @content_type can be %NULL to unset the custom body, or an empty string to indicate
 * no body should be set. The @body and @body_len parameters are ignored in these cases,
 * otherwise they are required.
 *
 * This function exists only to make internal API simpler.
 **/
void
e_soap_request_set_custom_body (ESoapRequest *req,
				const gchar *content_type,
				gconstpointer body,
				gssize body_len)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	if (content_type && *content_type)
		g_return_if_fail (body != NULL);

	g_clear_pointer (&req->priv->custom_body_content_type, g_free);
	g_clear_pointer (&req->priv->custom_body_data, g_free);
	req->priv->custom_body_data_len = 0;

	if (content_type) {
		req->priv->custom_body_content_type = g_strdup (content_type);

		if (*content_type) {
			req->priv->custom_body_data = g_memdup2 (body, body_len);
			req->priv->custom_body_data_len = body_len;
		}
	}
}

void
e_soap_request_set_custom_process_fn (ESoapRequest *req,
				      ESoapRequestCustomProcessFn fn,
				      gpointer user_data)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	req->priv->custom_process_fn = fn;
	req->priv->custom_process_data = user_data;
}

void
e_soap_request_get_custom_process_fn (ESoapRequest *req,
				      ESoapRequestCustomProcessFn *out_fn,
				      gpointer *out_user_data)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));
	g_return_if_fail (out_fn != NULL);
	g_return_if_fail (out_user_data != NULL);

	*out_fn = req->priv->custom_process_fn;
	*out_user_data = req->priv->custom_process_data;
}

/**
 * e_soap_request_take_tls_error_details:
 * @req: an #ESoapRequest
 * @certificate_pem: (transfer full) (nullable): a certificate in PEM format as string, or %NULL
 * @certificate_errors: a %GTlsCertificateFlags describing the errors
 *
 * Set the TLS/SSL error details for the @req. Read it back
 * with e_soap_request_get_tls_error_details().
 *
 * The function assumes ownership of the @certificate_pem.
 **/
void
e_soap_request_take_tls_error_details (ESoapRequest *req,
				       gchar *certificate_pem,
				       GTlsCertificateFlags certificate_errors)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	g_clear_pointer (&req->priv->certificate_pem, g_free);

	req->priv->certificate_pem = certificate_pem;
	req->priv->certificate_errors = certificate_errors;
}

/**
 * e_soap_request_get_tls_error_details:
 * @req: an #ESoapRequest
 * @out_certificate_pem: (out) (transfer none) (optional) (nullable): the certificatete in PEM format as string, or %NULL if not set
 * @out_certificate_errors: (out) (optional): certificate errors, or 0 when none set
 *
 * Retries previously set TLS error details by e_soap_request_take_tls_error_details().
 * The @out_certificate_pem is owned by the @req and is valid until the @req is freed
 * or another call to e_soap_request_take_tls_error_details() is made.
 *
 * Returns: %TRUE, when the certificate PEM was set and the requested arguments
 *    populated; %FALSE otherwise
 **/
gboolean
e_soap_request_get_tls_error_details (ESoapRequest *req,
				      const gchar **out_certificate_pem,
				      GTlsCertificateFlags *out_certificate_errors)
{
	g_return_val_if_fail (E_IS_SOAP_REQUEST (req), FALSE);

	if (!req->priv->certificate_pem)
		return FALSE;

	if (out_certificate_pem)
		*out_certificate_pem = req->priv->certificate_pem;

	if (out_certificate_errors)
		*out_certificate_errors = req->priv->certificate_errors;

	return TRUE;
}

void
e_soap_request_set_etag (ESoapRequest *req,
			 const gchar *etag)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	if (g_strcmp0 (req->priv->etag, etag) != 0) {
		g_clear_pointer (&req->priv->etag, g_free);
		req->priv->etag = g_strdup ((etag && *etag) ? etag : NULL);
	}
}

const gchar *
e_soap_request_get_etag (ESoapRequest *req)
{
	g_return_val_if_fail (E_IS_SOAP_REQUEST (req), NULL);

	return req->priv->etag;
}

void
e_soap_request_set_store_node_data (ESoapRequest *req,
				    const gchar *nodename,
				    const gchar *directory,
				    gboolean base64)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));

	if (g_strcmp0 (req->priv->store_node_data_nodename, nodename) != 0) {
		g_free (req->priv->store_node_data_nodename);
		req->priv->store_node_data_nodename = g_strdup (nodename);
	}

	if (g_strcmp0 (req->priv->store_node_data_directory, directory) != 0) {
		g_free (req->priv->store_node_data_directory);
		req->priv->store_node_data_directory = g_strdup (directory);
	}

	req->priv->store_node_data_base64 = base64;
}

void
e_soap_request_get_store_node_data (ESoapRequest *req,
				    const gchar **out_nodename,
				    const gchar **out_directory,
				    gboolean *out_base64)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));
	g_return_if_fail (out_nodename != NULL);
	g_return_if_fail (out_directory != NULL);
	g_return_if_fail (out_base64 != NULL);

	*out_nodename = req->priv->store_node_data_nodename;
	*out_directory = req->priv->store_node_data_directory;
	*out_base64 = req->priv->store_node_data_base64;
}

/**
 * e_soap_request_persist:
 * @req: the #ESoapRequest.
 * @soup_session: an #ESoupSession to create the #SoupMessage for
 * @settings: a #CamelEwsSettings object, to read User-Agent header information from
 * @error: (optional) (out): return location for a #GError, or %NULL
 *
 * Writes the serialized XML tree to the #SoupMessage's buffer.
 *
 * When a custom body was set with e_soap_request_set_custom_body(), then that body
 * is used instead.
 *
 * Returns: (nullable) (transfer full): a #SoupMessage containing the SOAP request
 *    as its request body, or %NULL on error.
 */
SoupMessage *
e_soap_request_persist (ESoapRequest *req,
			ESoupSession *soup_session,
			CamelEwsSettings *settings,
			GError **error)
{
	SoupMessage *message;

	g_return_val_if_fail (E_IS_SOAP_REQUEST (req), NULL);

	message = e_soup_session_new_message_from_uri (soup_session, req->priv->method, req->priv->uri, error);

	if (!message)
		return NULL;

	if (req->priv->custom_body_content_type) {
		/* Can use empty string as content_type to indicate no body to be set */
		if (*req->priv->custom_body_content_type && req->priv->custom_body_data) {
			e_soup_session_util_set_message_request_body_from_data (message, TRUE,
				req->priv->custom_body_content_type,
				req->priv->custom_body_data,
				req->priv->custom_body_data_len, NULL);
		}
	} else {
		xmlChar *body;
		gint len;

		xmlDocDumpMemory (req->priv->doc, &body, &len);

		e_soup_session_util_set_message_request_body_from_data (message, FALSE,
			"text/xml; charset=utf-8", body, len, (GDestroyNotify) xmlFree);
	}

	e_ews_connection_utils_set_user_agent_header (message, settings);

	soup_message_headers_replace (soup_message_get_request_headers (message), "Connection", "Keep-Alive");

	if (req->priv->etag && *req->priv->etag)
		soup_message_headers_replace (soup_message_get_request_headers (message), "If-None-Match", req->priv->etag);

	return message;
}

void
e_soap_request_setup_response (ESoapRequest *req,
			       ESoapResponse *response)
{
	g_return_if_fail (E_IS_SOAP_REQUEST (req));
	g_return_if_fail (E_IS_SOAP_RESPONSE (response));

	e_soap_response_set_progress_fn (response, req->priv->progress_fn, req->priv->progress_data);
	e_soap_response_set_store_node_data (response, req->priv->store_node_data_nodename,
		req->priv->store_node_data_directory, req->priv->store_node_data_base64);
}
