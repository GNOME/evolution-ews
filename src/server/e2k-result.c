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

#include "e2k-result.h"
#include "e2k-http-utils.h"
#include "e2k-propnames.h"
#include "e2k-xml-utils.h"

#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

static void
prop_get_binary_array (E2kResult *result, const gchar *propname, xmlNode *node)
{
	GPtrArray *array;

	array = g_ptr_array_new ();
	for (node = node->xmlChildrenNode; node; node = node->next) {
		GByteArray *byte_array;

		byte_array = g_byte_array_new ();
		if (node->xmlChildrenNode && node->xmlChildrenNode->content) {
			guchar *data;
			gsize length = 0;

			data = g_base64_decode (
				(gchar *) node->xmlChildrenNode->content, &length);
			g_byte_array_append (byte_array, data, length);
			g_free (data);
		}
		g_ptr_array_add (array, byte_array);
	}

	e2k_properties_set_binary_array (result->props, propname, array);
}

static void
prop_get_string_array (E2kResult *result, const gchar *propname,
		       E2kPropType real_type, xmlNode *node)
{
	GPtrArray *array;

	array = g_ptr_array_new ();
	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (node->xmlChildrenNode && node->xmlChildrenNode->content)
			g_ptr_array_add (array, g_strdup ((gchar *) node->xmlChildrenNode->content));
		else
			g_ptr_array_add (array, g_strdup (""));
	}

	e2k_properties_set_type_as_string_array (result->props, propname,
						 real_type, array);
}

static void
prop_get_binary (E2kResult *result, const gchar *propname, xmlNode *node)
{
	GByteArray *byte_array;

	byte_array = g_byte_array_new ();
	if (node->xmlChildrenNode && node->xmlChildrenNode->content) {
		guchar *data;
		gsize length = 0;

		data = g_base64_decode (
			(gchar *) node->xmlChildrenNode->content, &length);
		g_byte_array_append (byte_array, data, length);
		g_free (data);
	}

	e2k_properties_set_binary (result->props, propname, byte_array);
}

static void
prop_get_string (E2kResult *result, const gchar *propname,
		 E2kPropType real_type, xmlNode *node)
{
	gchar *content;

	if (node->xmlChildrenNode && node->xmlChildrenNode->content)
		content = g_strdup ((gchar *) node->xmlChildrenNode->content);
	else
		content = g_strdup ("");

	e2k_properties_set_type_as_string (result->props, propname,
					   real_type, content);
}

static void
prop_get_xml (E2kResult *result, const gchar *propname, xmlNode *node)
{
	e2k_properties_set_xml (result->props, propname,
				xmlCopyNode (node, TRUE));
}

static void
prop_parse (xmlNode *node, E2kResult *result)
{
	gchar *name;
	xmlChar *type;

	g_return_if_fail (node->ns != NULL);

	if (!result->props)
		result->props = e2k_properties_new ();

	if (!strncmp ((gchar *) node->ns->href, E2K_NS_MAPI_ID, E2K_NS_MAPI_ID_LEN)) {
		/* Reinsert the illegal initial '0' that was stripped out
		 * by sanitize_bad_multistatus. (This also covers us in
		 * the cases where the server returns the property without
		 * the '0'.)
		 */
		name = g_strdup_printf ("%s0%s", node->ns->href, node->name);
	} else
		name = g_strdup_printf ("%s%s", node->ns->href, node->name);

	type = xmlGetNsProp (
		node,
		(xmlChar *) "dt",
		(xmlChar *) E2K_NS_TYPE);
	if (type && !xmlStrcmp (type, (xmlChar *) "mv.bin.base64"))
		prop_get_binary_array (result, name, node);
	else if (type && !xmlStrcmp (type, (xmlChar *) "mv.int"))
		prop_get_string_array (result, name, E2K_PROP_TYPE_INT_ARRAY, node);
	else if (type && !xmlStrncmp (type, (xmlChar *) "mv.", 3))
		prop_get_string_array (result, name, E2K_PROP_TYPE_STRING_ARRAY, node);
	else if (type && !xmlStrcmp (type, (xmlChar *) "bin.base64"))
		prop_get_binary (result, name, node);
	else if (type && !xmlStrcmp (type, (xmlChar *) "int"))
		prop_get_string (result, name, E2K_PROP_TYPE_INT, node);
	else if (type && !xmlStrcmp (type, (xmlChar *) "boolean"))
		prop_get_string (result, name, E2K_PROP_TYPE_BOOL, node);
	else if (type && !xmlStrcmp (type, (xmlChar *) "float"))
		prop_get_string (result, name, E2K_PROP_TYPE_FLOAT, node);
	else if (type && !xmlStrcmp (type, (xmlChar *) "dateTime.tz"))
		prop_get_string (result, name, E2K_PROP_TYPE_DATE, node);
	else if (!node->xmlChildrenNode ||
		 !node->xmlChildrenNode->xmlChildrenNode)
		prop_get_string (result, name, E2K_PROP_TYPE_STRING, node);
	else
		prop_get_xml (result, name, node);

	if (type)
		xmlFree (type);
	g_free (name);
}

