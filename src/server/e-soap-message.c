/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libedataserver/eds-version.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <libsoup/soup.h>
#ifdef G_OS_WIN32
#include <io.h>
#endif

#include "e-soap-message.h"
#include "e-ews-debug.h"

#define E_SOAP_MESSAGE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOAP_MESSAGE, ESoapMessagePrivate))

struct _ESoapMessagePrivate {
	/* Serialization fields */
	xmlParserCtxtPtr ctxt;
	xmlDocPtr doc;
	xmlNodePtr last_node;
	xmlNsPtr soap_ns;
	xmlNsPtr xsi_ns;
	xmlChar *env_prefix;
	xmlChar *env_uri;
	gboolean body_started;
	gchar *action;

	/* Content stealing */
	gchar *steal_node;
	gchar *steal_dir;
	gboolean steal_base64;

	gint steal_b64_state;
	guint steal_b64_save;
	gint steal_fd;

	/* Progress callbacks */
	gsize response_size;
	gsize response_received;
	ESoapProgressFn progress_fn;
	gpointer progress_data;
};

G_DEFINE_TYPE (ESoapMessage, e_soap_message, SOUP_TYPE_MESSAGE)

static void
soap_message_finalize (GObject *object)
{
	ESoapMessagePrivate *priv;

	priv = E_SOAP_MESSAGE_GET_PRIVATE (object);

	if (priv->ctxt != NULL) {
		if (priv->ctxt->myDoc != NULL)
			xmlFreeDoc (priv->ctxt->myDoc);
		xmlFreeParserCtxt (priv->ctxt);
	}

	if (priv->doc != NULL)
		xmlFreeDoc (priv->doc);

	if (priv->action != NULL)
		g_free (priv->action);

	if (priv->env_uri != NULL)
		xmlFree (priv->env_uri);

	if (priv->env_prefix != NULL)
		xmlFree (priv->env_prefix);

	g_free (priv->steal_node);
	g_free (priv->steal_dir);

	if (priv->steal_fd != -1)
		close (priv->steal_fd);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_soap_message_parent_class)->finalize (object);
}

static void
e_soap_message_class_init (ESoapMessageClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ESoapMessagePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = soap_message_finalize;
}

static void
e_soap_message_init (ESoapMessage *msg)
{
	msg->priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	/* initialize XML structures */
	msg->priv->doc = xmlNewDoc ((const xmlChar *) "1.0");
	msg->priv->doc->standalone = FALSE;
	msg->priv->doc->encoding = xmlCharStrdup ("UTF-8");
	msg->priv->steal_fd = -1;

#ifdef HAVE_SOUP_MESSAGE_IDEMPOTENT_FLAG
	soup_message_set_flags (SOUP_MESSAGE (msg), SOUP_MESSAGE_IDEMPOTENT);
#endif
}

static xmlNsPtr
fetch_ns (ESoapMessage *msg,
          const gchar *prefix,
          const gchar *ns_uri)
{
	ESoapMessagePrivate *priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);
	xmlNsPtr ns = NULL;

	if (prefix && ns_uri)
		ns = xmlNewNs (
			priv->last_node,
			(const xmlChar *) ns_uri,
			(const xmlChar *) prefix);
	else if (prefix && !ns_uri) {
		ns = xmlSearchNs (
			priv->doc, priv->last_node,
			(const xmlChar *) prefix);
		if (!ns)
			ns = xmlNewNs (
				priv->last_node,
				(const xmlChar *) "",
				(const xmlChar *) prefix);
	}

	return ns;
}

static void
soap_got_headers (SoupMessage *msg,
                  gpointer data)
{
	ESoapMessagePrivate *priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);
	const gchar *size;

	size = soup_message_headers_get_one (
		msg->response_headers,
		"Content-Length");

	if (size)
		priv->response_size = strtol (size, NULL, 10);
}

static void
soap_restarted (SoupMessage *msg,
                gpointer data)
{
	ESoapMessagePrivate *priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->response_size = 0;
	priv->response_received = 0;

	/* Discard the existing context, if there is one, and start again */
	if (priv->ctxt) {
		if (priv->ctxt->myDoc)
			xmlFreeDoc (priv->ctxt->myDoc);
		xmlFreeParserCtxt (priv->ctxt);
		priv->ctxt = NULL;
	}
}

