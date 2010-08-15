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

#include "e2k-properties.h"
#include "e2k-propnames.h"
#include "e2k-utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/xmlmemory.h>

struct E2kProperties {
	GHashTable *set, *removed;
};

typedef struct {
	gchar *name;
	const gchar *namespace;
	const gchar *short_name;

	E2kPropType type;
	guint32 proptag;
} E2kPropInfo;

static GHashTable *known_properties;
static GStaticMutex known_properties_lock = G_STATIC_MUTEX_INIT;

/**
 * e2k_properties_new:
 *
 * Creates a new (empty) #E2kProperties structure
 *
 * Return value: the structure
 **/
E2kProperties *
e2k_properties_new (void)
{
	E2kProperties *props;

	props = g_new0 (E2kProperties, 1);
	props->set = g_hash_table_new (g_str_hash, g_str_equal);
	props->removed = g_hash_table_new (g_str_hash, g_str_equal);

	return props;
}

static void
copy_prop (gpointer key, gpointer value, gpointer data)
{
	const gchar *name = key;
	GHashTable *props_copy = data;
	gpointer value_copy;
	E2kPropInfo *pi;

	g_static_mutex_lock (&known_properties_lock);
	pi = g_hash_table_lookup (known_properties, name);
	g_static_mutex_unlock (&known_properties_lock);

	switch (pi->type) {
	case E2K_PROP_TYPE_BINARY_ARRAY:
	{
		GPtrArray *orig = value, *copy;
		GByteArray *new, *old;
		gint i;

		copy = g_ptr_array_new ();
		for (i = 0; i < orig->len; i++) {
			old = orig->pdata[i];
			new = g_byte_array_new ();
			g_byte_array_append (new, old->data, old->len);
			g_ptr_array_add (copy, new);
		}
		value_copy = copy;
		break;
	}

	case E2K_PROP_TYPE_STRING_ARRAY:
	{
		GPtrArray *orig = value, *copy;
		gint i;

		copy = g_ptr_array_new ();
		for (i = 0; i < orig->len; i++)
			g_ptr_array_add (copy, g_strdup (orig->pdata[i]));
		value_copy = copy;
		break;
	}

	case E2K_PROP_TYPE_BINARY:
	{
		GByteArray *orig = value, *copy;

		copy = g_byte_array_new ();
		g_byte_array_append (copy, orig->data, orig->len);
		value_copy = copy;
		break;
	}

	case E2K_PROP_TYPE_XML:
		value_copy = xmlCopyNode (value, TRUE);
		break;

	case E2K_PROP_TYPE_STRING:
	default:
		value_copy = g_strdup (value);
		break;
	}

	g_hash_table_insert (props_copy, pi->name, value_copy);
}

/**
 * e2k_properties_copy:
 * @props: an #E2kProperties
 *
 * Performs a deep copy of @props
 *
 * Return value: a new copy of @props
 **/
E2kProperties *
e2k_properties_copy (E2kProperties *props)
{
	E2kProperties *copy;

	g_return_val_if_fail (props != NULL, NULL);

	copy = e2k_properties_new ();
	g_hash_table_foreach (props->set, copy_prop, copy->set);
	g_hash_table_foreach (props->removed, copy_prop, copy->removed);
	return copy;
}

static void
free_prop (E2kPropInfo *pi, gpointer value)
{
	if (!value)
		return;

	switch (pi->type) {
	case E2K_PROP_TYPE_BINARY_ARRAY:
	{
		GPtrArray *array = value;
		gint i;

		for (i = 0; i < array->len; i++)
			g_byte_array_free (array->pdata[i], TRUE);
		g_ptr_array_free (array, TRUE);
		break;
	}

	case E2K_PROP_TYPE_STRING_ARRAY:
	case E2K_PROP_TYPE_INT_ARRAY:
	{
		GPtrArray *array = value;
		gint i;

		for (i = 0; i < array->len; i++)
			g_free (array->pdata[i]);
		g_ptr_array_free (array, TRUE);
		break;
	}

	case E2K_PROP_TYPE_BINARY:
		g_byte_array_free (value, TRUE);
		break;

	case E2K_PROP_TYPE_XML:
		xmlFreeNode (value);
		break;

	case E2K_PROP_TYPE_STRING:
	default:
		g_free (value);
		break;
	}
}

