/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

/* e2k-action.c: Exchange server-side rule actions */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e2k-action.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-rule.h"
#include "e2k-utils.h"
#include "mapi.h"

/* The apparently-constant store entryid prefix for a move or copy action */
#define E2K_ACTION_XFER_STORE_ENTRYID_PREFIX "\x00\x00\x00\x00\x38\xa1\xbb\x10\x05\xe5\x10\x1a\xa1\xbb\x08\x00\x2b\x2a\x56\xc2\x00\x00\x45\x4d\x53\x4d\x44\x42\x2e\x44\x4c\x4c\x00\x00\x00\x00"
#define E2K_ACTION_XFER_STORE_ENTRYID_PREFIX_LEN (sizeof (E2K_ACTION_XFER_STORE_ENTRYID_PREFIX) - 1)

static GByteArray *
copy_bytearray (GByteArray *ba)
{
	GByteArray *copy;

	copy = g_byte_array_sized_new (ba->len);
	copy->len = ba->len;
	memcpy (copy->data, ba->data, copy->len);

	return copy;
}

static E2kAction *
xfer_action (E2kActionType type, GByteArray *store_entryid,
	     GByteArray *folder_source_key)
{
	E2kAction *act;

	act = g_new0 (E2kAction, 1);
	act->type = type;
	act->act.xfer.store_entryid = copy_bytearray (store_entryid);
	act->act.xfer.folder_source_key = copy_bytearray (folder_source_key);

	return act;
}

/**
 * e2k_action_move:
 * @store_entryid: The PR_STORE_ENTRYID of the message store
 * @folder_source_key: The PR_SOURCE_KEY of a folder in that store
 *
 * Creates a rule action to move a message into the indicated folder
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_move (GByteArray *store_entryid, GByteArray *folder_source_key)
{
	return xfer_action (E2K_ACTION_MOVE, store_entryid, folder_source_key);
}

/**
 * e2k_action_copy:
 * @store_entryid: The PR_STORE_ENTRYID of the message store
 * @folder_source_key: The PR_SOURCE_KEY of a folder in that store
 *
 * Creates a rule action to copy a message into the indicated folder
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_copy (GByteArray *store_entryid, GByteArray *folder_source_key)
{
	return xfer_action (E2K_ACTION_COPY, store_entryid, folder_source_key);
}

static E2kAction *
reply_action (E2kActionType type, GByteArray *template_entryid,
	      guint8 template_guid[16])
{
	E2kAction *act;

	act = g_new0 (E2kAction, 1);
	act->type = type;
	act->act.reply.entryid = copy_bytearray (template_entryid);
	memcpy (act->act.reply.reply_template_guid, template_guid, 16);

	return act;
}

/**
 * e2k_action_reply:
 * @template_entryid: The entryid of the reply template
 * @template_guid: The GUID of the reply template
 *
 * Creates a rule action to reply to a message using the indicated
 * template
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_reply (GByteArray *template_entryid, guint8 template_guid[16])
{
	return reply_action (E2K_ACTION_REPLY, template_entryid, template_guid);
}

/**
 * e2k_action_oof_reply:
 * @template_entryid: The entryid of the reply template
 * @template_guid: The GUID of the reply template
 *
 * Creates a rule action to send an Out-of-Office reply to a message
 * using the indicated template
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_oof_reply (GByteArray *template_entryid, guint8 template_guid[16])
{
	return reply_action (E2K_ACTION_OOF_REPLY, template_entryid, template_guid);
}

/**
 * e2k_action_defer:
 * @data: data identifying the deferred action
 *
 * Creates a rule action to defer processing on a message
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_defer (GByteArray *data)
{
	E2kAction *act;

	act = g_new0 (E2kAction, 1);
	act->type = E2K_ACTION_DEFER;
	act->act.defer_data = copy_bytearray (data);

	return act;
}

/**
 * e2k_action_bounce:
 * @bounce_code: a bounce code
 *
 * Creates a rule action to bounce a message
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_bounce (E2kActionBounceCode bounce_code)
{
	E2kAction *act;

	act = g_new0 (E2kAction, 1);
	act->type = E2K_ACTION_BOUNCE;
	act->act.bounce_code = bounce_code;

	return act;
}

static E2kAction *
forward_action (E2kActionType type, E2kAddrList *list)
{
	E2kAction *act;

	g_return_val_if_fail (type == E2K_ACTION_FORWARD || type == E2K_ACTION_DELEGATE, NULL);
	g_return_val_if_fail (list->nentries > 0, NULL);

	act = g_new0 (E2kAction, 1);
	act->type = type;
	act->act.addr_list = list;

	return act;
}

/**
 * e2k_action_forward:
 * @list: a list of recipients
 *
 * Creates a rule action to forward a message to the indicated list of
 * recipients
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_forward (E2kAddrList *list)
{
	return forward_action (E2K_ACTION_FORWARD, list);
}

/**
 * e2k_action_delegate:
 * @list: a list of recipients
 *
 * Creates a rule action to delegate a meeting request to the
 * indicated list of recipients
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_delegate (E2kAddrList *list)
{
	return forward_action (E2K_ACTION_DELEGATE, list);
}

/**
 * e2k_action_tag:
 * @propname: a MAPI property name
 * @type: the type of @propname
 * @value: the value for @propname
 *
 * Creates a rule action to set the given property to the given value
 * on a message.
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_tag (const gchar *propname, E2kPropType type, gpointer value)
{
	E2kAction *act;

	act = g_new0 (E2kAction, 1);
	act->type = E2K_ACTION_TAG;
	e2k_rule_prop_set (&act->act.proptag.prop, propname);
	act->act.proptag.type = type;
	act->act.proptag.value = value; /* FIXME: copy? */

	return act;
}

