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

/* e2k-restriction.c: message restrictions (WHERE clauses / Rule conditions) */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e2k-restriction.h"
#include "e2k-properties.h"
#include "e2k-rule.h"

#include <stdarg.h>
#include <string.h>

static E2kRestriction *
conjoin (E2kRestrictionType type, gint nrns, E2kRestriction **rns, gboolean unref)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);
	gint i;

	ret->type = type;
	ret->res.and.nrns = nrns;
	ret->res.and.rns = g_new (E2kRestriction *, nrns);
	for (i = 0; i < nrns; i++) {
		ret->res.and.rns[i] = rns[i];
		if (!unref)
			e2k_restriction_ref (rns[i]);
	}

	return ret;
}

/**
 * e2k_restriction_and:
 * @nrns: length of @rns
 * @rns: an array of #E2kRestriction
 * @unref: whether or not to unref the restrictions when it is done
 *
 * Creates a new restriction which is true if all of the restrictions
 * in @rns are true.
 *
 * If @unref is %TRUE, then e2k_restriction_and() is essentially
 * stealing the caller's references on the restrictions. If it is
 * %FALSE, then e2k_restriction_and() will acquire its own references
 * to each of the restrictions.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_and (gint nrns, E2kRestriction **rns, gboolean unref)
{
	return conjoin (E2K_RESTRICTION_AND, nrns, rns, unref);
}

/**
 * e2k_restriction_or:
 * @nrns: length of @rns
 * @rns: an array of #E2kRestriction
 * @unref: see e2k_restriction_and()
 *
 * Creates a new restriction which is true if any of the restrictions
 * in @rns are true.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_or (gint nrns, E2kRestriction **rns, gboolean unref)
{
	return conjoin (E2K_RESTRICTION_OR, nrns, rns, unref);
}

static E2kRestriction *
conjoinv (E2kRestrictionType type, E2kRestriction *rn, va_list ap)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);
	GPtrArray *rns;

	rns = g_ptr_array_new ();
	while (rn) {
		g_ptr_array_add (rns, rn);
		rn = va_arg (ap, E2kRestriction *);
	}
	va_end (ap);

	ret->type = type;
	ret->res.and.nrns = rns->len;
	ret->res.and.rns = (E2kRestriction **)rns->pdata;
	g_ptr_array_free (rns, FALSE);

	return ret;
}

/**
 * e2k_restriction_andv:
 * @rn: an #E2kRestriction
 * @...: a %NULL-terminated list of additional #E2kRestrictions
 *
 * Creates a new restriction which is true if all of the passed-in
 * restrictions are true. e2k_restriction_andv() steals the caller's
 * reference on each of the passed-in restrictions.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_andv (E2kRestriction *rn, ...)
{
	va_list ap;

	va_start (ap, rn);
	return conjoinv (E2K_RESTRICTION_AND, rn, ap);
}

/**
 * e2k_restriction_orv:
 * @rn: an #E2kRestriction
 * @...: a %NULL-terminated list of additional #E2kRestrictions
 *
 * Creates a new restriction which is true if any of the passed-in
 * restrictions are true. e2k_restriction_orv() steals the caller's
 * reference on each of the passed-in restrictions.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_orv (E2kRestriction *rn, ...)
{
	va_list ap;

	va_start (ap, rn);
	return conjoinv (E2K_RESTRICTION_OR, rn, ap);
}

/**
 * e2k_restriction_not:
 * @rn: an #E2kRestriction
 * @unref: see e2k_restriction_and()
 *
 * Creates a new restriction which is true if @rn is false.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_not (E2kRestriction *rn, gboolean unref)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_NOT;
	ret->res.not.rn = rn;
	if (!unref)
		e2k_restriction_ref (rn);

	return ret;
}

/**
 * e2k_restriction_content:
 * @propname: text property to compare against
 * @fuzzy_level: how to compare
 * @value: value to compare against
 *
 * Creates a new restriction that is true for objects where the
 * indicated property's value matches @value according to @fuzzy_level.
 *
 * For a WebDAV SEARCH, @fuzzy_level should be %E2K_FL_FULLSTRING,
 * %E2K_FL_SUBSTRING, %E2K_FL_PREFIX, or %E2K_FL_SUFFIX.
 *
 * For a MAPI restriction, @fuzzy_level may not be %E2K_FL_SUFFIX, but
 * may be ORed with any of the additional values %E2K_FL_IGNORECASE,
 * %E2K_FL_IGNORENONSPACE, or %E2K_FL_LOOSE.
 *
 * To compare a property's sort order to another string, use
 * e2k_restriction_prop_string().
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_content (const gchar *propname,
			 E2kRestrictionFuzzyLevel fuzzy_level,
			 const gchar *value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_CONTENT;
	ret->res.content.fuzzy_level = fuzzy_level;
	e2k_rule_prop_set (&ret->res.content.pv.prop, propname);
	ret->res.content.pv.type = E2K_PROP_TYPE_STRING;
	ret->res.content.pv.value = g_strdup (value);

	return ret;
}

/**
 * e2k_restriction_prop_bool:
 * @propname: boolean property to compare against
 * @relop: %E2K_RELOP_EQ or %E2K_RELOP_NE
 * @value: %TRUE or %FALSE
 *
 * Creates a new restriction that is true for objects where the
 * indicated property matches @relop and @value.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_prop_bool (const gchar *propname, E2kRestrictionRelop relop,
			   gboolean value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	e2k_rule_prop_set (&ret->res.property.pv.prop, propname);
	ret->res.property.pv.type = E2K_PROP_TYPE_BOOL;
	ret->res.property.pv.value = GUINT_TO_POINTER (value);

	return ret;
}

/**
 * e2k_restriction_prop_int:
 * @propname: integer property to compare against
 * @relop: an #E2kRestrictionRelop
 * @value: number to compare against
 *
 * Creates a new restriction that is true for objects where the
 * indicated property matches @value according to @relop.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_prop_int (const gchar *propname, E2kRestrictionRelop relop,
			  gint value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	e2k_rule_prop_set (&ret->res.property.pv.prop, propname);
	ret->res.property.pv.type = E2K_PROP_TYPE_INT;
	ret->res.property.pv.value = GINT_TO_POINTER (value);

	return ret;
}

/**
 * e2k_restriction_prop_date:
 * @propname: date/time property to compare against
 * @relop: an #E2kRestrictionRelop
 * @value: date/time to compare against (as returned by e2k_make_timestamp())
 *
 * Creates a new restriction that is true for objects where the
 * indicated property matches @value according to @relop.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_prop_date (const gchar *propname, E2kRestrictionRelop relop,
			   const gchar *value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	e2k_rule_prop_set (&ret->res.property.pv.prop, propname);
	ret->res.property.pv.type = E2K_PROP_TYPE_DATE;
	ret->res.property.pv.value = g_strdup (value);

	return ret;
}

/**
 * e2k_restriction_prop_string:
 * @propname: text property to compare against
 * @relop: an #E2kRestrictionRelop
 * @value: text to compare against
 *
 * Creates a new restriction that is true for objects where the
 * indicated property matches @value according to @relop.
 *
 * To do a substring match, use e2k_restriction_content().
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_prop_string (const gchar *propname, E2kRestrictionRelop relop,
			     const gchar *value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	e2k_rule_prop_set (&ret->res.property.pv.prop, propname);
	ret->res.property.pv.type = E2K_PROP_TYPE_STRING;
	ret->res.property.pv.value = g_strdup (value);

	return ret;
}

/**
 * e2k_restriction_prop_binary:
 * @propname: binary property to compare against
 * @relop: %E2K_RELOP_EQ or %E2K_RELOP_NE
 * @data: data to compare against
 * @len: length of @data
 *
 * Creates a new restriction that is true for objects where the
 * indicated property matches @value according to @relop.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_prop_binary (const gchar *propname, E2kRestrictionRelop relop,
			     gconstpointer data, gint len)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	e2k_rule_prop_set (&ret->res.property.pv.prop, propname);
	ret->res.property.pv.type = E2K_PROP_TYPE_BINARY;
	ret->res.property.pv.value = g_byte_array_new ();
	g_byte_array_append (ret->res.property.pv.value, data, len);

	return ret;
}

/**
 * e2k_restriction_compare:
 * @propname1: first property
 * @relop: an #E2kRestrictionRelop
 * @propname2: second property
 *
 * Creates a new restriction which is true for objects where
 * @propname1 and @propname2 have the relationship described by
 * @relop.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_compare (const gchar *propname1, E2kRestrictionRelop relop,
			 const gchar *propname2)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_COMPAREPROPS;
	ret->res.compare.relop = relop;
	e2k_rule_prop_set (&ret->res.compare.prop1, propname1);
	e2k_rule_prop_set (&ret->res.compare.prop2, propname2);

	return ret;
}

/**
 * e2k_restriction_bitmask:
 * @propname: integer property to compare
 * @bitop: an #E2kRestrictionBitop
 * @mask: mask of bits to compare against
 *
 * Creates a new restriction that is true for objects where the
 * indicated bits of the value of @propname either are or aren't zero,
 * as indicated by @bitop.
 *
 * This cannot be used for WebDAV SEARCH restrictions.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_bitmask (const gchar *propname, E2kRestrictionBitop bitop,
			 guint32 mask)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_BITMASK;
	ret->res.bitmask.bitop = bitop;
	e2k_rule_prop_set (&ret->res.bitmask.prop, propname);
	ret->res.bitmask.mask = mask;

	return ret;
}

/**
 * e2k_restriction_size:
 * @propname: property to compare
 * @relop: an #E2kRestrictionRelop
 * @size: the size to compare @propname to
 *
 * Creates a new restriction which is true for objects where the size
 * of the value of @propname matches @size according to @relop.
 *
 * This cannot be used for WebDAV SEARCH restrictions.
 *
 * You probably do not want to use this. The standard idiom for
 * checking the size of a message is to use e2k_restriction_prop_int()
 * on its %PR_MESSAGE_SIZE property, not to use e2k_restriction_size()
 * on its %PR_BODY.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_size (const gchar *propname, E2kRestrictionRelop relop,
		      guint32 size)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_SIZE;
	ret->res.size.relop = relop;
	e2k_rule_prop_set (&ret->res.size.prop, propname);
	ret->res.size.size = size;

	return ret;
}

/**
 * e2k_restriction_exist:
 * @propname: property to check
 *
 * Creates a new restriction which is true for objects that have
 * a @propname property.
 *
 * This cannot be used for WebDAV SEARCH restrictions.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_exist (const gchar *propname)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_EXIST;
	e2k_rule_prop_set (&ret->res.exist.prop, propname);

	return ret;
}

/**
 * e2k_restriction_sub:
 * @subtable: the WebDAV name of a MAPI property of type PT_OBJECT
 * @rn: the restriction to apply against the values of @subtable
 * @unref: see e2k_restriction_and()
 *
 * Creates a new restriction that is true for objects where @rn is
 * true when applied to the value of @subtable on that object.
 *
 * @subtable is generally %PR_MESSAGE_RECIPIENTS (for finding messages
 * whose recipients match a given restriction) or
 * %PR_MESSAGE_ATTACHMENTS (for finding messages whose attachments
 * match a given restriction).
 *
 * This cannot be used for WebDAV SEARCH restrictions.
 *
 * Return value: the new restriction
 **/
