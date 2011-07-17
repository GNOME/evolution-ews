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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <libsoup/soup-misc.h>
#include "e-ews-item.h"
#include "e-ews-connection.h"
#include "e-ews-message.h"

#ifdef G_OS_WIN32

static gchar *
g_mkdtemp (gchar *tmpl, int mode)
{
	static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	static const int NLETTERS = sizeof (letters) - 1;
	static int counter = 0;
	char *XXXXXX;
	GTimeVal tv;
	glong value;
	int count;

	/* find the last occurrence of "XXXXXX" */
	XXXXXX = g_strrstr (tmpl, "XXXXXX");

	if (!XXXXXX || strncmp (XXXXXX, "XXXXXX", 6)) {
		errno = EINVAL;
		return NULL;
	}

	/* Get some more or less random data.  */
	g_get_current_time (&tv);
	value = (tv.tv_usec ^ tv.tv_sec) + counter++;

	for (count = 0; count < 100; value += 7777, ++count) {
		glong v = value;

		/* Fill in the random bits.  */
		XXXXXX[0] = letters[v % NLETTERS];
		v /= NLETTERS;
		XXXXXX[1] = letters[v % NLETTERS];
		v /= NLETTERS;
		XXXXXX[2] = letters[v % NLETTERS];
		v /= NLETTERS;
		XXXXXX[3] = letters[v % NLETTERS];
		v /= NLETTERS;
		XXXXXX[4] = letters[v % NLETTERS];
		v /= NLETTERS;
		XXXXXX[5] = letters[v % NLETTERS];

		/* tmpl is in UTF-8 on Windows, thus use g_mkdir() */
		if (g_mkdir (tmpl, mode) == 0)
			return tmpl;

		if (errno != EEXIST)
			/* Any other error will apply also to other names we might
			 *  try, and there are 2^32 or so of them, so give up now.
			 */
			return NULL;
	}

	/* We got out of the loop because we ran out of combinations to try.  */
	errno = EEXIST;
	return NULL;
}

#define mkdtemp(t) g_mkdtemp(t, 0700)

#endif

G_DEFINE_TYPE (EEwsItem, e_ews_item, G_TYPE_OBJECT)

struct _EEwsContactFields {
	gchar *fileas;
	EwsCompleteName *complete_name;
	
	GHashTable *email_addresses;
	GHashTable *physical_addresses;
	GHashTable *phone_numbers;
	GHashTable *im_addresses;

	gchar *company_name;
	gchar *department;
	gchar *job_title;
	gchar *assistant_name;
	gchar *manager;
	gchar *office_location;
	
	gchar *business_homepage;
	
	time_t birthday;
	time_t wedding_anniversary;
	
	gchar *spouse_name;
	gchar *culture;
	gchar *surname;
};

struct _EEwsTaskFields {
	gchar *percent_complete;
	gchar *status;
	gchar *body;
	gchar *sensitivity;
	gchar *owner;
	gchar *delegator;
	time_t due_date;
	time_t start_date;
	time_t complete_date;
	gboolean has_due_date;
	gboolean has_start_date;
	gboolean has_complete_date;
};

struct _EEwsItemPrivate {
	EEwsItemType item_type;

	/* MAPI properties */
	/* The Exchange server is so fundamentally misdesigned that it doesn't expose
	   certain information in a coherent way; the \Answered, and \Deleted message
	   flags don't even seem to work properly. It looks like the only way to work
	   it out is from the PidTagIconIndex field. Quite what the hell an *icon*
	   selector is doing in the database, I have absolutely no fucking idea; a
	   database is supposed to represent the *data*, not do bizarre things that
	   live in the client. But that's typical Exchange brain damage for you... */
	guint32 mapi_icon_index;		/* http://msdn.microsoft.com/en-us/library/cc815472.aspx */
	guint32 mapi_last_verb_executed;	/* http://msdn.microsoft.com/en-us/library/cc841968.aspx */
	guint32 mapi_message_status;		/* http://msdn.microsoft.com/en-us/library/cc839915.aspx */
	guint32 mapi_message_flags;		/* http://msdn.microsoft.com/en-us/library/cc839733.aspx */

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

	GSList *modified_occurrences;
	GSList *attachments_list;
	GSList *attendees;

	EwsId *calendar_item_accept_id;

