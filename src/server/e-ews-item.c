/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *  Harish Krishnaswamy <kharish@novell.com>
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
	gchar *container;
	GList *category_list; /*list of category ids*/

	/* properties */
	gchar *id;
	gchar *creation_date;
	gchar *delivered_date;
	gchar *start_date;
	gchar *end_date;
	gchar *due_date;
	gchar *completed_date;
	gchar *modified_date;
	gboolean completed;
	gboolean is_allday_event;
	gchar *subject;
	gchar *message;
	gchar *classification;
	gchar *accept_level;
	gchar *priority;
	gchar *task_priority;
	gchar *place;
	gchar *source;
	gchar *security;
	GSList *recipient_list;
	GSList *recurrence_dates;
	GSList *exdate_list;
	EEwsItemRecurrenceRule *rrule;
	gint recurrence_key;
	gint trigger; /* alarm */
	/*message size*/
	gint size;
	EEwsItemOrganizer *organizer;

	/*properties for mail*/
	gchar *from;
	gchar *to;
	gchar *content_type;
	gchar *msg_body_id;
	gint item_status;
	/*Attachments*/
	gboolean has_attachment;
	GSList *attach_list;
	/*linkInfo for replies*/
	EEwsItemLinkInfo *link_info;

	/* properties for tasks/calendars */
	gchar *icalid;
	/* if the self is not the organizer of the item, the
	 * status is not reflected in the recipientStatus.
	 * Hence it should be gleaned from the 'status' element
	 * of the Mail, the parent item.*/
	guint32 self_status;
	/* properties for category items*/
	gchar *category_name;

	/* properties for contacts */
	FullName *full_name;
	GList *email_list;
	GList *im_list;
	GHashTable *simple_fields;
	GList *member_list;
	GHashTable *addresses;

	/***** Send Options *****/

	gboolean set_sendoptions;
	/* Reply Request */
	gchar *reply_within;
	gboolean reply_request_set;

	/* Status Tracking through sent Item */
	EEwsItemTrack track_info;
	gboolean autodelete;

	/* Return Notification */
	EEwsItemReturnNotify notify_completed;
	EEwsItemReturnNotify notify_accepted;
	EEwsItemReturnNotify notify_declined;
	EEwsItemReturnNotify notify_opened;
	EEwsItemReturnNotify notify_deleted;

	/* Expiration Date */
	gchar *expires;

	/* Delay delivery */
	gchar *delay_until;

	/* changes */
	GHashTable *additions;
	GHashTable *updates;
	GHashTable *deletions;

	gboolean internet;

	/* Message Threading */
	gchar *message_id;
	gchar *parent_threads;

	/*padding*/
	guint padding[10];
};

static GObjectClass *parent_class = NULL;

static void
free_recipient (EEwsItemRecipient *recipient, gpointer data)
{
	g_free (recipient->email);
	g_free (recipient->display_name);
	g_free (recipient->delivered_date);
	g_free (recipient->opened_date);
	g_free (recipient->accepted_date);
	g_free (recipient->deleted_date);
	g_free (recipient->declined_date);
	g_free (recipient->completed_date);
	g_free (recipient->undelivered_date);
	g_free (recipient);
}

static void
free_postal_address (gpointer  postal_address)
{
	PostalAddress *address;
	address = (PostalAddress *) postal_address;
	if (address) {
		g_free (address->street_address);
		g_free (address->location);
		g_free(address->city);
		g_free(address->country);
		g_free(address->state);
		g_free(address->postal_code);
		g_free(address);
	}
}

static void
free_full_name (gpointer full_name)
{
	FullName *name = (FullName *) full_name;
	g_free (name->name_prefix);
	g_free (name->first_name);
	g_free (name->middle_name);
	g_free (name->last_name);
	g_free (name->name_suffix);
	g_free (name);

}

static void
free_string (gpointer s, gpointer data)
{
	if (s)
		free (s);
}

static void
free_attach (gpointer s, gpointer data)
{
	EEwsItemAttachment *attach = (EEwsItemAttachment *) s;
	if (attach) {
		if (attach->id)
			g_free (attach->id), attach->id = NULL;
		if (attach->name)
			g_free (attach->name), attach->name = NULL;
		if (attach->contentid)
			g_free (attach->contentid), attach->contentid= NULL;
		if (attach->contentType)
			g_free (attach->contentType), attach->contentType = NULL;
		if (attach->date)
			g_free (attach->date), attach->date = NULL;
		if (attach->data)
			g_free (attach->data), attach->data = NULL;

		g_free(attach);
	}

}
static void
free_member (gpointer member, gpointer data)
{
	EGroupMember *group_member = (EGroupMember *) member;
	if (group_member->id)
		g_free (group_member->id);
	if (group_member->email)
		g_free (group_member->email);
	if (group_member->name)
		g_free (group_member->name);
	g_free (group_member);
}

static void
free_im_address ( gpointer address, gpointer data)
{
	IMAddress *im_address;
	im_address = (IMAddress *) address;

	if (im_address) {
		if (im_address->service)
			g_free (im_address->service);
		if (im_address->address)
			g_free (im_address->address);
		g_free (im_address);
	}
}

static void
free_link_info (EEwsItemLinkInfo *info)
{
	if (info) {
		if (info->id )
			g_free (info->id), info->id = NULL;
		if (info->type)
			g_free (info->type), info->type = NULL;
		if (info->thread)
			g_free (info->thread), info->thread = NULL;
		g_free (info);
		info = NULL;
	}
}

static void
free_changes ( GHashTable *changes)
{
	gpointer value;
	if (!changes)
		return;
	value = g_hash_table_lookup (changes, "full_name");
	if (value)
		free_full_name (value);
	value = g_hash_table_lookup (changes, "email");
	if (value)
		g_list_free ((GList*) value);
	value = g_hash_table_lookup (changes, "ims");
	if (value)
		g_list_free ((GList*) value);
	value = g_hash_table_lookup (changes, "Home");
	if (value)
		free_postal_address (value);
	value = g_hash_table_lookup (changes, "Office");
	if (value)
		free_postal_address (value);
	g_hash_table_destroy (changes);
}
static void
e_ews_item_dispose (GObject *object)
{
	EEwsItem *item = (EEwsItem *) object;
	EEwsItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

	priv = item->priv;
	if (priv) {
		if (priv->container) {
			g_free (priv->container);
			priv->container = NULL;
		}

		if (priv->id) {
			g_free (priv->id);
			priv->id = NULL;
		}

		if (priv->subject) {
			g_free (priv->subject);
			priv->subject = NULL;
		}

		if (priv->message) {
			g_free (priv->message);
			priv->message = NULL;
		}

		if (priv->classification) {
			g_free (priv->classification);
			priv->classification = NULL;
		}

		if (priv->security) {
			g_free (priv->security);
			priv->security = NULL;
		}

		if (priv->accept_level) {
			g_free (priv->accept_level);
			priv->accept_level = NULL;
		}

		if (priv->priority) {
			g_free (priv->priority);
			priv->priority = NULL;
		}

		if (priv->task_priority) {
			g_free (priv->task_priority);
			priv->task_priority = NULL;
		}

		if (priv->place) {
			g_free (priv->place);
			priv->place = NULL;
		}

		if (priv->from) {
			g_free (priv->from);
			priv->from = NULL;
		}

		if (priv->to) {
			g_free (priv->to);
			priv->to = NULL;
		}

		if (priv->content_type) {
			g_free (priv->content_type);
			priv->content_type = NULL;
		}
		if (priv->msg_body_id) {
			g_free (priv->msg_body_id);
			priv->msg_body_id = NULL;
		}

		if (priv->icalid) {
			g_free (priv->icalid);
			priv->icalid = NULL;
		}

		if (priv->reply_within) {
			g_free (priv->reply_within);
			priv->reply_within = NULL;
		}

		if (priv->expires) {
			g_free (priv->expires);
			priv->expires = NULL;
		}

		if (priv->delay_until) {
			g_free (priv->delay_until);
			priv->delay_until = NULL;
		}

		if (priv->recipient_list) {
			g_slist_foreach (priv->recipient_list, (GFunc) free_recipient, NULL);
			g_slist_free (priv->recipient_list);
			priv->recipient_list = NULL;
		}

		if (priv->organizer) {
			g_free (priv->organizer->display_name);
			g_free (priv->organizer->email);
			g_free (priv->organizer);
			priv->organizer = NULL;
		}

		if (priv->recurrence_dates) {
			g_slist_foreach (priv->recurrence_dates, free_string, NULL);
			g_slist_free (priv->recurrence_dates);
			priv->recurrence_dates = NULL;
		}

		if (priv->exdate_list) {
			g_slist_foreach (priv->exdate_list, free_string, NULL);
			g_slist_free (priv->exdate_list);
			priv->exdate_list = NULL;
		}

		if (priv->rrule) {
			/*TODO free all the strings */
			priv->rrule = NULL;
		}

		if (priv->full_name) {
			free_full_name (priv->full_name);
			priv->full_name = NULL;
			}

		if (priv->simple_fields)
			g_hash_table_destroy (priv->simple_fields);

		if (priv->addresses)
			g_hash_table_destroy (priv->addresses);

		if (priv->email_list) {
			g_list_foreach (priv->email_list,  free_string , NULL);
			g_list_free (priv->email_list);
			priv->email_list = NULL;
		}

		if (priv->member_list) {
			g_list_foreach (priv->member_list,  free_member, NULL);
			g_list_free (priv->member_list);
			priv->member_list = NULL;
		}

		if (priv->im_list) {
			g_list_foreach (priv->im_list, free_im_address, NULL);
			g_list_free (priv->im_list);
			priv->im_list = NULL;
		}

		if (priv->category_list) {
			g_list_foreach (priv->category_list,  free_string, NULL);
			g_list_free (priv->category_list);
			priv->category_list = NULL;
		}

		if (priv->attach_list) {
			g_slist_foreach (priv->attach_list, free_attach, NULL);
			g_slist_free (priv->attach_list);
			priv->attach_list = NULL;
		}

		if (priv->category_name) {
			g_free (priv->category_name);
			priv->category_name = NULL;
		}

		if (priv->source) {
			g_free (priv->source);
			priv->source = NULL;
		}

		if (priv->link_info) {
			free_link_info (priv->link_info);
			priv->link_info = NULL;
		}

		if (priv->end_date) {
			g_free (priv->end_date);
			priv->end_date = NULL;
		}

		if (priv->delivered_date) {
			g_free (priv->delivered_date);
			priv->delivered_date = NULL;
		}

		if (priv->modified_date) {
			g_free (priv->modified_date);
			priv->modified_date = NULL;
		}

		if (priv->start_date) {
			g_free (priv->start_date);
			priv->start_date = NULL;
		}

		if (priv->creation_date) {
			g_free (priv->creation_date);
			priv->creation_date = NULL;
		}

		if (priv->message_id) {
			g_free (priv->message_id);
			priv->message_id = NULL;
		}

		if (priv->parent_threads) {
			g_free (priv->parent_threads);
			priv->parent_threads = NULL;
		}

		free_changes (priv->additions);
		free_changes (priv->deletions);
		free_changes (priv->updates);

	}

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_ews_item_finalize (GObject *object)
{
	EEwsItem *item = (EEwsItem *) object;
	EEwsItemPrivate *priv;

	g_return_if_fail (E_IS_GW_ITEM (item));

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
	priv->item_type = E_EWS_ITEM_TYPE_UNKNOWN;
	priv->creation_date = NULL;
	priv->delivered_date = NULL;
	priv->modified_date = NULL;
	priv->start_date = NULL;
	priv->end_date = NULL;
	priv->due_date = NULL;
	priv->completed_date = NULL;
	priv->trigger = 0;
	priv->recipient_list = NULL;
	priv->organizer = NULL;
	priv->recurrence_dates = NULL;
	priv->completed = FALSE;
	priv->is_allday_event = FALSE;
	priv->im_list = NULL;
	priv->email_list = NULL;
	priv->member_list = NULL;
	priv->category_list = NULL;
	priv->reply_within = NULL;
	priv->reply_request_set = FALSE;
	priv->autodelete = FALSE;
	priv->set_sendoptions = FALSE;
	priv->expires = NULL;
	priv->delay_until = NULL;
	priv->attach_list = NULL;
	priv->simple_fields = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	priv->full_name = g_new0(FullName, 1);
	priv->addresses = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, free_postal_address);
	priv->additions = g_hash_table_new(g_str_hash, g_str_equal);
	priv->updates =   g_hash_table_new (g_str_hash, g_str_equal);
	priv->deletions = g_hash_table_new (g_str_hash, g_str_equal);
	priv->self_status = 0;
	priv->link_info = NULL;
	priv->msg_body_id = NULL;
	priv->has_attachment = FALSE;
	priv->internet = FALSE;
	priv->message_id = NULL;
	priv->parent_threads = NULL;
	item->priv = priv;

}

