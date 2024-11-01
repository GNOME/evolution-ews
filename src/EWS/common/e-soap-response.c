/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <libedataserver/eds-version.h>
#include <libedataserver/libedataserver.h>

#include <stdlib.h>
#include <string.h>
#include <libxml/tree.h>
#include <libsoup/soup.h>
#include "e-soap-response.h"

struct _ESoapResponsePrivate {
	/* the XML document */
	xmlDocPtr xmldoc;
	xmlNodePtr xml_root;
	xmlNodePtr xml_body;
	xmlNodePtr xml_method;
	xmlNodePtr soap_fault;
	GList *parameters;

	/* Serialization fields */
	xmlParserCtxtPtr ctxt;

	/* Content stealing */
	gchar *steal_node;
	gchar *steal_dir;
	gboolean steal_base64;

	gint steal_b64_state;
	guint steal_b64_save;
	gint steal_fd;

	/* Progress callbacks */
	ESoapResponseProgressFn progress_fn;
	gpointer progress_data;
};

G_DEFINE_TYPE_WITH_PRIVATE (ESoapResponse, e_soap_response, G_TYPE_OBJECT)

static xmlNode *
soup_xml_real_node (xmlNode *node)
{
	while (node && (node->type == XML_COMMENT_NODE ||
		xmlIsBlankNode (node)))
		node = node->next;
	return node;
}

static void
soap_response_finalize (GObject *object)
{
	ESoapResponse *resp = E_SOAP_RESPONSE (object);

	g_clear_pointer (&resp->priv->xmldoc, xmlFreeDoc);
	g_list_free (resp->priv->parameters);

	if (resp->priv->ctxt) {
		if (resp->priv->ctxt->myDoc)
			xmlFreeDoc (resp->priv->ctxt->myDoc);
		xmlFreeParserCtxt (resp->priv->ctxt);
	}

	g_free (resp->priv->steal_node);
	g_free (resp->priv->steal_dir);

	if (resp->priv->steal_fd != -1)
		close (resp->priv->steal_fd);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_soap_response_parent_class)->finalize (object);
}

static void
e_soap_response_class_init (ESoapResponseClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = soap_response_finalize;
}

static void
e_soap_response_init (ESoapResponse *response)
{
	response->priv = e_soap_response_get_instance_private (response);

	response->priv->xmldoc = xmlNewDoc ((const xmlChar *)"1.0");
	response->priv->steal_fd = -1;
}

/**
 * e_soap_response_new:
 *
 * Create a new empty #ESoapResponse object, which can be modified
 * with the accessor functions provided with this class.
 *
 * Returns: the new #ESoapResponse (or %NULL if there was an
 * error).
 */
ESoapResponse *
e_soap_response_new (void)
{
	ESoapResponse *response;

	response = g_object_new (E_TYPE_SOAP_RESPONSE, NULL);
	return response;
}

/**
 * e_soap_response_new_from_string:
 * @xmlstr: the XML string to parse.
 * @xmlstr_len: XML string's length or -1 for a null-terminated string
 *
 * Create a new #ESoapResponse object from the XML string contained
 * in @xmlstr.
 *
 * Returns: the new #ESoapResponse (or %NULL if there was an
 * error).
 */
ESoapResponse *
e_soap_response_new_from_string (const gchar *xmlstr,
				 gint xmlstr_length)
{
	ESoapResponse *response;

	g_return_val_if_fail (xmlstr != NULL, NULL);

	response = g_object_new (E_TYPE_SOAP_RESPONSE, NULL);
	if (!e_soap_response_from_string (response, xmlstr, xmlstr_length)) {
		g_object_unref (response);
		return NULL;
	}

	return response;
}

/**
 * e_soap_response_new_from_xmldoc:
 * @xmldoc: the XML document to parse.
 *
 * Create a new #ESoapResponse object from the XML document contained
 * in @xmldoc.
 *
 * Returns: the new #ESoapResponse (or %NULL if there was an
 * error).
 */
ESoapResponse *
e_soap_response_new_from_xmldoc (xmlDoc *xmldoc)
{
	ESoapResponse *response;

	g_return_val_if_fail (xmldoc != NULL, NULL);

	response = g_object_new (E_TYPE_SOAP_RESPONSE, NULL);
	if (!e_soap_response_from_xmldoc (response, xmldoc)) {
		g_object_unref (response);
		return NULL;
	}

	return response;
}