/**
 * e2k_action_delete:
 *
 * Creates a rule action to permanently delete a message (ie, not just
 * move it to the trash).
 *
 * Return value: the new rule action
 **/
E2kAction *
e2k_action_delete (void)
{
	E2kAction *act;

	act = g_new0 (E2kAction, 1);
	act->type = E2K_ACTION_DELETE;

	return act;
}

/**
 * e2k_addr_list_new:
 * @nentries: the number of entries
 *
 * Creates an address list for a forward or delegate rule, with
 * @nentries slots
 *
 * Return value: the new address list
 **/
E2kAddrList *
e2k_addr_list_new (gint nentries)
{
	E2kAddrList *list;

	list = g_malloc0 (sizeof (E2kAddrList) +
			  (nentries - 1) * sizeof (E2kAddrEntry));
	list->nentries = nentries;

	return list;
}

static void
addr_entry_set_core (E2kPropValue *pv, GByteArray *entryid,
		     const gchar *display_name, const gchar *email_type,
		     const gchar *email_addr)
{
	e2k_rule_prop_set (&pv[0].prop, PR_ENTRYID);
	pv[0].type = E2K_PROP_TYPE_BINARY;
	pv[0].value = entryid;

	e2k_rule_prop_set (&pv[1].prop, PR_DISPLAY_NAME);
	pv[1].type = E2K_PROP_TYPE_STRING;
	pv[1].value = g_strdup (display_name);

	e2k_rule_prop_set (&pv[2].prop, PR_OBJECT_TYPE);
	pv[2].type = E2K_PROP_TYPE_INT;
	pv[2].value = GINT_TO_POINTER (MAPI_MAILUSER);

	e2k_rule_prop_set (&pv[3].prop, PR_DISPLAY_TYPE);
	pv[3].type = E2K_PROP_TYPE_INT;
	pv[3].value = GINT_TO_POINTER (DT_MAILUSER);

	e2k_rule_prop_set (&pv[4].prop, PR_TRANSMITTABLE_DISPLAY_NAME);
	pv[4].type = E2K_PROP_TYPE_STRING;
	pv[4].value = g_strdup (display_name);

	e2k_rule_prop_set (&pv[5].prop, PR_EMAIL_ADDRESS);
	pv[5].type = E2K_PROP_TYPE_STRING;
	pv[5].value = g_strdup (email_addr);

	e2k_rule_prop_set (&pv[6].prop, PR_ADDRTYPE);
	pv[6].type = E2K_PROP_TYPE_STRING;
	pv[6].value = g_strdup (email_type);

	e2k_rule_prop_set (&pv[7].prop, PR_SEND_INTERNET_ENCODING);
	pv[7].type = E2K_PROP_TYPE_INT;
	pv[7].value = GINT_TO_POINTER (0); /* "Let transport decide" */

	e2k_rule_prop_set (&pv[8].prop, PR_RECIPIENT_TYPE);
	pv[8].type = E2K_PROP_TYPE_INT;
	pv[8].value = GINT_TO_POINTER (MAPI_TO);

	e2k_rule_prop_set (&pv[9].prop, PR_SEARCH_KEY);
	pv[9].type = E2K_PROP_TYPE_BINARY;
	pv[9].value = e2k_search_key_generate (email_type, email_addr);
}