void
e_ews_item_free_cal_id (EEwsItemCalId *calid)
{
	if (calid->item_id) {
		g_free (calid->item_id);
		calid->item_id = NULL;
	}

	if (calid->ical_id) {
		g_free (calid->ical_id);
		calid->ical_id = NULL;
	}

	if (calid->recur_key) {
		g_free (calid->recur_key);
		calid->recur_key = NULL;
	}

	if (calid->start_date) {
		g_free (calid->start_date);
		calid->start_date = NULL;
	}

	g_free (calid);
}

EEwsItem *
e_ews_item_new_empty (void)
{
	return g_object_new (E_TYPE_EWS_ITEM, NULL);
}

static void
set_recipient_list_from_soap_parameter (EEwsItem *item, SoupSoapParameter *param)
{
	SoupSoapParameter *param_recipient;
	gchar *email, *cn;
	EEwsItemRecipient *recipient;
	GList *email_list;

	email_list = e_ews_item_get_email_list (item);

	for (param_recipient = soup_soap_parameter_get_first_child_by_name (param, "recipient");
			param_recipient != NULL;
			param_recipient = soup_soap_parameter_get_next_child_by_name (param_recipient, "recipient")) {
		SoupSoapParameter *subparam;

		recipient = g_new0 (EEwsItemRecipient, 1);
		subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "email");
		if (subparam) {
			email = soup_soap_parameter_get_string_value (subparam);
			if (email)
				recipient->email = email;
		}
		subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "displayName");
		if (subparam) {
			cn = soup_soap_parameter_get_string_value (subparam);
			if (cn)
				recipient->display_name = cn;
		}

		subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "distType");
		if (subparam) {
			gchar *dist_type;
			dist_type = soup_soap_parameter_get_string_value (subparam);
			if (!strcmp (dist_type, "TO"))
				recipient->type = E_EWS_ITEM_RECIPIENT_TO;
			else if (!strcmp (dist_type, "CC"))
				recipient->type = E_EWS_ITEM_RECIPIENT_CC;
			else if (!strcmp (dist_type, "BC"))
				recipient->type = E_EWS_ITEM_RECIPIENT_BC;
			else
				recipient->type = E_EWS_ITEM_RECIPIENT_NONE;
			g_free (dist_type);
		}
		/*FIXME  gw recipientTypes need to be added after the server is fixed. */

		/* update Recipient Status
		 look for accepted/declined and update the item else set it
		 to none. */
		subparam = soup_soap_parameter_get_first_child_by_name (param_recipient, "recipientStatus");
		if (subparam) {
			gchar *formatted_date, *value;
			SoupSoapParameter *temp_param;

			recipient->status_enabled = TRUE;
			if ((temp_param = soup_soap_parameter_get_first_child_by_name (subparam, "deleted"))) {
				recipient->status = E_EWS_ITEM_STAT_DELETED;
				value = soup_soap_parameter_get_string_value (temp_param);
				formatted_date = e_ews_connection_format_date_string (value);
				recipient->deleted_date = g_strdup (formatted_date);
				g_free (value), value = NULL;
				g_free (formatted_date), formatted_date = NULL;
			}
			if ((temp_param = soup_soap_parameter_get_first_child_by_name (subparam, "declined"))) {
				recipient->status = E_EWS_ITEM_STAT_DECLINED;
				value = soup_soap_parameter_get_string_value (temp_param);
				formatted_date = e_ews_connection_format_date_string (value);
				recipient->declined_date = g_strdup (formatted_date);
				g_free (value), value = NULL;
				g_free (formatted_date), formatted_date = NULL;

			} if ( (temp_param = soup_soap_parameter_get_first_child_by_name (subparam, "accepted"))) {
				recipient->status = E_EWS_ITEM_STAT_ACCEPTED;
				value = soup_soap_parameter_get_string_value (temp_param);
				formatted_date = e_ews_connection_format_date_string (value);
				recipient->accepted_date = g_strdup (formatted_date);
				g_free (value), value = NULL;
				g_free (formatted_date), formatted_date = NULL;
			} else
				recipient->status = E_EWS_ITEM_STAT_NONE;
			temp_param = soup_soap_parameter_get_first_child_by_name (subparam, "completed");
			if (temp_param) {
				value = soup_soap_parameter_get_string_value (temp_param);
				formatted_date = e_ews_connection_format_date_string (value);
				e_ews_item_set_completed_date (item, formatted_date);
				recipient->completed_date = g_strdup (formatted_date);
				g_free (value), value = NULL;
				g_free (formatted_date), formatted_date = NULL;
			}
			if ( (temp_param = soup_soap_parameter_get_first_child_by_name (subparam, "opened"))) {
				value = soup_soap_parameter_get_string_value (temp_param);
				formatted_date = e_ews_connection_format_date_string (value);
				recipient->opened_date = g_strdup (formatted_date);
				g_free (value), value = NULL;
				g_free (formatted_date), formatted_date = NULL;
			}
			if ( ( temp_param = soup_soap_parameter_get_first_child_by_name (subparam, "delivered"))) {
				value = soup_soap_parameter_get_string_value (temp_param);
				formatted_date = e_ews_connection_format_date_string (value);
				recipient->delivered_date = g_strdup (formatted_date);
				g_free (value), value = NULL;
				g_free (formatted_date), formatted_date = NULL;
			}
			if ( (temp_param = soup_soap_parameter_get_first_child_by_name (subparam, "undeliverable_date"))) {
				value = soup_soap_parameter_get_string_value (temp_param);
				formatted_date = e_ews_connection_format_date_string (value);
				recipient->undelivered_date = g_strdup (formatted_date);
				g_free (value), value = NULL;
				g_free (formatted_date), formatted_date = NULL;
			}
		} else {
			/* if recipientStatus is not provided, use the
			 * self_status, obtained from the mail properties. */
			if (!strcmp ((const gchar *)email_list->data, recipient->email))
				recipient->status = item->priv->self_status & (E_EWS_ITEM_STAT_DECLINED |
										E_EWS_ITEM_STAT_ACCEPTED);
			else
				recipient->status = E_EWS_ITEM_STAT_NONE;
		}

		item->priv->recipient_list = g_slist_append (item->priv->recipient_list, recipient);
	}
}

static EEwsItemReturnNotify
get_notification_value (SoupSoapParameter *param, const gchar *param_name)
{
	SoupSoapParameter *subparam;

	if ((subparam = soup_soap_parameter_get_first_child_by_name (param, param_name))) {
		gchar *value = NULL;

		subparam = soup_soap_parameter_get_first_child_by_name (subparam, "mail");
		if (subparam)
			value = soup_soap_parameter_get_string_value (subparam);
		if (value && !g_ascii_strcasecmp (value, "1")) {
			g_free (value), value = NULL;
			return E_EWS_ITEM_NOTIFY_MAIL;
		}
		g_free (value), value = NULL;
	}
	return E_EWS_ITEM_NOTIFY_NONE;
}

static void
set_sendoptions_from_soap_parameter (EEwsItem *item, SoupSoapParameter *param)
{
	EEwsItemPrivate *priv;
	SoupSoapParameter *subparam, *child;
	gchar *value = NULL;

	priv = item->priv;

	if ((subparam = soup_soap_parameter_get_first_child_by_name (param, "requestReply"))) {
		child = soup_soap_parameter_get_first_child_by_name (subparam, "whenConvenient");
		if (child) {
			value = soup_soap_parameter_get_string_value (child);
			if (value && !g_ascii_strcasecmp (value, "1"))
				priv->reply_request_set = TRUE;
			g_free (value), value = NULL;
		}

		if (!priv->reply_request_set) {
			child = soup_soap_parameter_get_first_child_by_name (subparam, "byDate");

			if (child)
				value = soup_soap_parameter_get_string_value (child);
			if (value) {
				gchar *date;
				date = e_ews_connection_format_date_string (value);
				priv->reply_request_set = TRUE;
				priv->reply_within = date;
			}

		}
		g_free (value), value = NULL;
	}

	if ( (subparam = soup_soap_parameter_get_first_child_by_name (param, "statusTracking"))) {
		value = soup_soap_parameter_get_string_value (subparam);
	       if (value) {
		       if (!g_ascii_strcasecmp (value, "Delivered"))
			       priv->track_info = E_EWS_ITEM_DELIVERED;
		       else if (!g_ascii_strcasecmp (value, "DeliveredAndOpened"))
				priv->track_info = E_EWS_ITEM_DELIVERED_OPENED;
		       else if (!g_ascii_strcasecmp (value, "All"))
				priv->track_info = E_EWS_ITEM_ALL;

		       g_free (value), value = NULL;

		       value = soup_soap_parameter_get_property (subparam, "autoDelete");
		       if (value && !g_ascii_strcasecmp (value, "1"))
				priv->autodelete = TRUE;
		       g_free (value), value = NULL;
	       }
	}

	if ( (subparam = soup_soap_parameter_get_first_child_by_name (param, "notification"))) {
		priv->notify_opened = get_notification_value (subparam, "opened");
		priv->notify_deleted = get_notification_value (subparam, "deleted");
		priv->notify_accepted = get_notification_value (subparam, "accepted");
		priv->notify_declined = get_notification_value (subparam, "declined");
		priv->notify_completed = get_notification_value (subparam, "completed");
	}
}

gchar *
e_ews_item_get_field_value (EEwsItem *item, const gchar *field_name)
{
	gpointer value;

	g_return_val_if_fail (field_name != NULL, NULL);
	g_return_val_if_fail (E_IS_GW_ITEM(item), NULL);

	if (item->priv->simple_fields == NULL)
		return NULL;

	value =  (gchar *) g_hash_table_lookup (item->priv->simple_fields, field_name);
	if (value)
		return value;

	return NULL;
}

void
e_ews_item_set_field_value (EEwsItem *item, const gchar *field_name, gchar * field_value)
{
	g_return_if_fail (field_name != NULL);
	g_return_if_fail (field_name != NULL);
	g_return_if_fail (E_IS_GW_ITEM(item));

	if (item->priv->simple_fields != NULL)
		g_hash_table_insert (item->priv->simple_fields, (gpointer) field_name, g_strdup (field_value));

}
guint32
e_ews_item_get_item_status (EEwsItem *item)
{

	return item->priv->self_status;
}
GList *
e_ews_item_get_email_list (EEwsItem *item)
{
	return item->priv->email_list;

}

void
e_ews_item_set_email_list (EEwsItem *item, GList* email_list)
{
	item->priv->email_list = email_list;
}

GList *
e_ews_item_get_im_list (EEwsItem *item)

{
	return item->priv->im_list;
}

void
e_ews_item_set_im_list (EEwsItem *item, GList *im_list)
{
	item->priv->im_list = im_list;
}
FullName*
e_ews_item_get_full_name (EEwsItem *item)
{
	return item->priv->full_name;
}

void
e_ews_item_set_full_name (EEwsItem *item, FullName *full_name)
{
	item->priv->full_name = full_name;
}

GList *
e_ews_item_get_member_list (EEwsItem *item)
{
	return item->priv->member_list;
}

void
e_ews_item_set_member_list (EEwsItem *item, GList *list)
{
	item->priv->member_list = list;

}

void
e_ews_item_set_address (EEwsItem *item, const gchar *address_type, PostalAddress *address)
{
	if (address_type && address)
		g_hash_table_insert (item->priv->addresses, (gpointer) address_type, address);

}

PostalAddress *e_ews_item_get_address (EEwsItem *item, const gchar *address_type)
{
	return (PostalAddress *) g_hash_table_lookup (item->priv->addresses, address_type);
}

void
e_ews_item_set_categories (EEwsItem *item, GList *category_list)
{
	item->priv->category_list = category_list;

}

GList*
e_ews_item_get_categories (EEwsItem *item)
{
	return item->priv->category_list;
}

void
e_ews_item_set_category_name (EEwsItem *item, gchar *category_name)
{
	item->priv->category_name = category_name;
}

gchar *
e_ews_item_get_category_name (EEwsItem *item)
{
	return item->priv->category_name;
}

const gchar *
e_ews_item_get_parent_thread_ids (EEwsItem *item)
{
	return item->priv->parent_threads;
}

const gchar *
e_ews_item_get_message_id (EEwsItem *item)
{
	return item->priv->message_id;
}

void e_ews_item_set_change (EEwsItem *item, EEwsItemChangeType change_type, const gchar *field_name, gpointer field_value)
{
	GHashTable *hash_table;
	EEwsItemPrivate *priv;

	priv = item->priv;
	hash_table = NULL;
	switch (change_type) {
	case E_EWS_ITEM_CHANGE_TYPE_ADD :
		hash_table = priv->additions;
		break;
	case E_EWS_ITEM_CHANGE_TYPE_UPDATE :
		hash_table = priv->updates;
		break;
	case E_EWS_ITEM_CHANGE_TYPE_DELETE :
		hash_table = priv->deletions;
		break;
	case E_EWS_ITEM_CHNAGE_TYPE_UNKNOWN :
		hash_table = NULL;
		break;

	}

	if (hash_table)
		g_hash_table_insert (hash_table, (gpointer) field_name, field_value);

}