static void
parse_parameters (ESoapResponse *response,
                  xmlNodePtr xml_method)
{
	xmlNodePtr tmp;

	for (tmp = soup_xml_real_node (xml_method->children);
	     tmp != NULL;
	     tmp = soup_xml_real_node (tmp->next)) {
		if (!strcmp ((const gchar *) tmp->name, "Fault")) {
			response->priv->soap_fault = tmp;
			continue;
		} else {
			/* regular parameters */
			response->priv->parameters = g_list_append (
				response->priv->parameters, tmp);
		}
	}
}

/**
 * e_soap_response_from_string:
 * @response: the #ESoapResponse object.
 * @xmlstr: XML string to parse.
 * @xmlstr_len: XML string's length or -1 for a null-terminated string
 *
 * Parses the string contained in @xmlstr and sets all properties from
 * it in the @response object.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
e_soap_response_from_string (ESoapResponse *response,
                             const gchar *xmlstr,
			     gint xmlstr_length)
{
	xmlDocPtr xmldoc;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), FALSE);
	g_return_val_if_fail (xmlstr != NULL, FALSE);

	/* parse the string */
	xmldoc = xmlParseMemory (xmlstr, xmlstr_length == -1 ? strlen (xmlstr) : xmlstr_length);
	if (!xmldoc)
		return FALSE;

	return e_soap_response_from_xmldoc (response, xmldoc);
}

