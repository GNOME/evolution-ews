/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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

#include <string.h>

#include "e2k-rule.h"
#include "e2k-action.h"
#include "e2k-properties.h"
#include "e2k-propnames.h"
#include "e2k-utils.h"

/**
 * e2k_rule_prop_set:
 * @prop: an #E2kRuleProp
 * @propname: a MAPI property name
 *
 * This is a convenience function to set both the %name and %proptag
 * fields of @prop.
 **/
void
e2k_rule_prop_set (E2kRuleProp *prop, const gchar *propname)
{
	prop->name = propname;
	prop->proptag = e2k_prop_proptag (propname);
}

/**
 * e2k_rule_write_uint32:
 * @ptr: pointer into a binary rule
 * @val: a uint32 value
 *
 * Writes @val into the rule at @ptr
 **/
void
e2k_rule_write_uint32 (guint8 *ptr, guint32 val)
{
	*ptr++ = ( val        & 0xFF);
	*ptr++ = ((val >>  8) & 0xFF);
	*ptr++ = ((val >> 16) & 0xFF);
	*ptr++ = ((val >> 24) & 0xFF);
}

/**
 * e2k_rule_append_uint32:
 * @ba: a byte array containing a binary rule
 * @val: a uint32 value
 *
 * Appends @val to the rule in @ba
 **/
void
e2k_rule_append_uint32 (GByteArray *ba, guint32 val)
{
	g_byte_array_set_size (ba, ba->len + 4);
	e2k_rule_write_uint32 (ba->data + ba->len - 4, val);
}

/**
 * e2k_rule_read_uint32:
 * @ptr: pointer into a binary rule
 *
 * Reads a uint32 value from the rule at @ptr
 *
 * Return value: the uint32 value
 **/