static void
set_common_addressbook_item_fields_from_soap_parameter (EEwsItem *item, SoupSoapParameter *param)
{
	SoupSoapParameter *subparam, *category_param;
	GHashTable *simple_fields;
	gchar *value;
	EEwsItemPrivate *priv;
	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}
	priv = item->priv;
	simple_fields = priv->simple_fields;

	subparam = soup_soap_parameter_get_first_child_by_name(param, "id");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		g_hash_table_insert (simple_fields, (gpointer) "id", value);
		item->priv->id = g_strdup (value);
	}
	value = NULL;
	subparam = soup_soap_parameter_get_first_child_by_name (param, "modified");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		g_hash_table_insert (simple_fields, (gpointer) "modified_time", value);
	}
	value = NULL;
	subparam = soup_soap_parameter_get_first_child_by_name (param, "comment");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields , (gpointer) "comment", value);
	}
	value = NULL;
	subparam = soup_soap_parameter_get_first_child_by_name(param, "name");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields, (gpointer) "name", value);
	}
	value = NULL;
	subparam = soup_soap_parameter_get_first_child_by_name (param, "categories");
	if (subparam) {
		for (category_param = soup_soap_parameter_get_first_child_by_name (subparam, "category");
		     category_param != NULL;
		     category_param = soup_soap_parameter_get_next_child_by_name (category_param, "category")) {

			value = soup_soap_parameter_get_string_value (category_param);
			if (value) {
				gchar **components = g_strsplit (value, "@", -1);
				g_free (value);
				value = components[0];
				priv->category_list = g_list_append (priv->category_list, g_strdup (value));
				g_strfreev(components);

			}

		}
	}

}

static void
set_postal_address_from_soap_parameter (PostalAddress *address, SoupSoapParameter *param)
{
	SoupSoapParameter *subparam;
	gchar *value;

	subparam= soup_soap_parameter_get_first_child_by_name (param, "streetAddress");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->street_address = value;
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "location");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);

		if (value)
			address->location = value;
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "city");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->city = value;
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "state");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->state = value;
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "postalCode");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->postal_code = value;
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "country");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			address->country = value;
	}

}

static void
set_contact_fields_from_soap_parameter (EEwsItem *item, SoupSoapParameter *param)
{
	gchar *value;
	gchar *type;
	gchar *primary_email;
	SoupSoapParameter *subparam;
	SoupSoapParameter *temp;
	SoupSoapParameter *second_level_child;
	GHashTable *simple_fields;
	FullName *full_name;
	PostalAddress *address;

	value = NULL;
	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}
	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;
	full_name = item->priv->full_name;
	if (full_name) {
		subparam = soup_soap_parameter_get_first_child_by_name (param, "fullName");
		if (subparam) {
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "namePrefix");
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->name_prefix = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "firstName");
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->first_name = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "middleName");
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->middle_name = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "lastName");
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->last_name = value;
			}
			temp = soup_soap_parameter_get_first_child_by_name(subparam, "nameSuffix");
			if (temp) {
				value = soup_soap_parameter_get_string_value (temp);
				if (value)
					full_name->name_suffix = value;
			}
		}
	}
	subparam = soup_soap_parameter_get_first_child_by_name(param, "emailList");
	if (subparam) {
		primary_email = NULL;
		value = soup_soap_parameter_get_property(subparam, "primary");
		if (value) {
			primary_email = value;
			item->priv->email_list = g_list_append (item->priv->email_list, g_strdup (primary_email));
		}
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp)) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value) {
				if (!primary_email || !g_str_equal (primary_email, value))
					item->priv->email_list = g_list_append (item->priv->email_list, value);
				else
					g_free (value);
			}
		}
		g_free (primary_email);
	}

	subparam =  soup_soap_parameter_get_first_child_by_name(param, "imList");
	if (subparam) {
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp))
			{
				IMAddress *im_address = g_new0(IMAddress, 1);
				im_address->address = im_address->service = NULL;
				second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "service");
				if (second_level_child) {
					value = soup_soap_parameter_get_string_value (second_level_child);
					if (value )
						im_address->service = value;
				}
				second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "address");
				if (second_level_child) {
					value = soup_soap_parameter_get_string_value (second_level_child);
					if (value)
						im_address->address = value;
				}
				if (im_address->service && im_address->address)
					item->priv->im_list = g_list_append (item->priv->im_list, im_address);
				else
					free_im_address (im_address, NULL);

			}
	}

	subparam =  soup_soap_parameter_get_first_child_by_name(param, "phoneList");
	if (subparam) {
		g_hash_table_insert (simple_fields, (gpointer) "default_phone", soup_soap_parameter_get_property(subparam, "default"));
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp))
			{
				const gchar *key = NULL;

				type =  soup_soap_parameter_get_property (temp, "type");
				switch (*type) {
					case 'O' :
						key = "phone_Office";
						break;
					case 'H' :
						key = "phone_Home";
						break;
					case 'P' :
						key = "phone_Pager";
						break;
					case 'M' :
						key = "phone_Mobile";
						break;
					case 'F' :
						key = "phone_Fax";
						break;
					default:
						/* It should never come here. For the worst */
						key = "phone_";
						break;
				};

				if (type) {
					value = soup_soap_parameter_get_string_value (temp);
					g_hash_table_insert (item->priv->simple_fields, (gchar *)key, value);
					g_free (type);
				}
			}
	}
	subparam =  soup_soap_parameter_get_first_child_by_name(param, "personalInfo");
	if (subparam) {
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "birthday");
		if (temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value)
				g_hash_table_insert (simple_fields, (gpointer) "birthday", value);

		}
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "website");
		if (temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value)
				g_hash_table_insert (simple_fields, (gpointer) "website", value);
		}
	}

	subparam =  soup_soap_parameter_get_first_child_by_name(param, "officeInfo");
	if (subparam) {
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "organization");
		if (temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value)
				g_hash_table_insert (simple_fields, (gpointer) "organization", value);
			value = soup_soap_parameter_get_property(temp, "uid");
			if (value)
				g_hash_table_insert (simple_fields, (gpointer) "organization_id", value);
		}
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "department");
		if (temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value)
				g_hash_table_insert (simple_fields, (gpointer) "department", value);
		}
		temp = soup_soap_parameter_get_first_child_by_name (subparam, "title");
		if (temp) {
			value = soup_soap_parameter_get_string_value (temp);
			if (value)
				g_hash_table_insert (simple_fields, (gpointer) "title", value);
		}

	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "addressList");
	if (subparam) {
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp)) {
			const gchar *add = NULL;
			address = g_new0 (PostalAddress, 1);
			set_postal_address_from_soap_parameter (address, temp);
			value = soup_soap_parameter_get_property(temp, "type");

			if (value && value[0] == 'H')
				add = "Home";
			else if (value && value[0] == 'O')
				add = "Office";
			else
				add = "Other";

			if (value)
				g_hash_table_insert (item->priv->addresses, (gchar *) add, address);
			else
				free_postal_address (address);
			g_free (value);
		}

	}

}
static void
set_group_fields_from_soap_parameter (EEwsItem *item, SoupSoapParameter *param)
{

	gchar *value;
	SoupSoapParameter *subparam, *temp, *second_level_child;
	GHashTable *simple_fields;

	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}

	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;

	/* set name as the ful name also , as it is needed for searching*/
	value = g_hash_table_lookup (simple_fields, "name");
	if (value)
		item->priv->full_name->first_name = g_strdup (value);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "members");
	if (subparam) {
		gchar *id, *email;
		for ( temp = soup_soap_parameter_get_first_child (subparam); temp != NULL; temp = soup_soap_parameter_get_next_child (temp)) {
			id = email = NULL;
			second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "email");
			if (second_level_child)
				email = soup_soap_parameter_get_string_value (second_level_child);
			second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "id");
			if (second_level_child)
				id = soup_soap_parameter_get_string_value (second_level_child);

			if (id && email) {
				EGroupMember *member = g_new0 (EGroupMember, 1);
				member->id = id;
				member->email = email;
				second_level_child = soup_soap_parameter_get_first_child_by_name (temp, "name");
				member->name =  soup_soap_parameter_get_string_value (second_level_child);
				item->priv->member_list = g_list_append (item->priv->member_list, member);
			} else {
				g_free (id);
				g_free (email);
			}
		}
	}

}

static void
set_resource_fields_from_soap_parameter (EEwsItem *item, SoupSoapParameter *param)
{

	gchar *value;
	SoupSoapParameter *subparam;
	GHashTable *simple_fields;

	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}

	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;

	/* set name as the ful name also , as it is needed for searching*/
	value = g_hash_table_lookup (simple_fields, "name");
	if (value)
		item->priv->full_name->first_name = g_strdup (value);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "phone");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields, (gpointer) "default_phone", value);
	}
	subparam = soup_soap_parameter_get_first_child_by_name (param, "email");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			item->priv->email_list = g_list_append (item->priv->email_list, value);
	}

}

static void
set_organization_fields_from_soap_parameter (EEwsItem *item, SoupSoapParameter *param)
{

	gchar *value;
	SoupSoapParameter *subparam;
	PostalAddress *address;
	GHashTable *simple_fields;

	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return;
	}
	set_common_addressbook_item_fields_from_soap_parameter (item, param);
	simple_fields = item->priv->simple_fields;

	/* set name as the ful name also , as it is needed for searching*/
	value = g_hash_table_lookup (simple_fields, "name");
	if (value)
		item->priv->full_name->first_name = g_strdup (value);

	subparam = soup_soap_parameter_get_first_child_by_name (param, "phone");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields, (gpointer) "default_phone", value);
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "fax");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields, (gpointer) "phone_Fax", value);
	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "address");
	if (subparam) {
		address = g_new0 (PostalAddress, 1);
		set_postal_address_from_soap_parameter (address, subparam);
		g_hash_table_insert (item->priv->addresses, (gpointer) "Office", address);

	}

	subparam = soup_soap_parameter_get_first_child_by_name (param, "website");
	if (subparam) {
		value = soup_soap_parameter_get_string_value (subparam);
		if (value)
			g_hash_table_insert (simple_fields, (gpointer) "website", value);
	}

}

static void
append_postal_address_to_soap_message (SoupSoapMessage *msg, PostalAddress *address, const gchar *address_type)
{
	soup_soap_message_start_element (msg, "address", NULL, NULL);
	soup_soap_message_add_attribute (msg, "type", address_type, NULL, NULL);
	if (address->street_address)
		e_ews_message_write_string_parameter (msg, "streetAddress", NULL, address->street_address);
	if (address->location)
		e_ews_message_write_string_parameter (msg, "location", NULL, address->location);
	if (address->city)
		e_ews_message_write_string_parameter (msg, "city", NULL, address->city);
	if (address->state)
		e_ews_message_write_string_parameter (msg, "state", NULL, address->state);
	if (address->postal_code)
		e_ews_message_write_string_parameter (msg, "postalCode", NULL, address->postal_code);
	if (address->country)
		e_ews_message_write_string_parameter (msg, "country", NULL, address->country);
	soup_soap_message_end_element(msg);

}

static void
append_common_addressbook_item_fields_to_soap_message (GHashTable *simple_fields, GList *category_list,  SoupSoapMessage *msg)
{
	gchar * value;

	value =  g_hash_table_lookup (simple_fields, "name");
	if (value)
		e_ews_message_write_string_parameter (msg, "name", NULL, value);

	soup_soap_message_start_element (msg, "categories", NULL, NULL);
	if (category_list && category_list->data)
		soup_soap_message_add_attribute (msg, "types:primary", category_list->data, NULL, NULL);
	for (; category_list != NULL; category_list = g_list_next (category_list))
		if (category_list->data) {
			e_ews_message_write_string_parameter (msg, "category", NULL, category_list->data);
		}
	soup_soap_message_end_element (msg);

	value = g_hash_table_lookup (simple_fields, "comment");
	if (value)
		e_ews_message_write_string_parameter (msg, "comment", NULL, value);

}

static void
append_full_name_to_soap_message (FullName *full_name, gchar *display_name, SoupSoapMessage *msg)
{
	g_return_if_fail (full_name != NULL);
	soup_soap_message_start_element (msg, "fullName", NULL, NULL);
	if (display_name)
		e_ews_message_write_string_parameter (msg, "displayName", NULL, display_name);
	if (full_name->name_prefix)
		e_ews_message_write_string_parameter (msg, "namePrefix", NULL, full_name->name_prefix);
	if (full_name->first_name)
		e_ews_message_write_string_parameter (msg, "firstName", NULL, full_name->first_name);
	if (full_name->middle_name)
		e_ews_message_write_string_parameter (msg, "middleName", NULL, full_name->middle_name);
	if (full_name->last_name)
		e_ews_message_write_string_parameter (msg, "lastName", NULL, full_name->last_name);
	if (full_name->name_suffix)
		e_ews_message_write_string_parameter (msg, "nameSuffix", NULL, full_name->name_suffix);
	soup_soap_message_end_element (msg);

}