/**
 * e_soap_response_from_xmldoc:
 * @response: the #ESoapResponse object.
 * @xmldoc: XML document.
 *
 * Sets all properties from the @xmldoc in the @response object.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
e_soap_response_from_xmldoc (ESoapResponse *response,
                             xmlDoc *xmldoc)
{
	xmlNodePtr xml_root, xml_body, xml_method = NULL;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), FALSE);
	g_return_val_if_fail (xmldoc != NULL, FALSE);

	xml_root = xmlDocGetRootElement (xmldoc);
	if (!xml_root) {
		xmlFreeDoc (xmldoc);
		return FALSE;
	}

	if (strcmp ((const gchar *) xml_root->name, "Envelope") != 0) {
		xmlFreeDoc (xmldoc);
		return FALSE;
	}

	xml_body = soup_xml_real_node (xml_root->children);
	if (xml_body != NULL) {
		if (strcmp ((const gchar *) xml_body->name, "Header") == 0) {
			/* read header parameters */
			parse_parameters (response, xml_body);
			xml_body = soup_xml_real_node (xml_body->next);
		}
		if (strcmp ((const gchar *) xml_body->name, "Body") != 0) {
			xmlFreeDoc (xmldoc);
			return FALSE;
		}

		xml_method = soup_xml_real_node (xml_body->children);

		/* read all parameters */
		if (xml_method)
			parse_parameters (response, xml_method);
	}

	xmlFreeDoc (response->priv->xmldoc);
	response->priv->xmldoc = xmldoc;

	response->priv->xml_root = xml_root;
	response->priv->xml_body = xml_body;
	response->priv->xml_method = xml_method;

	return TRUE;
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
	ESoapResponse *response = ctxt->_private;
	gchar *fname;

	xmlSAX2StartElementNs (
		ctxt, localname, prefix, uri, nb_namespaces,
		namespaces, nb_attributes, nb_defaulted,
		attributes);

	/* steal_node can contain multiple node name separated by " " */
	if (response->priv->steal_node && *response->priv->steal_node) {
		gchar **prop = g_strsplit (response->priv->steal_node, " ", 0);
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

	fname = g_build_filename (response->priv->steal_dir, "XXXXXX", NULL);
	response->priv->steal_fd = g_mkstemp (fname);
	if (response->priv->steal_fd != -1) {
		if (response->priv->steal_base64) {
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
	ESoapResponse *response = ctxt->_private;

	if (response->priv->steal_fd != -1) {
		close (response->priv->steal_fd);
		response->priv->steal_fd = -1;
	}
	xmlSAX2EndElementNs (ctxt, localname, prefix, uri);
}

static void
soap_sax_characters (gpointer _ctxt,
                     const xmlChar *ch,
                     gint len)
{
	xmlParserCtxt *ctxt = _ctxt;
	ESoapResponse *response = ctxt->_private;

	if (response->priv->steal_fd == -1)
		xmlSAX2Characters (ctxt, ch, len);
	else if (!response->priv->steal_base64) {
		if (write (response->priv->steal_fd, (const gchar *) ch, len) != len) {
		write_err:
			/* Handle error better */
			g_warning ("Failed to write streaming data to file");
		}
	} else {
		guchar *bdata = g_malloc ((len * 3 / 4) + 3);
		gsize blen;

		blen = g_base64_decode_step (
			(const gchar *) ch, len,
			bdata, &response->priv->steal_b64_state,
			&response->priv->steal_b64_save);
		if (write (response->priv->steal_fd, (const gchar *) bdata, blen) != blen) {
			g_free (bdata);
			goto write_err;
		}
		g_free (bdata);
	}
}

#define BUFFER_SIZE 16384

xmlDoc *
e_soap_response_xmldoc_from_message_sync (ESoapResponse *response,
					  SoupMessage *msg,
					  GInputStream *response_data,
					  GCancellable *cancellable,
					  GError **error)
{
	const gchar *size;
	xmlDoc *xmldoc = NULL;
	gboolean success;
	gpointer buffer;
	gsize response_size = 0;
	gsize response_received = 0;
	gsize progress_percent = 0;
	gsize nread = 0;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), FALSE);
	g_return_val_if_fail (G_IS_INPUT_STREAM (response_data), FALSE);

	/* Discard the existing context, if there is one, and start again */
	if (response->priv->ctxt) {
		if (response->priv->ctxt->myDoc)
			xmlFreeDoc (response->priv->ctxt->myDoc);
		xmlFreeParserCtxt (response->priv->ctxt);
		response->priv->ctxt = NULL;
	}

	if (response->priv->steal_fd != -1) {
		close (response->priv->steal_fd);
		response->priv->steal_fd = -1;
	}

	if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (msg))) {
		g_set_error_literal (error, E_SOUP_SESSION_ERROR, soup_message_get_status (msg), soup_message_get_reason_phrase (msg));
		return NULL;
	}

	size = soup_message_headers_get_one (soup_message_get_response_headers (msg), "Content-Length");

	if (size)
		response_size = g_ascii_strtoll (size, NULL, 10);

	buffer = g_malloc (BUFFER_SIZE);

	while (success = g_input_stream_read_all (response_data, buffer, BUFFER_SIZE, &nread, cancellable, error),
	       success && nread > 0) {
		response_received += nread;

		if (response_size && response->priv->progress_fn) {
			gint pc = response_received * 100 / response_size;
			if (progress_percent != pc) {
				progress_percent = pc;
				response->priv->progress_fn (response->priv->progress_data, progress_percent);
			}
		}

		if (!response->priv->ctxt) {
			response->priv->ctxt = xmlCreatePushParserCtxt (NULL, response, buffer, nread, NULL);
			response->priv->ctxt->_private = response;
			response->priv->ctxt->sax->startElementNs = soap_sax_startElementNs;
			response->priv->ctxt->sax->endElementNs = soap_sax_endElementNs;
			response->priv->ctxt->sax->characters = soap_sax_characters;
			xmlCtxtUseOptions (response->priv->ctxt, XML_PARSE_RECOVER | XML_PARSE_HUGE | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET);
		} else {
			xmlParseChunk (response->priv->ctxt, buffer, nread, 0);
		}
	}

	g_free (buffer);

	if (success) {
		if (response->priv->ctxt) {
			xmlParseChunk (response->priv->ctxt, 0, 0, 1);

			xmldoc = response->priv->ctxt->myDoc;

			xmlFreeParserCtxt (response->priv->ctxt);
			response->priv->ctxt = NULL;
		} else {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "No data read");
			success = FALSE;
		}
	}

	if (response->priv->ctxt) {
		if (response->priv->ctxt->myDoc)
			xmlFreeDoc (response->priv->ctxt->myDoc);
		xmlFreeParserCtxt (response->priv->ctxt);
		response->priv->ctxt = NULL;
	}

	if (response->priv->steal_fd != -1) {
		close (response->priv->steal_fd);
		response->priv->steal_fd = -1;
	}

	return xmldoc;
}

gboolean
e_soap_response_from_message_sync (ESoapResponse *response,
				   SoupMessage *msg,
				   GInputStream *response_data,
				   GCancellable *cancellable,
				   GError **error)
{
	xmlDoc *xmldoc;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), FALSE);
	g_return_val_if_fail (G_IS_INPUT_STREAM (response_data), FALSE);

	xmldoc = e_soap_response_xmldoc_from_message_sync (response, msg, response_data, cancellable, error);

	if (xmldoc) {
		success = e_soap_response_from_xmldoc (response, xmldoc);
		if (!success)
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Received invalid SOAP response");
	}

	return success;
}

