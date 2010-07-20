/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <stdlib.h>
#include <string.h>
#include <libxml/tree.h>
#include <libsoup/soup.h>
#include "soup-soap-response.h"

G_DEFINE_TYPE (SoupSoapResponse, soup_soap_response, G_TYPE_OBJECT)

typedef struct {
	/* the XML document */
	xmlDocPtr xmldoc;
	xmlNodePtr xml_root;
	xmlNodePtr xml_body;
	xmlNodePtr xml_method;
	xmlNodePtr soap_fault;
	GList *parameters;
} SoupSoapResponsePrivate;
#define SOUP_SOAP_RESPONSE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SOUP_TYPE_SOAP_RESPONSE, SoupSoapResponsePrivate))

static xmlNode *soup_xml_real_node (xmlNode *node);

static void
finalize (GObject *object)
{
	SoupSoapResponsePrivate *priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (object);

	if (priv->xmldoc)
		xmlFreeDoc (priv->xmldoc);
	if (priv->parameters != NULL)
		g_list_free (priv->parameters);

	G_OBJECT_CLASS (soup_soap_response_parent_class)->finalize (object);
}

static void
soup_soap_response_class_init (SoupSoapResponseClass *soup_soap_response_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (soup_soap_response_class);

	g_type_class_add_private (soup_soap_response_class, sizeof (SoupSoapResponsePrivate));

	object_class->finalize = finalize;
}

static void
soup_soap_response_init (SoupSoapResponse *response)
{
	SoupSoapResponsePrivate *priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);

	priv->xmldoc = xmlNewDoc ((const xmlChar *)"1.0");
}

/**
 * soup_soap_response_new:
 *
 * Create a new empty #SoupSoapResponse object, which can be modified
 * with the accessor functions provided with this class.
 *
 * Returns: the new #SoupSoapResponse (or %NULL if there was an
 * error).
 */
SoupSoapResponse *
soup_soap_response_new (void)
{
	SoupSoapResponse *response;

	response = g_object_new (SOUP_TYPE_SOAP_RESPONSE, NULL);
	return response;
}

/**
 * soup_soap_response_new_from_string:
 * @xmlstr: the XML string to parse.
 *
 * Create a new #SoupSoapResponse object from the XML string contained
 * in @xmlstr.
 *
 * Returns: the new #SoupSoapResponse (or %NULL if there was an
 * error).
 */
SoupSoapResponse *
soup_soap_response_new_from_string (const gchar *xmlstr)
{
	SoupSoapResponse *response;

	g_return_val_if_fail (xmlstr != NULL, NULL);

	response = g_object_new (SOUP_TYPE_SOAP_RESPONSE, NULL);
	if (!soup_soap_response_from_string (response, xmlstr)) {
		g_object_unref (response);
		return NULL;
	}

	return response;
}

static void
parse_parameters (SoupSoapResponsePrivate *priv, xmlNodePtr xml_method)
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
 * soup_soap_response_from_string:
 * @response: the #SoupSoapResponse object.
 * @xmlstr: XML string to parse.
 *
 * Parses the string contained in @xmlstr and sets all properties from
 * it in the @response object.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
gboolean
soup_soap_response_from_string (SoupSoapResponse *response, const gchar *xmlstr)
{
	SoupSoapResponsePrivate *priv;
	xmlDocPtr old_doc = NULL;
	xmlNodePtr xml_root, xml_body, xml_method = NULL;

	g_return_val_if_fail (SOUP_IS_SOAP_RESPONSE (response), FALSE);
	priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (xmlstr != NULL, FALSE);

	/* clear the previous contents */
	if (priv->xmldoc)
		old_doc = priv->xmldoc;

	/* parse the string */
	priv->xmldoc = xmlParseMemory (xmlstr, strlen (xmlstr));
	if (!priv->xmldoc) {
		priv->xmldoc = old_doc;
		return FALSE;
	}

	xml_root = xmlDocGetRootElement (priv->xmldoc);
	if (!xml_root) {
		xmlFreeDoc (priv->xmldoc);
		priv->xmldoc = old_doc;
		return FALSE;
	}

	if (strcmp ((const gchar *)xml_root->name, "Envelope") != 0) {
		xmlFreeDoc (priv->xmldoc);
		priv->xmldoc = old_doc;
		return FALSE;
	}

	xml_body = soup_xml_real_node (xml_root->children);
	if (xml_body != NULL) {
		if (strcmp ((const gchar *)xml_body->name, "Header") == 0)
			xml_body = soup_xml_real_node (xml_body->next);
		if (strcmp ((const gchar *)xml_body->name, "Body") != 0) {
			xmlFreeDoc (priv->xmldoc);
			priv->xmldoc = old_doc;
			return FALSE;
		}

		xml_method = soup_xml_real_node (xml_body->children);

		/* read all parameters */
		if (xml_method)
			parse_parameters (priv, xml_method);
	}

	xmlFreeDoc (old_doc);

	priv->xml_root = xml_root;
	priv->xml_body = xml_body;
	priv->xml_method = xml_method;

	return TRUE;
}