static void
append_email_list_soap_message (GList *email_list, SoupSoapMessage *msg)
{
	g_return_if_fail (email_list != NULL);

	soup_soap_message_start_element (msg, "emailList", NULL, NULL);
	soup_soap_message_add_attribute (msg, "primary", email_list->data, NULL, NULL);
	for (; email_list != NULL; email_list = g_list_next (email_list))
		if (email_list->data)
			e_ews_message_write_string_parameter (msg, "email", NULL, email_list->data);
	soup_soap_message_end_element (msg);

}

static void
append_im_list_to_soap_message (GList *ims, SoupSoapMessage *msg)
{
	IMAddress *address;
	g_return_if_fail (ims != NULL);

	soup_soap_message_start_element (msg, "imList", NULL, NULL);
	for (; ims != NULL; ims = g_list_next (ims)) {
		soup_soap_message_start_element (msg, "im", NULL, NULL);
		address = (IMAddress *) ims->data;
		e_ews_message_write_string_parameter (msg, "service", NULL, address->service);
		e_ews_message_write_string_parameter (msg, "address", NULL, address->address);
		soup_soap_message_end_element (msg);
	}
	soup_soap_message_end_element (msg);

}
static void
append_phone_list_to_soap_message (GHashTable *simple_fields, SoupSoapMessage *msg)
{
	gchar *value;

	g_return_if_fail (simple_fields != NULL);

	soup_soap_message_start_element (msg, "phoneList", NULL, NULL);
	value = g_hash_table_lookup (simple_fields, "default_phone");
	if (value)
		soup_soap_message_add_attribute (msg, "default", value, NULL, NULL);
	value = g_hash_table_lookup (simple_fields, "phone_Office");
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Office");
	value = g_hash_table_lookup (simple_fields, "phone_Home");
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Home");
	value = g_hash_table_lookup (simple_fields, "phone_Pager");
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Pager");
	value = g_hash_table_lookup (simple_fields, "phone_Mobile");
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Mobile");
	value = g_hash_table_lookup (simple_fields, "phone_Fax");
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "phone", NULL, value, "type", "Fax");

	soup_soap_message_end_element (msg);

}

static void
append_office_info_to_soap_message (GHashTable *simple_fields, SoupSoapMessage *msg)
{
	gchar *value;
	gchar *org_name;
	g_return_if_fail (simple_fields != NULL);

	soup_soap_message_start_element (msg, "officeInfo", NULL, NULL);
	value = g_hash_table_lookup (simple_fields, "organization_id");
	org_name = g_hash_table_lookup (simple_fields, "organization");
	if (value && org_name)
		e_ews_message_write_string_parameter_with_attribute (msg, "organization", NULL, org_name, "uid", value);
	else if (org_name)
		e_ews_message_write_string_parameter (msg, "organization", NULL, org_name);
	value = g_hash_table_lookup (simple_fields, "department");
	if (value)
		e_ews_message_write_string_parameter (msg, "department", NULL, value);

		value = g_hash_table_lookup (simple_fields, "title");
	if (value)
		e_ews_message_write_string_parameter (msg, "title", NULL, value);

	value = g_hash_table_lookup (simple_fields, "website");
	if (value)
		e_ews_message_write_string_parameter (msg, "website", NULL, value);
	soup_soap_message_end_element (msg);

}

static void
append_personal_info_to_soap_message (GHashTable *simple_fields, SoupSoapMessage *msg)
{
	gchar *value;

	g_return_if_fail (simple_fields != NULL);

	soup_soap_message_start_element (msg, "personalInfo", NULL, NULL);
	value =  g_hash_table_lookup (simple_fields, "birthday");
	if (value)
		e_ews_message_write_string_parameter (msg, "birthday", NULL, value);
	value =  g_hash_table_lookup (simple_fields, "website");
	if (value)
		e_ews_message_write_string_parameter (msg, "website",NULL,  value);

	soup_soap_message_end_element (msg);

}

static void
append_contact_fields_to_soap_message (EEwsItem *item, SoupSoapMessage *msg)
{
	gchar * value;
	GHashTable *simple_fields;
	FullName *full_name;
	PostalAddress *postal_address;

	simple_fields = item->priv->simple_fields;
	value = g_hash_table_lookup (simple_fields, "id");
	if (value)
		e_ews_message_write_string_parameter (msg, "id", NULL, value);

	if (item->priv->container)
		e_ews_message_write_string_parameter (msg, "container", NULL, item->priv->container);

	append_common_addressbook_item_fields_to_soap_message (simple_fields, item->priv->category_list, msg);
	value =  g_hash_table_lookup (simple_fields, "name");

	full_name = item->priv->full_name;

	if (full_name)
		append_full_name_to_soap_message (full_name, value, msg);

	if (item->priv->email_list)
		append_email_list_soap_message (item->priv->email_list, msg);

	if (item->priv->im_list)
		append_im_list_to_soap_message (item->priv->im_list, msg);

	if (simple_fields)
		append_phone_list_to_soap_message (simple_fields, msg);

	soup_soap_message_start_element (msg, "addressList", NULL, NULL);
	postal_address = g_hash_table_lookup (item->priv->addresses, "Home");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Home");
	postal_address = g_hash_table_lookup (item->priv->addresses, "Office");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Office");
	soup_soap_message_end_element (msg);
	append_office_info_to_soap_message (simple_fields, msg);
	append_personal_info_to_soap_message (simple_fields, msg);

}

static void
append_group_fields_to_soap_message (EEwsItem *item, SoupSoapMessage *msg)
{
	GHashTable *simple_fields;
	GList *members;

	simple_fields = item->priv->simple_fields;
	append_common_addressbook_item_fields_to_soap_message (simple_fields, item->priv->category_list, msg);
	if (item->priv->container)
		e_ews_message_write_string_parameter (msg, "container", NULL, item->priv->container);
	soup_soap_message_start_element (msg, "members", NULL, NULL);
	members = g_list_copy (item->priv->member_list);
	for (; members != NULL; members = g_list_next (members)) {
		EGroupMember *member = (EGroupMember *) members->data;
		soup_soap_message_start_element (msg, "member", NULL, NULL);
		e_ews_message_write_string_parameter (msg, "id", NULL, member->id);
		e_ews_message_write_string_parameter (msg, "distType", NULL, "TO");
		e_ews_message_write_string_parameter (msg, "itemType", NULL, "Contact");
		soup_soap_message_end_element(msg);

	}
	soup_soap_message_end_element (msg);

}