static void
properties_free_cb (gpointer key, gpointer value, gpointer data)
{
	E2kPropInfo *pi;

	g_static_mutex_lock (&known_properties_lock);
	pi = g_hash_table_lookup (known_properties, key);
	g_static_mutex_unlock (&known_properties_lock);
	if (pi)
		free_prop (pi, value);
}

/**
 * e2k_properties_free:
 * @props: an #E2kProperties
 *
 * Frees @props and all of the properties it contains.
 **/
void
e2k_properties_free (E2kProperties *props)
{
	g_return_if_fail (props != NULL);

	g_hash_table_foreach (props->set, properties_free_cb, NULL);
	g_hash_table_destroy (props->set);
	g_hash_table_destroy (props->removed);
	g_free (props);
}

/**
 * e2k_properties_get_prop:
 * @props: an #E2kProperties
 * @propname: a property name
 *
 * Retrieves the value of @propname in @props.
 *
 * Return value: the value of @propname in @props, or %NULL if it is
 * not set. The caller should not free the value; it is owned by
 * @props.
 **/
gpointer
e2k_properties_get_prop (E2kProperties *props, const gchar *propname)
{
	g_return_val_if_fail (props != NULL, NULL);

	return g_hash_table_lookup (props->set, propname);
}

/**
 * e2k_properties_empty:
 * @props: an #E2kProperties
 *
 * Tests if @props is empty.
 *
 * Return value: %TRUE if @props has no properties set, %FALSE if it
 * has at least one value set.
 **/
gboolean
e2k_properties_empty (E2kProperties *props)
{
	g_return_val_if_fail (props != NULL, TRUE);

	return g_hash_table_size (props->set) == 0;
}

extern gchar e2k_des_key[8];

static E2kPropInfo *
get_propinfo (const gchar *propname, E2kPropType type)
{
	E2kPropInfo *pi;

	g_static_mutex_lock (&known_properties_lock);
	if (!known_properties)
		known_properties = g_hash_table_new (g_str_hash, g_str_equal);

	pi = g_hash_table_lookup (known_properties, propname);
	if (pi) {
		if (pi->type == E2K_PROP_TYPE_UNKNOWN)
			pi->type = type;
		g_static_mutex_unlock (&known_properties_lock);
		return pi;
	}

	pi = g_new (E2kPropInfo, 1);
	pi->name = g_strdup (propname);
	pi->namespace = e2k_prop_namespace_name (pi->name);
	pi->short_name = e2k_prop_property_name (pi->name);
	pi->type = type;

	if (pi->short_name[0] == 'x')
		pi->proptag = strtoul (pi->short_name + 1, NULL, 16);
	else
		pi->proptag = 0;

	g_hash_table_insert (known_properties, pi->name, pi);

	g_static_mutex_unlock (&known_properties_lock);

	return pi;
}

/**
 * e2k_properties_set_string:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: an allocated string
 *
 * Sets @propname in @props to @value. @props assumes ownership of
 * @value.
 **/

/**
 * e2k_properties_set_string_array:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: an array of allocated strings
 *
 * Sets @propname in @props to @value. @props assumes ownership of
 * @value.
 **/

/**
 * e2k_properties_set_binary:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: a byte array
 *
 * Sets @propname in @props to @value. @props assumes ownership of
 * @value.
 **/

/**
 * e2k_properties_set_binary_array:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: an array of byte arrays
 *
 * Sets @propname in @props to @value. @props assumes ownership of
 * @value.
 **/

/**
 * e2k_properties_set_xml:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: an #xmlNode
 *
 * Sets @propname in @props to @value. @props assumes ownership of
 * @value.
 **/

/**
 * e2k_properties_set_int:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: an integer
 *
 * Sets @propname in @props to @value.
 **/

/**
 * e2k_properties_set_int_array:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: an array of integers
 *
 * Sets @propname in @props to @value. @props assumes ownership of
 * @value.
 **/

/**
 * e2k_properties_set_float:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: a floating-point value
 *
 * Sets @propname in @props to @value.
 **/

/**
 * e2k_properties_set_bool:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: a boolean value
 *
 * Sets @propname in @props to @value.
 **/

/**
 * e2k_properties_set_date:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @value: an allocated string containing an Exchange timestamp
 *
 * Sets @propname in @props to @value. @props assumes ownership of
 * @value.
 **/