/**
 * e2k_addr_list_set_local:
 * @list: the address list
 * @entry_num: the list entry to set
 * @display_name: the UTF-8 display name of the recipient
 * @exchange_dn: the Exchange 5.5-style DN of the recipient
 * @email: the SMTP email address of the recipient
 *
 * Sets entry number @entry_num of @list to refer to the indicated
 * local Exchange user.
 **/
void
e2k_addr_list_set_local (E2kAddrList *list, gint entry_num,
			 const gchar *display_name,
			 const gchar *exchange_dn,
			 const gchar *email)
{
	E2kPropValue *pv;

	list->entry[entry_num].nvalues = 12;
	list->entry[entry_num].propval = pv = g_new0 (E2kPropValue, 12);

	addr_entry_set_core (pv, e2k_entryid_generate_local (exchange_dn),
			     display_name, "EX", exchange_dn);

	e2k_rule_prop_set (&pv[10].prop, PR_EMS_AB_DISPLAY_NAME_PRINTABLE);
	pv[10].type = E2K_PROP_TYPE_STRING;
	pv[10].value = g_strdup ("FIXME");

	e2k_rule_prop_set (&pv[11].prop, PR_SMTP_ADDRESS);
	pv[11].type = E2K_PROP_TYPE_STRING;
	pv[11].value = g_strdup (email);
}

/**
 * e2k_addr_list_set_oneoff:
 * @list: the address list
 * @entry_num: the list entry to set
 * @display_name: the UTF-8 display name of the recipient
 * @email: the SMTP email address of the recipient
 *
 * Sets entry number @entry_num of @list to refer to the indicated
 * "one-off" SMTP user.
 **/
void
e2k_addr_list_set_oneoff (E2kAddrList *list, gint entry_num,
			  const gchar *display_name, const gchar *email)
{
	E2kPropValue *pv;

	list->entry[entry_num].nvalues = 12;
	list->entry[entry_num].propval = pv = g_new0 (E2kPropValue, 12);

	addr_entry_set_core (pv, e2k_entryid_generate_oneoff (display_name, email, TRUE),
			     display_name, "SMTP", email);

	e2k_rule_prop_set (&pv[10].prop, PR_SEND_RICH_INFO);
	pv[10].type = E2K_PROP_TYPE_BOOL;
	pv[10].value = GINT_TO_POINTER (FALSE);

	e2k_rule_prop_set (&pv[11].prop, PR_RECORD_KEY);
	pv[11].type = E2K_PROP_TYPE_BINARY;
	pv[11].value = e2k_entryid_generate_oneoff (display_name, email, FALSE);
}

/**
 * e2k_addr_list_free:
 * @list: the address list
 *
 * Frees @list and all its entries.
 **/