EEwsItem *
e_ews_item_new_from_soap_parameter (const gchar *email, const gchar *container, SoupSoapParameter *param)
{
	EEwsItem *item;
	gchar *item_type, *internet_prop;
	SoupSoapParameter *subparameter, *child, *category_param, *attachment_param;
	gboolean is_group_item = TRUE;
	GList *user_email = NULL;

	g_return_val_if_fail (param != NULL, NULL);

	if (strcmp (soup_soap_parameter_get_name (param), "item") != 0) {
		g_warning (G_STRLOC ": Invalid SOAP parameter %s", soup_soap_parameter_get_name (param));
		return NULL;
	}

	item = g_object_new (E_TYPE_EWS_ITEM, NULL);
	item_type = soup_soap_parameter_get_property (param, "type");

	internet_prop = soup_soap_parameter_get_property (param, "internet");

	if (internet_prop && !g_ascii_strcasecmp (internet_prop, "1"))
		item->priv->internet = TRUE;
	g_free (internet_prop);

	if (!g_ascii_strcasecmp (item_type, "Mail"))
		item->priv->item_type = E_EWS_ITEM_TYPE_MAIL;
	else if (!g_ascii_strcasecmp (item_type, "Appointment"))
		item->priv->item_type = E_EWS_ITEM_TYPE_APPOINTMENT;
	else if (!g_ascii_strcasecmp (item_type, "Task"))
		item->priv->item_type = E_EWS_ITEM_TYPE_TASK;
	else if (!g_ascii_strcasecmp (item_type, "Note"))
		item->priv->item_type = E_EWS_ITEM_TYPE_NOTE;
	else if (!g_ascii_strcasecmp (item_type, "Contact") ) {
		item->priv->item_type = E_EWS_ITEM_TYPE_CONTACT;
		set_contact_fields_from_soap_parameter (item, param);
		g_free (item_type);
		return item;
	}
	else if (!g_ascii_strcasecmp (item_type,"SharedNotification"))
		item->priv->item_type = E_EWS_ITEM_TYPE_NOTIFICATION;

	else if (!g_ascii_strcasecmp (item_type, "Organization")) {

		item->priv->item_type =  E_EWS_ITEM_TYPE_ORGANISATION;
		set_organization_fields_from_soap_parameter (item, param);
		g_free (item_type);
		return item;
	}

	else if (!g_ascii_strcasecmp (item_type, "Resource")) {

		item->priv->item_type = E_EWS_ITEM_TYPE_CONTACT;
		set_resource_fields_from_soap_parameter (item, param);
		g_free (item_type);
		return item;
	}

	else if (!g_ascii_strcasecmp (item_type, "Group")) {
		item->priv->item_type = E_EWS_ITEM_TYPE_GROUP;
		set_group_fields_from_soap_parameter (item, param);
		g_free (item_type);
		return item;
	}

	else {
		g_free (item_type);
		g_object_unref (item);
		return NULL;
	}

	g_free (item_type);

	item->priv->container = g_strdup (container);
	/* set the email of the user */
	user_email = g_list_append (user_email, g_strdup (email));
	e_ews_item_set_email_list (item, user_email);

	/* If the parameter consists of changes - populate deltas */
	subparameter = soup_soap_parameter_get_first_child_by_name (param, "changes");
	if (subparameter) {
		SoupSoapParameter *changes = subparameter;
		subparameter = soup_soap_parameter_get_first_child_by_name (changes, "add");
		if (!subparameter)
			subparameter = soup_soap_parameter_get_first_child_by_name (changes, "delete");
		if (!subparameter)
			subparameter = soup_soap_parameter_get_first_child_by_name (changes, "update");
	}
	else subparameter = param; /* The item is a complete one, not a delta  */

	/* now add all properties to the private structure */
	for (child = soup_soap_parameter_get_first_child (subparameter);
			child != NULL;
			child = soup_soap_parameter_get_next_child (child)) {
		const gchar *name;
		gchar *value = NULL;

		name = soup_soap_parameter_get_name (child);

		if (!g_ascii_strcasecmp (name, "security")) {
			item->priv->security = soup_soap_parameter_get_string_value (child);
			item->priv->classification = soup_soap_parameter_get_string_value (child);
			continue;
		}
		if (!g_ascii_strcasecmp (name, "acceptLevel"))
			item->priv->accept_level = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "class")) {
			item->priv->classification = soup_soap_parameter_get_string_value (child);

		} else if (!g_ascii_strcasecmp (name, "completed")) {
			value = soup_soap_parameter_get_string_value (child);
			if (!g_ascii_strcasecmp (value, "1"))
				item->priv->completed = TRUE;
			else
				item->priv->completed = FALSE;
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "allDayEvent")) {
			value = soup_soap_parameter_get_string_value (child);
			if (!g_ascii_strcasecmp (value, "1"))
				item->priv->is_allday_event = TRUE;
			else
				item->priv->is_allday_event = FALSE;
			g_free (value);

		} else if (!g_ascii_strcasecmp (name, "status")) {
			SoupSoapParameter *status_param;
			const gchar *status_name;
			item->priv->self_status = 0;
			for (status_param = soup_soap_parameter_get_first_child (child); status_param != NULL;
					status_param = soup_soap_parameter_get_next_child (status_param)) {
				status_name = soup_soap_parameter_get_name (status_param);

				if (!strcmp (status_name, "accepted"))
					item->priv->self_status |= E_EWS_ITEM_STAT_ACCEPTED;
				else if (!strcmp (status_name, "declined"))
					item->priv->self_status |= E_EWS_ITEM_STAT_DECLINED;
				else if (!strcmp (status_name, "deleted"))
					item->priv->self_status |= E_EWS_ITEM_STAT_DELETED;
				else if (!strcmp (status_name, "read"))
					item->priv->self_status |= E_EWS_ITEM_STAT_READ;
				else if (!strcmp (status_name, "opened"))
					item->priv->self_status |= E_EWS_ITEM_STAT_OPENED;
				else if (!strcmp (status_name, "completed"))
					item->priv->self_status |= E_EWS_ITEM_STAT_COMPLETED;
				else if (!strcmp (status_name, "forwarded"))
					item->priv->self_status |= E_EWS_ITEM_STAT_FORWARDED;
				else if (!strcmp (status_name, "replied"))
					item->priv->self_status |= E_EWS_ITEM_STAT_REPLIED;
				else if (!strcmp (status_name, "delegated"))
					item->priv->self_status |= E_EWS_ITEM_STAT_DELEGATED;
			}

		} else if (!g_ascii_strcasecmp (name, "created")) {
			gchar *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_ews_connection_format_date_string (value);
			e_ews_item_set_creation_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);

		} else if (!g_ascii_strcasecmp (name, "delivered")) {
			gchar *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_ews_connection_format_date_string (value);
			e_ews_item_set_delivered_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);

		} else if (!g_ascii_strcasecmp (name, "modified")) {
			gchar *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_ews_connection_format_date_string (value);
			e_ews_item_set_modified_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);
		} else if (!g_ascii_strcasecmp (name, "distribution")) {
			SoupSoapParameter *tp;

			tp = soup_soap_parameter_get_first_child_by_name (child, "recipients");
			if (tp) {
				g_slist_foreach (item->priv->recipient_list, (GFunc) free_recipient, NULL);
				item->priv->recipient_list = NULL;
				set_recipient_list_from_soap_parameter (item, tp);
			}

			tp = soup_soap_parameter_get_first_child_by_name (child, "sendoptions");
			if (tp)
				set_sendoptions_from_soap_parameter (item, tp);

			tp = soup_soap_parameter_get_first_child_by_name (child, "from");
			if (tp && is_group_item) {
				SoupSoapParameter *subparam;
				EEwsItemOrganizer *organizer = g_new0 (EEwsItemOrganizer, 1);

				subparam = soup_soap_parameter_get_first_child_by_name (tp, "displayName");
				if (subparam)
					organizer->display_name = soup_soap_parameter_get_string_value (subparam);
				subparam = soup_soap_parameter_get_first_child_by_name (tp, "email");
				if (subparam)
					organizer->email = soup_soap_parameter_get_string_value (subparam);
				e_ews_item_set_organizer (item, organizer);
			}
		} else if (!g_ascii_strcasecmp (name, "size")) {
			item->priv->size = soup_soap_parameter_get_int_value (child);

		} else if (!g_ascii_strcasecmp (name, "hasAttachment")) {
			item->priv->has_attachment = soup_soap_parameter_get_int_value (child);

		} else if (!g_ascii_strcasecmp (name, "options")) {
			SoupSoapParameter *subparam;

			subparam = soup_soap_parameter_get_first_child_by_name (child, "priority");
			if (subparam) {
				item->priv->priority = soup_soap_parameter_get_string_value (subparam);
			}

			subparam = soup_soap_parameter_get_first_child_by_name (child, "expires");
			if (subparam)
				value = soup_soap_parameter_get_string_value (subparam);
			if (value)
				item->priv->expires = g_strdup (value);
			g_free (value), value = NULL;

			subparam = soup_soap_parameter_get_first_child_by_name (child, "delayDeliveryUntil");
			if (subparam)
				value = soup_soap_parameter_get_string_value (subparam);
			if (value)
				item->priv->delay_until = g_strdup (value);
			g_free (value), value = NULL;

		} else if (!g_ascii_strcasecmp (name, "dueDate")) {
			gchar *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_ews_connection_format_date_string (value);
			e_ews_item_set_due_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);

		} else if (!g_ascii_strcasecmp (name, "endDate") || !g_ascii_strcasecmp (name, "endDay")) {
			gchar *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_ews_connection_format_date_string (value);
			e_ews_item_set_end_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);

		} else if (!g_ascii_strcasecmp (name, "to")) {
			gchar *to;
			to = soup_soap_parameter_get_string_value (child);
			e_ews_item_set_to (item, to);
			g_free (to);
		} else if (!g_ascii_strcasecmp (name, "iCalId"))
			item->priv->icalid = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "categories")) {
			for (category_param = soup_soap_parameter_get_first_child_by_name (child, "category");
					category_param != NULL;
					category_param = soup_soap_parameter_get_next_child_by_name (category_param, "category")) {

				value = soup_soap_parameter_get_string_value (category_param);
				if (value) {
					gchar **components = g_strsplit (value, "@", -1);
					g_free (value);
					value = components[0];
					item->priv->category_list = g_list_append (item->priv->category_list, g_strdup (value));
					g_strfreev(components);
				}
			}

		} else if (!g_ascii_strcasecmp (name, "id"))
			item->priv->id = soup_soap_parameter_get_string_value (child);
		else if (!g_ascii_strcasecmp (name, "recurrenceKey"))
			item->priv->recurrence_key = soup_soap_parameter_get_int_value (child);
		else if (!g_ascii_strcasecmp (name, "message")) {
			SoupSoapParameter *part;
			gsize len;
			gchar *msg;
			gchar *length;

			part = soup_soap_parameter_get_first_child_by_name (child, "part");
			msg = soup_soap_parameter_get_string_value (part);
			length = soup_soap_parameter_get_property (part, "length");

			if (msg && length && atoi (length) > 0) {
				len = atoi (length);
				item->priv->message = (gchar *) g_base64_decode (msg, &len);
				if (!(item->priv->message)) {
					item->priv->msg_body_id = soup_soap_parameter_get_property (part, "id");
				}
				item->priv->content_type = soup_soap_parameter_get_property (part, "contentType");
			}

			g_free (length);
			g_free (msg);

		} else if (!g_ascii_strcasecmp (name, "attachments")) {
			for (attachment_param = soup_soap_parameter_get_first_child_by_name(child,"attachment");
					attachment_param != NULL;
					attachment_param = soup_soap_parameter_get_next_child_by_name (attachment_param, "attachment")) {

				SoupSoapParameter *temp;
				EEwsItemAttachment *attach = g_new0 (EEwsItemAttachment, 1);

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "id");
				if (temp) {
					attach->id = soup_soap_parameter_get_string_value (temp);
					attach->item_reference =  soup_soap_parameter_get_property (temp, "itemReference");
				}

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "contentId");
				if (temp)
					attach->contentid = soup_soap_parameter_get_string_value (temp);

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "name");
				if (temp)
					attach->name = soup_soap_parameter_get_string_value (temp);

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "contentType");
				if (temp)
					attach->contentType = soup_soap_parameter_get_string_value (temp);

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "size");
				if (temp) {
					value = soup_soap_parameter_get_string_value (temp);
					attach->size = atoi (value);
					g_free (value);
				}

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "date");
				if (temp)
					attach->date = soup_soap_parameter_get_string_value (temp);

				temp = soup_soap_parameter_get_first_child_by_name (attachment_param, "hidden");
				if (temp)
					if (soup_soap_parameter_get_int_value (temp) == 1)
						attach->hidden = TRUE;

				item->priv->attach_list = g_slist_append (item->priv->attach_list, attach);
			}

		} else if (!g_ascii_strcasecmp (name, "place"))
			item->priv->place = soup_soap_parameter_get_string_value (child);

		else if (!g_ascii_strcasecmp (name, "taskPriority")) {
			gchar *priority;

			priority = soup_soap_parameter_get_string_value (child);
			e_ews_item_set_task_priority (item, priority);
			g_free (priority);
		}

		else if (!g_ascii_strcasecmp (name, "startDate") || !g_ascii_strcasecmp (name, "startDay")) {
			gchar *formatted_date;
			value = soup_soap_parameter_get_string_value (child);
			formatted_date = e_ews_connection_format_date_string (value);
			e_ews_item_set_start_date (item, formatted_date);
			g_free (value);
			g_free (formatted_date);

		} else if (!g_ascii_strcasecmp (name, "subject"))
			item->priv->subject = soup_soap_parameter_get_string_value (child);
		else if (!g_ascii_strcasecmp (name, "source")) {
			value = soup_soap_parameter_get_string_value (child);
			if (!strcmp (value, "personal")) {
				if (item->priv->item_type == E_EWS_ITEM_TYPE_TASK ||
						item->priv->item_type == E_EWS_ITEM_TYPE_APPOINTMENT)
					is_group_item = FALSE;
			}
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "link")) {
			SoupSoapParameter *subparam;
			EEwsItemLinkInfo *info = g_new0 (EEwsItemLinkInfo, 1);

			subparam = soup_soap_parameter_get_first_child_by_name (child, "id");
			if (subparam)
				info->id = soup_soap_parameter_get_string_value (subparam);

			subparam = soup_soap_parameter_get_first_child_by_name (child, "type");
			if (subparam)
				info->type =soup_soap_parameter_get_string_value (subparam);

			subparam = soup_soap_parameter_get_first_child_by_name (child, "thread");
			if (subparam)
				info->thread =soup_soap_parameter_get_string_value (subparam);

			e_ews_item_set_link_info (item, info);

		} else if (!g_ascii_strcasecmp (name, "alarm")) {
			gchar *enabled;
			enabled = soup_soap_parameter_get_property (child, "enabled");
			if (enabled && !g_ascii_strcasecmp (enabled, "1") ) {
				value = soup_soap_parameter_get_string_value (child);
				/* convert it into integer */
				item->priv->trigger = atoi (value);
				g_free (value);
			}
			g_free (enabled);
		} else if (!g_ascii_strcasecmp (name, "threading")) {
			SoupSoapParameter *subparam;

			subparam = soup_soap_parameter_get_first_child_by_name (child, "parent");
			if (subparam)
				item->priv->parent_threads = soup_soap_parameter_get_string_value (subparam);

		} else if (!g_ascii_strcasecmp (name, "messageId")) {
			item->priv->message_id = soup_soap_parameter_get_string_value (child);
		}

	}

return item;
}

EEwsItemType
e_ews_item_get_item_type (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_EWS_ITEM_TYPE_UNKNOWN);

	return item->priv->item_type;
}

void
e_ews_item_set_item_type (EEwsItem *item, EEwsItemType new_type)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->item_type = new_type;
}

const gchar *
e_ews_item_get_container_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->container;
}

void
e_ews_item_set_container_id (EEwsItem *item, const gchar *new_id)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->container)
		g_free (item->priv->container);
	item->priv->container = g_strdup (new_id);
}

const gchar *
e_ews_item_get_icalid (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->icalid;
}

void
e_ews_item_set_icalid (EEwsItem *item, const gchar *new_icalid)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->icalid)
		g_free (item->priv->icalid);
	item->priv->icalid = g_strdup (new_icalid);
}

const gchar *
e_ews_item_get_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->id;
}

void
e_ews_item_set_id (EEwsItem *item, const gchar *new_id)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->id)
		g_free (item->priv->id);
	item->priv->id = g_strdup (new_id);
}

gint
e_ews_item_get_mail_size (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), 0);

	return item->priv->size;
}

gboolean
e_ews_item_has_attachment (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), 0);

	return item->priv->has_attachment;
}

gboolean
e_ews_item_is_from_internet (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), 0);

	return item->priv->internet;
}

gchar *
e_ews_item_get_creation_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->creation_date;
}

void
e_ews_item_set_creation_date (EEwsItem *item, const gchar *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->creation_date)
		g_free (item->priv->creation_date);
	item->priv->creation_date = g_strdup (new_date);
}

gchar *
e_ews_item_get_delivered_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->delivered_date;
}

void
e_ews_item_set_delivered_date (EEwsItem *item, const gchar *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->delivered_date)
		g_free (item->priv->delivered_date);
	item->priv->delivered_date = g_strdup (new_date);
}

const gchar *
e_ews_item_get_modified_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->modified_date;
}

void
e_ews_item_set_modified_date (EEwsItem *item, const gchar *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->modified_date)
		g_free (item->priv->modified_date);
	item->priv->modified_date = g_strdup (new_date);
}

gchar *
e_ews_item_get_start_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->start_date;
}

void
e_ews_item_set_start_date (EEwsItem *item, const gchar *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->start_date)
		g_free (item->priv->start_date);
	item->priv->start_date = g_strdup (new_date);
}

gchar *
e_ews_item_get_end_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->end_date;
}

void
e_ews_item_set_end_date (EEwsItem *item, const gchar *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->end_date)
		g_free (item->priv->end_date);
	item->priv->end_date = g_strdup (new_date);
}

gchar *
e_ews_item_get_due_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->due_date;
}

void
e_ews_item_set_due_date (EEwsItem *item, const gchar *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->due_date)
		g_free (item->priv->due_date);
	item->priv->due_date = g_strdup (new_date);
}

gchar *
e_ews_item_get_completed_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->completed_date;
}

void
e_ews_item_set_completed_date (EEwsItem *item, const gchar *new_date)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->completed_date)
		g_free (item->priv->completed_date);
	item->priv->completed_date = g_strdup (new_date);
}

const gchar *
e_ews_item_get_subject (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->subject;
}