#define E2K_PROPERTIES_SETTER(fname, valuetype, pitype, data)		\
void									\
e2k_properties_set_ ## fname (E2kProperties *props,			\
			      const gchar    *propname,			\
			      valuetype      value)			\
{									\
	E2kPropInfo *pi;						\
									\
	pi = get_propinfo (propname, E2K_PROP_TYPE_ ## pitype);		\
	free_prop (pi, g_hash_table_lookup (props->set, pi->name));	\
	g_hash_table_insert (props->set, pi->name, data);		\
	g_hash_table_remove (props->removed, pi->name);			\
}

E2K_PROPERTIES_SETTER (string, gchar *, STRING, value)
E2K_PROPERTIES_SETTER (string_array, GPtrArray *, STRING_ARRAY, value)
E2K_PROPERTIES_SETTER (binary, GByteArray *, BINARY, value)
E2K_PROPERTIES_SETTER (binary_array, GPtrArray *, BINARY_ARRAY, value)
E2K_PROPERTIES_SETTER (xml, xmlNode *, XML, value)

E2K_PROPERTIES_SETTER (int, int, INT, g_strdup_printf ("%d", value))
E2K_PROPERTIES_SETTER (int_array, GPtrArray *, INT_ARRAY, value)
E2K_PROPERTIES_SETTER (float, float, FLOAT, g_strdup_printf ("%f", value))
E2K_PROPERTIES_SETTER (bool, gboolean, BOOL, g_strdup_printf ("%d", value != FALSE))
E2K_PROPERTIES_SETTER (date, gchar *, DATE, value)

/**
 * e2k_properties_set_type_as_string:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @type: the type of @value
 * @value: an allocated string
 *
 * Sets @propname in @props to @value, but with type @type. @props
 * assumes ownership of @value.
 **/

/**
 * e2k_properties_set_type_as_string_array:
 * @props: an #E2kProperties
 * @propname: the name of a property
 * @type: the type of @value
 * @value: an array of allocated strings
 *
 * Sets @propname in @props to @value, but with type @type. @props
 * assumes ownership of @value.
 **/

#define E2K_PROPERTIES_SETTER_AS(fname, valuetype)			\
void									\
e2k_properties_set_type_as_ ## fname (E2kProperties *props,		\
				      const gchar    *propname,		\
				      E2kPropType    type,		\
				      valuetype      value)		\
{									\
	E2kPropInfo *pi;						\
									\
	pi = get_propinfo (propname, type);				\
	free_prop (pi, g_hash_table_lookup (props->set, pi->name));	\
	g_hash_table_insert (props->set, pi->name, value);		\
	g_hash_table_remove (props->removed, pi->name);			\
}

E2K_PROPERTIES_SETTER_AS (string, gchar *)
E2K_PROPERTIES_SETTER_AS (string_array, GPtrArray *)

/**
 * e2k_properties_remove:
 * @props: an #E2kProperties
 * @propname: the name of a property
 *
 * Marks @propname removed in @props, so that the corresponding
 * property will be removed from the object on the server if @props is
 * used in a PROPPATCH. If the property was formerly set in @props,
 * this frees the old value.
 **/
void
e2k_properties_remove (E2kProperties *props, const gchar *propname)
{
	E2kPropInfo *pi;

	pi = get_propinfo (propname, E2K_PROP_TYPE_UNKNOWN);
	free_prop (pi, g_hash_table_lookup (props->set, pi->name));
	g_hash_table_remove (props->set, pi->name);
	g_hash_table_insert (props->removed, pi->name, NULL);
}

struct foreach_data {
	E2kPropertiesForeachFunc callback;
	gpointer user_data;
};

static void
foreach_callback (gpointer key, gpointer value, gpointer data)
{
	struct foreach_data *fd = data;
	E2kPropInfo *pi;

	g_static_mutex_lock (&known_properties_lock);
	pi = g_hash_table_lookup (known_properties, key);
	g_static_mutex_unlock (&known_properties_lock);
	if (pi)
		fd->callback (pi->name, pi->type, value, fd->user_data);
}