	/* the evolution labels are implemented as exchange
	 * Categories.  These appear in the message headers as
	 * Keywords: and are set and extracted from the EWS server as
	 * <Categories> which is a string array valued XML element */
	GSList *categories;

	struct _EEwsContactFields *contact_fields;
	struct _EEwsTaskFields *task_fields;
};

static GObjectClass *parent_class = NULL;
static void	ews_item_free_mailbox (EwsMailbox *mb);
static void	ews_item_free_attendee (EwsAttendee *attendee);
static void	ews_free_contact_fields (struct _EEwsContactFields *con_fields);

typedef gpointer (* EwsGetValFunc) (ESoapParameter *param);

static void
e_ews_item_dispose (GObject *object)
{
	EEwsItem *item = (EEwsItem *) object;
	EEwsItemPrivate *priv;

	g_return_if_fail (E_IS_EWS_ITEM (item));

	priv = item->priv;

	if (priv->item_id) {
		g_free (priv->item_id->id);
		g_free (priv->item_id->change_key);
		g_free (priv->item_id);
		priv->item_id = NULL;
	}

	g_free (priv->mime_content);

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

	if (priv->modified_occurrences) {
		g_slist_foreach (priv->modified_occurrences, (GFunc) g_free, NULL);
		g_slist_free (priv->modified_occurrences);
		priv->modified_occurrences = NULL;
	}

	if (priv->attachments_list) {
		g_slist_foreach (priv->attachments_list, (GFunc) g_free, NULL);
		g_slist_free (priv->attachments_list);
		priv->attachments_list = NULL;
	}

	if (priv->attendees) {
		g_slist_foreach (priv->attendees, (GFunc) ews_item_free_attendee, NULL);
		g_slist_free (priv->attendees);
		priv->attendees = NULL;

	}

	if (priv->calendar_item_accept_id) {
		g_free (priv->calendar_item_accept_id->id);
		g_free (priv->calendar_item_accept_id->change_key);
		g_free (priv->calendar_item_accept_id);
		priv->calendar_item_accept_id = NULL;
	}

	ews_item_free_mailbox (priv->sender);
	ews_item_free_mailbox (priv->from);

	if (priv->item_type == E_EWS_ITEM_TYPE_CONTACT)
		ews_free_contact_fields (priv->contact_fields);

	if (priv->task_fields) {
		g_free (priv->task_fields->percent_complete);
		priv->task_fields->percent_complete = NULL;
		g_free (priv->task_fields->status);
		priv->task_fields->status = NULL;
		g_free (priv->task_fields->body);
		priv->task_fields->body = NULL;
		g_free (priv->task_fields->sensitivity);
		priv->task_fields->sensitivity = NULL;
		g_free (priv->task_fields->owner);
		priv->task_fields->owner = NULL;
		g_free (priv->task_fields);
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

static void
ews_free_contact_fields (struct _EEwsContactFields *con_fields)
{
	if (con_fields) {
		if (con_fields->complete_name) {
			EwsCompleteName *cn = con_fields->complete_name;
			
			g_free (cn->title);
			g_free (cn->first_name);
			g_free (cn->middle_name);
			g_free (cn->last_name);
			g_free (cn->suffix);
			g_free (cn->initials);
			g_free (cn->full_name);
			g_free (cn->nick_name);
			g_free (cn->yomi_first_name);
			g_free (cn->yomi_last_name);
		}

		if (con_fields->email_addresses)
			g_hash_table_destroy (con_fields->email_addresses);
		
		if (con_fields->physical_addresses)
			g_hash_table_destroy (con_fields->physical_addresses);
		
		if (con_fields->phone_numbers)
			g_hash_table_destroy (con_fields->phone_numbers);
		
		if (con_fields->im_addresses)
			g_hash_table_destroy (con_fields->im_addresses);
		
		g_free (con_fields->fileas);
		g_free (con_fields->company_name);
		g_free (con_fields->department);
		g_free (con_fields->job_title);
		g_free (con_fields->assistant_name);
		g_free (con_fields->manager);
		g_free (con_fields->office_location);
		g_free (con_fields->business_homepage);
		g_free (con_fields->spouse_name);
		g_free (con_fields->culture);
		g_free (con_fields->surname);
		g_free (con_fields);
	}	
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

static void
ews_item_free_attendee (EwsAttendee *attendee)
{
	if (attendee) {
		ews_item_free_mailbox (attendee->mailbox);
		g_free (attendee->responsetype);
		g_free (attendee);
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

static void parse_extended_property (EEwsItemPrivate *priv, ESoapParameter *param)
{
	ESoapParameter *subparam;
	gchar *str;
	guint32 tag, value;

	subparam = e_soap_parameter_get_first_child_by_name (param, "ExtendedFieldURI");
	if (!subparam)
		return;

	str = e_soap_parameter_get_property (subparam, "PropertyType");
	if (!str)
		return;

	/* We only handle integer MAPI properties for now... */
	if (g_ascii_strcasecmp (str, "Integer")) {
		g_free (str);
		return;
	}
	g_free (str);

	str = e_soap_parameter_get_property (subparam, "PropertyTag");
	if (!str)
		return;

	tag = strtol (str, NULL, 0);
	g_free (str);

	subparam = e_soap_parameter_get_first_child_by_name (param, "Value");
	if (!subparam)
		return;

	str = e_soap_parameter_get_string_value (subparam);
	if (!str)
		return;

	value = strtol (str, NULL, 0);
	g_free (str);

	switch (tag) {
	case 0x01080: /* PidTagIconIndex */
		priv->mapi_icon_index = value;
		break;

	case 0x1081:
		priv->mapi_last_verb_executed = value;
		break;

	case 0xe07:
		priv->mapi_message_flags = value;
		break;

	case 0xe17:
		priv->mapi_message_status = value;
		break;

	default:
		g_print ("Fetched unrecognised MAPI property 0x%x, value %d\n",
			 tag, value);
	}
}

static void parse_categories (EEwsItemPrivate *priv, ESoapParameter *param)
{
	gchar *value;
	ESoapParameter *subparam;

	/* release all the old data (if any) */
	if (priv->categories) {
		g_slist_foreach (priv->categories, (GFunc) g_free, NULL);
		g_slist_free (priv->categories);
		priv->categories = NULL;
	}

	/* categories are an array of <string> */
	for (subparam = e_soap_parameter_get_first_child(param);
	     subparam != NULL;
	     subparam = e_soap_parameter_get_next_child(subparam)) {
		value = e_soap_parameter_get_string_value (subparam);

		priv->categories = g_slist_append(priv->categories, value);
	}

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

	g_free (value);
	return importance;
}

static void process_modified_occurrences(EEwsItemPrivate *priv, ESoapParameter *param) {
	ESoapParameter *subparam, *subparam1;
	gchar *modified_occurrence_id;

	for (subparam = e_soap_parameter_get_first_child(param); subparam != NULL; subparam = e_soap_parameter_get_next_child(subparam)) {

		subparam1 = e_soap_parameter_get_first_child_by_name(subparam, "ItemId");
		modified_occurrence_id = e_soap_parameter_get_property(subparam1, "Id");
		priv->modified_occurrences = g_slist_append(priv->modified_occurrences, modified_occurrence_id);
	}

	return;
}

static void process_attachments_list(EEwsItemPrivate *priv, ESoapParameter *param) {

	ESoapParameter *subparam, *subparam1;

	GSList *list = NULL;

	for (subparam = e_soap_parameter_get_first_child (param); subparam != NULL; subparam = e_soap_parameter_get_next_child (subparam)) {

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "AttachmentId");

		list = g_slist_append (list, e_soap_parameter_get_property (subparam1, "Id"));
	}

	priv->attachments_list = list;
	return;
}

static void process_attendees(EEwsItemPrivate *priv, ESoapParameter *param, const gchar *type) {
	ESoapParameter *subparam, *subparam1;
	EwsAttendee *attendee;

	for (subparam = e_soap_parameter_get_first_child(param); subparam != NULL; subparam = e_soap_parameter_get_next_child(subparam)) {
		EwsMailbox *mailbox = NULL;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Mailbox");
		mailbox = e_ews_item_mailbox_from_soap_param (subparam1);
		/* Ignore attendee if mailbox is not valid,
		   for instance, ppl that does not exists any more */
		if (!mailbox)
			continue;

		attendee = g_new0 (EwsAttendee, 1);

		attendee->mailbox = mailbox;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "ResponseType");
		attendee->responsetype = e_soap_parameter_get_string_value (subparam1);

		attendee->attendeetype = (gchar *)type;

		priv->attendees = g_slist_append (priv->attendees, attendee);
	}

	return;
}

static void
parse_complete_name (struct _EEwsContactFields *con_fields, ESoapParameter *param)
{
	ESoapParameter *subparam;
	EwsCompleteName *cn;

	cn = g_new0 (EwsCompleteName, 1);

	subparam = e_soap_parameter_get_first_child_by_name (param, "Title");
	if (subparam)	
		cn->title = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "FirstName");
	if (subparam)	
		cn->first_name = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "MiddleName");
	if (subparam)	
		cn->middle_name = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "LastName");
	if (subparam)	
		cn->last_name = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "Suffix");
	if (subparam)	
		cn->suffix = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "Initials");
	if (subparam)	
		cn->initials = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "FullName");
	if (subparam)	
		cn->full_name = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "Nickname");
	if (subparam)	
		cn->nick_name = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "YomiFirstName");
	if (subparam)	
		cn->yomi_first_name = e_soap_parameter_get_string_value (subparam);
	subparam = e_soap_parameter_get_first_child_by_name (param, "YomiLastName");
	if (subparam)	
		cn->yomi_last_name = e_soap_parameter_get_string_value (subparam);

	con_fields->complete_name = cn;
}