/**
 * e_soap_response_set_store_node_data:
 * @response: the %ESoapResponse
 * @nodename: the name of the XML node from which to store data
 * @directory: cache directory in which to create data files
 * @base64: flag to request base64 decoding of node content
 *
 * This requests that character data for certain XML nodes should
 * be streamed directly to a disk file as it arrives, rather than
 * being stored in memory.
 *
 * It is used only with e_soap_response_from_message_sync().
 */
void
e_soap_response_set_store_node_data (ESoapResponse *response,
				     const gchar *nodename,
				     const gchar *directory,
				     gboolean base64)
{
	g_return_if_fail (E_IS_SOAP_RESPONSE (response));
	g_return_if_fail (response->priv->steal_node == NULL);

	response->priv->steal_node = g_strdup (nodename);
	response->priv->steal_dir = g_strdup (directory);
	response->priv->steal_base64 = base64;
}

/**
 * e_soap_response_set_progress_fn:
 * @response: the %ESoapResponse
 * @fn: callback function to be given progress updates
 * @object: first argument to callback function
 *
 * Starts the top level SOAP Envelope element.
 *
 * It is used only with e_soap_response_from_message_sync().
 */
void
e_soap_response_set_progress_fn (ESoapResponse *response,
				 ESoapResponseProgressFn fn,
				 gpointer object)
{
	g_return_if_fail (E_IS_SOAP_RESPONSE (response));

	response->priv->progress_fn = fn;
	response->priv->progress_data = object;
}

/**
 * e_soap_response_get_method_name:
 * @response: the #ESoapResponse object.
 *
 * Gets the method name from the SOAP response.
 *
 * Returns: the method name.
 */
const gchar *
e_soap_response_get_method_name (ESoapResponse *response)
{
	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	g_return_val_if_fail (response->priv->xml_method != NULL, NULL);

	return (const gchar *) response->priv->xml_method->name;
}

/**
 * e_soap_response_set_method_name:
 * @response: the #ESoapResponse object.
 * @method_name: the method name to set.
 *
 * Sets the method name on the given #ESoapResponse.
 */
void
e_soap_response_set_method_name (ESoapResponse *response,
                                 const gchar *method_name)
{
	g_return_if_fail (E_IS_SOAP_RESPONSE (response));
	g_return_if_fail (response->priv->xml_method != NULL);
	g_return_if_fail (method_name != NULL);

	xmlNodeSetName (
		response->priv->xml_method,
		(const xmlChar *) method_name);
}

/**
 * e_soap_parameter_get_name:
 * @param: the parameter
 *
 * Returns the parameter name.
 *
 * Returns: the parameter name.
 */
const gchar *
e_soap_parameter_get_name (ESoapParameter *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	return (const gchar *) param->name;
}

/**
 * e_soap_parameter_get_int_value:
 * @param: the parameter
 *
 * Returns the parameter's (integer) value.
 *
 * Returns: the parameter value as an integer
 */
gint
e_soap_parameter_get_int_value (ESoapParameter *param)
{
	gint i;
	xmlChar *s;
	g_return_val_if_fail (param != NULL, -1);

	s = xmlNodeGetContent (param);
	if (s) {
		i = atoi ((gchar *) s);
		xmlFree (s);

		return i;
	}

	return -1;
}

/**
 * e_soap_parameter_get_uint64_value:
 * @param: the parameter
 *
 * Returns: the parameter value as a 64-bit unsigned integer
 */
guint64
e_soap_parameter_get_uint64_value (ESoapParameter *param)
{
	xmlChar *str;

	g_return_val_if_fail (param != NULL, G_MAXUINT64);

	str = xmlNodeGetContent (param);
	if (str) {
		guint64 val;

		val = g_ascii_strtoull ((gchar *) str, NULL, 10);
		xmlFree (str);

		return val;
	}

	return G_MAXUINT64;
}

/**
 * e_soap_parameter_get_string_value:
 * @param: the parameter
 *
 * Returns the parameter's value.
 *
 * Returns: the parameter value as a string, which must be freed
 * by the caller.
 */