/**
 * e2k_properties_foreach:
 * @props: an #E2kProperties
 * @callback: callback function to call for each set property
 * @user_data: data to pass to @callback
 *
 * Calls @callback once for each property that is set in @props (in
 * unspecified order), passing it the name of the property, the
 * property's type, its value, and @user_data.
 **/
void
e2k_properties_foreach (E2kProperties *props,
			E2kPropertiesForeachFunc callback,
			gpointer user_data)
{
	struct foreach_data fd;

	g_return_if_fail (props != NULL);

	fd.callback = callback;
	fd.user_data = user_data;

	g_hash_table_foreach (props->set, foreach_callback, &fd);
}

/**
 * e2k_properties_foreach_removed:
 * @props: an #E2kProperties
 * @callback: callback function to call for each set property
 * @user_data: data to pass to @callback
 *
 * Calls @callback once for each property marked removed in @props (in
 * unspecified order), passing it the name of the property, the
 * property's type (if known), a %NULL value, and @user_data.
 **/
void
e2k_properties_foreach_removed (E2kProperties *props,
				E2kPropertiesForeachFunc callback,
				gpointer user_data)
{
	struct foreach_data fd;

	g_return_if_fail (props != NULL);

	fd.callback = callback;
	fd.user_data = user_data;

	g_hash_table_foreach (props->removed, foreach_callback, &fd);
}

struct foreach_namespace_data {
	E2kPropertiesForeachNamespaceFunc callback;
	gpointer user_data;
	gboolean need_array_namespace, need_type_namespace;
	GHashTable *seen_namespaces;
};

static void
foreach_namespace_callback (gpointer key, gpointer value, gpointer data)
{
	struct foreach_namespace_data *fnd = data;
	E2kPropInfo *pi;
	const gchar *name;

	g_static_mutex_lock (&known_properties_lock);
	pi = g_hash_table_lookup (known_properties, key);
	g_static_mutex_unlock (&known_properties_lock);
	if (!pi)
		return;

	name = e2k_prop_namespace_name (pi->name);
	if (!g_hash_table_lookup (fnd->seen_namespaces, name)) {
		g_hash_table_insert (fnd->seen_namespaces,
				     (gchar *)name, (gchar *)name);
		fnd->callback (name, e2k_prop_namespace_abbrev (pi->name),
			       fnd->user_data);
	}

	switch (pi->type) {
	case E2K_PROP_TYPE_STRING_ARRAY:
	case E2K_PROP_TYPE_BINARY_ARRAY:
	case E2K_PROP_TYPE_INT_ARRAY:
		fnd->need_array_namespace = TRUE;
		/* fall through */

	case E2K_PROP_TYPE_BINARY:
	case E2K_PROP_TYPE_INT:
	case E2K_PROP_TYPE_BOOL:
	case E2K_PROP_TYPE_FLOAT:
	case E2K_PROP_TYPE_DATE:
		fnd->need_type_namespace = TRUE;
		break;

	default:
		break;
	}
}

/**
 * e2k_properties_foreach_namespace:
 * @props: an #E2kProperties
 * @callback: callback function to call for each namespace
 * @user_data: data to pass to @callback
 *
 * Calls @callback once for each unique namespace used by the
 * properties (set or removed) in @props, passing it the name of the
 * namespace, its standard abbreviation, and @user_data.
 **/
void
e2k_properties_foreach_namespace (E2kProperties *props,
				  E2kPropertiesForeachNamespaceFunc callback,
				  gpointer user_data)
{
	struct foreach_namespace_data fnd;

	g_return_if_fail (props != NULL);

	fnd.callback = callback;
	fnd.user_data = user_data;
	fnd.need_array_namespace = FALSE;
	fnd.need_type_namespace = FALSE;
	fnd.seen_namespaces = g_hash_table_new (NULL, NULL);

	g_hash_table_foreach (props->set, foreach_namespace_callback, &fnd);
	g_hash_table_foreach (props->removed, foreach_namespace_callback, &fnd);

	if (fnd.need_type_namespace)
		callback (E2K_NS_TYPE, 'T', user_data);
	if (fnd.need_array_namespace)
		callback ("xml:", 'X', user_data);

	g_hash_table_destroy (fnd.seen_namespaces);
}

static GHashTable *namespaces;
static gint next_namespace = 'a';
static GStaticMutex namespaces_lock = G_STATIC_MUTEX_INIT;