static gpointer
ews_get_physical_address (ESoapParameter *param)
{
	ESoapParameter *subparam;
	EwsAddress *address;

	address = g_new0 (EwsAddress, 1);

	subparam = e_soap_parameter_get_first_child_by_name (param, "Street");
	if (subparam)	
		address->street = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "City");
	if (subparam)	
		address->city = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "State");
	if (subparam)	
		address->state = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "Country");
	if (subparam)	
		address->country = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "PostalCode");
	if (subparam)	
		address->postal_code = e_soap_parameter_get_string_value (subparam);

	return address;
}

static void
parse_entries (GHashTable *hash_table, ESoapParameter *param, EwsGetValFunc get_val_func)
{
	ESoapParameter *subparam;

	for (subparam = e_soap_parameter_get_first_child_by_name (param, "Entry");
			subparam != NULL;
			subparam = e_soap_parameter_get_next_child_by_name (subparam, "Entry")) {
		gchar *key;
		gpointer value;
			
		key = e_soap_parameter_get_property (subparam, "Key");
		value = get_val_func (subparam);
	
		if (value)
			g_hash_table_insert (hash_table, key, value);
		else
			g_free (key);
	}
}

static void
ews_free_physical_address (gpointer value)
{
	EwsAddress *address = (EwsAddress *) value;

	if (address) {
		g_free (address->street);
		g_free (address->city);
		g_free (address->state);
		g_free (address->country);
		g_free (address->postal_code);
		g_free (address);
	}
}