gchar *
e_soap_parameter_get_string_value (ESoapParameter *param)
{
	xmlChar *xml_s;
	gchar *s;
	g_return_val_if_fail (param != NULL, NULL);

	xml_s = xmlNodeGetContent (param);
	s = g_strdup ((gchar *) xml_s);
	xmlFree (xml_s);

	return s;
}

/**
 * e_soap_parameter_get_first_child:
 * @param: (nullable): An #ESoapParameter.
 *
 * Gets the first child of the given #ESoapParameter. This is used
 * for compound data types, which can contain several parameters
 * themselves.
 *
 * Returns: the first child or %NULL if there are no children.
 */
ESoapParameter *
e_soap_parameter_get_first_child (ESoapParameter *param)
{
	if (!param)
		return NULL;

	return soup_xml_real_node (param->children);
}

/**
 * e_soap_parameter_get_first_child_by_name:
 * @param: (nullable): An #ESoapParameter.
 * @name: The name of the child parameter to look for.
 *
 * Gets the first child of the given #ESoapParameter whose name is
 * @name.
 *
 * Returns: the first child with the given name or %NULL if there
 * are no children.
 */
ESoapParameter *
e_soap_parameter_get_first_child_by_name (ESoapParameter *param,
                                          const gchar *name)
{
	ESoapParameter *tmp;

	g_return_val_if_fail (name != NULL, NULL);

	for (tmp = e_soap_parameter_get_first_child (param);
	     tmp != NULL;
	     tmp = e_soap_parameter_get_next_child (tmp)) {
		if (!strcmp (name, (const gchar *) tmp->name))
			return tmp;
	}

	return NULL;
}

/**
 * e_soap_parameter_get_next_child:
 * @param: (nullable): An #ESoapParameter, or %NULL.
 *
 * Gets the next sibling of the given #ESoapParameter. This is used
 * for compound data types, which can contain several parameters
 * themselves.
 *
 * FIXME: the name of this method is wrong
 *
 * Returns: the next sibling, or %NULL if there are no more
 * siblings.
 */
ESoapParameter *
e_soap_parameter_get_next_child (ESoapParameter *param)
{
	if (!param)
		return NULL;

	return soup_xml_real_node (param->next);
}

/**
 * e_soap_parameter_get_next_child_by_name:
 * @param: (nullable): An #ESoapParameter.
 * @name: The name of the sibling parameter to look for.
 *
 * Gets the next sibling of the given #ESoapParameter whose name is
 * @name.
 *
 * FIXME: the name of this method is wrong
 *
 * Returns: the next sibling with the given name, or %NULL
 */
ESoapParameter *
e_soap_parameter_get_next_child_by_name (ESoapParameter *param,
                                         const gchar *name)
{
	ESoapParameter *tmp;

	g_return_val_if_fail (name != NULL, NULL);

	for (tmp = e_soap_parameter_get_next_child (param);
	     tmp != NULL;
	     tmp = e_soap_parameter_get_next_child (tmp)) {
		if (!strcmp (name, (const gchar *) tmp->name))
			return tmp;
	}

	return NULL;
}

/**
 * e_soap_parameter_get_property:
 * @param: the parameter
 * @prop_name: Name of the property to retrieve.
 *
 * Returns the named property of @param.
 *
 * Returns: the property, which must be freed by the caller.
 */
gchar *
e_soap_parameter_get_property (ESoapParameter *param,
                               const gchar *prop_name)
{
	xmlChar *xml_s;
	gchar *s;

	g_return_val_if_fail (param != NULL, NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	xml_s = xmlGetProp (param, (const xmlChar *) prop_name);
	s = g_strdup ((gchar *) xml_s);
	xmlFree (xml_s);

	return s;
}

/**
 * e_soap_response_get_parameters:
 * @response: the #ESoapResponse object.
 *
 * Returns the list of parameters received in the SOAP response.
 *
 * Returns: a list of #ESoapParameter
 */
const GList *
e_soap_response_get_parameters (ESoapResponse *response)
{
	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);

	return (const GList *) response->priv->parameters;
}

ESoapParameter *
e_soap_response_get_parameter (ESoapResponse *response)
{
	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);

	return response->priv->xml_method;
}

/**
 * e_soap_response_get_first_parameter:
 * @response: the #ESoapResponse object.
 *
 * Retrieves the first parameter contained in the SOAP response.
 *
 * Returns: an #ESoapParameter representing the first
 * parameter, or %NULL if there are no parameters.
 */
