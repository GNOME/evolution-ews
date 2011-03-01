/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libsoup/soup-misc.h>
#include "e-ews-item.h"
#include "e-ews-connection.h"
#include "e-ews-message.h"

G_DEFINE_TYPE (EEwsItem, e_ews_item, G_TYPE_OBJECT)

struct _EEwsItemPrivate {
	EEwsItemType item_type;

	/* properties */
	EwsId *item_id;
	gchar *subject;
	gchar *mime_content;

	time_t date_received;
	time_t date_sent;
	time_t date_created;

	gsize size;
	gchar *msg_id;
	gchar *in_replyto;
	gchar *references;
	gboolean has_attachments;
	gboolean is_read;
	EwsImportance importance;

	GSList *to_recipients;
	GSList *cc_recipients;
	GSList *bcc_recipients;

	EwsMailbox *from;
	EwsMailbox *sender;
};

static GObjectClass *parent_class = NULL;
static void	ews_item_free_mailbox (EwsMailbox *mb);

static void
e_ews_item_dispose (GObject *object)
{
	EEwsItem *item = (EEwsItem *) object;
	EEwsItemPrivate *priv;

	g_return_if_fail (E_IS_EWS_ITEM (item));

	priv = item->priv;

	if (priv->item_id) {
		g_free (priv->item_id);
		priv->item_id = NULL;
	}

	g_free (priv->subject);
	priv->subject = NULL;

	g_free (priv->msg_id);
	priv->msg_id = NULL;

	g_free (priv->in_replyto);
	priv->in_replyto = NULL;

	g_free (priv->references);
	priv->references = NULL;
	
	if (priv->to_recipients) {
		g_slist_foreach (priv->to_recipients, (GFunc) ews_item_free_mailbox, NULL);
		g_slist_free (priv->to_recipients);
		priv->to_recipients = NULL;
	}

	if (priv->cc_recipients) {
		g_slist_foreach (priv->cc_recipients, (GFunc) ews_item_free_mailbox, NULL);
		g_slist_free (priv->cc_recipients);
		priv->cc_recipients = NULL;
	}

	if (priv->bcc_recipients) {
		g_slist_foreach (priv->bcc_recipients, (GFunc) ews_item_free_mailbox, NULL);
		g_slist_free (priv->bcc_recipients);
		priv->bcc_recipients = NULL;
	}

	ews_item_free_mailbox (priv->sender);
	ews_item_free_mailbox (priv->from);

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_ews_item_finalize (GObject *object)
{
	EEwsItem *item = (EEwsItem *) object;
	EEwsItemPrivate *priv;

	g_return_if_fail (E_IS_EWS_ITEM (item));

	priv = item->priv;

	/* clean up */
	g_free (priv);
	item->priv = NULL;

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_ews_item_class_init (EEwsItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_ews_item_dispose;
	object_class->finalize = e_ews_item_finalize;
}

static void
e_ews_item_init (EEwsItem *item)
{
	EEwsItemPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EEwsItemPrivate, 1);
	item->priv = priv;

	priv->item_type = E_EWS_ITEM_TYPE_UNKNOWN;
}

static void
ews_item_free_mailbox (EwsMailbox *mb)
{
	if (mb) {
		g_free (mb->name);
		g_free (mb->email);
		g_free (mb);
	}
}


static time_t
ews_item_parse_date (const gchar *dtstring)
{
	time_t t = 0;
	GTimeVal t_val;

	g_return_val_if_fail (dtstring != NULL, 0);

	if (g_time_val_from_iso8601 (dtstring, &t_val)) {
		t = (time_t) t_val.tv_sec;
	} else if (strlen (dtstring) == 8) {
		/* It might be a date value */
		GDate date;
		struct tm tt;
		guint16 year;
		guint month;
		guint8 day;

		g_date_clear (&date, 1);
#define digit_at(x,y) (x[y] - '0')
		year = digit_at (dtstring, 0) * 1000
			+ digit_at (dtstring, 1) * 100
			+ digit_at (dtstring, 2) * 10
			+ digit_at (dtstring, 3);
		month = digit_at (dtstring, 4) * 10 + digit_at (dtstring, 5);
		day = digit_at (dtstring, 6) * 10 + digit_at (dtstring, 7);

		g_date_set_year (&date, year);
		g_date_set_month (&date, month);
		g_date_set_day (&date, day);

		g_date_to_struct_tm (&date, &tt);
		t = mktime (&tt);

	} else
		g_warning ("Could not parse the string \n");

	return t;
}

static EwsImportance 
parse_importance (ESoapParameter *param)
{
	gchar *value;
	EwsImportance importance = EWS_ITEM_LOW;

	value = e_soap_parameter_get_string_value (param);
	
	if (!g_ascii_strcasecmp (value, "Normal"))
		importance = EWS_ITEM_NORMAL;
	else if (!g_ascii_strcasecmp (value, "High") )
		importance = EWS_ITEM_HIGH;

	return importance;
}

static gboolean
e_ews_item_set_from_soap_parameter (EEwsItem *item, ESoapParameter *param)
{
	EEwsItemPrivate *priv = item->priv;
	ESoapParameter *subparam, *node;

	g_return_val_if_fail (param != NULL, FALSE);

	if ((node = e_soap_parameter_get_first_child_by_name (param, "Message")))
		priv->item_type = E_EWS_ITEM_TYPE_MESSAGE;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "CalendarItem")))
		priv->item_type = E_EWS_ITEM_TYPE_CALENDAR_ITEM;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "Contact")))
		priv->item_type = E_EWS_ITEM_TYPE_CONTACT;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "DistributionList")))
		priv->item_type = E_EWS_ITEM_TYPE_GROUP;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingMessage")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_MESSAGE;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingRequest")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_REQUEST;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingResponse")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_RESPONSE;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingCancellation")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_CANCELLATION;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "Task")))
		priv->item_type = E_EWS_ITEM_TYPE_TASK;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "Item")))
		priv->item_type = E_EWS_ITEM_TYPE_GENERIC_ITEM;
	else {
		g_warning ("Unable to find the Item type \n");
		return FALSE;
	}
	
	for (subparam = e_soap_parameter_get_first_child (node);
			subparam != NULL;
			subparam = e_soap_parameter_get_next_child (subparam)) {
		ESoapParameter *subparam1;
		const gchar *name;
		gchar *value = NULL;

		name = e_soap_parameter_get_name (subparam);

		/* The order is maintained according to the order in soap response */
		if (!g_ascii_strcasecmp (name, "MimeContent")) {
			guchar *data;
			gsize data_len = 0;

			value = e_soap_parameter_get_string_value (subparam);
			data = g_base64_decode (value, &data_len);
			if (!data || !data_len) {
				g_free (value);
				g_free (data);
				return FALSE;
			}
			priv->mime_content = (gchar *) data;

			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "ItemId")) {
			priv->item_id = g_new0 (EwsId, 1);
			priv->item_id->id = e_soap_parameter_get_property (subparam, "Id");
			priv->item_id->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
		} else if (!g_ascii_strcasecmp (name, "Subject")) {
			priv->subject = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "DateTimeReceived")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->date_received = ews_item_parse_date (value);
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "Size")) {
			priv->size = e_soap_parameter_get_int_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "Importance")) {
			priv->importance = parse_importance (subparam);
		} else if (!g_ascii_strcasecmp (name, "InReplyTo")) {
			priv->in_replyto = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "DateTimeSent")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->date_sent = ews_item_parse_date (value);
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "DateTimeCreated")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->date_created = ews_item_parse_date (value);
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "HasAttachments")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->has_attachments = (!g_ascii_strcasecmp (value, "true"));
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "Sender")) {
			subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Mailbox");
			priv->sender = e_ews_item_mailbox_from_soap_param (subparam1);
		} else if (!g_ascii_strcasecmp (name, "ToRecipients")) {
			GSList *list = NULL;
			for (subparam1 = e_soap_parameter_get_first_child (subparam);
				subparam1 != NULL;
				subparam1 = e_soap_parameter_get_next_child (subparam1)) {
				EwsMailbox *mb = e_ews_item_mailbox_from_soap_param (subparam1);
				list = g_slist_append (list, mb);
			}
			priv->to_recipients = list;
		} else if (!g_ascii_strcasecmp (name, "CcRecipients")) {
			GSList *list = NULL;
			for (subparam1 = e_soap_parameter_get_first_child (subparam);
				subparam1 != NULL;
				subparam1 = e_soap_parameter_get_next_child (subparam1)) {
				EwsMailbox *mb = e_ews_item_mailbox_from_soap_param (subparam1);
				list = g_slist_append (list, mb);
			}
			priv->cc_recipients = list;
		} else if (!g_ascii_strcasecmp (name, "BccRecipients")) {
			GSList *list = NULL;
			for (subparam1 = e_soap_parameter_get_first_child (subparam);
				subparam1 != NULL;
				subparam1 = e_soap_parameter_get_next_child (subparam1)) {
				EwsMailbox *mb = e_ews_item_mailbox_from_soap_param (subparam1);
				list = g_slist_append (list, mb);
			}
			priv->bcc_recipients = list;
		} else if (!g_ascii_strcasecmp (name, "From")) {
			subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Mailbox");
			priv->from = e_ews_item_mailbox_from_soap_param (subparam1);
		} else if (!g_ascii_strcasecmp (name, "InternetMessageId")) {
			priv->msg_id = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "IsRead")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->is_read = (!g_ascii_strcasecmp (value, "true"));
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "References")) {
			priv->references = e_soap_parameter_get_string_value (subparam);
		}
	}

	return TRUE;
}

