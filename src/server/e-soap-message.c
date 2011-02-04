/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <string.h>
#include <libsoup/soup.h>
#include "e-soap-message.h"

G_DEFINE_TYPE (ESoapMessage, e_soap_message, SOUP_TYPE_MESSAGE)

typedef struct {
	/* Serialization fields */
	xmlDocPtr doc;
	xmlNodePtr last_node;
	xmlNsPtr soap_ns;
	xmlNsPtr xsi_ns;
	xmlChar *env_prefix;
	xmlChar *env_uri;
	gboolean body_started;
	gchar *action;
} ESoapMessagePrivate;
#define E_SOAP_MESSAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_SOAP_MESSAGE, ESoapMessagePrivate))

static void
finalize (GObject *object)
{
	ESoapMessagePrivate *priv = E_SOAP_MESSAGE_GET_PRIVATE (object);

	if (priv->doc)
		xmlFreeDoc (priv->doc);
	if (priv->action)
		g_free (priv->action);
	if (priv->env_uri)
                xmlFree (priv->env_uri);
	if (priv->env_prefix)
                xmlFree (priv->env_prefix);

	G_OBJECT_CLASS (e_soap_message_parent_class)->finalize (object);
}

static void
e_soap_message_class_init (ESoapMessageClass *e_soap_message_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (e_soap_message_class);

	g_type_class_add_private (e_soap_message_class, sizeof (ESoapMessagePrivate));

	object_class->finalize = finalize;
}

static void
e_soap_message_init (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	/* initialize XML structures */
	priv->doc = xmlNewDoc ((const xmlChar *)"1.0");
	priv->doc->standalone = FALSE;
	priv->doc->encoding = xmlCharStrdup ("UTF-8");
}

static xmlNsPtr
fetch_ns (ESoapMessage *msg, const gchar *prefix, const gchar *ns_uri)
{
	ESoapMessagePrivate *priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);
        xmlNsPtr ns = NULL;

        if (prefix && ns_uri)
                ns = xmlNewNs (priv->last_node, (const xmlChar *)ns_uri, (const xmlChar *)prefix);
        else if (prefix && !ns_uri) {
                ns = xmlSearchNs (priv->doc, priv->last_node, (const xmlChar *)prefix);
                if (!ns)
			ns = xmlNewNs (priv->last_node, (const xmlChar *)"", (const xmlChar *)prefix);
        }

        return ns;
}

/**
 * e_soap_message_new:
 * @method: the HTTP method for the created request.
 * @uri_string: the destination endpoint (as a string).
 * @standalone: ??? FIXME
 * @xml_encoding: ??? FIXME
 * @env_prefix: ??? FIXME
 * @env_uri: ??? FIXME
 *
 * Creates a new empty #ESoapMessage, which will connect to @uri_string.
 *
 * Returns: the new #ESoapMessage (or %NULL if @uri_string could not be
 * parsed).
 *
 * Since: 2.92
 */
ESoapMessage *
e_soap_message_new (const gchar *method, const gchar *uri_string,
		       gboolean standalone, const gchar *xml_encoding,
		       const gchar *env_prefix, const gchar *env_uri)
{
	ESoapMessage *msg;
	SoupURI *uri;

	uri = soup_uri_new (uri_string);
	if (!uri)
		return NULL;

	msg = e_soap_message_new_from_uri (method, uri, standalone,
					      xml_encoding, env_prefix, env_uri);

	soup_uri_free (uri);

	return msg;
}

/**
 * e_soap_message_new_from_uri:
 * @method: the HTTP method for the created request.
 * @uri: the destination endpoint (as a #SoupURI).
 * @standalone: ??? FIXME
 * @xml_encoding: ??? FIXME
 * @env_prefix: ??? FIXME
 * @env_uri: ??? FIXME
 *
 * Creates a new empty #ESoapMessage, which will connect to @uri
 *
 * Returns: the new #ESoapMessage
 *
 * Since: 2.92
 */
ESoapMessage *
e_soap_message_new_from_uri (const gchar *method, SoupURI *uri,
				gboolean standalone, const gchar *xml_encoding,
				const gchar *env_prefix, const gchar *env_uri)
{
	ESoapMessage *msg;
	ESoapMessagePrivate *priv;

	msg = g_object_new (E_TYPE_SOAP_MESSAGE,
			    SOUP_MESSAGE_METHOD, method,
			    SOUP_MESSAGE_URI, uri,
			    NULL);

	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->doc->standalone = standalone;

	if (xml_encoding) {
		xmlFree ((xmlChar *)priv->doc->encoding);
		priv->doc->encoding = xmlCharStrdup (xml_encoding);
	}

	if (env_prefix)
		priv->env_prefix = xmlCharStrdup (env_prefix);
	if (env_uri)
		priv->env_uri = xmlCharStrdup (env_uri);

	return msg;
}