static void
soap_sax_startElementNs (gpointer _ctxt,
                         const xmlChar *localname,
                         const xmlChar *prefix,
                         const xmlChar *uri,
                         gint nb_namespaces,
                         const xmlChar **namespaces,
                         gint nb_attributes,
                         gint nb_defaulted,
                         const xmlChar **attributes)
{
	xmlParserCtxt *ctxt = _ctxt;
	ESoapMessagePrivate *priv = ctxt->_private;
	gchar *fname;

	xmlSAX2StartElementNs (
		ctxt, localname, prefix, uri, nb_namespaces,
		namespaces, nb_attributes, nb_defaulted,
		attributes);

	/* steal_node can contain multiple node name separated by " " */
	if (priv->steal_node && *priv->steal_node) {
		gchar **prop = g_strsplit (priv->steal_node, " ", 0);
		gint i = 0;
		gboolean isnode = FALSE;

		while (prop[i]) {
			if (strcmp ((const gchar *) localname, prop[i]) == 0) {
				isnode = TRUE;
				break;
			}
			i++;
		}
		g_strfreev (prop);

		if (!isnode) return;
	} else
		return;

	fname = g_build_filename (priv->steal_dir, "XXXXXX", NULL);
	priv->steal_fd = g_mkstemp (fname);
	if (priv->steal_fd != -1) {
		if (priv->steal_base64) {
			gchar *enc = g_base64_encode ((guchar *) fname, strlen (fname));
			xmlSAX2Characters (ctxt, (xmlChar *) enc, strlen (enc));
			g_free (enc);
		} else
			xmlSAX2Characters (ctxt, (xmlChar *) fname, strlen (fname));
	} else {
		gint err = errno;

		g_warning ("%s: Failed to create temp file '%s': %s\n", G_STRFUNC, fname, g_strerror (err));
	}
	g_free (fname);
}

static void
soap_sax_endElementNs (gpointer _ctxt,
                       const xmlChar *localname,
                       const xmlChar *prefix,
                       const xmlChar *uri)
{
	xmlParserCtxt *ctxt = _ctxt;
	ESoapMessagePrivate *priv = ctxt->_private;

	if (priv->steal_fd != -1) {
		close (priv->steal_fd);
		priv->steal_fd = -1;
	}
	xmlSAX2EndElementNs (ctxt, localname, prefix, uri);
}

static void
soap_sax_characters (gpointer _ctxt,
                     const xmlChar *ch,
                     gint len)
{
	xmlParserCtxt *ctxt = _ctxt;
	ESoapMessagePrivate *priv = ctxt->_private;

	if (priv->steal_fd == -1)
		xmlSAX2Characters (ctxt, ch, len);
	else if (!priv->steal_base64) {
		if (write (priv->steal_fd, (const gchar *) ch, len) != len) {
		write_err:
			/* Handle error better */
			g_warning ("Failed to write streaming data to file");
		}
	} else {
		guchar *bdata = g_malloc (len);
		gsize blen;

		blen = g_base64_decode_step (
			(const gchar *) ch, len,
			bdata, &priv->steal_b64_state,
			&priv->steal_b64_save);
		if (write (priv->steal_fd, (const gchar *) bdata, blen) != blen) {
			g_free (bdata);
			goto write_err;
		}
		g_free (bdata);
	}
}