static void
parse_contact_field (EEwsItem *item, const gchar *name, ESoapParameter *subparam)
{
	EEwsItemPrivate *priv = item->priv;
	gchar *value = NULL;
	
	if (!g_ascii_strcasecmp (name, "Culture")) {
		priv->contact_fields->culture = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "FileAs")) {
		priv->contact_fields->fileas = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "CompleteName")) {
		parse_complete_name (priv->contact_fields, subparam);
	} else if (!g_ascii_strcasecmp (name, "CompanyName")) {
		priv->contact_fields->company_name = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "EmailAddresses")) {
		priv->contact_fields->email_addresses = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		parse_entries (priv->contact_fields->email_addresses, subparam, (EwsGetValFunc) e_soap_parameter_get_string_value);
	} else if (!g_ascii_strcasecmp (name, "PhysicalAddresses")) {
		priv->contact_fields->physical_addresses = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, ews_free_physical_address);
		parse_entries (priv->contact_fields->physical_addresses, subparam, ews_get_physical_address);
	} else if (!g_ascii_strcasecmp (name, "PhoneNumbers")) {
		priv->contact_fields->phone_numbers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		parse_entries (priv->contact_fields->phone_numbers, subparam, (EwsGetValFunc) e_soap_parameter_get_string_value);
	} else if (!g_ascii_strcasecmp (name, "AssistantName")) {
		priv->contact_fields->assistant_name = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Birthday")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->contact_fields->birthday = ews_item_parse_date (value);
			g_free (value);
	} else if (!g_ascii_strcasecmp (name, "BusinessHomePage")) {
		priv->contact_fields->business_homepage = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Department")) {
		priv->contact_fields->department = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "ImAddresses")) {
		priv->contact_fields->im_addresses = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		parse_entries (priv->contact_fields->im_addresses, subparam, (EwsGetValFunc) e_soap_parameter_get_string_value);
	} else if (!g_ascii_strcasecmp (name, "JobTitle")) {
		priv->contact_fields->job_title = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Manager")) {
		priv->contact_fields->manager = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "OfficeLocation")) {
		priv->contact_fields->office_location = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "SpouseName")) {
		priv->contact_fields->spouse_name = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Surname")) {
		priv->contact_fields->surname = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "WeddingAnniversary")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->contact_fields->wedding_anniversary = ews_item_parse_date (value);
			g_free (value);
	}
}

