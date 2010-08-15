/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e2k-xml-utils.h"
#include <stdlib.h>
#include <libxml/HTMLparser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>

static void
my_xml_parser_error_handler (gpointer ctx, const gchar *msg, ...)
{
	;
}

/**
 * e2k_parse_xml:
 * @buf: the data to parse
 * @len: the length of the buffer, or -1 if it is '\0'-terminated
 *
 * Parses the XML document in @buf.
 *
 * Return value: a pointer to an #xmlDoc
 **/
xmlDoc *
e2k_parse_xml (const gchar *buf, gint len)
{
	static xmlSAXHandler *sax;
	xmlParserCtxtPtr ctxt;
	xmlDoc *doc;

	g_return_val_if_fail (buf != NULL, NULL);

	if (!sax) {
		xmlInitParser();
		sax = xmlMalloc (sizeof (xmlSAXHandler));
#if LIBXML_VERSION > 20600
		xmlSAXVersion (sax, 2);
#else
		memcpy (sax, &xmlDefaultSAXHandler, sizeof (xmlSAXHandler));
#endif
		sax->warning = my_xml_parser_error_handler;
		sax->error = my_xml_parser_error_handler;
	}

	if (len == -1)
		len = strlen (buf);
	ctxt = xmlCreateMemoryParserCtxt (buf, len);
	if (!ctxt)
		return NULL;

	xmlFree (ctxt->sax);
        ctxt->sax = sax;
#if LIBXML_VERSION > 20600
	ctxt->sax2 = 1;
	ctxt->str_xml = xmlDictLookup (ctxt->dict, BAD_CAST "xml", 3);
	ctxt->str_xmlns = xmlDictLookup (ctxt->dict, BAD_CAST "xmlns", 5);
	ctxt->str_xml_ns = xmlDictLookup (ctxt->dict, XML_XML_NAMESPACE, 36);
#endif

	/* We set recover to TRUE because Exchange will let you
	 * put control-characters into data, which will make the
	 * XML be not well-formed.
	 */
	ctxt->recovery = TRUE;
	ctxt->vctxt.error = my_xml_parser_error_handler;
	ctxt->vctxt.warning = my_xml_parser_error_handler;

	xmlParseDocument (ctxt);

	doc = ctxt->myDoc;
	ctxt->sax = NULL;
	xmlFreeParserCtxt (ctxt);

	return doc;
}

/**
 * e2k_parse_html:
 * @buf: the data to parse
 * @len: the length of the buffer, or -1 if it is '\0'-terminated
 *
 * Parses the HTML document in @buf.
 *
 * Return value: a pointer to an #xmlDoc
 **/
xmlDoc *
e2k_parse_html (const gchar *buf, gint len)
{
	xmlDoc *doc;
#if LIBXML_VERSION > 20600
	static xmlSAXHandler *sax;
	htmlParserCtxtPtr ctxt;

	g_return_val_if_fail (buf != NULL, NULL);

	if (!sax) {
		xmlInitParser();
		sax = xmlMalloc (sizeof (htmlSAXHandler));
		memcpy (sax, &htmlDefaultSAXHandler, sizeof (xmlSAXHandlerV1));
		sax->warning = my_xml_parser_error_handler;
		sax->error = my_xml_parser_error_handler;
	}

	if (len == -1)
		len = strlen (buf);
	ctxt = htmlCreateMemoryParserCtxt (buf, len);
	if (!ctxt)
		return NULL;

        xmlFree (ctxt->sax);
        ctxt->sax = sax;
	ctxt->vctxt.error = my_xml_parser_error_handler;
	ctxt->vctxt.warning = my_xml_parser_error_handler;

	htmlParseDocument (ctxt);
	doc = ctxt->myDoc;

	ctxt->sax = NULL;
	htmlFreeParserCtxt (ctxt);

#else /* LIBXML_VERSION <= 20600 */
	gchar *buf_copy = g_strndup (buf, len);

	doc = htmlParseDoc (buf_copy, NULL);
	g_free (buf_copy);
#endif

	return doc;
}

/**
 * e2k_g_string_append_xml_escaped:
 * @string: a %GString containing XML data
 * @value: data to append to @string
 *
 * Appends @value to @string, escaping any characters that can't appear
 * unencoded in XML text (eg, "<").
 **/
void
e2k_g_string_append_xml_escaped (GString *string, const gchar *value)
{
	while (*value) {
		switch (*value) {
		case '<':
			g_string_append (string, "&lt;");
			break;
		case '>':
			g_string_append (string, "&gt;");
			break;
		case '&':
			g_string_append (string, "&amp;");
			break;
		case '"':
			g_string_append (string, "&quot;");
			break;

		default:
			g_string_append_c (string, *value);
			break;
		}
		value++;
	}
}

/**
 * e2k_xml_find:
 * @node: a node of an xml document
 * @name: the name of the element to find
 *
 * Starts or continues a pre-order depth-first search of an xml
 * document for an element named @name. @node is used as the starting
 * point of the search, but is not examined itself.
 *
 * To search the complete document, pass the root node of the document
 * as @node on the first call, and then on each successive call,
 * pass the previous match as @node.
 *
 * Return value: the first matching element after @node, or %NULL when
 * there are no more matches.
 **/
xmlNode *
e2k_xml_find (xmlNode *node, const gchar *name)
{
	return e2k_xml_find_in (node, NULL, name);
}

/**
 * e2k_xml_find_in:
 * @node: a node of an xml document
 * @top: top of the search space
 * @name: the name of the element to find
 *
 * Starts or continues a pre-order depth-first search of a subset of
 * an xml document for an element named @name. @node is used as the
 * starting point of the search, but is not examined itself. @top is
 * the upper-most node that will be examined.
 *
 * To search the complete tree under a given node, pass that node as
 * both @node and @top on the first call, and then on each successive
 * call, pass the previous match as @node (with the original node
 * still as @top).
 *
 * Return value: the first matching element after @node, or %NULL when
 * there are no more matches.
 **/
xmlNode *
e2k_xml_find_in (xmlNode *node, xmlNode *top, const gchar *name)
{
	g_return_val_if_fail (name != NULL, NULL);

	while (node) {
		/* If the current node has children, then the first
		 * child is the next node to examine. If it doesn't
		 * have children but does have a younger sibling, then
		 * that sibling is next up. Otherwise, climb back up
		 * the tree until we find a node that does have a
		 * younger sibling.
		 */
		if (node->children)
			node = node->children;
		else {
			while (node && !node->next && node != top)
				node = node->parent;
			if (!node || node == top)
				return NULL;
			node = node->next;
		}

		if (node->name && !strcmp ((gchar *) node->name, name))
			return node;
	}

	return NULL;
}