/**
 * soup_soap_response_get_method_name:
 * @response: the #SoupSoapResponse object.
 *
 * Gets the method name from the SOAP response.
 *
 * Returns: the method name.
 */
const gchar *
soup_soap_response_get_method_name (SoupSoapResponse *response)
{
	SoupSoapResponsePrivate *priv;

	g_return_val_if_fail (SOUP_IS_SOAP_RESPONSE (response), NULL);
	priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (priv->xml_method != NULL, NULL);

	return (const gchar *) priv->xml_method->name;
}

/**
 * soup_soap_response_set_method_name:
 * @response: the #SoupSoapResponse object.
 * @method_name: the method name to set.
 *
 * Sets the method name on the given #SoupSoapResponse.
 */
void
soup_soap_response_set_method_name (SoupSoapResponse *response, const gchar *method_name)
{
	SoupSoapResponsePrivate *priv;

	g_return_if_fail (SOUP_IS_SOAP_RESPONSE (response));
	priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_if_fail (priv->xml_method != NULL);
	g_return_if_fail (method_name != NULL);

	xmlNodeSetName (priv->xml_method, (const xmlChar *)method_name);
}

/**
 * soup_soap_parameter_get_name:
 * @param: the parameter
 *
 * Returns the parameter name.
 *
 * Returns: the parameter name.
 */
const gchar *
soup_soap_parameter_get_name (SoupSoapParameter *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	return (const gchar *) param->name;
}

/**
 * soup_soap_parameter_get_int_value:
 * @param: the parameter
 *
 * Returns the parameter's (integer) value.
 *
 * Returns: the parameter value as an integer
 */
gint
soup_soap_parameter_get_int_value (SoupSoapParameter *param)
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
 * soup_soap_parameter_get_string_value:
 * @param: the parameter
 *
 * Returns the parameter's value.
 *
 * Returns: the parameter value as a string, which must be freed
 * by the caller.
 */
gchar *
soup_soap_parameter_get_string_value (SoupSoapParameter *param)
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
 * soup_soap_parameter_get_first_child:
 * @param: A #SoupSoapParameter.
 *
 * Gets the first child of the given #SoupSoapParameter. This is used
 * for compound data types, which can contain several parameters
 * themselves.
 *
 * Returns: the first child or %NULL if there are no children.
 */
SoupSoapParameter *
soup_soap_parameter_get_first_child (SoupSoapParameter *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	return soup_xml_real_node (param->children);
}

/**
 * soup_soap_parameter_get_first_child_by_name:
 * @param: A #SoupSoapParameter.
 * @name: The name of the child parameter to look for.
 *
 * Gets the first child of the given #SoupSoapParameter whose name is
 * @name.
 *
 * Returns: the first child with the given name or %NULL if there
 * are no children.
 */
SoupSoapParameter *
soup_soap_parameter_get_first_child_by_name (SoupSoapParameter *param, const gchar *name)
{
	SoupSoapParameter *tmp;

	g_return_val_if_fail (param != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (tmp = soup_soap_parameter_get_first_child (param);
	     tmp != NULL;
	     tmp = soup_soap_parameter_get_next_child (tmp)) {
		if (!strcmp (name, (const gchar *)tmp->name))
			return tmp;
	}

	return NULL;
}

/**
 * soup_soap_parameter_get_next_child:
 * @param: A #SoupSoapParameter.
 *
 * Gets the next sibling of the given #SoupSoapParameter. This is used
 * for compound data types, which can contain several parameters
 * themselves.
 *
 * FIXME: the name of this method is wrong
 *
 * Returns: the next sibling, or %NULL if there are no more
 * siblings.
 */
SoupSoapParameter *
soup_soap_parameter_get_next_child (SoupSoapParameter *param)
{
	g_return_val_if_fail (param != NULL, NULL);

	return soup_xml_real_node (param->next);
}

/**
 * soup_soap_parameter_get_next_child_by_name:
 * @param: A #SoupSoapParameter.
 * @name: The name of the sibling parameter to look for.
 *
 * Gets the next sibling of the given #SoupSoapParameter whose name is
 * @name.
 *
 * FIXME: the name of this method is wrong
 *
 * Returns: the next sibling with the given name, or %NULL
 */
SoupSoapParameter *
soup_soap_parameter_get_next_child_by_name (SoupSoapParameter *param,
					    const gchar *name)
{
	SoupSoapParameter *tmp;

	g_return_val_if_fail (param != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (tmp = soup_soap_parameter_get_next_child (param);
	     tmp != NULL;
	     tmp = soup_soap_parameter_get_next_child (tmp)) {
		if (!strcmp (name, (const gchar *)tmp->name))
			return tmp;
	}

	return NULL;
}

/**
 * soup_soap_parameter_get_property:
 * @param: the parameter
 * @prop_name: Name of the property to retrieve.
 *
 * Returns the named property of @param.
 *
 * Returns: the property, which must be freed by the caller.
 */
gchar *
soup_soap_parameter_get_property (SoupSoapParameter *param, const gchar *prop_name)
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
 * soup_soap_response_get_parameters:
 * @response: the #SoupSoapResponse object.
 *
 * Returns the list of parameters received in the SOAP response.
 *
 * Returns: a list of #SoupSoapParameter
 */
const GList *
soup_soap_response_get_parameters (SoupSoapResponse *response)
{
	SoupSoapResponsePrivate *priv;

	g_return_val_if_fail (SOUP_IS_SOAP_RESPONSE (response), NULL);
	priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);

	return (const GList *) priv->parameters;
}

/**
 * soup_soap_response_get_first_parameter:
 * @response: the #SoupSoapResponse object.
 *
 * Retrieves the first parameter contained in the SOAP response.
 *
 * Returns: a #SoupSoapParameter representing the first
 * parameter, or %NULL if there are no parameters.
 */
SoupSoapParameter *
soup_soap_response_get_first_parameter (SoupSoapResponse *response)
{
	SoupSoapResponsePrivate *priv;

	g_return_val_if_fail (SOUP_IS_SOAP_RESPONSE (response), NULL);
	priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);

	return priv->parameters ? priv->parameters->data : NULL;
}