static void
soap_got_chunk (SoupMessage *msg,
                SoupBuffer *chunk,
                gpointer data)
{
	ESoapMessagePrivate *priv = E_SOAP_MESSAGE_GET_PRIVATE (msg);

	priv->response_received += chunk->length;

	if (priv->response_size && priv->progress_fn) {
		gint pc = priv->response_received * 100 / priv->response_size;
		priv->progress_fn (priv->progress_data, pc);
	}

	if (!priv->ctxt) {
		priv->ctxt = xmlCreatePushParserCtxt (
			NULL, msg, chunk->data,
			chunk->length, NULL);
		priv->ctxt->_private = priv;
		priv->ctxt->sax->startElementNs = soap_sax_startElementNs;
		priv->ctxt->sax->endElementNs = soap_sax_endElementNs;
		priv->ctxt->sax->characters = soap_sax_characters;
	}
	else
		xmlParseChunk (priv->ctxt, chunk->data, chunk->length, 0);
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
 */
ESoapMessage *
e_soap_message_new (const gchar *method,
                    const gchar *uri_string,
                    gboolean standalone,
                    const gchar *xml_encoding,
                    const gchar *env_prefix,
                    const gchar *env_uri,
		    gboolean standard_handlers)
{
	ESoapMessage *msg;
	SoupURI *uri;

	uri = soup_uri_new (uri_string);
	if (!uri)
		return NULL;

	msg = e_soap_message_new_from_uri (
		method, uri, standalone,
		xml_encoding, env_prefix, env_uri);

	soup_uri_free (uri);

	/*
	 * Don't use streaming-based messages when we are loggin the traffic
	 * to generate trace files for tests
	 */
	if (e_ews_debug_get_log_level () <= 2) {
		/* Don't accumulate body data into a huge buffer.
		 * Instead, parse it as it arrives. */
		soup_message_body_set_accumulate (
				SOUP_MESSAGE (msg)->response_body,
				FALSE);
	}

	if (standard_handlers) {
		g_signal_connect (msg, "got-headers", G_CALLBACK (soap_got_headers), NULL);
		g_signal_connect (msg, "got-chunk", G_CALLBACK (soap_got_chunk), NULL);
		g_signal_connect (msg, "restarted", G_CALLBACK (soap_restarted), NULL);
	}

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
 */
ESoapMessage *
e_soap_message_new_from_uri (const gchar *method,
                             SoupURI *uri,
                             gboolean standalone,
                             const gchar *xml_encoding,
                             const gchar *env_prefix,
                             const gchar *env_uri)
{
	ESoapMessage *msg;

	msg = g_object_new (
		E_TYPE_SOAP_MESSAGE,
		SOUP_MESSAGE_METHOD, method,
		SOUP_MESSAGE_URI, uri, NULL);

	msg->priv->doc->standalone = standalone;

	if (xml_encoding) {
		xmlFree ((xmlChar *) msg->priv->doc->encoding);
		msg->priv->doc->encoding = xmlCharStrdup (xml_encoding);
	}

	if (env_prefix != NULL)
		msg->priv->env_prefix = xmlCharStrdup (env_prefix);
	if (env_uri != NULL)
		msg->priv->env_uri = xmlCharStrdup (env_uri);

	return msg;
}

/**
 * e_soap_message_store_node_data:
 * @msg: the %ESoapMessage.
 * @nodename: the name of the XML node from which to store data
 * @directory: cache directory in which to create data files
 * @base64: flag to request base64 decoding of node content
 *
 * This requests that character data for certain XML nodes should
 * be streamed directly to a disk file as it arrives, rather than
 * being stored in memory in the soup response buffer.
 */

void
e_soap_message_store_node_data (ESoapMessage *msg,
                                const gchar *nodename,
                                const gchar *directory,
                                gboolean base64)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->steal_node = g_strdup (nodename);
	msg->priv->steal_dir = g_strdup (directory);
	msg->priv->steal_base64 = base64;
}

/**
 * e_soap_message_set_progress_fn:
 * @msg: the %ESoapMessage.
 * @fn: callback function to be given progress updates
 * @object: first argument to callback function
 *
 * Starts the top level SOAP Envelope element.
 */
void
e_soap_message_set_progress_fn (ESoapMessage *msg,
                                ESoapProgressFn fn,
                                gpointer object)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->progress_fn = fn;
	msg->priv->progress_data = object;
}

/**
 * e_soap_message_start_envelope:
 * @msg: the %ESoapMessage.
 *
 * Starts the top level SOAP Envelope element.
 */