/**
 * e_soap_message_start_envelope:
 * @msg: the %ESoapMessage.
 *
 * Starts the top level SOAP Envelope element.
 *
 * Since: 2.92
 */
void
e_soap_message_start_envelope (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->last_node = priv->doc->xmlRootNode =
		xmlNewDocNode (priv->doc, NULL, (const xmlChar *)"Envelope", NULL);

	priv->soap_ns = xmlNewNs (priv->doc->xmlRootNode,
				  priv->env_uri ? priv->env_uri :
				  (const xmlChar *)"http://schemas.xmlsoap.org/soap/envelope/",
				  priv->env_prefix ? priv->env_prefix : (const xmlChar *)"SOAP-ENV");
	if (priv->env_uri) {
		xmlFree (priv->env_uri);
		priv->env_uri = NULL;
	}
	if (priv->env_prefix) {
		xmlFree (priv->env_prefix);
		priv->env_prefix = NULL;
	}

	xmlSetNs (priv->doc->xmlRootNode, priv->soap_ns);

	xmlNewNs (priv->doc->xmlRootNode,
		  (const xmlChar *)"http://schemas.xmlsoap.org/soap/encoding/",
                  (const xmlChar *)"SOAP-ENC");
	xmlNewNs (priv->doc->xmlRootNode,
                  (const xmlChar *)"http://www.w3.org/1999/XMLSchema",
                  (const xmlChar *)"xsd");
	xmlNewNs (priv->doc->xmlRootNode,
		  (const xmlChar *)"http://schemas.xmlsoap.org/soap/envelope/",
		  (const xmlChar *)"SOAP-ENV");
	priv->xsi_ns = xmlNewNs (priv->doc->xmlRootNode,
				 (const xmlChar *)"http://www.w3.org/1999/XMLSchema-instance",
				 (const xmlChar *)"xsi");
}

/**
 * e_soap_message_end_envelope:
 * @msg: the %ESoapMessage.
 *
 * Closes the top level SOAP Envelope element.
 *
 * Since: 2.92
 */
void
e_soap_message_end_envelope (ESoapMessage *msg)
{
	e_soap_message_end_element (msg);
}

/**
 * e_soap_message_start_body:
 * @msg: the %ESoapMessage.
 *
 * Starts the SOAP Body element.
 *
 * Since: 2.92
 */
void
e_soap_message_start_body (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	if (priv->body_started)
		return;

	priv->last_node = xmlNewChild (priv->last_node,
				       priv->soap_ns,
				       (const xmlChar *)"Body", NULL);

	priv->body_started = TRUE;
}

/**
 * e_soap_message_end_body:
 * @msg: the %ESoapMessage.
 *
 * Closes the SOAP Body element.
 *
 * Since: 2.92
 */
void
e_soap_message_end_body (ESoapMessage *msg)
{
	e_soap_message_end_element (msg);
}

/**
 * e_soap_message_start_element:
 * @msg: the #ESoapMessage.
 * @name: the element name.
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI
 *
 * Starts a new arbitrary message element, with @name as the element
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
 *
 * Since: 2.92
 */
void
e_soap_message_start_element (ESoapMessage *msg,
				 const gchar *name,
				 const gchar *prefix,
				 const gchar *ns_uri)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->last_node = xmlNewChild (priv->last_node, NULL, (const xmlChar *)name, NULL);

	xmlSetNs (priv->last_node, fetch_ns (msg, prefix, ns_uri));

	if (priv->body_started && !priv->action)
		priv->action = g_strconcat (ns_uri ? ns_uri : "",
					    "#", name, NULL);
}

/**
 * e_soap_message_end_element:
 * @msg: the #ESoapMessage.
 *
 * Closes the current message element.
 *
 * Since: 2.92
 */
void
e_soap_message_end_element (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->last_node = priv->last_node->parent;
}