EEwsItem *
e_ews_item_new_from_soap_parameter (ESoapParameter *param)
{
	EEwsItem *item;

	g_return_val_if_fail (param != NULL, NULL);

	item = g_object_new (E_TYPE_EWS_ITEM, NULL);
	if (!e_ews_item_set_from_soap_parameter (item, param)) {
		g_object_unref (item);
		return NULL;
	}

	return item;
}

EEwsItemType
e_ews_item_get_item_type (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), E_EWS_ITEM_TYPE_UNKNOWN);

	return item->priv->item_type;
}

void
e_ews_item_set_item_type (EEwsItem *item, EEwsItemType new_type)
{
	g_return_if_fail (E_IS_EWS_ITEM (item));

	item->priv->item_type = new_type;
}

const gchar *
e_ews_item_get_subject (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->subject;
}

void
e_ews_item_set_subject (EEwsItem *item, const gchar *new_subject)
{
	g_return_if_fail (E_IS_EWS_ITEM (item));

	if (item->priv->subject)
		g_free (item->priv->subject);
	item->priv->subject = g_strdup (new_subject);
}

const gchar *
e_ews_item_get_mime_content (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->mime_content;
}

void
e_ews_item_set_mime_content (EEwsItem *item, const gchar *new_mime_content)
{
	g_return_if_fail (E_IS_EWS_ITEM (item));

	if (item->priv->mime_content)
		g_free (item->priv->mime_content);
	item->priv->mime_content = g_strdup (new_mime_content);
}