void
e_soap_message_start_envelope (ESoapMessage *msg)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->doc->xmlRootNode = xmlNewDocNode (
		msg->priv->doc, NULL,
		(const xmlChar *) "Envelope",
		(const xmlChar *) NULL);
	msg->priv->last_node = msg->priv->doc->xmlRootNode;

	msg->priv->soap_ns = xmlNewNs (
		msg->priv->doc->xmlRootNode,
		msg->priv->env_uri ? msg->priv->env_uri :
		(const xmlChar *) "http://schemas.xmlsoap.org/soap/envelope/",
		msg->priv->env_prefix ? msg->priv->env_prefix :
		(const xmlChar *) "SOAP-ENV");

	if (msg->priv->env_uri != NULL) {
		xmlFree (msg->priv->env_uri);
		msg->priv->env_uri = NULL;
	}

	if (msg->priv->env_prefix) {
		xmlFree (msg->priv->env_prefix);
		msg->priv->env_prefix = NULL;
	}

	xmlSetNs (
		msg->priv->doc->xmlRootNode,
		msg->priv->soap_ns);
	xmlNewNs (
		msg->priv->doc->xmlRootNode,
		(const xmlChar *) "http://schemas.xmlsoap.org/soap/encoding/",
		(const xmlChar *) "SOAP-ENC");
	xmlNewNs (
		msg->priv->doc->xmlRootNode,
		(const xmlChar *) "http://www.w3.org/2001/XMLSchema",
		(const xmlChar *) "xsd");
	xmlNewNs (
		msg->priv->doc->xmlRootNode,
		(const xmlChar *) "http://schemas.xmlsoap.org/soap/envelope/",
		(const xmlChar *) "SOAP-ENV");
	msg->priv->xsi_ns = xmlNewNs (
		msg->priv->doc->xmlRootNode,
		(const xmlChar *) "http://www.w3.org/2001/XMLSchema-instance",
		(const xmlChar *) "xsi");
}

/**
 * e_soap_message_end_envelope:
 * @msg: the %ESoapMessage.
 *
 * Closes the top level SOAP Envelope element.
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
 */
void
e_soap_message_start_body (ESoapMessage *msg)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	if (msg->priv->body_started)
		return;

	msg->priv->last_node = xmlNewChild (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "Body", NULL);

	msg->priv->body_started = TRUE;
}

/**
 * e_soap_message_end_body:
 * @msg: the %ESoapMessage.
 *
 * Closes the SOAP Body element.
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
 */
void
e_soap_message_start_element (ESoapMessage *msg,
                              const gchar *name,
                              const gchar *prefix,
                              const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->last_node = xmlNewChild (
		msg->priv->last_node, NULL,
		(const xmlChar *) name, NULL);

	xmlSetNs (msg->priv->last_node, fetch_ns (msg, prefix, ns_uri));

	if (ns_uri == NULL)
		ns_uri = "";

	if (msg->priv->body_started && msg->priv->action == NULL)
		msg->priv->action = g_strconcat (ns_uri, "#", name, NULL);
}

/**
 * e_soap_message_end_element:
 * @msg: the #ESoapMessage.
 *
 * Closes the current message element.
 */
void
e_soap_message_end_element (ESoapMessage *msg)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->last_node = msg->priv->last_node->parent;
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
 */
void
e_soap_message_start_fault (ESoapMessage *msg,
                            const gchar *faultcode,
                            const gchar *faultstring,
                            const gchar *faultfactor)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->last_node = xmlNewChild (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "Fault", NULL);
	xmlNewChild (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "faultcode",
		(const xmlChar *) faultcode);
	xmlNewChild (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "faultstring",
		(const xmlChar *) faultstring);

	msg->priv->last_node = xmlNewChild (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "faultfactor",
		(const xmlChar *) faultfactor);
	if (faultfactor == NULL)
		e_soap_message_set_null (msg);

	e_soap_message_end_element (msg);
}

/**
 * e_soap_message_end_fault:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP Fault element.
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
 **/
void
e_soap_message_start_fault_detail (ESoapMessage *msg)
{

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->last_node = xmlNewChild (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "detail",
		(const xmlChar *) NULL);
}

/**
 * e_soap_message_end_fault_detail:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP faultdetail element.
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
 */
void
e_soap_message_start_header (ESoapMessage *msg)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	msg->priv->last_node = xmlNewChild (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "Header",
		(const xmlChar *) NULL);
}

/**
 * e_soap_message_end_header:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP Header element.
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
 */
void
e_soap_message_start_header_element (ESoapMessage *msg,
                                     const gchar *name,
                                     gboolean must_understand,
                                     const gchar *actor_uri,
                                     const gchar *prefix,
                                     const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	e_soap_message_start_element (msg, name, prefix, ns_uri);
	if (actor_uri != NULL)
		xmlNewNsProp (
			msg->priv->last_node,
			msg->priv->soap_ns,
			(const xmlChar *) "actorUri",
			(const xmlChar *) actor_uri);
	if (must_understand)
		xmlNewNsProp (
			msg->priv->last_node,
			msg->priv->soap_ns,
			(const xmlChar *) "mustUnderstand",
			(const xmlChar *) "1");
}

/**
 * e_soap_message_end_header_element:
 * @msg: the #ESoapMessage.
 *
 * Closes the current SOAP header element.
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
 */