void
e_ews_item_set_subject (EEwsItem *item, const gchar *new_subject)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->subject)
		g_free (item->priv->subject);
	item->priv->subject = g_strdup (new_subject);
}

const gchar *
e_ews_item_get_message (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->message;
}

void
e_ews_item_set_message (EEwsItem *item, const gchar *new_message)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->message)
		g_free (item->priv->message);
	item->priv->message = g_strdup (new_message);
}

const gchar *
e_ews_item_get_place (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->place;
}

void
e_ews_item_set_place (EEwsItem *item, const gchar *new_place)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->place)
		g_free (item->priv->place);
	item->priv->place = g_strdup (new_place);
}

const gchar *
e_ews_item_get_classification (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->classification;
}

void
e_ews_item_set_classification (EEwsItem *item, const gchar *new_class)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->classification)
		g_free (item->priv->classification);
	item->priv->classification = g_strdup (new_class);
}

const gchar *
e_ews_item_get_security (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->security;
}

void
e_ews_item_set_security (EEwsItem *item, const gchar *new_class)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->security)
		g_free (item->priv->security);
	item->priv->security = g_strdup (new_class);
}

gboolean
e_ews_item_get_completed (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->completed;
}

void
e_ews_item_set_completed (EEwsItem *item, gboolean new_completed)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->completed = new_completed;
}

gboolean
e_ews_item_get_is_allday_event (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->is_allday_event;
}

void
e_ews_item_set_is_allday_event (EEwsItem *item, gboolean allday_event)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->is_allday_event = allday_event;
}

const gchar *
e_ews_item_get_accept_level (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->accept_level;
}

void
e_ews_item_set_accept_level (EEwsItem *item, const gchar *new_level)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->accept_level)
		g_free (item->priv->accept_level);
	item->priv->accept_level = g_strdup (new_level);
}

const gchar *
e_ews_item_get_priority (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return (const gchar *) item->priv->priority;
}

void
e_ews_item_set_priority (EEwsItem *item, const gchar *new_priority)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->priority)
		g_free (item->priv->priority);
	item->priv->priority = g_strdup (new_priority);
}

const gchar *
e_ews_item_get_task_priority (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return (const gchar *) item->priv->task_priority;
}

void
e_ews_item_set_task_priority (EEwsItem *item, const gchar *new_priority)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->task_priority)
		g_free (item->priv->task_priority);
	item->priv->task_priority = g_strdup (new_priority);
}
GSList *
e_ews_item_get_recipient_list (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->recipient_list;
}

void
e_ews_item_set_recipient_list (EEwsItem  *item, GSList *new_recipient_list)
{
	/* free old list and set a new one*/
	g_slist_foreach (item->priv->recipient_list, (GFunc) free_recipient, NULL);
	g_slist_free (item->priv->recipient_list);
	item->priv->recipient_list = new_recipient_list;
}

EEwsItemOrganizer *
e_ews_item_get_organizer (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->organizer;
}

void
e_ews_item_set_attach_id_list (EEwsItem *item, GSList *attach_list)
{
	g_return_if_fail (E_IS_GW_ITEM (item));
	if (attach_list) {
		g_slist_foreach (item->priv->attach_list, (GFunc)free_attach, NULL);
		g_slist_free (item->priv->attach_list);
	}
	item->priv->attach_list = attach_list;
}

GSList *
e_ews_item_get_attach_id_list (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->attach_list;
}

void
e_ews_item_set_organizer (EEwsItem  *item, EEwsItemOrganizer *organizer)
{
	/* free organizer */
	if (item->priv->organizer) {
		g_free (item->priv->organizer->display_name);
		g_free (item->priv->organizer->email);
		g_free (item->priv->organizer);
	}
	item->priv->organizer = organizer;
}

GSList *
e_ews_item_get_recurrence_dates (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->recurrence_dates;
}

void
e_ews_item_set_recurrence_dates (EEwsItem  *item, GSList *new_recurrence_dates)
{
	/* free old list and set a new one*/
	g_slist_foreach (item->priv->recurrence_dates, free_string, NULL);
	/*free the list */
	g_slist_free (item->priv->recurrence_dates);
	item->priv->recurrence_dates = new_recurrence_dates;
}

GSList *
e_ews_item_get_exdate_list (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->exdate_list;
}

void
e_ews_item_set_exdate_list (EEwsItem  *item, GSList *new_exdate_list)
{
	/* free old list and set a new one*/
	g_slist_foreach (item->priv->exdate_list, free_string, NULL);
	/*free the list */
	g_slist_free (item->priv->exdate_list);
	item->priv->exdate_list = new_exdate_list;
}

EEwsItemRecurrenceRule *
e_ews_item_get_rrule (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->rrule;
}

void
e_ews_item_set_rrule (EEwsItem  *item, EEwsItemRecurrenceRule *new_rrule)
{
	if (item->priv->rrule) {
	/* TODO free old list and set a new one*/
	}
	item->priv->rrule = new_rrule;
}

gint
e_ews_item_get_recurrence_key (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), 0);
	return item->priv->recurrence_key;
}

void
e_ews_item_set_recurrence_key (EEwsItem *item, gint recur_key)
{
	item->priv->recurrence_key = recur_key;
}

gint
e_ews_item_get_trigger (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), 0);

	return item->priv->trigger;
}

void
e_ews_item_set_trigger (EEwsItem *item, gint trigger)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->trigger = trigger;
}

void
e_ews_item_set_to (EEwsItem *item, const gchar *to)
{
	g_return_if_fail (E_IS_GW_ITEM (item));
	item->priv->to = g_strdup (to);
}

const gchar *
e_ews_item_get_to (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM(item), NULL);
	return item->priv->to;
}

const gchar *
e_ews_item_get_msg_content_type (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->content_type;
}

const gchar *
e_ews_item_get_msg_body_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->msg_body_id;
}

void
e_ews_item_set_sendoptions (EEwsItem *item, gboolean set)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->set_sendoptions = set;
}

void
e_ews_item_set_reply_request (EEwsItem *item, gboolean set)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->reply_request_set = set;
}

gboolean
e_ews_item_get_reply_request (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->reply_request_set;
}

void
e_ews_item_set_reply_within (EEwsItem *item, gchar *reply_within)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->reply_within = g_strdup (reply_within);
}

gchar *
e_ews_item_get_reply_within (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->reply_within;
}

void
e_ews_item_set_track_info (EEwsItem *item, EEwsItemTrack track_info)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->track_info = track_info;
}

EEwsItemTrack
e_ews_item_get_track_info (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), E_EWS_ITEM_NONE);

	return item->priv->track_info;
}

void
e_ews_item_set_autodelete (EEwsItem *item, gboolean set)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->autodelete = set;
}

gboolean
e_ews_item_get_autodelete (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->autodelete;
}

void
e_ews_item_set_notify_completed (EEwsItem *item, EEwsItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_completed = notify;
}

EEwsItemReturnNotify
e_ews_item_get_notify_completed (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_completed;
}

void
e_ews_item_set_notify_accepted (EEwsItem *item, EEwsItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_accepted = notify;
}

EEwsItemReturnNotify
e_ews_item_get_notify_accepted (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_accepted;
}

void
e_ews_item_set_notify_declined (EEwsItem *item, EEwsItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_declined = notify;
}

EEwsItemReturnNotify
e_ews_item_get_notify_declined (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_declined;
}

void
e_ews_item_set_notify_opened (EEwsItem *item, EEwsItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_opened = notify;
}

EEwsItemReturnNotify
e_ews_item_get_notify_opened (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_opened;
}

void
e_ews_item_set_notify_deleted (EEwsItem *item, EEwsItemReturnNotify notify)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->notify_deleted = notify;
}

EEwsItemReturnNotify
e_ews_item_get_notify_deleted (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);

	return item->priv->notify_deleted;
}

void
e_ews_item_set_expires (EEwsItem *item, gchar *expires)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->expires = g_strdup (expires);
}

gchar *
e_ews_item_get_expires (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->expires;
}

void
e_ews_item_set_delay_until (EEwsItem *item, gchar *delay_until)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	item->priv->delay_until = g_strdup (delay_until);
}

gchar *
e_ews_item_get_delay_until (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->delay_until;
}

void
e_ews_item_set_source (EEwsItem *item, const gchar *source)
{
	g_return_if_fail (E_IS_GW_ITEM (item));
	item->priv->source = g_strdup (source);
}

void
e_ews_item_set_content_type (EEwsItem *item, const gchar *content_type)
{
	g_return_if_fail (E_IS_GW_ITEM (item));

	if (item->priv->content_type)
		g_free (item->priv->content_type);
	item->priv->content_type= g_strdup (content_type);
}

gchar *
e_ews_item_get_content_type (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);

	return item->priv->content_type;
}

void
e_ews_item_set_link_info (EEwsItem *item, EEwsItemLinkInfo *info)
{
	g_return_if_fail (E_IS_GW_ITEM (item));
	item->priv->link_info = info;
}

EEwsItemLinkInfo *
e_ews_item_get_link_info (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_GW_ITEM (item), NULL);
	return item->priv->link_info;
}

static void
add_return_notification (SoupSoapMessage *msg, const gchar *option, EEwsItemReturnNotify value)
{
	soup_soap_message_start_element (msg, option, NULL, NULL);

	switch (value) {
		case E_EWS_ITEM_NOTIFY_MAIL:
			e_ews_message_write_string_parameter (msg, "mail", NULL, "1");
			break;
		case E_EWS_ITEM_NOTIFY_NONE:
			e_ews_message_write_string_parameter (msg, "mail", NULL, "0");
	}

	soup_soap_message_end_element (msg);
}

static void
append_gw_item_options (SoupSoapMessage *msg, EEwsItem *item)
{
	EEwsItemPrivate *priv;

	priv = item->priv;

	soup_soap_message_start_element (msg, "options", NULL, NULL);

	/* Priority */
	e_ews_message_write_string_parameter (msg, "priority", NULL, priv->priority ? priv->priority : "0");

	/* Expiration date */
	e_ews_message_write_string_parameter (msg, "expires", NULL, priv->expires ? priv->expires : "0");

	/* Delay delivery */
	e_ews_message_write_string_parameter (msg, "delayDeliveryUntil", NULL, priv->delay_until ? priv->delay_until : "0");

	soup_soap_message_end_element (msg);
}

void
e_ews_item_add_distribution_to_soap_message (EEwsItem *item, SoupSoapMessage *msg)
{
	GSList *rl;
	EEwsItemPrivate *priv;
	EEwsItemOrganizer *organizer;
	GSList *recipient_list;

	priv = item->priv;
	organizer = priv->organizer;
	recipient_list = priv->recipient_list;

	/* start distribution element */
	soup_soap_message_start_element (msg, "distribution", NULL, NULL);
	if (organizer) {
		soup_soap_message_start_element (msg, "from", NULL, NULL);
		e_ews_message_write_string_parameter (msg, "displayName", NULL,
				organizer->display_name ? organizer->display_name : "");
		e_ews_message_write_string_parameter (msg, "email", NULL,
				organizer->email ? organizer->email : "");

		soup_soap_message_end_element (msg);
	}
	/* start recipients */
	soup_soap_message_start_element (msg, "recipients", NULL, NULL);
	/* add each recipient */
	for (rl = recipient_list; rl != NULL; rl = rl->next) {
		const gchar *dist_type;
		const gchar *status = NULL;

		EEwsItemRecipient *recipient = (EEwsItemRecipient *) rl->data;

		soup_soap_message_start_element (msg, "recipient", NULL, NULL);
		e_ews_message_write_string_parameter (msg, "displayName", NULL, recipient->display_name ? recipient->display_name : "");
		e_ews_message_write_string_parameter (msg, "email", NULL, recipient->email ? recipient->email : "");
		if (recipient->type == E_EWS_ITEM_RECIPIENT_TO)
			dist_type = "TO";
		else if (recipient->type == E_EWS_ITEM_RECIPIENT_CC)
			dist_type = "CC";
		else if (recipient->type == E_EWS_ITEM_RECIPIENT_BC)
			dist_type = "BC";
		else
			dist_type ="";
		e_ews_message_write_string_parameter (msg, "distType", NULL, dist_type);
		/* add recip_status */
		if (recipient->status == E_EWS_ITEM_STAT_ACCEPTED)
			status = "accepted";
		else if (recipient->status == E_EWS_ITEM_STAT_DECLINED)
			status = "declined";
		else
			status = "";
		if (status && *status)
			e_ews_message_write_string_parameter (msg, "recipientStatus", NULL, status);

		soup_soap_message_end_element (msg);
	}

	soup_soap_message_end_element (msg);

	if (priv->set_sendoptions) {
		soup_soap_message_start_element (msg, "sendoptions", NULL, NULL);

		soup_soap_message_start_element (msg, "requestReply", NULL, NULL);
		if (priv->reply_request_set) {

			if (priv->reply_within)
				e_ews_message_write_string_parameter (msg, "byDate", NULL, priv->reply_within);
			else
				e_ews_message_write_string_parameter (msg, "whenConvenient", NULL, "1");

		} else
			soup_soap_message_write_string (msg, "0");
		soup_soap_message_end_element (msg);

		soup_soap_message_start_element (msg, "statusTracking", NULL, NULL);

		soup_soap_message_add_attribute (msg, "autoDelete", priv->autodelete ? "1" : "0", NULL, NULL);

		switch (priv->track_info) {
			case E_EWS_ITEM_DELIVERED : soup_soap_message_write_string (msg, "Delivered");
				 break;
			case E_EWS_ITEM_DELIVERED_OPENED : soup_soap_message_write_string (msg, "DeliveredAndOpened");
				 break;
			case E_EWS_ITEM_ALL : soup_soap_message_write_string (msg, "All");
				 break;
			default: soup_soap_message_write_string (msg, "None");
		}

		soup_soap_message_end_element (msg);

		soup_soap_message_start_element (msg, "notification", NULL, NULL);
		switch (priv->item_type) {

		case E_EWS_ITEM_TYPE_TASK :
			add_return_notification (msg, "completed", priv->notify_completed);

		case E_EWS_ITEM_TYPE_APPOINTMENT:
			add_return_notification (msg, "accepted", priv->notify_accepted);
			add_return_notification (msg, "declined", priv->notify_declined);
			add_return_notification (msg, "opened", priv->notify_opened);
			break;

		default:
			add_return_notification (msg, "opened", priv->notify_opened);
			add_return_notification (msg, "deleted", priv->notify_deleted);
		}
		soup_soap_message_end_element (msg);

		soup_soap_message_end_element (msg);
	}

	soup_soap_message_end_element (msg);
}

