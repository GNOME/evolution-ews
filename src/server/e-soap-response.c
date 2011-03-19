/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <libedataserver/eds-version.h>
#if ! EDS_CHECK_VERSION(2,33,0)

#include <stdlib.h>
#include <string.h>
#include <libxml/tree.h>
#include <libsoup/soup.h>
#include "e-soap-response.h"

G_DEFINE_TYPE (ESoapResponse, e_soap_response, G_TYPE_OBJECT)

typedef struct {
	/* the XML document */
	xmlDocPtr xmldoc;
	xmlNodePtr xml_root;
	xmlNodePtr xml_body;
	xmlNodePtr xml_method;
	xmlNodePtr soap_fault;
	GList *parameters;
} ESoapResponsePrivate;
#define E_SOAP_RESPONSE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_SOAP_RESPONSE, ESoapResponsePrivate))

static xmlNode *soup_xml_real_node (xmlNode *node);

static void
finalize (GObject *object)
{
	ESoapResponsePrivate *priv = E_SOAP_RESPONSE_GET_PRIVATE (object);

	if (priv->xmldoc)
		xmlFreeDoc (priv->xmldoc);
	if (priv->parameters != NULL)
		g_list_free (priv->parameters);

	G_OBJECT_CLASS (e_soap_response_parent_class)->finalize (object);
}

static void
e_soap_response_class_init (ESoapResponseClass *e_soap_response_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (e_soap_response_class);

	g_type_class_add_private (e_soap_response_class, sizeof (ESoapResponsePrivate));

	object_class->finalize = finalize;
}

static void
e_soap_response_init (ESoapResponse *response)
{
	ESoapResponsePrivate *priv = E_SOAP_RESPONSE_GET_PRIVATE (response);

	priv->xmldoc = xmlNewDoc ((const xmlChar *)"1.0");
}

/**
 * e_soap_response_new:
 *
 * Create a new empty #ESoapResponse object, which can be modified
 * with the accessor functions provided with this class.
 *
 * Returns: the new #ESoapResponse (or %NULL if there was an
 * error).
 *
 * Since: 2.92
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
 *
 * Create a new #ESoapResponse object from the XML string contained
 * in @xmlstr.
 *
 * Returns: the new #ESoapResponse (or %NULL if there was an
 * error).
 *
 * Since: 2.92
 */
ESoapResponse *
e_soap_response_new_from_string (const gchar *xmlstr)
{
	ESoapResponse *response;

	g_return_val_if_fail (xmlstr != NULL, NULL);

	response = g_object_new (E_TYPE_SOAP_RESPONSE, NULL);
	if (!e_soap_response_from_string (response, xmlstr)) {
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
 *
 * Since: xxx
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
parse_parameters (ESoapResponsePrivate *priv, xmlNodePtr xml_method)
{
	xmlNodePtr tmp;

	for (tmp = soup_xml_real_node (xml_method->children);
	     tmp != NULL;
	     tmp = soup_xml_real_node (tmp->next)) {
		if (!strcmp ((const gchar *)tmp->name, "Fault")) {
			priv->soap_fault = tmp;
			continue;
		} else {
			/* regular parameters */
			priv->parameters = g_list_append (priv->parameters, tmp);
		}
	}
}

/**
 * e_soap_response_from_string:
 * @response: the #ESoapResponse object.
 * @xmlstr: XML string to parse.
 *
 * Parses the string contained in @xmlstr and sets all properties from
 * it in the @response object.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 2.92
 */
gboolean
e_soap_response_from_string (ESoapResponse *response, const gchar *xmlstr)
{
	ESoapResponsePrivate *priv;
	xmlDocPtr xmldoc;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), FALSE);
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (xmlstr != NULL, FALSE);

	/* parse the string */
	xmldoc = xmlParseMemory (xmlstr, strlen (xmlstr));
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
 *
 * Since: xxx
 */
gboolean e_soap_response_from_xmldoc (ESoapResponse *response, xmlDoc *xmldoc)
{
	ESoapResponsePrivate *priv;
	xmlNodePtr xml_root, xml_body, xml_method = NULL;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), FALSE);
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (xmldoc != NULL, FALSE);

	xml_root = xmlDocGetRootElement (xmldoc);
	if (!xml_root) {
		xmlFreeDoc (xmldoc);
		return FALSE;
	}

	if (strcmp ((const gchar *)xml_root->name, "Envelope") != 0) {
		xmlFreeDoc (xmldoc);
		return FALSE;
	}

	xml_body = soup_xml_real_node (xml_root->children);
	if (xml_body != NULL) {
		if (strcmp ((const gchar *)xml_body->name, "Header") == 0)
			xml_body = soup_xml_real_node (xml_body->next);
		if (strcmp ((const gchar *)xml_body->name, "Body") != 0) {
			xmlFreeDoc (xmldoc);
			return FALSE;
		}

		xml_method = soup_xml_real_node (xml_body->children);

		/* read all parameters */
		if (xml_method)
			parse_parameters (priv, xml_method);
	}

	xmlFreeDoc (priv->xmldoc);
	priv->xmldoc = xmldoc;

	priv->xml_root = xml_root;
	priv->xml_body = xml_body;
	priv->xml_method = xml_method;

	return TRUE;
}

/**
 * e_soap_response_get_method_name:
 * @response: the #ESoapResponse object.
 *
 * Gets the method name from the SOAP response.
 *
 * Returns: the method name.
 *
 * Since: 2.92
 */