/**
 * soup_soap_response_get_first_parameter_by_name:
 * @response: the #SoupSoapResponse object.
 * @name: the name of the parameter to look for.
 *
 * Retrieves the first parameter contained in the SOAP response whose
 * name is @name.
 *
 * Returns: a #SoupSoapParameter representing the first parameter
 * with the given name, or %NULL.
 */
SoupSoapParameter *
soup_soap_response_get_first_parameter_by_name (SoupSoapResponse *response,
						const gchar *name)
{
	SoupSoapResponsePrivate *priv;
	GList *l;

	g_return_val_if_fail (SOUP_IS_SOAP_RESPONSE (response), NULL);
	priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (name != NULL, NULL);

	for (l = priv->parameters; l != NULL; l = l->next) {
		SoupSoapParameter *param = (SoupSoapParameter *) l->data;

		if (!strcmp (name, (const gchar *)param->name))
			return param;
	}

	return NULL;
}

/**
 * soup_soap_response_get_next_parameter:
 * @response: the #SoupSoapResponse object.
 * @from: the parameter to start from.
 *
 * Retrieves the parameter following @from in the #SoupSoapResponse
 * object.
 *
 * Returns: a #SoupSoapParameter representing the parameter.
 */
SoupSoapParameter *
soup_soap_response_get_next_parameter (SoupSoapResponse *response,
				       SoupSoapParameter *from)
{
	SoupSoapResponsePrivate *priv;
	GList *l;

	g_return_val_if_fail (SOUP_IS_SOAP_RESPONSE (response), NULL);
	priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);
	g_return_val_if_fail (from != NULL, NULL);

	l = g_list_find (priv->parameters, (gconstpointer) from);
	if (!l)
		return NULL;

	return l->next ? (SoupSoapParameter *) l->next->data : NULL;
}

/**
 * soup_soap_response_get_next_parameter_by_name:
 * @response: the #SoupSoapResponse object.
 * @from: the parameter to start from.
 * @name: the name of the parameter to look for.
 *
 * Retrieves the first parameter following @from in the
 * #SoupSoapResponse object whose name matches @name.
 *
 * Returns: a #SoupSoapParameter representing the parameter.
 */
SoupSoapParameter *
soup_soap_response_get_next_parameter_by_name (SoupSoapResponse *response,
					       SoupSoapParameter *from,
					       const gchar *name)
{
	SoupSoapParameter *param;

	g_return_val_if_fail (SOUP_IS_SOAP_RESPONSE (response), NULL);
	g_return_val_if_fail (from != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	param = soup_soap_response_get_next_parameter (response, from);
	while (param) {
		const gchar *param_name = soup_soap_parameter_get_name (param);

		if (param_name) {
			if (!strcmp (name, param_name))
				return param;
		}

		param = soup_soap_response_get_next_parameter (response, param);
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

gint
soup_soap_response_dump_response (SoupSoapResponse *response, FILE *buffer)
{
	xmlChar *xmlbuff;
	gint buffersize, ret;

	SoupSoapResponsePrivate *priv = SOUP_SOAP_RESPONSE_GET_PRIVATE (response);
	xmlDocDumpFormatMemory(priv->xmldoc, &xmlbuff, &buffersize, 1);

	ret = fputs ((gchar *) xmlbuff, buffer);
	xmlFree (xmlbuff);

	return ret;
}