static void
add_attachment_to_soap_message(EEwsItemAttachment *attachment, SoupSoapMessage *msg)
{
	gchar *size = NULL;

	soup_soap_message_start_element (msg, "attachment", NULL, NULL);

	/*id*/
	if (attachment->id && attachment->item_reference)
		e_ews_message_write_string_parameter_with_attribute (msg,"id", NULL, attachment->id, "itemReference", attachment->item_reference);
	else if (attachment->id)
		e_ews_message_write_string_parameter (msg, "id", NULL, attachment->id);
	else
		e_ews_message_write_string_parameter (msg, "id", NULL, "");
	/*name*/
	e_ews_message_write_string_parameter (msg, "name", NULL, attachment->name);
	/*content id*/
	e_ews_message_write_string_parameter (msg, "contentId", NULL, attachment->contentid);
	/*content type*/
	e_ews_message_write_string_parameter (msg, "contentType", NULL, attachment->contentType);
	/*size*/
	if (attachment->size > 0) {
		size = g_strdup_printf ("%d", attachment->size);
		e_ews_message_write_string_parameter (msg, "size", NULL, size);
	}
	/*date*/
	if (attachment->date)
		e_ews_message_write_string_parameter (msg, "date", NULL, attachment->date);

	/*data*/
	if (attachment->size > 0) {
		soup_soap_message_start_element (msg, "data", NULL, NULL);
		soup_soap_message_add_attribute (msg, "contentId", attachment->id, NULL, NULL);
		soup_soap_message_add_attribute (msg, "contentType", attachment->contentType, NULL, NULL);
		soup_soap_message_add_attribute (msg, "length", size, NULL, NULL);
		soup_soap_message_write_string (msg, attachment->data);
		soup_soap_message_end_element (msg);
	}

	soup_soap_message_end_element (msg);
	g_free (size);
}

static void
e_ews_item_set_calendar_item_elements (EEwsItem *item, SoupSoapMessage *msg)
{
	EEwsItemPrivate *priv = item->priv;
	gchar *dtstring;

	if (priv->id)
		e_ews_message_write_string_parameter (msg, "id", NULL, priv->id);
	if (priv->container)
		e_ews_message_write_string_parameter (msg, "container", NULL, priv->container);

	if (priv->icalid)
		e_ews_message_write_string_parameter (msg, "iCalId", NULL, priv->icalid);

	if (item->priv->source)
		e_ews_message_write_string_parameter (msg, "source", NULL, item->priv->source );

	if (priv->classification)
		e_ews_message_write_string_parameter (msg, "class", NULL, priv->classification);
	else
		e_ews_message_write_string_parameter (msg, "class", NULL, "");

	e_ews_message_write_string_parameter (msg, "subject", NULL, priv->subject ? priv->subject : "");

	if (priv->recipient_list != NULL) {
		e_ews_item_add_distribution_to_soap_message (item, msg);
		if (priv->set_sendoptions)
			append_gw_item_options (msg, item);
	}

	soup_soap_message_start_element (msg, "message", NULL, NULL);
	if (priv->message) {
		gchar *str;

		str = g_base64_encode ((guchar *) priv->message, strlen (priv->message));
		dtstring = g_strdup_printf ("%d", (gint)strlen (str));
		soup_soap_message_add_attribute (msg, "length", dtstring, NULL, NULL);
		g_free (dtstring);
		soup_soap_message_write_string (msg, str);
		g_free (str);
	} else {
		soup_soap_message_add_attribute (msg, "length", "0", NULL, NULL);
		soup_soap_message_write_string (msg, "");
	}

	soup_soap_message_end_element (msg);

	if (priv->start_date) {
		e_ews_message_write_string_parameter (msg, "startDate", NULL,
				priv->start_date);
	}

	if (priv->category_list) {
			soup_soap_message_start_element (msg, "categories", NULL, NULL);

			if (priv->category_list && priv->category_list->data)
				soup_soap_message_add_attribute (msg, "types:primary", priv->category_list->data, NULL, NULL);

			for (; priv->category_list != NULL; priv->category_list = g_list_next (priv->category_list))
				if (priv->category_list->data) {
					e_ews_message_write_string_parameter (msg, "category", NULL, priv->category_list->data);
				}
			soup_soap_message_end_element (msg);
		}

	/* handle recurrences */
	 if (item->priv->rrule) {
		EEwsItemRecurrenceRule *rrule = item->priv->rrule;

		soup_soap_message_start_element (msg, "rrule", NULL, NULL);

		e_ews_message_write_string_parameter (msg, "frequency", NULL, rrule->frequency);
		e_ews_message_write_int_parameter (msg, "interval", NULL, rrule->interval);

		if (rrule->until)
			e_ews_message_write_string_parameter (msg, "until", NULL, rrule->until);
		else
			e_ews_message_write_int_parameter (msg, "count", NULL, rrule->count);

		/* byDay */
		if (rrule->by_day[0]) {
			gint i, max_elements;
			soup_soap_message_start_element (msg, "byDay", NULL, NULL);
			max_elements = G_N_ELEMENTS (rrule->by_day);
			/* expand into  a sequence of 'day' here  */
			for (i = 0; i < max_elements && rrule->by_day [i] != E_EWS_ITEM_RECUR_END_MARKER; i++) {
				const gchar *dow = e_ews_recur_get_day_of_week (rrule->by_day [i]);

				if (rrule->by_setpos [i] == E_EWS_ITEM_RECUR_END_MARKER)
					e_ews_message_write_string_parameter (msg, "day", NULL, dow);
				else {
					gchar occur [3];

					g_sprintf (occur, "%d", rrule->by_setpos [i]);
					e_ews_message_write_string_parameter_with_attribute (msg, "day", NULL, dow, "occurrence", occur);
				}
			}
			soup_soap_message_end_element (msg);
		}

		/* byMonthDay*/
		if (rrule->by_month_day) {
			gint i, max_elements;
			gchar month_day[3];

			soup_soap_message_start_element (msg, "byMonthDay", NULL, NULL);
			max_elements = G_N_ELEMENTS (rrule->by_month_day);
			/* expand into  a sequence of 'day' here  */
			for (i = 0; i < max_elements && rrule->by_month_day [i] != E_EWS_ITEM_RECUR_END_MARKER; i++) {
				/*TODO occurence attribute */
				g_sprintf (month_day, "%d", rrule->by_month_day [i]);
				e_ews_message_write_string_parameter (msg, "day", NULL, month_day);

			}
			soup_soap_message_end_element (msg);
		}
		/* byYearDay */
	if (rrule->by_year_day) {
			gint i, max_elements;
			gchar year_day[4];
			soup_soap_message_start_element (msg, "byYearDay", NULL, NULL);
			max_elements = G_N_ELEMENTS (rrule->by_year_day);
			/* expand into  a sequence of 'day' here  */
			for (i = 0; i < max_elements && rrule->by_year_day [i] != E_EWS_ITEM_RECUR_END_MARKER; i++) {
				/*TODO occurence attribute */
				g_sprintf (year_day, "%d", rrule->by_year_day [i]);
				e_ews_message_write_string_parameter (msg, "day", NULL, year_day);

			}
		soup_soap_message_end_element (msg);
		}
		/* byMonth */
		if (rrule->by_month) {
			gint i, max_elements;
			gchar month[3];
			soup_soap_message_start_element (msg, "byMonth", NULL, NULL);
			max_elements = G_N_ELEMENTS (rrule->by_month);
			/* expand into  a sequence of 'month' here  */
			for (i = 0; i < max_elements && rrule->by_month [i] != E_EWS_ITEM_RECUR_END_MARKER; i++) {
				/*TODO occurence attribute */
				g_sprintf (month, "%d", rrule->by_month [i]);
				e_ews_message_write_string_parameter (msg, "month", NULL, month);

			}
			soup_soap_message_end_element (msg);
		}
		soup_soap_message_end_element (msg);

	} else if (item->priv->recurrence_dates) {
		GSList *date;
		soup_soap_message_start_element (msg, "rdate", NULL, NULL);
		for (date = item->priv->recurrence_dates; date != NULL; date = g_slist_next (date)) {
			e_ews_message_write_string_parameter (msg, "date", NULL, (gchar *) date->data);
		}
		soup_soap_message_end_element (msg);
	}

	if (item->priv->exdate_list) {
		GSList *date;
		soup_soap_message_start_element (msg, "exdate", NULL, NULL);
		for (date = item->priv->exdate_list; date != NULL; date = g_slist_next (date)) {
			e_ews_message_write_string_parameter (msg, "date", NULL, (gchar *) date->data);
		}
		soup_soap_message_end_element (msg);
	}
	/*xxx - byday, bymonthday and byyearday not handled - FIXME */

	/*attachments*/
	if (priv->attach_list) {
		GSList *al;
		soup_soap_message_start_element (msg, "attachments", NULL, NULL);
		for (al = priv->attach_list; al != NULL;  al = al->next) {
			EEwsItemAttachment *attachment = (EEwsItemAttachment *)al->data;
			add_attachment_to_soap_message (attachment, msg);

		}
		soup_soap_message_end_element (msg);
	}
}