E2kRestriction *
e2k_restriction_sub (const gchar *subtable, E2kRestriction *rn, gboolean unref)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_SUBRESTRICTION;
	e2k_rule_prop_set (&ret->res.sub.subtable, subtable);
	ret->res.sub.rn = rn;
	if (!unref)
		e2k_restriction_ref (rn);

	return ret;
}

/**
 * e2k_restriction_unref:
 * @rn: a restriction
 *
 * Unrefs @rn. If there are no more references to @rn, it is freed.
 **/
void
e2k_restriction_unref (E2kRestriction *rn)
{
	gint i;

	if (rn->ref_count--)
		return;

	switch (rn->type) {
	case E2K_RESTRICTION_AND:
	case E2K_RESTRICTION_OR:
		for (i = 0; i < rn->res.and.nrns; i++)
			e2k_restriction_unref (rn->res.and.rns[i]);
		g_free (rn->res.and.rns);
		break;

	case E2K_RESTRICTION_NOT:
		e2k_restriction_unref (rn->res.not.rn);
		break;

	case E2K_RESTRICTION_CONTENT:
		e2k_rule_free_propvalue (&rn->res.content.pv);
		break;

	case E2K_RESTRICTION_PROPERTY:
		e2k_rule_free_propvalue (&rn->res.property.pv);
		break;

	default:
		break;
	}

	g_free (rn);
}