static gchar *
strip_html_tags (const gchar *html_text)
{
	gssize haystack_len = strlen (html_text);
	gchar *plain_text = g_malloc (haystack_len);
	gchar *start = g_strstr_len (html_text, haystack_len, "<body>"),
		*end = g_strstr_len (html_text, haystack_len, "</body>"),
		*i, *j;

	for (j = plain_text, i = start + 6; i < end; i++) {
		if (*i == '<') { while (*i != '>') i++; }
		else { *j = *i; j++; }
	}

	*j = '\0';

	return plain_text;
}

static void
parse_task_field (EEwsItem *item, const gchar *name, ESoapParameter *subparam)
{
	EEwsItemPrivate *priv = item->priv;
	gchar *value = NULL;

	if (!g_ascii_strcasecmp (name, "Status")) {
		priv->task_fields->status = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "PercentComplete")) {
		priv->task_fields->percent_complete = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "DueDate")) {
		value = e_soap_parameter_get_string_value (subparam);
		priv->task_fields->due_date = ews_item_parse_date (value);
		g_free (value);
		priv->task_fields->has_due_date = TRUE;
	} else if (!g_ascii_strcasecmp (name, "StartDate")) {
		value = e_soap_parameter_get_string_value (subparam);
		priv->task_fields->start_date = ews_item_parse_date (value);
		g_free (value);
		priv->task_fields->has_start_date = TRUE;
	}
	else if (!g_ascii_strcasecmp (name, "CompleteDate")) {
		value = e_soap_parameter_get_string_value (subparam);
		priv->task_fields->complete_date = ews_item_parse_date (value);
		g_free (value);
		priv->task_fields->has_complete_date = TRUE;
	} else if (!g_ascii_strcasecmp (name, "Sensitivity")) {
		priv->task_fields->sensitivity = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Body")) {
		if (!g_ascii_strcasecmp (e_soap_parameter_get_property (subparam, "BodyType"),"HTML")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->task_fields->body = strip_html_tags (value);
			g_free (value);
		} else
			priv->task_fields->body = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Owner")) {
		priv->task_fields->owner = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Delegator")) {
		priv->task_fields->delegator = e_soap_parameter_get_string_value (subparam);
	}

}

static gboolean
e_ews_item_set_from_soap_parameter (EEwsItem *item, ESoapParameter *param)
{
	EEwsItemPrivate *priv = item->priv;
	ESoapParameter *subparam, *node;
	gboolean contact = FALSE, task = FALSE;

	g_return_val_if_fail (param != NULL, FALSE);

	if ((node = e_soap_parameter_get_first_child_by_name (param, "Message")))
		priv->item_type = E_EWS_ITEM_TYPE_MESSAGE;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "CalendarItem")))
		priv->item_type = E_EWS_ITEM_TYPE_CALENDAR_ITEM;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "Contact"))) {
		contact = TRUE;	
		priv->item_type = E_EWS_ITEM_TYPE_CONTACT;
		priv->contact_fields = g_new0 (struct _EEwsContactFields, 1);
	} else if ((node = e_soap_parameter_get_first_child_by_name (param, "DistributionList")))
		priv->item_type = E_EWS_ITEM_TYPE_GROUP;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingMessage")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_MESSAGE;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingRequest")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_REQUEST;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingResponse")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_RESPONSE;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "MeetingCancellation")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_CANCELLATION;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "Task"))) {
		task = TRUE;
		priv->item_type = E_EWS_ITEM_TYPE_TASK;
		priv->task_fields = g_new0 (struct _EEwsTaskFields, 1);
		priv->task_fields->has_due_date = FALSE;
		priv->task_fields->has_start_date = FALSE;
		priv->task_fields->has_complete_date = FALSE;
	}
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
		} else if (!g_ascii_strcasecmp (name, "Categories")) {
			parse_categories (priv, subparam);
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
		} else if (!g_ascii_strcasecmp (name, "Attachments")) {
			process_attachments_list(priv, subparam);
		} else if (contact)
			parse_contact_field (item, name, subparam);
			/* fields below are not relevant for contacts, so skip them */	
		else if (!g_ascii_strcasecmp (name, "Sender")) {
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
		} else if (task) {
			parse_task_field (item, name, subparam);
			/* fields below are not relevant for task, so skip them */
		} else if (!g_ascii_strcasecmp (name, "References")) {
			priv->references = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "ExtendedProperty")) {
			parse_extended_property (priv, subparam);
		} else if (!g_ascii_strcasecmp (name, "ModifiedOccurrences")) {
			process_modified_occurrences(priv, subparam);
		} else if (!g_ascii_strcasecmp (name, "RequiredAttendees")) {
			process_attendees (priv, subparam, "Required");
		} else if (!g_ascii_strcasecmp (name, "OptionalAttendees")) {
			process_attendees (priv, subparam, "Optional");
		} else if (!g_ascii_strcasecmp (name, "AssociatedCalendarItemId")) {
			priv->calendar_item_accept_id = g_new0 (EwsId, 1);
			priv->calendar_item_accept_id->id = e_soap_parameter_get_property (subparam, "Id");
			priv->calendar_item_accept_id->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
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

gboolean
e_ews_item_is_forwarded		(EEwsItem *item, gboolean *forwarded)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*forwarded = (item->priv->mapi_icon_index == 0x106);

	return TRUE;
}