void
e_soap_message_write_int (ESoapMessage *msg,
                          glong i)
{
	gchar *string;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	string = g_strdup_printf ("%ld", i);
	e_soap_message_write_string (msg, string);
	g_free (string);
}

/**
 * e_soap_message_write_double:
 * @msg: the #ESoapMessage.
 * @d: the double value to write.
 *
 * Writes the stringified value of @d as the current element's content.
 */
void
e_soap_message_write_double (ESoapMessage *msg,
                             gdouble d)
{
	gchar *string;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	string = g_strdup_printf ("%f", d);
	e_soap_message_write_string (msg, string);
	g_free (string);
}

/**
 * e_soap_message_write_base64:
 * @msg: the #ESoapMessage
 * @string: the binary data buffer to encode
 * @len: the length of data to encode
 *
 * Writes the Base-64 encoded value of @string as the current
 * element's content.
 **/
void
e_soap_message_write_base64 (ESoapMessage *msg,
                             const gchar *string,
                             gint len)
{
	gchar *encoded;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	encoded = g_base64_encode ((const guchar *) string, len);
	e_soap_message_write_string (msg, encoded);
	g_free (encoded);
}

/**
 * e_soap_message_write_time:
 * @msg: the #ESoapMessage.
 * @timeval: the time_t to encode
 *
 * Writes the iso8601 value of @timeval as the current element's
 * content.
 **/
void
e_soap_message_write_time (ESoapMessage *msg,
                           time_t timeval)
{
	GTimeVal tv;
	gchar *iso_time;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	tv.tv_usec = 0;
	tv.tv_sec = timeval;

	iso_time = g_time_val_to_iso8601 (&tv);
	e_soap_message_write_string (msg, iso_time);
	g_free (iso_time);
}

/**
 * e_soap_message_write_string:
 * @msg: the #ESoapMessage.
 * @string: string to write.
 *
 * Writes the @string as the current element's content.
 */
void
e_soap_message_write_string (ESoapMessage *msg,
                             const gchar *string)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlNodeAddContent (
		msg->priv->last_node,
		(const xmlChar *) string);
}

/**
 * e_soap_message_write_buffer:
 * @msg: the #ESoapMessage.
 * @buffer: the string data buffer to write.
 * @len: length of @buffer.
 *
 * Writes the string buffer pointed to by @buffer as the current
 * element's content.
 */
void
e_soap_message_write_buffer (ESoapMessage *msg,
                             const gchar *buffer,
                             gint len)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlNodeAddContentLen (
		msg->priv->last_node,
		(const xmlChar *) buffer, len);
}

/**
 * e_soap_message_set_element_type:
 * @msg: the #ESoapMessage.
 * @xsi_type: the type name for the element.
 *
 * Sets the current element's XML schema xsi:type attribute, which
 * specifies the element's type name.
 */
void
e_soap_message_set_element_type (ESoapMessage *msg,
                                 const gchar *xsi_type)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlNewNsProp (
		msg->priv->last_node,
		msg->priv->xsi_ns,
		(const xmlChar *) "type",
		(const xmlChar *) xsi_type);
}

/**
 * e_soap_message_set_null:
 * @msg: the #ESoapMessage.
 *
 * Sets the current element's XML Schema xsi:null attribute.
 */
void
e_soap_message_set_null (ESoapMessage *msg)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlNewNsProp (
		msg->priv->last_node,
		msg->priv->xsi_ns,
		(const xmlChar *) "null",
		(const xmlChar *) "1");
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
 */
void
e_soap_message_add_attribute (ESoapMessage *msg,
                              const gchar *name,
                              const gchar *value,
                              const gchar *prefix,
                              const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlNewNsProp (
		msg->priv->last_node,
		fetch_ns (msg, prefix, ns_uri),
		(const xmlChar *) name,
		(const xmlChar *) value);
}

/**
 * e_soap_message_add_namespace:
 * @msg: the #ESoapMessage.
 * @prefix: the namespace prefix
 * @ns_uri: the namespace URI, or NULL for empty namespace
 *
 * Adds a new XML namespace to the current element.
 */
void
e_soap_message_add_namespace (ESoapMessage *msg,
                              const gchar *prefix,
                              const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	if (ns_uri == NULL)
		ns_uri = "";

	xmlNewNs (
		msg->priv->last_node,
		(const xmlChar *) ns_uri,
		(const xmlChar *) prefix);
}