/**
 * e_soap_message_start_fault:
 * @msg: the #ESoapMessage.
 * @faultcode: faultcode element value
 * @faultstring: faultstring element value
 * @faultfactor: faultfactor element value
 *
 * Starts a new SOAP Fault element, creating faultcode, faultstring,
 * and faultfactor child elements.
 *
 * If you wish to add the faultdetail element, use
 * e_soap_message_start_fault_detail(), and then
 * e_soap_message_start_element() to add arbitrary sub-elements.
 *
 * Since: 2.92
 */
void
e_soap_message_start_fault (ESoapMessage *msg,
			       const gchar *faultcode,
			       const gchar *faultstring,
			       const gchar *faultfactor)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->last_node = xmlNewChild (priv->last_node,
				       priv->soap_ns,
				       (const xmlChar *)"Fault", NULL);
	xmlNewChild (priv->last_node, priv->soap_ns, (const xmlChar *)"faultcode", (const xmlChar *)faultcode);
	xmlNewChild (priv->last_node, priv->soap_ns, (const xmlChar *)"faultstring", (const xmlChar *)faultstring);

	priv->last_node = xmlNewChild (priv->last_node, priv->soap_ns,
				       (const xmlChar *)"faultfactor", (const xmlChar *)faultfactor);
	if (!faultfactor)
		e_soap_message_set_null (msg);

	e_soap_message_end_element (msg);
}

/**
 * e_soap_message_end_fault:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP Fault element.
 *
 * Since: 2.92
 */
void
e_soap_message_end_fault (ESoapMessage *msg)
{
        e_soap_message_end_element (msg);
}

/**
 * e_soap_message_start_fault_detail:
 * @msg: the #ESoapMessage.
 *
 * Start the faultdetail child element of the current SOAP Fault
 * element. The faultdetail element allows arbitrary data to be sent
 * in a returned fault.
 *
 * Since: 2.92
 **/
void
e_soap_message_start_fault_detail (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

        g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

        priv->last_node = xmlNewChild (priv->last_node,
				       priv->soap_ns,
				       (const xmlChar *)"detail",
				       NULL);
}

/**
 * e_soap_message_end_fault_detail:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP faultdetail element.
 *
 * Since: 2.92
 */
void
e_soap_message_end_fault_detail (ESoapMessage *msg)
{
	e_soap_message_end_element (msg);
}

/**
 * e_soap_message_start_header:
 * @msg: the #ESoapMessage.
 *
 * Creates a new SOAP Header element. You can call
 * e_soap_message_start_header_element() after this to add a new
 * header child element. SOAP Header elements allow out-of-band data
 * to be transferred while not interfering with the message body.
 *
 * This should be called after e_soap_message_start_envelope() and
 * before e_soap_message_start_body().
 *
 * Since: 2.92
 */
void
e_soap_message_start_header (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->last_node = xmlNewChild (priv->last_node, priv->soap_ns,
				       (const xmlChar *)"Header", NULL);
}

/**
 * e_soap_message_end_header:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP Header element.
 *
 * Since: 2.92
 */
void
e_soap_message_end_header (ESoapMessage *msg)
{
	e_soap_message_end_element (msg);
}

/**
 * e_soap_message_start_header_element:
 * @msg: the #ESoapMessage.
 * @name: name of the header element
 * @must_understand: whether the recipient must understand the header in order
 * to proceed with processing the message
 * @actor_uri: the URI which represents the destination actor for this header.
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI
 *
 * Starts a new SOAP arbitrary header element.
 *
 * Since: 2.92
 */
void
e_soap_message_start_header_element (ESoapMessage *msg,
					const gchar *name,
					gboolean must_understand,
					const gchar *actor_uri,
					const gchar *prefix,
					const gchar *ns_uri)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	e_soap_message_start_element (msg, name, prefix, ns_uri);
	if (actor_uri)
		xmlNewNsProp (priv->last_node, priv->soap_ns, (const xmlChar *)"actorUri", (const xmlChar *)actor_uri);
	if (must_understand)
		xmlNewNsProp (priv->last_node, priv->soap_ns, (const xmlChar *)"mustUnderstand", (const xmlChar *)"1");
}

/**
 * e_soap_message_end_header_element:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP header element.
 *
 * Since: 2.92
 */
void
e_soap_message_end_header_element (ESoapMessage *msg)
{
	e_soap_message_end_element (msg);
}

/**
 * e_soap_message_write_int:
 * @msg: the #ESoapMessage.
 * @i: the integer value to write.
 *
 * Writes the stringified value of @i as the current element's content.
 *
 * Since: 2.92
 */