void
e2k_addr_list_free (E2kAddrList *list)
{
	gint i, j;
	E2kAddrEntry *entry;

	for (i = 0; i < list->nentries; i++) {
		entry = &list->entry[i];

		for (j = 0; j < entry->nvalues; j++)
			e2k_rule_free_propvalue (&entry->propval[j]);
		g_free (entry->propval);
	}
	g_free (list);
}

/**
 * e2k_action_free:
 * @act: the action
 *
 * Frees @act
 **/
void
e2k_action_free (E2kAction *act)
{
	switch (act->type) {
	case E2K_ACTION_MOVE:
	case E2K_ACTION_COPY:
		if (act->act.xfer.store_entryid)
			g_byte_array_free (act->act.xfer.store_entryid, TRUE);
		if (act->act.xfer.folder_source_key)
			g_byte_array_free (act->act.xfer.folder_source_key, TRUE);
		break;

	case E2K_ACTION_REPLY:
	case E2K_ACTION_OOF_REPLY:
		if (act->act.reply.entryid)
			g_byte_array_free (act->act.reply.entryid, TRUE);
		break;

	case E2K_ACTION_DEFER:
		if (act->act.defer_data)
			g_byte_array_free (act->act.defer_data, TRUE);
		break;

	case E2K_ACTION_FORWARD:
	case E2K_ACTION_DELEGATE:
		if (act->act.addr_list)
			e2k_addr_list_free (act->act.addr_list);
		break;

	case E2K_ACTION_TAG:
		e2k_rule_free_propvalue (&act->act.proptag);
		break;

	default:
		/* Nothing to free */
		break;
	}

	g_free (act);
}

/**
 * e2k_actions_free:
 * @actions: an array of #E2kAction
 *
 * Frees @actions and all of its elements
 **/
void
e2k_actions_free (GPtrArray *actions)
{
	gint i;

	for (i = 0; i < actions->len; i++)
		e2k_action_free (actions->pdata[i]);
	g_ptr_array_free (actions, TRUE);
}