static const gchar *
get_div (const gchar *propname)
{
	const gchar *div;

	div = strrchr (propname, '/');
	if (div)
		return div;
	return strrchr (propname, ':');
}

static gint
prop_equal (gconstpointer v1, gconstpointer v2)
{
	const gchar *s1 = (const gchar *)v1, *s2 = (const gchar *)v2;
	const gchar *d1 = get_div (s1), *d2 = get_div (s2);

	return (d1 - s1 == d2 - s2) && !g_ascii_strncasecmp (s1, s2, d1 - s1);
}

static guint
prop_hash (gconstpointer v)
{
	const gchar *d = get_div (v);
	const gchar *p = v;
	guint h = g_ascii_tolower (*p);

	for (p += 1; p < d; p++)
		h = (h << 5) - h + *p;
	return h;
}

static void
setup_namespaces (void)
{
	namespaces = g_hash_table_new (prop_hash, prop_equal);
	g_hash_table_insert (namespaces, (gpointer) "DAV", GINT_TO_POINTER ('D'));
}

/**
 * e2k_prop_namespace_name:
 * @prop: the name of a property
 *
 * Splits out the namespace portion of @prop
 *
 * Return value: the URI of @prop's namespace
 **/
const gchar *
e2k_prop_namespace_name (const gchar *prop)
{
	const gchar *div = get_div (prop);
	gpointer key, value;
	gchar *name;

	g_static_mutex_lock (&namespaces_lock);
	if (!namespaces)
		setup_namespaces ();

	if (g_hash_table_lookup_extended (namespaces, prop, &key, &value)) {
		g_static_mutex_unlock (&namespaces_lock);
		return key;
	}

	name = g_strndup (prop, div - prop + 1);
	g_hash_table_insert (namespaces, name, GINT_TO_POINTER (next_namespace));
	next_namespace++;
	g_static_mutex_unlock (&namespaces_lock);
	return name;
}

/**
 * e2k_prop_namespace_abbrev:
 * @prop: the name of a property
 *
 * Splits out the namespace portion of @prop and assigns a unique
 * abbreviation for it.
 *
 * Return value: the abbreviation used for prop's namespace
 **/
gchar
e2k_prop_namespace_abbrev (const gchar *prop)
{
	const gchar *div = get_div (prop);
	gpointer key, value;
	gchar *name, res;

	g_static_mutex_lock (&namespaces_lock);
	if (!namespaces)
		setup_namespaces ();

	if (g_hash_table_lookup_extended (namespaces, prop, &key, &value)) {
		g_static_mutex_unlock (&namespaces_lock);
		return GPOINTER_TO_INT (value);
	}

	name = g_strndup (prop, div - prop + 1);
	g_hash_table_insert (namespaces, name, GINT_TO_POINTER (next_namespace));
	res = next_namespace++;
	g_static_mutex_unlock (&namespaces_lock);

	return res;
}

/**
 * e2k_prop_property_name:
 * @prop: the name of a property
 *
 * Splits out the non-namespace portion of @prop
 *
 * Return value: the non-namespaced name of @prop
 **/
const gchar *
e2k_prop_property_name (const gchar *prop)
{
	return get_div (prop) + 1;
}

/**
 * e2k_prop_proptag:
 * @prop: the name of a MAPI property
 *
 * Computes the MAPI proptag value of @prop, which must be the name
 * of a MAPI property.
 *
 * Return value: the MAPI proptag value
 **/
guint32
e2k_prop_proptag (const gchar *prop)
{
	E2kPropInfo *pi;

	pi = get_propinfo (prop, E2K_PROP_TYPE_UNKNOWN);
	return pi->proptag;
}

/**
 * e2k_proptag_prop:
 * @proptag: a MAPI property
 *
 * Computes the WebDAV property name of the property with the
 * given proptag.
 *
 * Return value: the WebDAV property name associated with @proptag
 **/
const gchar *
e2k_proptag_prop (guint32 proptag)
{
	E2kPropInfo *pi;
	gchar *tmpname;

	tmpname = g_strdup_printf (E2K_NS_MAPI_PROPTAG "x%08x",
				   (unsigned)proptag);

	pi = get_propinfo (tmpname, E2K_PROP_TYPE_UNKNOWN);
	g_free (tmpname);
	return pi->name;
}