/**
 * e2k_restriction_ref:
 * @rn: a restriction
 *
 * Refs @rn.
 **/
void
e2k_restriction_ref (E2kRestriction *rn)
{
	rn->ref_count++;
}

/* SQL export */

static gboolean rn_to_sql (E2kRestriction *rn, GString *sql, E2kRestrictionType inside);

static const gchar *sql_relops[] = { "<", "<=", ">", ">=", "=", "!=" };
static const gint n_sql_relops = G_N_ELEMENTS (sql_relops);

static gboolean
rns_to_sql (E2kRestrictionType type, E2kRestriction **rns, gint nrns, GString *sql)
{
	gint i;
	gboolean need_op = FALSE;
	gboolean rv = FALSE;

	for (i = 0; i < nrns; i++) {
		if (need_op) {
			g_string_append (sql, type == E2K_RESTRICTION_AND ?
					 " AND " : " OR ");
			need_op = FALSE;
		}
		if (rn_to_sql (rns[i], sql, type)) {
			need_op = TRUE;
			rv = TRUE;
		}
	}
	return rv;
}

static void
append_sql_quoted (GString *sql, const gchar *string)
{
	while (*string) {
		if (*string == '\'')
			g_string_append (sql, "''");
		else
			g_string_append_c (sql, *string);
		string++;
	}
}