guint32
e2k_rule_read_uint32 (guint8 *ptr)
{
	return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

/**
 * e2k_rule_extract_uint32:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @val: pointer to a uint32 value
 *
 * Reads a uint32 value from the rule at **@ptr into *@val and updates
 * *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_uint32 (guint8 **ptr, gint *len, guint32 *val)
{
	if (*len < 4)
		return FALSE;

	*val = e2k_rule_read_uint32 (*ptr);

	*ptr += 4;
	*len -= 4;
	return TRUE;
}

/**
 * e2k_rule_write_uint16:
 * @ptr: pointer into a binary rule
 * @val: a uint16 value
 *
 * Writes @val into the rule at @ptr
 **/
void
e2k_rule_write_uint16 (guint8 *ptr, guint16 val)
{
	*ptr++ = ( val        & 0xFF);
	*ptr++ = ((val >>  8) & 0xFF);
}

/**
 * e2k_rule_append_uint16:
 * @ba: a byte array containing a binary rule
 * @val: a uint16 value
 *
 * Appends @val to the rule in @ba
 **/
void
e2k_rule_append_uint16 (GByteArray *ba, guint16 val)
{
	g_byte_array_set_size (ba, ba->len + 2);
	e2k_rule_write_uint16 (ba->data + ba->len - 2, val);
}

/**
 * e2k_rule_read_uint16:
 * @ptr: pointer into a binary rule
 *
 * Reads a uint16 value from the rule at @ptr
 *
 * Return value: the uint16 value
 **/
guint16
e2k_rule_read_uint16 (guint8 *ptr)
{
	return ptr[0] | (ptr[1] << 8);
}

/**
 * e2k_rule_extract_uint16:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @val: pointer to a uint16 value
 *
 * Reads a uint16 value from the rule at **@ptr into *@val and updates
 * *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_uint16 (guint8 **ptr, gint *len, guint16 *val)
{
	if (*len < 2)
		return FALSE;

	*val = e2k_rule_read_uint16 (*ptr);

	*ptr += 2;
	*len -= 2;
	return TRUE;
}

/**
 * e2k_rule_append_byte:
 * @ba: a byte array containing a binary rule
 * @val: a byte value
 *
 * Appends @val to the rule in @ba
 **/
void
e2k_rule_append_byte (GByteArray *ba, guint8 val)
{
	g_byte_array_append (ba, &val, 1);
}

/**
 * e2k_rule_extract_byte:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @val: pointer to a byte value
 *
 * Reads a byte value from the rule at **@ptr into *@val and updates
 * *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_byte (guint8 **ptr, gint *len, guint8 *val)
{
	if (*len < 1)
		return FALSE;

	*val = **ptr;

	*ptr += 1;
	*len -= 1;
	return TRUE;
}

/**
 * e2k_rule_append_string:
 * @ba: a byte array containing a binary rule
 * @str: a (Windows) locale-encoded string
 *
 * Appends @str to the rule in @ba
 **/
void
e2k_rule_append_string (GByteArray *ba, const gchar *str)
{
	/* FIXME: verify encoding */
	g_byte_array_append (ba, (guint8 *) str, strlen (str) + 1);
}

/**
 * e2k_rule_extract_string:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @str: pointer to a string pointer
 *
 * Reads a (Windows) locale-encoded string from the rule at **@ptr
 * into *@str and updates *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_string (guint8 **ptr, gint *len, gchar **str)
{
	gint slen;

	for (slen = 0; slen < *len; slen++) {
		if ((*ptr)[slen] == '\0') {
			*str = g_strdup ((gchar *) *ptr);
			*ptr += slen + 1;
			*len -= slen + 1;
			return TRUE;
		}
	}

	return FALSE;
}

/**
 * e2k_rule_append_unicode:
 * @ba: a byte array containing a binary rule
 * @str: a UTF-8 string
 *
 * Appends @str to the rule in @ba
 **/
void
e2k_rule_append_unicode (GByteArray *ba, const gchar *str)
{
	gunichar2 *utf16;
	gint i;

	utf16 = g_utf8_to_utf16 (str, -1, NULL, NULL, NULL);
	g_return_if_fail (utf16 != NULL);

	for (i = 0; utf16[i]; i++)
		e2k_rule_append_uint16 (ba, utf16[i]);
	e2k_rule_append_uint16 (ba, 0);
	g_free (utf16);
}

/**
 * e2k_rule_extract_unicode:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @str: pointer to a string pointer
 *
 * Reads a Unicode-encoded string from the rule at **@ptr into *@str
 * and updates *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_unicode (guint8 **ptr, gint *len, gchar **str)
{
	guint8 *start, *end;
	gunichar2 *utf16;

	start = *ptr;
	end = *ptr + *len;

	for (; *ptr < end - 1; (*ptr) += 2) {
		if ((*ptr)[0] == '\0' && (*ptr)[1] == '\0') {
			*ptr += 2;
			*len -= *ptr - start;

			utf16 = g_memdup (start, *ptr - start);
			*str = g_utf16_to_utf8 (utf16, -1, NULL, NULL, NULL);
			g_free (utf16);
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * e2k_rule_append_binary:
 * @ba: a byte array containing a binary rule
 * @data: binary data
 *
 * Appends @data (with a 2-byte length prefix) to the rule in @ba
 **/
void
e2k_rule_append_binary (GByteArray *ba, GByteArray *data)
{
	e2k_rule_append_uint16 (ba, data->len);
	g_byte_array_append (ba, data->data, data->len);
}

/**
 * e2k_rule_extract_binary:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @data: pointer to a #GByteArray
 *
 * Reads binary data (preceded by a 2-byte length) from the rule at
 * **@ptr into *@data and updates *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_binary (guint8 **ptr, gint *len, GByteArray **data)
{
	guint16 datalen;

	if (!e2k_rule_extract_uint16 (ptr, len, &datalen))
		return FALSE;
	if (*len < datalen)
		return FALSE;

	*data = g_byte_array_sized_new (datalen);
	memcpy ((*data)->data, *ptr, datalen);
	(*data)->len = datalen;

	*ptr += datalen;
	*len -= datalen;
	return TRUE;
}

#define E2K_PT_UNICODE_RULE 0x84b0

/**
 * e2k_rule_append_proptag:
 * @ba: a byte array containing a binary rule
 * @prop: an #E2kRuleProp
 *
 * Appends a representation of @prop to the rule in @ba
 **/
void
e2k_rule_append_proptag (GByteArray *ba, E2kRuleProp *prop)
{
	guint32 proptag = prop->proptag;

	if (E2K_PROPTAG_TYPE (proptag) == E2K_PT_STRING8 ||
	    E2K_PROPTAG_TYPE (proptag) == E2K_PT_UNICODE)
		proptag = E2K_PROPTAG_ID (proptag) | E2K_PT_UNICODE_RULE;

	e2k_rule_append_uint32 (ba, proptag);
}

/**
 * e2k_rule_extract_proptag:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @prop: poitner to an #E2kRuleProp
 *
 * Reads a proptag from the rule at **@ptr into *@prop and updates
 * *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_proptag (guint8 **ptr, gint *len, E2kRuleProp *prop)
{
	if (!e2k_rule_extract_uint32 (ptr, len, &prop->proptag))
		return FALSE;

	if (E2K_PROPTAG_TYPE (prop->proptag) == E2K_PT_UNICODE_RULE)
		prop->proptag = E2K_PROPTAG_ID (prop->proptag) | E2K_PT_UNICODE;
	prop->name = e2k_proptag_prop (prop->proptag);

	return TRUE;
}

/**
 * e2k_rule_append_propvalue:
 * @ba: a byte array containing a binary rule
 * @pv: an #E2kPropValue
 *
 * Appends a representation of @pv (the proptag and its value) to the
 * rule in @ba
 **/
void
e2k_rule_append_propvalue (GByteArray *ba, E2kPropValue *pv)
{
	g_return_if_fail (pv->prop.proptag != 0);

	e2k_rule_append_proptag (ba, &pv->prop);

	switch (E2K_PROPTAG_TYPE (pv->prop.proptag)) {
	case E2K_PT_UNICODE:
	case E2K_PT_STRING8:
		e2k_rule_append_unicode (ba, pv->value);
		break;

	case E2K_PT_BINARY:
		e2k_rule_append_binary (ba, pv->value);
		break;

	case E2K_PT_LONG:
		e2k_rule_append_uint32 (ba, GPOINTER_TO_UINT (pv->value));
		break;

	case E2K_PT_BOOLEAN:
		e2k_rule_append_byte (ba, GPOINTER_TO_UINT (pv->value));
		break;

	default:
		/* FIXME */
		break;
	}
}

/**
 * e2k_rule_extract_propvalue:
 * @ptr: pointer to a pointer into a binary rule
 * @len: pointer to the remaining length of *@ptr
 * @pv: pointer to an #E2kPropValue
 *
 * Reads a representation of an #E2kPropValue from the rule at **@ptr
 * into *@pv and updates *@ptr and *@len accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_rule_extract_propvalue (guint8 **ptr, gint *len, E2kPropValue *pv)
{
	if (!e2k_rule_extract_proptag (ptr, len, &pv->prop))
		return FALSE;

	switch (E2K_PROPTAG_TYPE (pv->prop.proptag)) {
	case E2K_PT_UNICODE:
	case E2K_PT_STRING8:
		pv->type = E2K_PROP_TYPE_STRING;
		return e2k_rule_extract_unicode (ptr, len, (gchar **)&pv->value);

	case E2K_PT_BINARY:
		pv->type = E2K_PROP_TYPE_BINARY;
		return e2k_rule_extract_binary (ptr, len, (GByteArray **)&pv->value);

	case E2K_PT_SYSTIME:
	{
		guint64 temp;

		if (*len < 8)
			return FALSE;

		memcpy (&temp, *ptr, 8);
		*ptr += 8;
		*len -= 8;

		temp = GUINT64_FROM_LE (temp);
		pv->type = E2K_PROP_TYPE_DATE;
		pv->value = e2k_make_timestamp (e2k_filetime_to_time_t (temp));
		return TRUE;
	}

	case E2K_PT_LONG:
	{
		guint32 temp;

		if (!e2k_rule_extract_uint32 (ptr, len, &temp))
			return FALSE;
		pv->type = E2K_PROP_TYPE_INT;
		pv->value = GUINT_TO_POINTER (temp);
		return TRUE;
	}

	case E2K_PT_BOOLEAN:
	{
		guint8 temp;

		if (!e2k_rule_extract_byte (ptr, len, &temp))
			return FALSE;
		pv->type = E2K_PROP_TYPE_BOOL;
		pv->value = GUINT_TO_POINTER ((guint)temp);
		return TRUE;
	}

	default:
		/* FIXME */
		return FALSE;
	}
}