const gchar *
e_soap_response_get_method_name (ESoapResponse *response)
{
	ESoapResponsePrivate *priv;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (priv->xml_method != NULL, NULL);

	return (const gchar *) priv->xml_method->name;
}

/**
 * e_soap_response_set_method_name:
 * @response: the #ESoapResponse object.
 * @method_name: the method name to set.
 *
 * Sets the method name on the given #ESoapResponse.
 *
 * Since: 2.92
 */
void
e_soap_response_set_method_name (ESoapResponse *response, const gchar *method_name)
{
	ESoapResponsePrivate *priv;

	g_return_if_fail (E_IS_SOAP_RESPONSE (response));
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_if_fail (priv->xml_method != NULL);
	g_return_if_fail (method_name != NULL);

	xmlNodeSetName (priv->xml_method, (const xmlChar *)method_name);
}

/**
 * e_soap_parameter_get_name:
 * @param: the parameter
 *
 * Returns the parameter name.
 *
 * Returns: the parameter name.
 *
 * Since: 2.92
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
 *
 * Since: 2.92
 */
gint
e_soap_parameter_get_int_value (ESoapParameter *param)
{
	gint i;
	xmlChar *s;
	g_return_val_if_fail (param != NULL, -1);

	s = xmlNodeGetContent (param);
	if (s) {
		i = atoi ((gchar *)s);
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
 *
 * Since: 2.92
 */
gchar *
e_soap_parameter_get_string_value (ESoapParameter *param)
{
	xmlChar *xml_s;
	gchar *s;
	g_return_val_if_fail (param != NULL, NULL);

	xml_s = xmlNodeGetContent (param);
	s = g_strdup ((gchar *)xml_s);
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
 *
 * Since: 2.92
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
 *
 * Since: 2.92
 */
ESoapParameter *
e_soap_parameter_get_first_child_by_name (ESoapParameter *param, const gchar *name)
{
	ESoapParameter *tmp;

	g_return_val_if_fail (param != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (tmp = e_soap_parameter_get_first_child (param);
	     tmp != NULL;
	     tmp = e_soap_parameter_get_next_child (tmp)) {
		if (!strcmp (name, (const gchar *)tmp->name))
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
 *
 * Since: 2.92
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
 *
 * Since: 2.92
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
		if (!strcmp (name, (const gchar *)tmp->name))
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
 *
 * Since: 2.92
 */
gchar *
e_soap_parameter_get_property (ESoapParameter *param, const gchar *prop_name)
{
	xmlChar *xml_s;
	gchar *s;

	g_return_val_if_fail (param != NULL, NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	xml_s = xmlGetProp (param, (const xmlChar *)prop_name);
	s = g_strdup ((gchar *)xml_s);
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
 *
 * Since: 2.92
 */
const GList *
e_soap_response_get_parameters (ESoapResponse *response)
{
	ESoapResponsePrivate *priv;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);

	return (const GList *) priv->parameters;
}

/**
 * e_soap_response_get_first_parameter:
 * @response: the #ESoapResponse object.
 *
 * Retrieves the first parameter contained in the SOAP response.
 *
 * Returns: a #ESoapParameter representing the first
 * parameter, or %NULL if there are no parameters.
 *
 * Since: 2.92
 */
ESoapParameter *
e_soap_response_get_first_parameter (ESoapResponse *response)
{
	ESoapResponsePrivate *priv;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);

	return priv->parameters ? priv->parameters->data : NULL;
}

/**
 * e_soap_response_get_first_parameter_by_name:
 * @response: the #ESoapResponse object.
 * @name: the name of the parameter to look for.
 *
 * Retrieves the first parameter contained in the SOAP response whose
 * name is @name.
 *
 * Returns: a #ESoapParameter representing the first parameter
 * with the given name, or %NULL.
 *
 * Since: 2.92
 */
ESoapParameter *
e_soap_response_get_first_parameter_by_name (ESoapResponse *response,
						const gchar *name)
{
	ESoapResponsePrivate *priv;
	GList *l;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (name != NULL, NULL);

	for (l = priv->parameters; l != NULL; l = l->next) {
		ESoapParameter *param = (ESoapParameter *) l->data;

		if (!strcmp (name, (const gchar *)param->name))
			return param;
	}

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
 *
 * Since: 2.92
 */
ESoapParameter *
e_soap_response_get_next_parameter (ESoapResponse *response,
				       ESoapParameter *from)
{
	ESoapResponsePrivate *priv;
	GList *l;

	g_return_val_if_fail (E_IS_SOAP_RESPONSE (response), NULL);
	priv = E_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (from != NULL, NULL);

	l = g_list_find (priv->parameters, (gconstpointer) from);
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
 *
 * Since: 2.92
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

static xmlNode *
soup_xml_real_node (xmlNode *node)
{
	while (node && (node->type == XML_COMMENT_NODE ||
			xmlIsBlankNode (node)))
		node = node->next;
	return node;
}

/**
 * e_soap_response_dump_response:
 *
 * Since: 2.92
 **/
gint
e_soap_response_dump_response (ESoapResponse *response, FILE *buffer)
{
	xmlChar *xmlbuff;
	gint buffersize, ret;

	ESoapResponsePrivate *priv = E_SOAP_RESPONSE_GET_PRIVATE (response);
	xmlDocDumpFormatMemory (priv->xmldoc, &xmlbuff, &buffersize, 1);

	ret = fputs ((gchar *) xmlbuff, buffer);
	xmlFree (xmlbuff);

	return ret;
}
#endif