static gboolean
rn_to_sql (E2kRestriction *rn, GString *sql, E2kRestrictionType inside)
{
	E2kPropValue *pv;

	switch (rn->type) {
	case E2K_RESTRICTION_AND:
	case E2K_RESTRICTION_OR: {
		GString *subsql = g_string_new ("");
		gboolean rv;
		if ((rv = rns_to_sql (rn->type, rn->res.and.rns, rn->res.and.nrns, subsql))) {
			if (rn->type != inside)
				g_string_append (sql, "(");
			g_string_append (sql, subsql->str);
			if (rn->type != inside)
				g_string_append (sql, ")");
		}
		g_string_free (subsql, TRUE);

		return rv;
	}

	case E2K_RESTRICTION_NOT: {
		GString *subsql = g_string_new ("");
		gboolean rv;
		if ((rv = rn_to_sql (rn->res.not.rn, subsql, rn->type))) {
			g_string_append (sql, "NOT (");
			g_string_append (sql, subsql->str);
			g_string_append (sql, ")");
		}
		g_string_free (subsql, TRUE);

		return rv;
	}

	case E2K_RESTRICTION_CONTENT:
		pv = &rn->res.content.pv;
		g_string_append_printf (sql, "\"%s\" ", pv->prop.name);

		switch (E2K_FL_MATCH_TYPE (rn->res.content.fuzzy_level)) {
		case E2K_FL_SUFFIX:
			/* make suffix same as substring; it'll be checked for suffixes only later */
		case E2K_FL_SUBSTRING:
			g_string_append (sql, "LIKE '%");
			append_sql_quoted (sql, pv->value);
			g_string_append (sql, "%'");
			break;

		case E2K_FL_PREFIX:
			g_string_append (sql, "LIKE '");
			append_sql_quoted (sql, pv->value);
			g_string_append (sql, "%'");
			break;

		case E2K_FL_FULLSTRING:
		default:
			g_string_append (sql, "= '");
			append_sql_quoted (sql, pv->value);
			g_string_append_c (sql, '\'');
			break;
		}
		return TRUE;

	case E2K_RESTRICTION_PROPERTY:
		if (rn->res.property.relop >= n_sql_relops)
			return FALSE;

		pv = &rn->res.property.pv;
		g_string_append_printf (sql, "\"%s\" %s ", pv->prop.name,
					sql_relops[rn->res.property.relop]);

		switch (pv->type) {
		case E2K_PROP_TYPE_INT:
			g_string_append_printf (sql, "%d",
						GPOINTER_TO_UINT (pv->value));
			break;

		case E2K_PROP_TYPE_BOOL:
			g_string_append (sql, pv->value ? "True" : "False");
			break;

		case E2K_PROP_TYPE_DATE:
			g_string_append_printf (sql,
						"cast (\"%s\" as 'dateTime.tz')",
						(gchar *)pv->value);
			break;

		default:
			g_string_append_c (sql, '\'');
			append_sql_quoted (sql, pv->value);
			g_string_append_c (sql, '\'');
			break;
		}
		return TRUE;

	case E2K_RESTRICTION_COMPAREPROPS:
		if (rn->res.compare.relop >= n_sql_relops)
			return FALSE;

		g_string_append_printf (sql, "\"%s\" %s \"%s\"",
					rn->res.compare.prop1.name,
					sql_relops[rn->res.compare.relop],
					rn->res.compare.prop2.name);
		return TRUE;

	case E2K_RESTRICTION_COMMENT:
		return TRUE;

	case E2K_RESTRICTION_BITMASK:
	case E2K_RESTRICTION_EXIST:
	case E2K_RESTRICTION_SIZE:
	case E2K_RESTRICTION_SUBRESTRICTION:
	default:
		return FALSE;

	}
}