gboolean
e_ews_item_is_answered		(EEwsItem *item, gboolean *answered)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*answered = (item->priv->mapi_icon_index == 0x105);

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

const GSList *
e_ews_item_get_categories	(EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->categories;
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

	/* Return NULL if RoutingType of Mailbox is not SMTP
		   For instance, people who don't exist any more	*/
	subparam = e_soap_parameter_get_first_child_by_name (param, "RoutingType");
	if (subparam) {
		gchar *routingtype;
		routingtype = e_soap_parameter_get_string_value (subparam);
		if (g_ascii_strcasecmp (routingtype, "SMTP")) {
			g_free (routingtype);
			return NULL;
		}
		g_free (routingtype);
	}

	mb = g_new0 (EwsMailbox, 1);

	subparam = e_soap_parameter_get_first_child_by_name (param, "Name");
	if (subparam)
		mb->name = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "EmailAddress");
	if (subparam)
		mb->email = e_soap_parameter_get_string_value (subparam);

	return mb;
}

const GSList *
e_ews_item_get_modified_occurrences(EEwsItem *item)
{
	g_return_val_if_fail(E_IS_EWS_ITEM(item), NULL);

	return item->priv->modified_occurrences;
}

GSList *
e_ews_item_get_attachments_ids(EEwsItem *item)
{
	g_return_val_if_fail(E_IS_EWS_ITEM(item), NULL);

	return item->priv->attachments_list;
}

gchar *
e_ews_embed_attachment_id_in_uri (const gchar *olduri, const char *attach_id)
{
	gchar *tmpdir, *tmpfilename, filename[350], dirname[350], *name;

	tmpfilename = g_filename_from_uri (olduri, NULL, NULL);

	name = g_strrstr (tmpfilename, "/")+1;
	tmpdir = g_strndup(tmpfilename, g_strrstr (tmpfilename, "/") - tmpfilename);

	snprintf (dirname, 350, "%s/%s", tmpdir, attach_id);
	if (g_mkdir (dirname, 0775) == -1) {
		g_warning("Failed create directory to place file in [%s]: %s\n", dirname, strerror (errno));
	}

	snprintf(filename, 350, "%s/%s", dirname, name);
	if (g_rename (tmpfilename, filename) != 0) {
		g_warning("Failed to move attachment cache file [%s -> %s]: %s\n", tmpfilename, filename, strerror (errno));
	}

	g_free(tmpdir);

	return g_filename_to_uri(filename, NULL, NULL);
}