void
e_soap_message_write_int (ESoapMessage *msg, glong i)
{
	gchar *str = g_strdup_printf ("%ld", i);
	e_soap_message_write_string (msg, str);
	g_free (str);
}

/**
 * e_soap_message_write_double:
 * @msg: the #ESoapMessage.
 * @d: the double value to write.
 *
 * Writes the stringified value of @d as the current element's content.
 *
 * Since: 2.92
 */
void
e_soap_message_write_double (ESoapMessage *msg, gdouble d)
{
	gchar *str = g_strdup_printf ("%f", d);
	e_soap_message_write_string (msg, str);
	g_free (str);
}

/**
 * e_soap_message_write_base64:
 * @msg: the #ESoapMessage
 * @string: the binary data buffer to encode
 * @len: the length of data to encode
 *
 * Writes the Base-64 encoded value of @string as the current
 * element's content.
 *
 * Since: 2.92
 **/
void
e_soap_message_write_base64 (ESoapMessage *msg, const gchar *string, gint len)
{
        gchar *str = g_base64_encode ((const guchar *)string, len);
        e_soap_message_write_string (msg, str);
        g_free (str);
}

/**
 * e_soap_message_write_time:
 * @msg: the #ESoapMessage.
 * @timeval: pointer to a time_t to encode
 *
 * Writes the stringified value of @timeval as the current element's
 * content.
 *
 * Since: 2.92
 **/
void
e_soap_message_write_time (ESoapMessage *msg, const time_t *timeval)
{
        gchar *str = g_strchomp (ctime (timeval));
        e_soap_message_write_string (msg, str);
}

/**
 * e_soap_message_write_string:
 * @msg: the #ESoapMessage.
 * @string: string to write.
 *
 * Writes the @string as the current element's content.
 *
 * Since: 2.92
 */
void
e_soap_message_write_string (ESoapMessage *msg, const gchar *string)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlNodeAddContent (priv->last_node, (const xmlChar *)string);
}

/**
 * e_soap_message_write_buffer:
 * @msg: the #ESoapMessage.
 * @buffer: the string data buffer to write.
 * @len: length of @buffer.
 *
 * Writes the string buffer pointed to by @buffer as the current
 * element's content.
 *
 * Since: 2.92
 */
void
e_soap_message_write_buffer (ESoapMessage *msg, const gchar *buffer, gint len)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlNodeAddContentLen (priv->last_node, (const xmlChar *)buffer, len);
}

/**
 * e_soap_message_set_element_type:
 * @msg: the #ESoapMessage.
 * @xsi_type: the type name for the element.
 *
 * Sets the current element's XML schema xsi:type attribute, which
 * specifies the element's type name.
 *
 * Since: 2.92
 */
void
e_soap_message_set_element_type (ESoapMessage *msg, const gchar *xsi_type)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlNewNsProp (priv->last_node, priv->xsi_ns, (const xmlChar *)"type", (const xmlChar *)xsi_type);
}

/**
 * e_soap_message_set_null:
 * @msg: the #ESoapMessage.
 *
 * Sets the current element's XML Schema xsi:null attribute.
 *
 * Since: 2.92
 */
void
e_soap_message_set_null (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlNewNsProp (priv->last_node, priv->xsi_ns, (const xmlChar *)"null", (const xmlChar *)"1");
}

/**
 * e_soap_message_add_attribute:
 * @msg: the #ESoapMessage.
 * @name: name of the attribute
 * @value: value of the attribute
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI
 *
 * Adds an XML attribute to the current element.
 *
 * Since: 2.92
 */
void
e_soap_message_add_attribute (ESoapMessage *msg,
				 const gchar *name,
				 const gchar *value,
				 const gchar *prefix,
				 const gchar *ns_uri)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlNewNsProp (priv->last_node,
		      fetch_ns (msg, prefix, ns_uri),
		      (const xmlChar *)name, (const xmlChar *)value);
}

/**
 * e_soap_message_add_namespace:
 * @msg: the #ESoapMessage.
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI, or NULL for empty namespace
 *
 * Adds a new XML namespace to the current element.
 *
 * Since: 2.92
 */
void
e_soap_message_add_namespace (ESoapMessage *msg, const gchar *prefix, const gchar *ns_uri)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlNewNs (priv->last_node, (const xmlChar *)(ns_uri ? ns_uri : ""), (const xmlChar *)prefix);
}