/**
 * e2k_restriction_to_sql:
 * @rn: a restriction
 *
 * Converts @rn to an SQL WHERE clause to be used with the WebDAV
 * SEARCH method. Note that certain restriction types cannot be used
 * in SQL, as mentioned in their descriptions above.
 *
 * If the restriction matches all objects, the return value will
 * be the empty string. Otherwise it will start with "WHERE ".
 *
 * Return value: the SQL WHERE clause, which the caller must free,
 * or %NULL if @rn could not be converted to SQL.
 **/
gchar *
e2k_restriction_to_sql (E2kRestriction *rn)
{
	GString *sql;
	gchar *ret;

	sql = g_string_new (NULL);
	if (!rn_to_sql (rn, sql, E2K_RESTRICTION_AND)) {
		g_string_free (sql, TRUE);
		return NULL;
	}

	if (sql->len)
		g_string_prepend (sql, "WHERE ");

	ret = sql->str;
	g_string_free (sql, FALSE);
	return ret;
}

/* Binary import/export */

static gboolean
extract_restriction (guint8 **data, gint *len, E2kRestriction **rn)
{
	gint type;

	if (*len == 0)
		return FALSE;
	type = (*data)[0];
	(*data)++;
	(*len)--;

	switch (type) {
	case E2K_RESTRICTION_AND:
	case E2K_RESTRICTION_OR:
	{
		E2kRestriction **rns;
		guint16 nrns;
		gint i;

		if (!e2k_rule_extract_uint16 (data, len, &nrns))
			return FALSE;
		rns = g_new0 (E2kRestriction *, nrns);
		for (i = 0; i < nrns; i++) {
			if (!extract_restriction (data, len, &rns[i])) {
				while (i--)
					e2k_restriction_unref (rns[i]);
				g_free (rns);
				return FALSE;
			}
		}

		*rn = conjoin (type, nrns, rns, TRUE);
		return TRUE;
	}

	case E2K_RESTRICTION_NOT:
	{
		E2kRestriction *subrn;

		if (!extract_restriction (data, len, &subrn))
			return FALSE;
		*rn = e2k_restriction_not (subrn, TRUE);
		return TRUE;
	}

	case E2K_RESTRICTION_CONTENT:
	{
		guint32 fuzzy_level;
		E2kRuleProp prop;
		E2kPropValue pv;

		if (!e2k_rule_extract_uint32 (data, len, &fuzzy_level) ||
		    !e2k_rule_extract_proptag (data, len, &prop) ||
		    !e2k_rule_extract_propvalue (data, len, &pv))
			return FALSE;

		pv.prop = prop;

		*rn = g_new0 (E2kRestriction, 1);
		(*rn)->type = type;
		(*rn)->res.content.fuzzy_level = fuzzy_level;
		(*rn)->res.content.pv = pv;
		return TRUE;
	}

	case E2K_RESTRICTION_PROPERTY:
	{
		guint8 relop;
		E2kRuleProp prop;
		E2kPropValue pv;

		if (!e2k_rule_extract_byte (data, len, &relop) ||
		    !e2k_rule_extract_proptag (data, len, &prop) ||
		    !e2k_rule_extract_propvalue (data, len, &pv))
			return FALSE;

		pv.prop = prop;

		*rn = g_new0 (E2kRestriction, 1);
		(*rn)->type = type;
		(*rn)->res.property.relop = relop;
		(*rn)->res.property.pv = pv;
		return TRUE;
	}

	case E2K_RESTRICTION_COMPAREPROPS:
	{
		/* FIXME */
		return FALSE;
	}

	case E2K_RESTRICTION_BITMASK:
	{
		guint8 bitop;
		guint32 mask;
		E2kRuleProp prop;

		if (!e2k_rule_extract_byte (data, len, &bitop) ||
		    !e2k_rule_extract_proptag (data, len, &prop) ||
		    !e2k_rule_extract_uint32 (data, len, &mask))
			return FALSE;

		*rn = g_new0 (E2kRestriction, 1);
		(*rn)->type = type;
		(*rn)->res.bitmask.bitop = bitop;
		(*rn)->res.bitmask.prop = prop;
		(*rn)->res.bitmask.mask = mask;
		return TRUE;
	}

	case E2K_RESTRICTION_SIZE:
	{
		/* FIXME */
		return FALSE;
	}

	case E2K_RESTRICTION_EXIST:
	{
		E2kRuleProp prop;

		if (!e2k_rule_extract_proptag (data, len, &prop))
			return FALSE;

		*rn = g_new0 (E2kRestriction, 1);
		(*rn)->type = type;
		(*rn)->res.exist.prop = prop;
		return TRUE;
	}

	case E2K_RESTRICTION_SUBRESTRICTION:
	{
		E2kRuleProp subtable;
		E2kRestriction *subrn;

		if (!e2k_rule_extract_proptag (data, len, &subtable) ||
		    !extract_restriction (data, len, &subrn))
			return FALSE;

		*rn = g_new0 (E2kRestriction, 1);
		(*rn)->type = type;
		(*rn)->res.sub.subtable = subtable;
		(*rn)->res.sub.rn = subrn;
		return TRUE;
	}

	case E2K_RESTRICTION_COMMENT:
	{
		guint8 nprops, dummy;
		E2kPropValue *props;
		gint i;

		if (!e2k_rule_extract_byte (data, len, &nprops))
			return FALSE;

		props = g_new0 (E2kPropValue, nprops);
		for (i = 0; i < nprops; i++) {
			if (!e2k_rule_extract_propvalue (data, len, &props[i])) {
				while (i--)
					e2k_rule_free_propvalue (&props[i]);
				g_free (props);
				return FALSE;
			}
		}

		*rn = g_new0 (E2kRestriction, 1);
		(*rn)->type = type;
		(*rn)->res.comment.nprops = nprops;
		(*rn)->res.comment.props = props;

		/* FIXME: There is always a "1" byte here, but I don't
		 * know why.
		 */
		if (!e2k_rule_extract_byte (data, len, &dummy) || dummy != 1) {
			e2k_restriction_unref (*rn);
			return FALSE;
		}

		if (!extract_restriction (data, len, &(*rn)->res.comment.rn)) {
			e2k_restriction_unref (*rn);
			return FALSE;
		}

		return TRUE;
	}

	default:
		return FALSE;
	}
}