/**
 * e2k_rule_free_propvalue:
 * @pv: an #E2kPropValue
 *
 * Frees @pv
 **/
void
e2k_rule_free_propvalue (E2kPropValue *pv)
{
	if (pv->type == E2K_PROP_TYPE_STRING ||
	    pv->type == E2K_PROP_TYPE_DATE)
		g_free (pv->value);
	else if (pv->type == E2K_PROP_TYPE_BINARY && pv->value)
		g_byte_array_free (pv->value, TRUE);
}

/**
 * e2k_rule_free:
 * @rule: an #E2kRule
 *
 * Frees @rule
 **/
void
e2k_rule_free (E2kRule *rule)
{
	if (rule->name)
		g_free (rule->name);
	if (rule->condition)
		e2k_restriction_unref (rule->condition);
	if (rule->actions)
		e2k_actions_free (rule->actions);
	if (rule->provider)
		g_free (rule->provider);
	if (rule->provider_data)
		g_byte_array_free (rule->provider_data, TRUE);
}

/**
 * e2k_rules_free:
 * @rules: an #E2kRules structure
 *
 * Frees @rules and the rules it contains
 **/
void
e2k_rules_free (E2kRules *rules)
{
	gint i;

	for (i = 0; i < rules->rules->len; i++)
		e2k_rule_free (rules->rules->pdata[i]);
	g_ptr_array_free (rules->rules, TRUE);
	g_free (rules);
}