ESoapParameter *
e_soap_response_get_first_parameter (ESoapResponse *response)
{
	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);

	if (response->priv->parameters == NULL)
		return NULL;

	return response->priv->parameters->data;
}

/**
 * e_soap_response_get_first_parameter_by_name:
 * @response: the #ESoapResponse object.
 * @name: the name of the parameter to look for.
 * @error: return location for a #GError, or %NULL
 *
 * Retrieves the first parameter contained in the SOAP response whose
 * name is @name.  If no parameter is found, the function sets @error
 * and returns %NULL.
 *
 * The function also checks for a SOAP "faultstring" parameter and,
 * if found, uses it to set the #GError message.
 *
 * Returns: an #ESoapParameter representing the first parameter
 * with the given name, or %NULL.
 */
ESoapParameter *
e_soap_response_get_first_parameter_by_name (ESoapResponse *response,
                                             const gchar *name,
                                             GError **error)
{
	GList *l;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (l = response->priv->parameters; l != NULL; l = l->next) {
		ESoapParameter *param = (ESoapParameter *) l->data;

		if (strcmp (name, (const gchar *) param->name) == 0)
			return param;
	}

	/* XXX These are probably not the best error codes, but
	 *     wanted to avoid EWS_CONNECTION_ERROR codes since
	 *     this class is potentially reusable. */

	for (l = response->priv->parameters; l != NULL; l = l->next) {
		ESoapParameter *param = (ESoapParameter *) l->data;

		if (strcmp ("faultstring", (const gchar *) param->name) == 0) {
			gchar *string;

			string = e_soap_parameter_get_string_value (param);

			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				(string != NULL) ? string : "<faultstring> in SOAP response");

			g_free (string);

			return NULL;
		}
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		"Missing <%s> in SOAP response", name);

	return NULL;
}

/**
 * e_soap_response_get_next_parameter:
 * @response: the #ESoapResponse object.
 * @from: the parameter to start from.
 *
 * Retrieves the parameter following @from in the #ESoapResponse
 * object.
 *
 * Returns: an #ESoapParameter representing the parameter.
 */
ESoapParameter *
e_soap_response_get_next_parameter (ESoapResponse *response,
                                    ESoapParameter *from)
{
	GList *l;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	g_return_val_if_fail (from != NULL, NULL);

	l = g_list_find (response->priv->parameters, (gconstpointer) from);
	if (!l)
		return NULL;

	return l->next ? (ESoapParameter *) l->next->data : NULL;
}

/**
 * e_soap_response_get_next_parameter_by_name:
 * @response: the #ESoapResponse object.
 * @from: the parameter to start from.
 * @name: the name of the parameter to look for.
 *
 * Retrieves the first parameter following @from in the
 * #ESoapResponse object whose name matches @name.
 *
 * Returns: an #ESoapParameter representing the parameter.
 */
ESoapParameter *
e_soap_response_get_next_parameter_by_name (ESoapResponse *response,
                                            ESoapParameter *from,
                                            const gchar *name)
{
	ESoapParameter *param;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	g_return_val_if_fail (from != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	param = e_soap_response_get_next_parameter (response, from);
	while (param) {
		const gchar *param_name = e_soap_parameter_get_name (param);

		if (param_name) {
			if (!strcmp (name, param_name))
				return param;
		}

		param = e_soap_response_get_next_parameter (response, param);
	}

	return NULL;
}

/**
 * e_soap_response_dump_response:
 **/
gint
e_soap_response_dump_response (ESoapResponse *response,
                               FILE *buffer)
{
	xmlChar *xmlbuff;
	gint buffersize, ret;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), -1);

	xmlDocDumpFormatMemory (
		response->priv->xmldoc,
		&xmlbuff, &buffersize, 1);
	ret = fputs ((gchar *) xmlbuff, buffer);
	xmlFree (xmlbuff);

	return ret;
}

gchar *
e_soap_response_dump_parameter (ESoapResponse *response,
				ESoapParameter *param)
{
	xmlBuffer *buffer;
	gint len;
	gchar *data;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	g_return_val_if_fail (param != NULL, NULL);

	buffer = xmlBufferCreate ();
	len = xmlNodeDump (buffer, response->priv->xmldoc, param, 0, 0);

	if (len <= 0) {
		xmlBufferFree (buffer);
		return NULL;
	}

	data = g_strndup ((const gchar *) buffer->content, len);

	xmlBufferFree (buffer);

	return data;
}