static void
propstat_parse (xmlNode *node, E2kResult *result)
{
	node = node->xmlChildrenNode;
	if (!E2K_IS_NODE (node, "DAV:", "status"))
		return;
	result->status = e2k_http_parse_status (
		(gchar *) node->xmlChildrenNode->content);
	if (result->status != E2K_HTTP_OK)
		return;

	node = node->next;
	if (!E2K_IS_NODE (node, "DAV:", "prop"))
		return;

	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE)
			prop_parse (node, result);
	}
}

static void
e2k_result_clear (E2kResult *result)
{
	xmlFree (result->href);
	result->href = NULL;
	if (result->props) {
		e2k_properties_free (result->props);
		result->props = NULL;
	}
}

/**
 * e2k_results_array_new:
 *
 * Creates a new results array
 *
 * Return value: the array
 **/
GArray *
e2k_results_array_new (void)
{
	return g_array_new (FALSE, FALSE, sizeof (E2kResult));
}

/* Properties in the /mapi/id/{...} namespaces are usually (though not
 * always) returned with names that start with '0', which is illegal
 * and makes libxml choke. So we preprocess them to fix that.
 */
static gchar *
sanitize_bad_multistatus (const gchar *buf, gint len)
{
	GString *body;
	const gchar *p;
	gint start, end;
	gchar ns, badprop[7], *ret;

	/* If there are no "mapi/id/{...}" namespace declarations, then
	 * we don't need any cleanup.
	 */
	if (!memchr (buf, '{', len))
		return NULL;

	body = g_string_new_len (buf, len);

	/* Find the start and end of namespace declarations */
	p = strstr (body->str, " xmlns:");
	g_return_val_if_fail (p != NULL, NULL);
	start = p + 1 - body->str;

	p = strchr (p, '>');
	g_return_val_if_fail (p != NULL, NULL);
	end = p - body->str;

	while (1) {
		if (strncmp (body->str + start, "xmlns:", 6) != 0)
			break;
		if (strncmp (body->str + start + 7, "=\"", 2) != 0)
			break;
		if (strncmp (body->str + start + 9, E2K_NS_MAPI_ID, E2K_NS_MAPI_ID_LEN) != 0)
			goto next;

		ns = body->str[start + 6];

		/* Find properties in this namespace and strip the
		 * initial '0' from their names to make them valid
		 * XML NCNames.
		 */
		snprintf (badprop, 6, "<%c:0x", ns);
		while ((p = strstr (body->str, badprop)))
			g_string_erase (body, p + 3 - body->str, 1);
		snprintf (badprop, 7, "</%c:0x", ns);
		while ((p = strstr (body->str, badprop)))
			g_string_erase (body, p + 4 - body->str, 1);

	next:
		p = strchr (body->str + start, '"');
		if (!p)
			break;
		p = strchr (p + 1, '"');
		if (!p)
			break;
		if (p[1] != ' ')
			break;

		start = p + 2 - body->str;
	}

	ret = body->str;
	g_string_free (body, FALSE);
	return ret;
}

/**
 * e2k_results_array_add_from_multistatus:
 * @results_array: a results array, created by e2k_results_array_new()
 * @msg: a 207 Multi-Status response
 *
 * Constructs an #E2kResult for each response in @msg and appends them
 * to @results_array.
 **/