static gboolean
extract_action (guint8 **data, gint *len, E2kAction **act_ret)
{
	gint my_len;
	guint8 *my_data;
	guint16 actlen;
	E2kAction *act;

	if (!e2k_rule_extract_uint16 (data, len, &actlen))
		return FALSE;

	my_data = *data;
	my_len = actlen;

	*data += actlen;
	*len -= actlen;

	data = &my_data;
	len = &my_len;

	if (*len < 1)
		return FALSE;

	act = g_new0 (E2kAction, 1);
	act->type = **data;
	(*data)++;
	(*len)--;

	if (!e2k_rule_extract_uint32 (data, len, &act->flavor))
		goto lose;
	if (!e2k_rule_extract_uint32 (data, len, &act->flags))
		goto lose;

	switch (act->type) {
	case E2K_ACTION_MOVE:
	case E2K_ACTION_COPY:
		/* FIXME: what is this? */
		if (*len < 1 || **data != 1)
			goto lose;
		(*len)--;
		(*data)++;

		if (!e2k_rule_extract_binary (data, len, &act->act.xfer.store_entryid))
			goto lose;
		/* Remove the constant part */
		if (act->act.xfer.store_entryid->len <= E2K_ACTION_XFER_STORE_ENTRYID_PREFIX_LEN ||
		    memcmp (act->act.xfer.store_entryid->data,
			    E2K_ACTION_XFER_STORE_ENTRYID_PREFIX,
			    E2K_ACTION_XFER_STORE_ENTRYID_PREFIX_LEN) != 0)
			goto lose;
		act->act.xfer.store_entryid->len -=
			E2K_ACTION_XFER_STORE_ENTRYID_PREFIX_LEN;
		memmove (act->act.xfer.store_entryid->data,
			 act->act.xfer.store_entryid->data +
			 E2K_ACTION_XFER_STORE_ENTRYID_PREFIX_LEN,
			 act->act.xfer.store_entryid->len);

		if (!e2k_rule_extract_binary (data, len, &act->act.xfer.folder_source_key))
			goto lose;
		/* Likewise */
		if (act->act.xfer.folder_source_key->len < 1 ||
		    act->act.xfer.folder_source_key->data[0] != MAPI_FOLDER)
			goto lose;
		memmove (act->act.xfer.folder_source_key->data,
			 act->act.xfer.folder_source_key->data + 1,
			 act->act.xfer.folder_source_key->len);

		*act_ret = act;
		return TRUE;

	case E2K_ACTION_REPLY:
	case E2K_ACTION_OOF_REPLY:
		/* The reply template GUID is 16 bytes, the entryid
		 * is the rest.
		 */
		if (*len <= 16)
			goto lose;

		act->act.reply.entryid = g_byte_array_sized_new (*len - 16);
		memcpy (act->act.reply.entryid->data, *data, *len - 16);
		act->act.reply.entryid->len = *len - 16;
		memcpy (act->act.reply.reply_template_guid, *data + *len - 16, 16);

		*act_ret = act;
		return TRUE;

	case E2K_ACTION_DEFER:
		act->act.defer_data = g_byte_array_sized_new (*len);
		memcpy (act->act.defer_data->data, *data, *len);
		act->act.defer_data->len = *len;

		*act_ret = act;
		return TRUE;

	case E2K_ACTION_BOUNCE:
		if (!e2k_rule_extract_uint32 (data, len, &act->act.bounce_code))
			goto lose;

		*act_ret = act;
		return TRUE;

	case E2K_ACTION_FORWARD:
	case E2K_ACTION_DELEGATE:
	{
		guint16 nentries, nvalues;
		gint i, j;

		if (!e2k_rule_extract_uint16 (data, len, &nentries))
			goto lose;
		act->act.addr_list = e2k_addr_list_new (nentries);
		for (i = 0; i < nentries; i++) {
			/* FIXME: what is this? */
			if (*len < 1 || **data != 1)
				goto lose;
			(*len)--;
			(*data)++;

			if (!e2k_rule_extract_uint16 (data, len, &nvalues))
				goto lose;
			act->act.addr_list->entry[i].nvalues = nvalues;
			act->act.addr_list->entry[i].propval = g_new0 (E2kPropValue, nvalues);

			for (j = 0; j < nvalues; j++) {
				if (!e2k_rule_extract_propvalue (data, len, &act->act.addr_list->entry[i].propval[j]))
					goto lose;
			}
		}

		*act_ret = act;
		return TRUE;
	}

	case E2K_ACTION_TAG:
		if (!e2k_rule_extract_propvalue (data, len, &act->act.proptag))
			goto lose;

		*act_ret = act;
		return TRUE;

	case E2K_ACTION_DELETE:
		*act_ret = act;
		return TRUE;

	case E2K_ACTION_MARK_AS_READ:
		/* FIXME */
		return FALSE;

	default:
		break;
	}

 lose:
	e2k_action_free (act);
	return FALSE;
}

/**
 * e2k_actions_extract:
 * @data: pointer to data pointer
 * @len: pointer to data length
 * @actions: pointer to array to store actions in
 *
 * Attempts to extract a list of actions from *@data, which contains a
 * binary-encoded list of actions from a server-side rule.
 *
 * On success, *@actions will contain the extracted list, *@data will
 * be advanced past the end of the restriction data, and *@len will be
 * decremented accordingly.
 *
 * Return value: success or failure
 **/
gboolean
e2k_actions_extract (guint8 **data, gint *len, GPtrArray **actions)
{
	GPtrArray *acts;
	E2kAction *act;
	guint32 actlen;
	guint16 nacts;
	gint i;

	if (!e2k_rule_extract_uint32 (data, len, &actlen))
		return FALSE;
	if (actlen > *len)
		return FALSE;

	if (!e2k_rule_extract_uint16 (data, len, &nacts))
		return FALSE;

	acts = g_ptr_array_new ();
	for (i = 0; i < nacts; i++) {
		if (!extract_action (data, len, &act)) {
			e2k_actions_free (acts);
			return FALSE;
		} else
			g_ptr_array_add (acts, act);
	}

	*actions = acts;
	return TRUE;
}