/**
 * e_soap_message_set_default_namespace:
 * @msg: the #ESoapMessage.
 * @ns_uri: the namespace URI.
 *
 * Sets the default namespace to the URI specified in @ns_uri. The
 * default namespace becomes the namespace all non-explicitly
 * namespaced child elements fall into.
 *
 * Since: 2.92
 */
void
e_soap_message_set_default_namespace (ESoapMessage *msg, const gchar *ns_uri)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	e_soap_message_add_namespace (msg, NULL, ns_uri);
}

/**
 * e_soap_message_set_encoding_style:
 * @msg: the #ESoapMessage.
 * @enc_style: the new encodingStyle value
 *
 * Sets the encodingStyle attribute on the current element to the
 * value of @enc_style.
 *
 * Since: 2.92
 */
void
e_soap_message_set_encoding_style (ESoapMessage *msg, const gchar *enc_style)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlNewNsProp (priv->last_node, priv->soap_ns, (const xmlChar *)"encodingStyle", (const xmlChar *)enc_style);
}

/**
 * e_soap_message_reset:
 * @msg: the #ESoapMessage.
 *
 * Resets the internal XML representation of the SOAP message.
 *
 * Since: 2.92
 */
void
e_soap_message_reset (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlFreeDoc (priv->doc);
	priv->doc = xmlNewDoc ((const xmlChar *)"1.0");
	priv->last_node = NULL;

	g_free (priv->action);
	priv->action = NULL;
	priv->body_started = FALSE;

	if (priv->env_uri)
		xmlFree (priv->env_uri);
	priv->env_uri = NULL;

	if (priv->env_prefix)
		xmlFree (priv->env_prefix);
	priv->env_prefix = NULL;
}

/**
 * e_soap_message_persist:
 * @msg: the #ESoapMessage.
 *
 * Writes the serialized XML tree to the #SoupMessage's buffer.
 *
 * Since: 2.92
 */
void
e_soap_message_persist (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;
	xmlChar *body;
	gint len;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	xmlDocDumpMemory (priv->doc, &body, &len);

	/* serialize to SoupMessage class */
	soup_message_set_request (SOUP_MESSAGE (msg), "text/xml",
				  SOUP_MEMORY_TAKE, (gchar *)body, len);
}

/**
 * e_soap_message_get_namespace_prefix:
 * @msg: the #ESoapMessage.
 * @ns_uri: the namespace URI.
 *
 * Returns the namespace prefix for @ns_uri (or an empty string if
 * @ns_uri is set to the default namespace)
 *
 * Returns: The namespace prefix, or %NULL if no namespace exists
 * for the URI given.
 *
 * Since: 2.92
 */
const gchar *
e_soap_message_get_namespace_prefix (ESoapMessage *msg, const gchar *ns_uri)
{
	ESoapMessagePrivate *priv;
	xmlNsPtr ns = NULL;

	g_return_val_if_fail (E_IS_SOAP_MESSAGE (msg), NULL);
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);
	g_return_val_if_fail (ns_uri != NULL, NULL);

	ns = xmlSearchNsByHref (priv->doc, priv->last_node, (const xmlChar *)ns_uri);
	if (ns) {
		if (ns->prefix)
			return (const gchar *)ns->prefix;
		else
			return "";
	}

	return NULL;
}

/**
 * e_soap_message_get_xml_doc:
 * @msg: the #ESoapMessage.
 *
 * Returns the internal XML representation tree of the
 * #ESoapMessage pointed to by @msg.
 *
 * Returns: the #xmlDocPtr representing the SOAP message.
 *
 * Since: 2.92
 */
xmlDocPtr
e_soap_message_get_xml_doc (ESoapMessage *msg)
{
	ESoapMessagePrivate *priv;

	g_return_val_if_fail (E_IS_SOAP_MESSAGE (msg), NULL);
	priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	return priv->doc;
}

/**
 * e_soap_message_parse_response:
 * @msg: the #ESoapMessage.
 *
 * Parses the response returned by the server.
 *
 * Returns: a #ESoapResponse representing the response from
 * the server, or %NULL if there was an error.
 *
 * Since: 2.92
 */
ESoapResponse *
e_soap_message_parse_response (ESoapMessage *msg)
{
	g_return_val_if_fail (E_IS_SOAP_MESSAGE (msg), NULL);

	return e_soap_response_new_from_string (SOUP_MESSAGE (msg)->response_body->data);
}