/**
 * e2k_rules_from_binary:
 * @rules_data: binary-encoded rules data
 *
 * Extract rules from @rules_data and returns them in an #E2kRules
 * structure.
 *
 * Return value: the rules, or %NULL on error.
 **/
E2kRules *
e2k_rules_from_binary (GByteArray *rules_data)
{
	guint8 *data;
	gint len, i;
	guint32 nrules, pdlen;
	E2kRules *rules;
	E2kRule *rule;

	data = rules_data->data;
	len = rules_data->len;

	if (len < 9)
		return NULL;
	if (*data != 2)
		return NULL;
	data++;
	len--;

	rules = g_new0 (E2kRules, 1);
	rules->version = 2;

	if (!e2k_rule_extract_uint32 (&data, &len, &nrules) ||
	    !e2k_rule_extract_uint32 (&data, &len, &rules->codepage)) {
		g_free (rules);
		return NULL;
	}

	rules->rules = g_ptr_array_new ();
	for (i = 0; i < nrules; i++) {
		rule = g_new0 (E2kRule, 1);
		g_ptr_array_add (rules->rules, rule);

		if (!e2k_rule_extract_uint32 (&data, &len, &rule->sequence) ||
		    !e2k_rule_extract_uint32 (&data, &len, &rule->state) ||
		    !e2k_rule_extract_uint32 (&data, &len, &rule->user_flags) ||
		    !e2k_rule_extract_uint32 (&data, &len, &rule->condition_lcid) ||
		    !e2k_restriction_extract (&data, &len, &rule->condition) ||
		    !e2k_actions_extract (&data, &len, &rule->actions) ||
		    !e2k_rule_extract_string (&data, &len, &rule->provider) ||
		    !e2k_rule_extract_string (&data, &len, &rule->name) ||
		    !e2k_rule_extract_uint32 (&data, &len, &rule->level))
			goto error;

		/* The provider data has a 4-byte length, unlike the
		 * binary fields in a condition or rule.
		 */
		if (!e2k_rule_extract_uint32 (&data, &len, &pdlen))
			goto error;
		if (len < pdlen)
			goto error;
		rule->provider_data = g_byte_array_sized_new (pdlen);
		rule->provider_data->len = pdlen;
		memcpy (rule->provider_data->data, data, pdlen);
		data += pdlen;
		len -= pdlen;
	}

	return rules;

 error:
	e2k_rules_free (rules);
	return NULL;
}

/**
 * e2k_rules_to_binary:
 * @rules: an #E2kRules structure
 *
 * Encodes @rules into binary form
 *
 * Return value: the binary-encoded rules
 **/
GByteArray *
e2k_rules_to_binary (E2kRules *rules)
{
	GByteArray *ba;
	E2kRule *rule;
	gint i;

	ba = g_byte_array_new ();
	e2k_rule_append_byte (ba, rules->version);
	e2k_rule_append_uint32 (ba, rules->rules->len);
	e2k_rule_append_uint32 (ba, rules->codepage);

	for (i = 0; i < rules->rules->len; i++) {
		rule = rules->rules->pdata[i];

		e2k_rule_append_uint32 (ba, rule->sequence);
		e2k_rule_append_uint32 (ba, rule->state);
		e2k_rule_append_uint32 (ba, rule->user_flags);
		e2k_rule_append_uint32 (ba, rule->condition_lcid);
		e2k_restriction_append (ba, rule->condition);
		e2k_actions_append (ba, rule->actions);
		e2k_rule_append_string (ba, rule->provider);
		e2k_rule_append_string (ba, rule->name);
		e2k_rule_append_uint32 (ba, rule->level);

		/* The provider data has a 4-byte length, unlike the
		 * binary fields in a condition or rule.
		 */
		e2k_rule_append_uint32 (ba, rule->provider_data->len);
		g_byte_array_append (ba, rule->provider_data->data,
				     rule->provider_data->len);
	}

	return ba;
}
