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
};

static GObjectClass *parent_class = NULL;

static void
e_ews_item_dispose (GObject *object)
{
	EEwsItem *item = (EEwsItem *) object;
	EEwsItemPrivate *priv;

	g_return_if_fail (E_IS_EWS_ITEM (item));

	priv = item->priv;
	if (priv) {
		if (priv->item_id) {
			g_free (priv->item_id);
			priv->item_id = NULL;
		}

		if (priv->subject) {
			g_free (priv->subject);
			priv->subject = NULL;
		}

	}

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

static gboolean
e_ews_item_set_from_soap_parameter (EEwsItem *item, ESoapParameter *param)
{
	EEwsItemPrivate *priv = item->priv;
	gchar *value;
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

	subparam = e_soap_parameter_get_first_child_by_name (node, "ItemId");
	if (subparam) {
		priv->item_id = g_new0 (EwsId, 1);
		priv->item_id->id = e_soap_parameter_get_property (subparam, "Id");
		priv->item_id->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "Subject");
	if (subparam) {
		value = e_soap_parameter_get_string_value (subparam);
		e_ews_item_set_subject (item, (const gchar *) value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "MimeContent");
	if (subparam) {
		guchar *data;
		gsize data_len = 0;

		value = e_soap_parameter_get_string_value (subparam);
		data = g_base64_decode (value, &data_len);
		if (!data || !data_len) {
			g_free (value);
			g_free (data);
			return FALSE;
		}
		e_ews_item_set_mime_content (item, (const gchar *) data);

		g_free (value);
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
e_ews_item_get_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsId *) item->priv->item_id;
}
