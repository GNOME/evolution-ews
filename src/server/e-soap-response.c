/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libedataserver/eds-version.h>

#include <stdlib.h>
#include <string.h>
#include <libxml/tree.h>
#include <libsoup/soup.h>
#include "e-soap-response.h"

#define E_SOAP_RESPONSE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOAP_RESPONSE, ESoapResponsePrivate))

struct _ESoapResponsePrivate {
	/* the XML document */
	xmlDocPtr xmldoc;
	xmlNodePtr xml_root;
	xmlNodePtr xml_body;
	xmlNodePtr xml_method;
	xmlNodePtr soap_fault;
	GList *parameters;
};

G_DEFINE_TYPE (ESoapResponse, e_soap_response, G_TYPE_OBJECT)

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
	ESoapResponsePrivate *priv;

	priv = E_SOAP_RESPONSE_GET_PRIVATE (object);

	if (priv->xmldoc != NULL)
		xmlFreeDoc (priv->xmldoc);

	g_list_free (priv->parameters);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_soap_response_parent_class)->finalize (object);
}

static void
e_soap_response_class_init (ESoapResponseClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ESoapResponsePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = soap_response_finalize;
}

static void
e_soap_response_init (ESoapResponse *response)
{
	response->priv = E_SOAP_RESPONSE_GET_PRIVATE (response);

	response->priv->xmldoc = xmlNewDoc ((const xmlChar *)"1.0");
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
 * @param: A #ESoapParameter.
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
	g_return_val_if_fail (param != NULL, NULL);

	return soup_xml_real_node (param->children);
}

/**
 * e_soap_parameter_get_first_child_by_name:
 * @param: A #ESoapParameter.
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

	g_return_val_if_fail (param != NULL, NULL);
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
 * @param: A #ESoapParameter.
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
	g_return_val_if_fail (param != NULL, NULL);

	return soup_xml_real_node (param->next);
}

/**
 * e_soap_parameter_get_next_child_by_name:
 * @param: A #ESoapParameter.
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

	g_return_val_if_fail (param != NULL, NULL);
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
 * Returns: a #ESoapParameter representing the first
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
 * Returns: a #ESoapParameter representing the first parameter
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

			g_set_error (
				error,
				SOUP_HTTP_ERROR, SOUP_STATUS_IO_ERROR,
				"%s", (string != NULL) ? string :
				"<faultstring> in SOAP response");

			g_free (string);

			return NULL;
		}
	}

	g_set_error (
		error,
		SOUP_HTTP_ERROR, SOUP_STATUS_MALFORMED,
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
 * Returns: a #ESoapParameter representing the parameter.
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
 * Returns: a #ESoapParameter representing the parameter.
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