/**
 * e2k_restriction_extract:
 * @data: pointer to data pointer
 * @len: pointer to data length
 * @rn: pointer to variable to store the extracted restriction in
 *
 * Attempts to extract a restriction from *@data, which contains
 * a binary-encoded restriction from a server-side rule.
 *
 * On success, *@rn will contain the extracted restriction, *@data
 * will be advanced past the end of the restriction data, and *@len
 * will be decremented accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_restriction_extract (guint8 **data, gint *len, E2kRestriction **rn)
{
	guint32 rnlen;

	if (!e2k_rule_extract_uint32 (data, len, &rnlen))
		return FALSE;
	if (rnlen > *len)
		return FALSE;

	if (rnlen == 1 && (*data)[0] == 0xFF) {
		(*data)++;
		(*len)--;
		*rn = NULL;
		return TRUE;
	}

	if (*len < 2)
		return FALSE;
	if ((*data)[0] != 0 || (*data)[1] != 0)
		return FALSE;
	(*data) += 2;
	(*len) -= 2;

	return extract_restriction (data, len, rn);
}

static void
append_restriction (GByteArray *ba, E2kRestriction *rn)
{
	gint i;

	e2k_rule_append_byte (ba, rn->type);

	switch (rn->type) {
	case E2K_RESTRICTION_AND:
	case E2K_RESTRICTION_OR:
		e2k_rule_append_uint16 (ba, rn->res.and.nrns);
		for (i = 0; i < rn->res.and.nrns; i++)
			append_restriction (ba, rn->res.and.rns[i]);
		break;

	case E2K_RESTRICTION_NOT:
		append_restriction (ba, rn->res.not.rn);
		break;

	case E2K_RESTRICTION_CONTENT:
		e2k_rule_append_uint32 (ba, rn->res.content.fuzzy_level);
		e2k_rule_append_proptag (ba, &rn->res.content.pv.prop);
		e2k_rule_append_propvalue (ba, &rn->res.content.pv);
		break;

	case E2K_RESTRICTION_PROPERTY:
		e2k_rule_append_byte (ba, rn->res.property.relop);
		e2k_rule_append_proptag (ba, &rn->res.property.pv.prop);
		e2k_rule_append_propvalue (ba, &rn->res.property.pv);
		break;

	case E2K_RESTRICTION_COMPAREPROPS:
		/* FIXME */
		break;

	case E2K_RESTRICTION_BITMASK:
		e2k_rule_append_byte (ba, rn->res.bitmask.bitop);
		e2k_rule_append_proptag (ba, &rn->res.bitmask.prop);
		e2k_rule_append_uint32 (ba, rn->res.bitmask.mask);
		break;

	case E2K_RESTRICTION_SIZE:
		break;

	case E2K_RESTRICTION_EXIST:
		e2k_rule_append_proptag (ba, &rn->res.exist.prop);
		break;

	case E2K_RESTRICTION_SUBRESTRICTION:
		e2k_rule_append_proptag (ba, &rn->res.sub.subtable);
		append_restriction (ba, rn->res.sub.rn);
		break;

	case E2K_RESTRICTION_COMMENT:
		e2k_rule_append_byte (ba, rn->res.comment.nprops);

		for (i = 0; i < rn->res.comment.nprops; i++)
			e2k_rule_append_propvalue (ba, &rn->res.comment.props[i]);

		/* FIXME: There is always a "1" byte here, but I don't
		 * know why.
		 */
		e2k_rule_append_byte (ba, 1);

		append_restriction (ba, rn->res.comment.rn);
		break;

	default:
		break;
	}
}

/**
 * e2k_restriction_append:
 * @ba: a buffer into which a server-side rule is being constructed
 * @rn: the restriction to append to @ba
 *
 * Appends @rn to @ba as part of a server-side rule.
 **/
void
e2k_restriction_append (GByteArray *ba, E2kRestriction *rn)
{
	gint rnlen_offset, rnlen;

	if (!rn) {
		e2k_rule_append_uint32 (ba, 1);
		e2k_rule_append_byte (ba, 0xFF);
		return;
	}

	/* Save space for the length field */
	rnlen_offset = ba->len;
	e2k_rule_append_uint32 (ba, 0);

	/* FIXME: ??? */
	e2k_rule_append_uint16 (ba, 0);

	append_restriction (ba, rn);

	rnlen = ba->len - rnlen_offset - 4;
	e2k_rule_write_uint32 (ba->data + rnlen_offset, rnlen);
}