gchar *
e_ews_dump_file_attachment_from_soap_parameter (ESoapParameter *param, const gchar *cache)
{
	ESoapParameter *subparam;
	const gchar *param_name;
	gchar *name = NULL, *value, filename[350], dirname[350], *attach_id = NULL;
	guchar *content = NULL;
	gsize data_len = 0;
	gchar *tmpdir, *tmpfilename;

	g_return_val_if_fail (param != NULL, NULL);

	/* Parse element, look for filename and content */
	for (subparam = e_soap_parameter_get_first_child(param); subparam != NULL; subparam = e_soap_parameter_get_next_child(subparam)) {
		param_name = e_soap_parameter_get_name(subparam);

		if (g_ascii_strcasecmp(param_name, "Name") == 0) {
			value = e_soap_parameter_get_string_value(subparam);
			name = g_uri_escape_string(value, "", TRUE);
			g_free (value);
		} else if (g_ascii_strcasecmp(param_name, "Content") == 0) {
			value = e_soap_parameter_get_string_value (subparam);
			content = g_base64_decode (value, &data_len);
			g_free (value);
		} else if (g_ascii_strcasecmp(param_name, "AttachmentId") == 0) {
			value = e_soap_parameter_get_property (subparam, "Id");
			attach_id = g_uri_escape_string(value, "", TRUE);
			g_free (value);
		}
	}

	/* Make sure we have needed data */
	if (!content || !name || !attach_id) {
		g_free(name);
		g_free(content);
		g_free(attach_id);
		return NULL;
	}

	tmpfilename = (gchar *) content;
	tmpdir = g_strndup(tmpfilename, g_strrstr (tmpfilename, "/") - tmpfilename);

	snprintf (dirname, 350, "%s/%s", tmpdir, attach_id);
	if (g_mkdir (dirname, 0775) == -1) {
		g_warning("Failed create directory to place file in [%s]: %s\n", dirname, strerror (errno));
	}

	snprintf(filename, 350, "%s/%s", dirname, name);
	if (g_rename (tmpfilename, filename) != 0) {
		g_warning("Failed to move attachment cache file [%s -> %s]: %s\n", tmpfilename, filename, strerror (errno));
	}

	g_free(tmpdir);
	g_free(name);
	g_free(content);
	g_free(attach_id);

	/* Return URI to saved file */
	return g_filename_to_uri(filename, NULL, NULL);
}

gchar *
e_ews_item_dump_mime_content(EEwsItem *item, const gchar *cache) {
	gchar filename[512], *surename, dirname[350];
	gchar *tmpdir, *tmpfilename;

	g_return_val_if_fail (item->priv->mime_content != NULL, NULL);

	tmpfilename = (gchar *) item->priv->mime_content;
	tmpdir = g_strndup(tmpfilename, g_strrstr (tmpfilename, "/") - tmpfilename);

	snprintf(dirname, 350, "%s/XXXXXX", tmpdir);
	if (!mkdtemp(dirname))
		g_warning ("Failed to create directory for attachment cache");

	surename = g_uri_escape_string(item->priv->subject, "", TRUE);
	snprintf(filename, 350, "%s/%s", dirname, surename);

	if (g_rename ((const gchar *)item->priv->mime_content, filename) != 0) {
		g_warning("Failed to move attachment cache file");
	}

	g_free(tmpdir);
	g_free(tmpfilename);
	g_free(surename);

	/* Return URI to saved file */
	return g_filename_to_uri(filename, NULL, NULL);
}

const GSList *
e_ews_item_get_attendees (EEwsItem *item)
{
	g_return_val_if_fail(E_IS_EWS_ITEM(item), NULL);

	return item->priv->attendees;
}

const EwsId *
e_ews_item_get_calendar_item_accept_id (EEwsItem *item)
{
	g_return_val_if_fail(E_IS_EWS_ITEM(item), NULL);

	return (const EwsId*) item->priv->calendar_item_accept_id;
}

const gchar *
e_ews_item_get_fileas (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->fileas;
}

const EwsCompleteName *
e_ews_item_get_complete_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const EwsCompleteName *) item->priv->contact_fields->complete_name;
}

/**
 * e_ews_item_get_email_address 
 * @item: 
 * @field: "EmailAddress1", "EmailAddress2", "EmailAddress3"
 * 
 * 
 * Returns: 
 **/
const gchar *
e_ews_item_get_email_address (EEwsItem *item, const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);
	
	if (item->priv->contact_fields->email_addresses)
		return g_hash_table_lookup (item->priv->contact_fields->email_addresses, field);

	return NULL;
}

/**
 * e_ews_item_get_physical_address 
 * @item: 
 * @field: "Business", "Home", "Other"
 * 
 * 
 * Returns: 
 **/