static void
append_action (GByteArray *ba, E2kAction *act)
{
	gint actlen_offset, actlen;
	gchar type;

	/* Save space for length */
	actlen_offset = ba->len;
	e2k_rule_append_uint16 (ba, 0);

	e2k_rule_append_byte (ba, act->type);
	e2k_rule_append_uint32 (ba, act->flavor);
	e2k_rule_append_uint32 (ba, act->flags);

	switch (act->type) {
	case E2K_ACTION_MOVE:
	case E2K_ACTION_COPY:
		/* FIXME: what is this? */
		e2k_rule_append_byte (ba, 1);

		e2k_rule_append_uint16 (ba, act->act.xfer.store_entryid->len +
					E2K_ACTION_XFER_STORE_ENTRYID_PREFIX_LEN);
		g_byte_array_append (ba, (guint8 *) E2K_ACTION_XFER_STORE_ENTRYID_PREFIX,
				     E2K_ACTION_XFER_STORE_ENTRYID_PREFIX_LEN);
		g_byte_array_append (ba, act->act.xfer.store_entryid->data,
				     act->act.xfer.store_entryid->len);

		e2k_rule_append_uint16 (ba, 49);
		type = MAPI_FOLDER;
		g_byte_array_append (ba, (guint8 *) &type, 1);
		g_byte_array_append (ba, act->act.xfer.folder_source_key->data,
				     act->act.xfer.folder_source_key->len);
		break;

	case E2K_ACTION_REPLY:
	case E2K_ACTION_OOF_REPLY:
		g_byte_array_append (ba, act->act.reply.entryid->data,
				     act->act.reply.entryid->len);
		g_byte_array_append (ba, act->act.reply.reply_template_guid, 16);
		break;

	case E2K_ACTION_DEFER:
		g_byte_array_append (ba, act->act.defer_data->data,
				     act->act.defer_data->len);
		break;

	case E2K_ACTION_BOUNCE:
		e2k_rule_append_uint32 (ba, act->act.bounce_code);
		break;

	case E2K_ACTION_FORWARD:
	case E2K_ACTION_DELEGATE:
	{
		gint i, j;
		E2kAddrList *list;
		E2kAddrEntry *entry;

		list = act->act.addr_list;
		e2k_rule_append_uint16 (ba, list->nentries);
		for (i = 0; i < list->nentries; i++) {
			/* FIXME: what is this? */
			e2k_rule_append_byte (ba, 1);

			entry = &list->entry[i];
			e2k_rule_append_uint16 (ba, entry->nvalues);
			for (j = 0; j < entry->nvalues; j++)
				e2k_rule_append_propvalue (ba, &entry->propval[j]);
		}
		break;
	}

	case E2K_ACTION_TAG:
		e2k_rule_append_propvalue (ba, &act->act.proptag);
		break;

	case E2K_ACTION_DELETE:
		break;

	case E2K_ACTION_MARK_AS_READ:
		/* FIXME */
		break;

	default:
		break;
	}

	actlen = ba->len - actlen_offset - 2;
	e2k_rule_write_uint16 (ba->data + actlen_offset, actlen);
}

/**
 * e2k_actions_append:
 * @ba: a buffer into which a server-side rule is being constructed
 * @actions: the actions to append to @ba
 *
 * Appends @actions to @ba as part of a server-side rule.
 **/
void
e2k_actions_append (GByteArray *ba, GPtrArray *actions)
{
	gint actlen_offset, actlen, i;

	/* Save space for length */
	actlen_offset = ba->len;
	e2k_rule_append_uint32 (ba, 0);

	e2k_rule_append_uint16 (ba, actions->len);
	for (i = 0; i < actions->len; i++)
		append_action (ba, actions->pdata[i]);

	actlen = ba->len - actlen_offset - 4;
	e2k_rule_write_uint32 (ba->data + actlen_offset, actlen);
}