const EwsId *
e_ews_item_get_id	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsId *) item->priv->item_id;
}

gsize
e_ews_item_get_size	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->size;	
}

const gchar *
e_ews_item_get_msg_id	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->msg_id;
}

const gchar *
e_ews_item_get_in_replyto (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->in_replyto;
}
const gchar *
e_ews_item_get_references (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->references;
}

time_t
e_ews_item_get_date_received	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->date_received;	
}

time_t
e_ews_item_get_date_sent	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->date_sent;
}

time_t
e_ews_item_get_date_created	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->date_created;
}

gboolean
e_ews_item_has_attachments	(EEwsItem *item, gboolean *has_attachments)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*has_attachments = item->priv->has_attachments;

	return TRUE;
}

gboolean
e_ews_item_is_read		(EEwsItem *item, gboolean *read)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*read = item->priv->is_read;

	return TRUE;
}

const GSList *	
e_ews_item_get_to_recipients	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const GSList *)	item->priv->to_recipients;
}

const GSList *	
e_ews_item_get_cc_recipients	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const GSList *)	item->priv->cc_recipients;
}

const GSList *	
e_ews_item_get_bcc_recipients	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const GSList *)	item->priv->bcc_recipients;
}

const EwsMailbox *
e_ews_item_get_sender		(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsMailbox *) item->priv->sender;
}

const EwsMailbox *
e_ews_item_get_from		(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsMailbox *) item->priv->from;
}
	
EwsImportance
e_ews_item_get_importance	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), EWS_ITEM_LOW);

	return item->priv->importance;
}
	
EwsMailbox *
e_ews_item_mailbox_from_soap_param (ESoapParameter *param)
{
	EwsMailbox *mb;
	ESoapParameter *subparam;

	mb = g_new0 (EwsMailbox, 1);
	
	subparam = e_soap_parameter_get_first_child_by_name (param, "Name");
	mb->name = e_soap_parameter_get_string_value (subparam);
	
	subparam = e_soap_parameter_get_first_child_by_name (param, "EmailAddress");
	mb->email = e_soap_parameter_get_string_value (subparam);

	return mb;
}