const EwsAddress *
e_ews_item_get_physical_address (EEwsItem *item, const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);
	
	if (item->priv->contact_fields->physical_addresses)
		return g_hash_table_lookup (item->priv->contact_fields->physical_addresses, field);

	return NULL;
}

/**
 * e_ews_item_get_phone_number 
 * @item: 
 * @field: "AssistantPhone", "BusinessFax", "BusinessPhone", "BusinessPhone2", "Callback"
 * "CarPhone", "CompanyMainPhone", "HomeFax", "HomePhone", "HomePhone2", "Isdn", "MobilePhone"
 * "OtherFax", "OtherTelephone", "Pager", "PrimaryPhone", "RadioPhone", "Telex", "TtyTddPhone"
 * 
 * 
 * Returns: 
 **/
const gchar *
e_ews_item_get_phone_number (EEwsItem *item, const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);
	
	if (item->priv->contact_fields->phone_numbers)
		return g_hash_table_lookup (item->priv->contact_fields->phone_numbers, field);

	return NULL;
}

/**
 * e_ews_item_get_im_address 
 * @item: 
 * @field: "ImAddress1", "ImAddress2", "ImAddress3"
 * 
 * 
 * Returns: 
 **/
const gchar *
e_ews_item_get_im_address (EEwsItem *item, const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);
	
	if (item->priv->contact_fields->im_addresses)
		return g_hash_table_lookup (item->priv->contact_fields->im_addresses, field);

	return NULL;
}

const gchar *
e_ews_item_get_company_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->company_name;
}

const gchar *
e_ews_item_get_department (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->department;
}

const gchar *
e_ews_item_get_job_title (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->job_title;
}

const gchar *
e_ews_item_get_assistant_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->assistant_name;
}

const gchar *
e_ews_item_get_manager (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->manager;
}

const gchar *
e_ews_item_get_office_location (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->office_location;
}

const gchar *
e_ews_item_get_business_homepage (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->business_homepage;
}

const gchar *
e_ews_item_get_spouse_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->spouse_name;
}

const gchar *
e_ews_item_get_surname (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar*) item->priv->contact_fields->surname;
}
	
time_t
e_ews_item_get_birthday (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), -1);
	g_return_val_if_fail (item->priv->contact_fields != NULL, -1);

	return item->priv->contact_fields->birthday;
}

time_t
e_ews_item_get_wedding_anniversary (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), -1);
	g_return_val_if_fail (item->priv->contact_fields != NULL, -1);

	return item->priv->contact_fields->wedding_anniversary;
}

const gchar *
e_ews_item_get_status (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->status;
}

const gchar *	e_ews_item_get_percent_complete (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->percent_complete;
}

time_t
e_ews_item_get_due_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), -1);
	g_return_val_if_fail (item->priv->task_fields != NULL, -1);

	return item->priv->task_fields->due_date;
}

time_t
e_ews_item_get_start_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), -1);
	g_return_val_if_fail (item->priv->task_fields != NULL, -1);

	return item->priv->task_fields->start_date;
}

time_t
e_ews_item_get_complete_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), -1);
	g_return_val_if_fail (item->priv->task_fields != NULL, -1);

	return item->priv->task_fields->complete_date;
}

const gchar *
e_ews_item_get_sensitivity (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->sensitivity;
}

const gchar *
e_ews_item_get_body (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->body;
}

const gchar *
e_ews_item_get_owner (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->owner;
}

const gchar *
e_ews_item_get_delegator (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->delegator;
}

gboolean
e_ews_item_task_has_start_date (EEwsItem *item, gboolean *has_date)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), FALSE);
	g_return_val_if_fail (item->priv->task_fields != NULL, FALSE);

	*has_date =  item->priv->task_fields->has_start_date;

	return TRUE;
}

gboolean
e_ews_item_task_has_due_date (EEwsItem *item,  gboolean *has_date)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), FALSE);
	g_return_val_if_fail (item->priv->task_fields != NULL, FALSE);

	*has_date =  item->priv->task_fields->has_due_date;

	return TRUE;
}

gboolean
e_ews_item_task_has_complete_date (EEwsItem *item,  gboolean *has_date)
{
	g_return_val_if_fail (E_IS_EWS_ITEM(item), FALSE);
	g_return_val_if_fail (item->priv->task_fields != NULL, FALSE);

	*has_date =  item->priv->task_fields->has_complete_date;

	return TRUE;
}