void
e2k_results_array_add_from_multistatus (GArray *results_array,
					SoupMessage *msg)
{
	xmlDoc *doc;
	xmlNode *node, *rnode;
	E2kResult result;
	gchar *body;

	g_return_if_fail (msg->status_code == E2K_HTTP_MULTI_STATUS);

	body = sanitize_bad_multistatus (msg->response_body->data,
					 msg->response_body->length);
	if (body) {
		doc = e2k_parse_xml (body, -1);
		g_free (body);
	} else {
		doc = e2k_parse_xml (msg->response_body->data,
				     msg->response_body->length);
	}
	if (!doc)
		return;
	node = doc->xmlRootNode;
	if (!node || !E2K_IS_NODE (node, "DAV:", "multistatus")) {
		xmlFree (doc);
		return;
	}

	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (!E2K_IS_NODE (node, "DAV:", "response") ||
		    !node->xmlChildrenNode)
			continue;

		memset (&result, 0, sizeof (result));
		result.status = E2K_HTTP_OK; /* sometimes omitted if Brief */

		for (rnode = node->xmlChildrenNode; rnode; rnode = rnode->next) {
			if (rnode->type != XML_ELEMENT_NODE)
				continue;

			if (E2K_IS_NODE (rnode, "DAV:", "href"))
				result.href = (gchar *) xmlNodeGetContent (rnode);
			else if (E2K_IS_NODE (rnode, "DAV:", "status")) {
				result.status = e2k_http_parse_status (
					(gchar *) rnode->xmlChildrenNode->content);
			} else if (E2K_IS_NODE (rnode, "DAV:", "propstat"))
				propstat_parse (rnode, &result);
			else
				prop_parse (rnode, &result);
		}

		if (result.href) {
			if (E2K_HTTP_STATUS_IS_SUCCESSFUL (result.status) &&
			    !result.props)
				result.props = e2k_properties_new ();
			g_array_append_val (results_array, result);
		} else
			e2k_result_clear (&result);
	}

	xmlFreeDoc (doc);
}

/**
 * e2k_results_array_free:
 * @results_array: the array
 * @free_results: whether or not to also free the contents of the array
 *
 * Frees @results_array, and optionally its contents
 **/
void
e2k_results_array_free (GArray *results_array, gboolean free_results)
{
	if (free_results) {
		e2k_results_free ((E2kResult *)results_array->data,
				  results_array->len);
	}
	g_array_free (results_array, FALSE);
}

/**
 * e2k_results_from_multistatus:
 * @msg: a 207 Multi-Status response
 * @results: pointer to a variable to store an array of E2kResult in
 * @nresults: pointer to a variable to store the length of *@results in
 *
 * Parses @msg and puts the results in *@results and *@nresults.
 * The caller should free the data with e2k_results_free()
 **/
void
e2k_results_from_multistatus (SoupMessage *msg,
			      E2kResult **results, gint *nresults)
{
	GArray *results_array;

	results_array = e2k_results_array_new ();
	e2k_results_array_add_from_multistatus (results_array, msg);

	*results = (E2kResult *)results_array->data;
	*nresults = results_array->len;
	e2k_results_array_free (results_array, FALSE);
}

/**
 * e2k_results_copy:
 * @results: a results array returned from e2k_results_from_multistatus()
 * @nresults: the length of @results
 *
 * Performs a deep copy of @results
 *
 * Return value: a copy of @results.
 **/
E2kResult *
e2k_results_copy (E2kResult *results, gint nresults)
{
	GArray *results_array = NULL;
	E2kResult result, *new_results;
	gint i;

	results_array = g_array_new (TRUE, FALSE, sizeof (E2kResult));
	for (i = 0; i < nresults; i++) {
		result.href   = xmlMemStrdup (results[i].href);
		result.status = results[i].status;
		result.props  = e2k_properties_copy (results[i].props);

		g_array_append_val (results_array, result);
	}

	new_results = (E2kResult *) (results_array->data);
	g_array_free (results_array, FALSE);
	return new_results;
}

/**
 * e2k_results_free:
 * @results: a results array
 * @nresults: the length of @results
 *
 * Frees the data in @results.
 **/
void
e2k_results_free (E2kResult *results, gint nresults)
{
	gint i;

	for (i = 0; i < nresults; i++)
		e2k_result_clear (&results[i]);
	g_free (results);
}

/* Iterators */
struct E2kResultIter {
	E2kContext *ctx;
	E2kOperation *op;
	E2kHTTPStatus status;

	E2kResult *results;
	gint nresults, next;
	gint first, total;
	gboolean ascending;

	E2kResultIterFetchFunc fetch_func;
	E2kResultIterFreeFunc free_func;
	gpointer user_data;
};

static void
iter_fetch (E2kResultIter *iter)
{
	if (iter->nresults) {
		if (iter->ascending)
			iter->first += iter->nresults;
		else
			iter->first -= iter->nresults;
		e2k_results_free (iter->results, iter->nresults);
		iter->nresults = 0;
	}

	iter->status = iter->fetch_func (iter, iter->ctx, iter->op,
					 &iter->results,
					 &iter->nresults,
					 &iter->first,
					 &iter->total,
					 iter->user_data);
	iter->next = 0;
}