gboolean
e_ews_item_append_to_soap_message (EEwsItem *item, SoupSoapMessage *msg)
{
	EEwsItemPrivate *priv;
	gchar *alarm;

	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), FALSE);

	priv = item->priv;

	soup_soap_message_start_element (msg, "item", "types", NULL);

	switch (priv->item_type) {
	case E_EWS_ITEM_TYPE_MAIL :
		soup_soap_message_add_attribute (msg, "type", "Mail", "xsi", NULL);

		if (priv->security)
			e_ews_message_write_string_parameter (msg, "security", NULL, priv->security);

		/*The subject*/
		if (priv->subject)
			e_ews_message_write_string_parameter (msg, "subject", NULL, priv->subject);
		/*distribution*/
		e_ews_item_add_distribution_to_soap_message(item, msg);

		if (priv->set_sendoptions) {
			/* item options */
			append_gw_item_options (msg, item);
		}
		/*container*/
		if (item->priv->container)
			e_ews_message_write_string_parameter (msg, "container", NULL, item->priv->container);
		if (item->priv->source)
			e_ews_message_write_string_parameter (msg, "source", NULL, item->priv->source);

		/*message*/
		soup_soap_message_start_element (msg, "message", NULL, NULL);
		if (priv->message) {
			gchar *str;
			gchar *str_len;

			str = g_base64_encode ((guchar *) priv->message, strlen (priv->message));
			/*str = g_strdup (priv->message);*/
			str_len = g_strdup_printf ("%d", str ? (gint)strlen (str) : 0);
			soup_soap_message_start_element (msg, "part", NULL, NULL);
			soup_soap_message_add_attribute (msg, "length", str_len, NULL, NULL);
			soup_soap_message_add_attribute (msg, "contentType", priv->content_type, NULL, NULL);
			g_free (str_len);
			soup_soap_message_write_string (msg, str);
			soup_soap_message_end_element (msg);
			g_free (str);
		} else {
			soup_soap_message_add_attribute (msg, "length", "0", NULL, NULL);
			soup_soap_message_write_string (msg, "");
		}

		soup_soap_message_end_element (msg);

		/*attachments*/
		if (priv->attach_list) {
			GSList *al;
			soup_soap_message_start_element (msg, "attachments", NULL, NULL);
			for (al = priv->attach_list; al != NULL;  al = al->next) {
				EEwsItemAttachment *attachment = (EEwsItemAttachment *)al->data;
				add_attachment_to_soap_message (attachment, msg);
			}
			soup_soap_message_end_element (msg);
		}

		/*Link info - used when a mail is being replied to or forwarded*/
		if (priv->link_info) {
			soup_soap_message_start_element (msg, "link", NULL, NULL);
			e_ews_message_write_string_parameter (msg, "id", NULL, priv->link_info->id);
			e_ews_message_write_string_parameter (msg, "type", NULL, priv->link_info->type);
			e_ews_message_write_string_parameter (msg, "thread", NULL, priv->link_info->thread);
			soup_soap_message_end_element (msg);
		}

		break;

	case E_EWS_ITEM_TYPE_APPOINTMENT :
		soup_soap_message_add_attribute (msg, "type", "Appointment", "xsi", NULL);

		/* Calendar Item properties. */
		e_ews_item_set_calendar_item_elements (item, msg);

		/* Appointment specific properties */
		if (priv->end_date) {
			e_ews_message_write_string_parameter (msg, "endDate", NULL, priv->end_date);
		} else
			e_ews_message_write_string_parameter (msg, "endDate", NULL, "");

		e_ews_message_write_string_parameter (msg, "acceptLevel", NULL, priv->accept_level ? priv->accept_level : "");
		if (priv->is_allday_event)
			e_ews_message_write_string_parameter (msg, "allDayEvent", NULL, "1");
		else
			e_ews_message_write_string_parameter (msg, "allDayEvent", NULL ,"0");

		if (priv->trigger != 0) {
			alarm = g_strdup_printf ("%d", priv->trigger);
			e_ews_message_write_string_parameter_with_attribute (msg, "alarm", NULL, alarm, "enabled", "1");
			g_free (alarm);
		}
		e_ews_message_write_string_parameter (msg, "place", NULL, priv->place ? priv->place : "");

		break;

	case E_EWS_ITEM_TYPE_TASK :
		soup_soap_message_add_attribute (msg, "type", "Task", "xsi", NULL);

		/* Calendar Item properties. */
		e_ews_item_set_calendar_item_elements (item, msg);

		/* Task specific properties */
		if (priv->due_date) {
			e_ews_message_write_string_parameter (msg, "dueDate", NULL, priv->due_date);
		} else
			e_ews_message_write_string_parameter (msg, "dueDate", NULL, "");

		e_ews_message_write_string_parameter (msg, "taskPriority", NULL, priv->task_priority ? priv->task_priority : "");
		if (priv->completed)
			e_ews_message_write_string_parameter (msg, "completed", NULL, "1");
		else
			e_ews_message_write_string_parameter (msg, "completed", NULL, "0");

		break;
	case E_EWS_ITEM_TYPE_NOTE:
		soup_soap_message_add_attribute (msg, "type", "Note", "xsi", NULL);

		e_ews_item_set_calendar_item_elements (item, msg);
		break;
	case E_EWS_ITEM_TYPE_CONTACT :
		soup_soap_message_add_attribute (msg, "type", "Contact", "xsi", NULL);
		append_contact_fields_to_soap_message (item, msg);
		soup_soap_message_end_element(msg);
		return TRUE;
	case E_EWS_ITEM_TYPE_GROUP :
		soup_soap_message_add_attribute (msg, "type", "Group", "xsi", NULL);
		append_group_fields_to_soap_message (item, msg);
		soup_soap_message_end_element(msg);
		return TRUE;
	case E_EWS_ITEM_TYPE_ORGANISATION :
		soup_soap_message_add_attribute (msg, "type", "Organization", "xsi", NULL);
		append_contact_fields_to_soap_message (item, msg);
		soup_soap_message_end_element(msg);
		return TRUE;
	case E_EWS_ITEM_TYPE_CATEGORY :
		soup_soap_message_add_attribute (msg, "type", "Category", "xsi", NULL);
		e_ews_message_write_string_parameter (msg, "name", NULL, item->priv->category_name);
		soup_soap_message_end_element(msg);
		return TRUE;
	default :
		g_warning (G_STRLOC ": Unknown type for item");
		return FALSE;
	}

	soup_soap_message_end_element (msg);

	return TRUE;
}

static void
append_contact_changes_to_soap_message (EEwsItem *item, SoupSoapMessage *msg, gint change_type)
{
	GHashTable *changes;
	EEwsItemPrivate *priv;
	FullName *full_name;
	gchar *value;
	GList *list;
	PostalAddress *postal_address;

	priv = item->priv;
	changes = NULL;
	switch (change_type) {
	case E_EWS_ITEM_CHANGE_TYPE_ADD :
		changes = priv->additions;
		soup_soap_message_start_element (msg, "add", NULL, NULL);
		break;
	case E_EWS_ITEM_CHANGE_TYPE_UPDATE :
		changes = priv->updates;
		soup_soap_message_start_element (msg, "update", NULL, NULL);
		break;
	case E_EWS_ITEM_CHANGE_TYPE_DELETE :
		soup_soap_message_start_element (msg, "delete", NULL, NULL);
		changes = priv->deletions;
		break;

	}
	if (!changes)
		return;
	list = g_hash_table_lookup (changes, "categories");
	append_common_addressbook_item_fields_to_soap_message (changes, list, msg);
	full_name = g_hash_table_lookup (changes, "full_name");
	value = g_hash_table_lookup (changes, "name");
	if (full_name)
		append_full_name_to_soap_message (full_name, value, msg);
	list = g_hash_table_lookup (changes, "email");
	if (list)
		append_email_list_soap_message (list, msg);
	list = g_hash_table_lookup (changes, "ims");
	if (list)
		append_im_list_to_soap_message (list, msg);
	append_phone_list_to_soap_message (changes, msg);

	soup_soap_message_start_element (msg, "addressList", NULL, NULL);
	postal_address = g_hash_table_lookup (changes, "Home");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Home");
	postal_address = g_hash_table_lookup (changes, "Office");
	if (postal_address)
		append_postal_address_to_soap_message (msg, postal_address, "Office");
	soup_soap_message_end_element (msg);

	append_office_info_to_soap_message (changes, msg);
	append_personal_info_to_soap_message (changes, msg);

	soup_soap_message_end_element (msg);

}

static void
append_event_changes_to_soap_message (EEwsItem *item, SoupSoapMessage *msg, gint change_type)
{
	GHashTable *changes;
	EEwsItemPrivate *priv;

	priv = item->priv;
	changes = NULL;
	switch (change_type) {
	case E_EWS_ITEM_CHANGE_TYPE_ADD :
		changes = priv->additions;
		if (!changes)
			return;
		soup_soap_message_start_element (msg, "add", NULL, NULL);
		break;
	case E_EWS_ITEM_CHANGE_TYPE_UPDATE :
		changes = priv->updates;
		if (!changes)
			return;
		soup_soap_message_start_element (msg, "update", NULL, NULL);
		break;
	case E_EWS_ITEM_CHANGE_TYPE_DELETE :
		changes = priv->deletions;
		if (!changes)
			return;
		soup_soap_message_start_element (msg, "delete", NULL, NULL);
		break;
	}
	if (g_hash_table_lookup (changes, "categories")) {
		GList *list;
		list = g_hash_table_lookup (changes, "categories");

		soup_soap_message_start_element (msg, "categories", NULL, NULL);
		if (list != NULL && list->data)
			soup_soap_message_add_attribute (msg, "types:primary",list->data, NULL, NULL);
			for (; list != NULL; list = g_list_next (list))
				if (list->data) {
					e_ews_message_write_string_parameter (msg, "category", NULL, list->data);
			}
		soup_soap_message_end_element (msg);
		g_list_free (list);
	}
	if (g_hash_table_lookup (changes, "subject"))
		e_ews_message_write_string_parameter (msg, "subject", NULL, priv->subject ? priv->subject : "");
	if (g_hash_table_lookup (changes, "start_date")) {
		if (priv->start_date) {
			e_ews_message_write_string_parameter (msg, "startDate", NULL, priv->start_date);
		}
	}
	if (g_hash_table_lookup (changes, "end_date")) {
		if (priv->end_date) {
			e_ews_message_write_string_parameter (msg, "endDate", NULL, priv->end_date);
		}
	}
	if (g_hash_table_lookup (changes, "allDayEvent"))
		e_ews_message_write_string_parameter (msg, "allDayEvent", NULL, priv->is_allday_event ? "1" : "0");
	if (g_hash_table_lookup (changes, "message")) {
		soup_soap_message_start_element (msg, "message", NULL, NULL);
		if (priv->message) {
			gchar *str, *message;

			str = g_base64_encode ((guchar *) priv->message, strlen (priv->message));
			message = g_strdup_printf ("%d", (gint)strlen (str));
			soup_soap_message_add_attribute (msg, "length", message, NULL, NULL);
			g_free (message);
			soup_soap_message_write_string (msg, str);
			g_free (str);
		} else {
			soup_soap_message_add_attribute (msg, "length", "0", NULL, NULL);
			soup_soap_message_write_string (msg, "");
		}

		soup_soap_message_end_element (msg);
	}
	if (g_hash_table_lookup (changes, "classification"))
		e_ews_message_write_string_parameter (msg, "class", NULL, priv->classification);
	if (g_hash_table_lookup (changes, "task_priority")) {
		e_ews_message_write_string_parameter (msg, "taskPriority", NULL, priv->task_priority);
	}
	if (g_hash_table_lookup (changes, "accept_level"))
		e_ews_message_write_string_parameter (msg, "acceptLevel", NULL, priv->accept_level ? priv->accept_level : "");
	if (g_hash_table_lookup (changes, "place"))
		e_ews_message_write_string_parameter (msg, "place", NULL, priv->place ? priv->place : "");
	if (g_hash_table_lookup (changes, "alarm")) {
		if (priv->trigger != 0) {
			gchar *alarm = g_strdup_printf ("%d", priv->trigger);
			e_ews_message_write_string_parameter_with_attribute (msg, "alarm", NULL, alarm, "enabled", "1");
			g_free (alarm);
		}
		else
			e_ews_message_write_string_parameter_with_attribute (msg, "alarm", NULL, "0", "enabled", "0");
	}
	if (g_hash_table_lookup (changes, "completed"))
		e_ews_message_write_string_parameter (msg, "completed", NULL, priv->completed ? "1" : "0");
	if (g_hash_table_lookup (changes, "due_date"))
		e_ews_message_write_string_parameter (msg, "dueDate", NULL, priv->due_date);
	soup_soap_message_end_element (msg);

}

gboolean
e_ews_item_append_changes_to_soap_message (EEwsItem *item, SoupSoapMessage *msg)
{
	EEwsItemPrivate *priv;
	gchar *value;
	g_return_val_if_fail (E_IS_GW_ITEM (item), FALSE);
	g_return_val_if_fail (SOUP_IS_SOAP_MESSAGE (msg), FALSE);

	priv = item->priv;

	soup_soap_message_start_element (msg, "updates", NULL, NULL);

	switch (priv->item_type) {
	case E_EWS_ITEM_TYPE_CONTACT :
	case E_EWS_ITEM_TYPE_ORGANISATION :
		append_contact_changes_to_soap_message (item, msg, E_EWS_ITEM_CHANGE_TYPE_ADD);
		append_contact_changes_to_soap_message (item, msg, E_EWS_ITEM_CHANGE_TYPE_UPDATE);
		append_contact_changes_to_soap_message (item, msg, E_EWS_ITEM_CHANGE_TYPE_DELETE);
		soup_soap_message_end_element(msg);
		return TRUE;
	case E_EWS_ITEM_TYPE_GROUP :
		soup_soap_message_start_element (msg, "update", NULL, NULL);
		value = g_hash_table_lookup (item->priv->simple_fields, "name");
		if (value)
			e_ews_message_write_string_parameter (msg, "name", NULL, value);
		soup_soap_message_end_element (msg);
		soup_soap_message_end_element(msg);
		return TRUE;
	case E_EWS_ITEM_TYPE_APPOINTMENT:
	case E_EWS_ITEM_TYPE_TASK :
	case E_EWS_ITEM_TYPE_NOTE:
		append_event_changes_to_soap_message (item, msg, E_EWS_ITEM_CHANGE_TYPE_ADD);
		append_event_changes_to_soap_message (item, msg, E_EWS_ITEM_CHANGE_TYPE_UPDATE);
		append_event_changes_to_soap_message (item, msg, E_EWS_ITEM_CHANGE_TYPE_DELETE);
		soup_soap_message_end_element(msg);
		return TRUE;
	default :
		g_warning (G_STRLOC ": Unknown type for item");
		return FALSE;
	}

}