/**
 * e_soap_message_set_default_namespace:
 * @msg: the #ESoapMessage.
 * @ns_uri: the namespace URI.
 *
 * Sets the default namespace to the URI specified in @ns_uri. The
 * default namespace becomes the namespace all non-explicitly
 * namespaced child elements fall into.
 */
void
e_soap_message_set_default_namespace (ESoapMessage *msg,
                                      const gchar *ns_uri)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	e_soap_message_add_namespace (msg, NULL, ns_uri);
}

/**
 * e_soap_message_set_encoding_style:
 * @msg: the #ESoapMessage.
 * @enc_style: the new encodingStyle value
 *
 * Sets the encodingStyle attribute on the current element to the
 * value of @enc_style.
 */
void
e_soap_message_set_encoding_style (ESoapMessage *msg,
                                   const gchar *enc_style)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlNewNsProp (
		msg->priv->last_node,
		msg->priv->soap_ns,
		(const xmlChar *) "encodingStyle",
		(const xmlChar *) enc_style);
}

/**
 * e_soap_message_reset:
 * @msg: the #ESoapMessage.
 *
 * Resets the internal XML representation of the SOAP message.
 */
void
e_soap_message_reset (ESoapMessage *msg)
{
	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlFreeDoc (msg->priv->doc);
	msg->priv->doc = xmlNewDoc ((const xmlChar *) "1.0");
	msg->priv->last_node = NULL;

	g_free (msg->priv->action);
	msg->priv->action = NULL;
	msg->priv->body_started = FALSE;

	if (msg->priv->env_uri != NULL) {
		xmlFree (msg->priv->env_uri);
		msg->priv->env_uri = NULL;
	}

	if (msg->priv->env_prefix != NULL) {
		xmlFree (msg->priv->env_prefix);
		msg->priv->env_prefix = NULL;
	}
}

/**
 * e_soap_message_persist:
 * @msg: the #ESoapMessage.
 *
 * Writes the serialized XML tree to the #SoupMessage's buffer.
 */
void
e_soap_message_persist (ESoapMessage *msg)
{
	xmlChar *body;
	gint len;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	xmlDocDumpMemory (msg->priv->doc, &body, &len);

	/* serialize to SoupMessage class */
	soup_message_set_request (
		SOUP_MESSAGE (msg),
		"text/xml; charset=utf-8",
		SOUP_MEMORY_TAKE, (gchar *) body, len);
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
 */
const gchar *
e_soap_message_get_namespace_prefix (ESoapMessage *msg,
                                     const gchar *ns_uri)
{
	xmlNsPtr ns = NULL;

	g_return_val_if_fail (E_IS_SOAP_MESSAGE (msg), NULL);
	g_return_val_if_fail (ns_uri != NULL, NULL);

	ns = xmlSearchNsByHref (
		msg->priv->doc,
		msg->priv->last_node,
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
 * e_soap_message_get_xml_doc:
 * @msg: the #ESoapMessage.
 *
 * Returns the internal XML representation tree of the
 * #ESoapMessage pointed to by @msg.
 *
 * Returns: the #xmlDocPtr representing the SOAP message.
 */
xmlDocPtr
e_soap_message_get_xml_doc (ESoapMessage *msg)
{
	g_return_val_if_fail (E_IS_SOAP_MESSAGE (msg), NULL);

	return msg->priv->doc;
}

/**
 * e_soap_message_parse_response:
 * @msg: the #ESoapMessage.
 *
 * Parses the response returned by the server.
 *
 * Returns: a #ESoapResponse representing the response from
 * the server, or %NULL if there was an error.
 */
ESoapResponse *
e_soap_message_parse_response (ESoapMessage *msg)
{
	xmlDocPtr xmldoc;

	g_return_val_if_fail (E_IS_SOAP_MESSAGE (msg), NULL);

	if (msg->priv->ctxt == NULL)
		return NULL;

	xmlParseChunk (msg->priv->ctxt, 0, 0, 1);

	xmldoc = msg->priv->ctxt->myDoc;

	xmlFreeParserCtxt (msg->priv->ctxt);
	msg->priv->ctxt = NULL;

	if (xmldoc == NULL)
		return NULL;

	return e_soap_response_new_from_xmldoc (xmldoc);
}