/**
 * e2k_result_iter_new:
 * @ctx: an #E2kContext
 * @op: an #E2kOperation, to use for cancellation
 * @ascending: %TRUE if results should be returned in ascending
 * order, %FALSE if they should be returned in descending order
 * @total: the total number of results that will be returned, or -1
 * if not yet known
 * @fetch_func: function to call to fetch more results
 * @free_func: function to call when the iterator is freed
 * @user_data: data to pass to @fetch_func and @free_func
 *
 * Creates a object that can be used to return the results of
 * a Multi-Status query on @ctx.
 *
 * @fetch_func will be called to fetch results, and it may update the
 * #first and #total fields if necessary. If @ascending is %TRUE, then
 * e2k_result_iter_next() will first return the first result, then the
 * second result, etc. If @ascending is %FALSE, it will return the
 * last result, then the second-to-last result, etc.
 *
 * When all of the results returned by the first @fetch_func call have
 * been returned to the caller, @fetch_func will be called again to
 * get more results. This will continue until @fetch_func returns 0
 * results, or returns an error code.
 *
 * Return value: the new iterator
 **/
E2kResultIter *
e2k_result_iter_new (E2kContext *ctx, E2kOperation *op,
		     gboolean ascending, gint total,
		     E2kResultIterFetchFunc fetch_func,
		     E2kResultIterFreeFunc free_func,
		     gpointer user_data)
{
	E2kResultIter *iter;

	iter = g_new0 (E2kResultIter, 1);
	iter->ctx = g_object_ref (ctx);
	iter->op = op;
	iter->ascending = ascending;
	iter->total = total;
	iter->fetch_func = fetch_func;
	iter->free_func = free_func;
	iter->user_data = user_data;

	iter_fetch (iter);

	return iter;
}

/**
 * e2k_result_iter_next:
 * @iter: an #E2kResultIter
 *
 * Returns the next result in the operation being iterated by @iter.
 * If there are no more results, or if an error occurs, it will return
 * %NULL. (The return value of e2k_result_iter_free() distinguishes
 * these two cases.)
 *
 * Return value: the result, or %NULL
 **/
E2kResult *
e2k_result_iter_next (E2kResultIter *iter)
{
	g_return_val_if_fail (iter != NULL, NULL);

	if (iter->nresults == 0)
		return NULL;

	if (iter->next >= iter->nresults) {
		iter_fetch (iter);
		if (iter->nresults == 0)
			return NULL;
		if (iter->total <= 0)
			iter->status = E2K_HTTP_MALFORMED;
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (iter->status))
			return NULL;
	}

	return iter->ascending ?
		&iter->results[iter->next++] :
		&iter->results[iter->nresults - ++iter->next];
}

/**
 * e2k_result_iter_get_index:
 * @iter: an #E2kResultIter
 *
 * Returns the index of the current result in the complete list of
 * results. Note that for a descending search, %index will start at
 * %total - 1 and count backwards to 0.
 *
 * Return value: the index of the current result
 **/
gint
e2k_result_iter_get_index (E2kResultIter *iter)
{
	g_return_val_if_fail (iter != NULL, -1);

	return iter->ascending ?
		iter->first + iter->next - 1 :
		iter->first + (iter->nresults - iter->next);
}

/**
 * e2k_result_iter_get_total:
 * @iter: an #E2kResultIter
 *
 * Returns the total number of results expected for @iter. Note that
 * in some cases, this may change while the results are being iterated
 * (if objects that match the query are added to or removed from the
 * folder).
 *
 * Return value: the total number of results expected
 **/
gint
e2k_result_iter_get_total (E2kResultIter *iter)
{
	g_return_val_if_fail (iter != NULL, -1);

	return iter->total;
}

/**
 * e2k_result_iter_free:
 * @iter: an #E2kResultIter
 *
 * Frees @iter and all associated memory, and returns a status code
 * indicating whether it ended successfully or not. (Note that the
 * status may be %E2K_HTTP_OK rather than %E2K_HTTP_MULTI_STATUS.)
 *
 * Return value: the final status
 **/
E2kHTTPStatus
e2k_result_iter_free (E2kResultIter *iter)
{
	E2kHTTPStatus status;

	g_return_val_if_fail (iter != NULL, E2K_HTTP_MALFORMED);

	status = iter->status;
	if (iter->nresults)
		e2k_results_free (iter->results, iter->nresults);
	iter->free_func (iter, iter->user_data);
	g_object_unref (iter->ctx);
	g_free (iter);

	return status;
}
