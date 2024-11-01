/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <libsoup/soup.h>
#include <libedataserver/libedataserver.h>
#include <libecal/libecal.h>

#include "e-ews-item.h"
#include "e-ews-item-change.h"

struct _EEwsContactFields {
	gchar *fileas;
	gchar *display_name;
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

	gboolean has_birthday;
	time_t birthday;
	gboolean has_wedding_anniversary;
	time_t wedding_anniversary;

	gchar *profession;
	gchar *spouse_name;
	gchar *culture;
	gchar *surname;
	gchar *givenname;
	gchar *middlename;
	gchar *notes;

	gsize msexchange_cert_len;
	guchar *msexchange_cert;

	gsize user_cert_len;
	guchar *user_cert;
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
	EwsId *attachment_id;
	EEwsItemType item_type;
	GError *error;

	/* MAPI properties */
	/* The Exchange server is so fundamentally misdesigned that it doesn't expose
	 * certain information in a coherent way; the \Answered, and \Deleted message
	 * flags don't even seem to work properly. It looks like the only way to work
	 * it out is from the PidTagIconIndex field. Quite what the hell an *icon*
	 * selector is doing in the database, I have absolutely no fucking idea; a
	 * database is supposed to represent the *data*, not do bizarre things that
	 * live in the client. But that's typical Exchange brain damage for you... */
	guint32 mapi_icon_index;		/* http://msdn.microsoft.com/en-us/library/cc815472.aspx */
	guint32 mapi_last_verb_executed;	/* http://msdn.microsoft.com/en-us/library/cc841968.aspx */
	guint32 mapi_message_status;		/* http://msdn.microsoft.com/en-us/library/cc839915.aspx */
	guint32 mapi_message_flags;		/* http://msdn.microsoft.com/en-us/library/cc839733.aspx */

	GHashTable *mapi_extended_tags; /* simple tag->string_value */
	GHashTable *mapi_extended_sets; /* setid-> [ tag->string_value ] */

	/* properties */
	EwsId *item_id;
	gchar *subject;
	gchar *mime_content;
	gchar *body;
	EEwsBodyType body_type;

	gchar *date_header;
	time_t date_received;
	time_t date_sent;
	time_t date_created;
	time_t last_modified_time;

	gsize size;
	gchar *msg_id;
	gchar *in_replyto;
	gchar *references;
	gchar *preview;
	gboolean has_attachments;
	gboolean is_read;
	EwsImportance importance;

	gchar *uid;
	gchar *timezone;
	time_t calendar_start;
	time_t calendar_end;
	gchar *start_timezone;
	gchar *end_timezone;
	gchar *contact_photo_id;
	gchar *iana_start_time_zone;
	gchar *iana_end_time_zone;
	gchar *event_url;

	GSList *to_recipients;
	GSList *cc_recipients;
	GSList *bcc_recipients;

	EwsMailbox *from;
	EwsMailbox *sender;

	gboolean is_meeting;
	gboolean is_response_requested;
	GSList *modified_occurrences;
	GSList *attachments_ids;
	gchar *my_response_type;
	GSList *attendees;

	EwsId *calendar_item_accept_id;

	/* the evolution labels are implemented as exchange
	 * Categories.  These appear in the message headers as
	 * Keywords: and are set and extracted from the EWS server as
	 * <Categories> which is a string array valued XML element */
	GSList *categories;

	gboolean reminder_is_set;
	time_t reminder_due_by;
	gint reminder_minutes_before_start;
	EEwsRecurrence recurrence;

	struct _EEwsContactFields *contact_fields;
	struct _EEwsTaskFields *task_fields;
};

G_DEFINE_TYPE_WITH_PRIVATE (EEwsItem, e_ews_item, G_TYPE_OBJECT)

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

	g_clear_error (&priv->error);

	if (priv->mapi_extended_sets) {
		g_hash_table_destroy (priv->mapi_extended_sets);
		priv->mapi_extended_sets = NULL;
	}

	if (priv->mapi_extended_tags) {
		g_hash_table_destroy (priv->mapi_extended_tags);
		priv->mapi_extended_tags = NULL;
	}

	if (priv->item_id) {
		g_free (priv->item_id->id);
		g_free (priv->item_id->change_key);
		g_free (priv->item_id);
		priv->item_id = NULL;
	}

	if (priv->attachment_id) {
		g_free (priv->attachment_id->id);
		g_free (priv->attachment_id->change_key);
		g_free (priv->attachment_id);
		priv->attachment_id = NULL;
	}

	g_clear_pointer (&priv->mime_content, g_free);
	g_clear_pointer (&priv->body, g_free);
	g_clear_pointer (&priv->subject, g_free);
	g_clear_pointer (&priv->msg_id, g_free);
	g_clear_pointer (&priv->uid, g_free);
	g_clear_pointer (&priv->in_replyto, g_free);
	g_clear_pointer (&priv->references, g_free);
	g_clear_pointer (&priv->preview, g_free);
	g_clear_pointer (&priv->date_header, g_free);
	g_clear_pointer (&priv->timezone, g_free);
	g_clear_pointer (&priv->start_timezone, g_free);
	g_clear_pointer (&priv->end_timezone, g_free);
	g_clear_pointer (&priv->contact_photo_id, g_free);
	g_clear_pointer (&priv->iana_start_time_zone, g_free);
	g_clear_pointer (&priv->iana_end_time_zone, g_free);
	g_clear_pointer (&priv->event_url, g_free);

	g_slist_free_full (priv->to_recipients, (GDestroyNotify) e_ews_mailbox_free);
	priv->to_recipients = NULL;

	g_slist_free_full (priv->cc_recipients, (GDestroyNotify) e_ews_mailbox_free);
	priv->cc_recipients = NULL;

	g_slist_free_full (priv->bcc_recipients, (GDestroyNotify) e_ews_mailbox_free);
	priv->bcc_recipients = NULL;

	g_slist_free_full (priv->modified_occurrences, g_free);
	priv->modified_occurrences = NULL;

	g_slist_free_full (priv->attachments_ids, g_free);
	priv->attachments_ids = NULL;

	g_clear_pointer (&priv->my_response_type, g_free);

	g_slist_free_full (priv->attendees, (GDestroyNotify) ews_item_free_attendee);
	priv->attendees = NULL;

	if (priv->calendar_item_accept_id) {
		g_free (priv->calendar_item_accept_id->id);
		g_free (priv->calendar_item_accept_id->change_key);
		g_free (priv->calendar_item_accept_id);
		priv->calendar_item_accept_id = NULL;
	}

	e_ews_mailbox_free (priv->sender);
	e_ews_mailbox_free (priv->from);

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

	g_slist_free_full (priv->categories, g_free);
	priv->categories = NULL;

	G_OBJECT_CLASS (e_ews_item_parent_class)->dispose (object);
}

static void
e_ews_item_class_init (EEwsItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_ews_item_dispose;
}

static void
e_ews_item_init (EEwsItem *item)
{
	item->priv = e_ews_item_get_instance_private (item);

	item->priv->item_type = E_EWS_ITEM_TYPE_UNKNOWN;
	item->priv->body_type = E_EWS_BODY_TYPE_ANY;
	item->priv->is_meeting = FALSE;
	item->priv->is_response_requested = FALSE;

	item->priv->mapi_extended_tags = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	item->priv->mapi_extended_sets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_destroy);

	item->priv->reminder_is_set = FALSE;
	item->priv->reminder_minutes_before_start = -1;
	item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
	item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
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
			g_free (cn);
		}

		if (con_fields->email_addresses)
			g_hash_table_destroy (con_fields->email_addresses);

		if (con_fields->physical_addresses)
			g_hash_table_destroy (con_fields->physical_addresses);

		if (con_fields->phone_numbers)
			g_hash_table_destroy (con_fields->phone_numbers);

		if (con_fields->im_addresses)
			g_hash_table_destroy (con_fields->im_addresses);

		g_free (con_fields->display_name);
		g_free (con_fields->fileas);
		g_free (con_fields->company_name);
		g_free (con_fields->department);
		g_free (con_fields->job_title);
		g_free (con_fields->assistant_name);
		g_free (con_fields->manager);
		g_free (con_fields->office_location);
		g_free (con_fields->business_homepage);
		g_free (con_fields->profession);
		g_free (con_fields->spouse_name);
		g_free (con_fields->culture);
		g_free (con_fields->surname);
		g_free (con_fields->givenname);
		g_free (con_fields->middlename);
		g_free (con_fields->notes);
		g_free (con_fields->msexchange_cert);
		g_free (con_fields->user_cert);
		g_free (con_fields);
	}
}

static void
ews_item_free_attendee (EwsAttendee *attendee)
{
	if (attendee) {
		e_ews_mailbox_free (attendee->mailbox);
		g_free (attendee->responsetype);
		g_free (attendee);
	}
}

static time_t
ews_item_parse_date (ESoapParameter *param)
{
	time_t t = 0;
	GTimeVal t_val;
	gchar *dtstring;
	gint len;

	dtstring = e_soap_parameter_get_string_value (param);

	g_return_val_if_fail (dtstring != NULL, 0);

	len = strlen (dtstring);
	if (g_time_val_from_iso8601 (dtstring, &t_val)) {
		t = (time_t) t_val.tv_sec;
	} else if (len == 8 || (len == 11 && dtstring[4] == '-' && dtstring[7] == '-' && dtstring[10] == 'Z')) {
		/* It might be a date value */
		guint16 year;
		guint month;
		guint8 day;

		if (len == 11) {
			dtstring[4] = dtstring[5];
			dtstring[5] = dtstring[6];
			dtstring[6] = dtstring[8];
			dtstring[7] = dtstring[9];
			dtstring[8] = dtstring[10];
			dtstring[9] = '\0';
		}

#define digit_at(x,y) (x[y] - '0')
		year = digit_at (dtstring, 0) * 1000
			+ digit_at (dtstring, 1) * 100
			+ digit_at (dtstring, 2) * 10
			+ digit_at (dtstring, 3);
		month = digit_at (dtstring, 4) * 10 + digit_at (dtstring, 5);
		day = digit_at (dtstring, 6) * 10 + digit_at (dtstring, 7);

		if (len == 11) {
			ICalTime *itt;

			itt = i_cal_time_new_null_time ();
			i_cal_time_set_date (itt, year, month, day);
			i_cal_time_set_timezone (itt, i_cal_timezone_get_utc_timezone ());
			i_cal_time_set_is_date (itt, TRUE);

			t = i_cal_time_as_timet_with_zone (itt, i_cal_timezone_get_utc_timezone ());

			g_object_unref (itt);
		} else {
			GDate date;
			struct tm tt;

			g_date_clear (&date, 1);
			g_date_set_year (&date, year);
			g_date_set_month (&date, month);
			g_date_set_day (&date, day);

			g_date_to_struct_tm (&date, &tt);
			t = mktime (&tt);
		}
	} else
		g_warning ("%s: Could not parse the string '%s'", G_STRFUNC, dtstring ? dtstring : "[null]");

	g_free (dtstring);

	return t;
}

static void
parse_extended_property (EEwsItemPrivate *priv,
                         ESoapParameter *param)
{
	EEwsMessageDataType data_type;
	ESoapParameter *subparam;
	gchar *str, *setid, *name, *value;
	guint32 tag = 0;

	subparam = e_soap_parameter_get_first_child_by_name (param, "ExtendedFieldURI");
	if (!subparam)
		return;

	str = e_soap_parameter_get_property (subparam, "PropertyType");
	if (!str)
		return;

	/* We only handle some MAPI properties for now... */
	if (g_ascii_strcasecmp (str, "Boolean") == 0) {
		data_type = E_EWS_MESSAGE_DATA_TYPE_BOOLEAN;
	} else if (g_ascii_strcasecmp (str, "Integer") == 0) {
		data_type = E_EWS_MESSAGE_DATA_TYPE_INT;
	} else if (g_ascii_strcasecmp (str, "Double") == 0) {
		data_type = E_EWS_MESSAGE_DATA_TYPE_DOUBLE;
	} else if (g_ascii_strcasecmp (str, "String") == 0) {
		data_type = E_EWS_MESSAGE_DATA_TYPE_STRING;
	} else if (g_ascii_strcasecmp (str, "SystemTime") == 0) {
		data_type = E_EWS_MESSAGE_DATA_TYPE_TIME;
	} else {
		g_free (str);
		return;
	}
	g_free (str);

	name = e_soap_parameter_get_property (subparam, "PropertyName");
	if (!name) {
		str = e_soap_parameter_get_property (subparam, "PropertyTag");
		if (!str) {
			str = e_soap_parameter_get_property (subparam, "PropertyId");
			if (!str)
				return;
		}

		tag = strtol (str, NULL, 0);
		g_free (str);
	}

	setid = e_soap_parameter_get_property (subparam, "DistinguishedPropertySetId");

	subparam = e_soap_parameter_get_first_child_by_name (param, "Value");
	if (!subparam) {
		g_free (setid);
		g_free (name);
		return;
	}

	value = e_soap_parameter_get_string_value (subparam);
	if (!value) {
		g_free (setid);
		g_free (name);
		return;
	}

	if (data_type == E_EWS_MESSAGE_DATA_TYPE_INT) {
		guint32 num_value;

		num_value = strtol (value, NULL, 0);

		switch (tag) {
		case 0x01080: /* PidTagIconIndex */
			priv->mapi_icon_index = num_value;
			break;

		case 0x1081: /* PidTagLastVerbExecuted */
			priv->mapi_last_verb_executed = num_value;
			break;

		case 0x0e07: /* PidTagMessageFlags */
			priv->mapi_message_flags = num_value;
			break;

		case 0x0e17: /* PidTagMessageStatus */
			priv->mapi_message_status = num_value;
			break;
		}
	}

	if (setid) {
		if (g_strcmp0 (name, "EvolutionEWSStartTimeZone") == 0) {
			priv->iana_start_time_zone = g_strdup (value);
		} else if (g_strcmp0 (name, "EvolutionEWSEndTimeZone") == 0) {
			priv->iana_end_time_zone = g_strdup (value);
		} else if (g_strcmp0 (name, "EvolutionEWSURL") == 0) {
			priv->event_url = value && *value ? g_strdup (value) : NULL;
		} else {
			GHashTable *set_hash = g_hash_table_lookup (priv->mapi_extended_sets, setid);

			if (!set_hash) {
				set_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
				g_hash_table_insert (priv->mapi_extended_sets, setid, set_hash);
				setid = NULL;
			}

			g_hash_table_insert (set_hash, GUINT_TO_POINTER (tag), g_strdup (value));
		}
	} else if (tag != 0) {
		g_hash_table_insert (priv->mapi_extended_tags, GUINT_TO_POINTER (tag), g_strdup (value));
	}

	g_free (setid);
	g_free (value);
	g_free (name);
}

static void
parse_categories (EEwsItemPrivate *priv,
                  ESoapParameter *param)
{
	gchar *value;
	ESoapParameter *subparam;

	/* release all the old data (if any) */
	g_slist_free_full (priv->categories, g_free);
	priv->categories = NULL;

	/* categories are an array of <string> */
	for (subparam = e_soap_parameter_get_first_child (param);
	     subparam != NULL;
	     subparam = e_soap_parameter_get_next_child (subparam)) {
		value = e_soap_parameter_get_string_value (subparam);

		priv->categories = g_slist_append (priv->categories, value);
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

static void
process_modified_occurrences (EEwsItemPrivate *priv,
                              ESoapParameter *param)
{
	ESoapParameter *subparam, *subparam1;
	gchar *modified_occurrence_id;

	for (subparam = e_soap_parameter_get_first_child (param); subparam != NULL; subparam = e_soap_parameter_get_next_child (subparam)) {

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "ItemId");
		modified_occurrence_id = e_soap_parameter_get_property (subparam1, "Id");
		priv->modified_occurrences = g_slist_append (priv->modified_occurrences, modified_occurrence_id);
	}

	return;
}

static void
process_attachments_list (EEwsItemPrivate *priv,
                          ESoapParameter *param)
{
	ESoapParameter *subparam, *subparam1;

	GSList *ids = NULL;

	for (subparam = e_soap_parameter_get_first_child (param); subparam != NULL; subparam = e_soap_parameter_get_next_child (subparam)) {
		gchar *id;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "AttachmentId");
		id = e_soap_parameter_get_property (subparam1, "Id");

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "IsContactPhoto");
		if (subparam1) {
			gchar *value = e_soap_parameter_get_string_value (subparam1);
			if (g_strcmp0 (value, "true") == 0) {
				priv->contact_photo_id = id;
				g_free (value);
				continue;
			}
			g_free (value);
		}

		ids = g_slist_append (ids, id);
	}

	priv->attachments_ids = ids;
	return;
}

static void
process_attendees (EEwsItemPrivate *priv,
                   ESoapParameter *param,
                   const gchar *type)
{
	ESoapParameter *subparam, *subparam1;
	EwsAttendee *attendee;

	for (subparam = e_soap_parameter_get_first_child (param); subparam != NULL; subparam = e_soap_parameter_get_next_child (subparam)) {
		EwsMailbox *mailbox = NULL;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Mailbox");
		mailbox = e_ews_item_mailbox_from_soap_param (subparam1);
		/* Ignore attendee if mailbox is not valid,
		 * for instance, ppl that does not exists any more */
		if (!mailbox)
			continue;

		attendee = g_new0 (EwsAttendee, 1);

		attendee->mailbox = mailbox;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "ResponseType");
		attendee->responsetype = subparam1 ? e_soap_parameter_get_string_value (subparam1) : NULL;

		attendee->attendeetype = (gchar *) type;

		priv->attendees = g_slist_append (priv->attendees, attendee);
	}

	return;
}

static void
parse_complete_name (struct _EEwsContactFields *con_fields,
                     ESoapParameter *param)
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

	subparam = e_soap_parameter_get_first_child_by_name (param, "CountryOrRegion");
	if (subparam)
		address->country = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "PostalCode");
	if (subparam)
		address->postal_code = e_soap_parameter_get_string_value (subparam);

	return address;
}

static void
parse_entries (GHashTable *hash_table,
               ESoapParameter *param,
               EwsGetValFunc get_val_func)
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
parse_contact_field (EEwsItem *item,
                     const gchar *name,
                     ESoapParameter *subparam)
{
	EEwsItemPrivate *priv = item->priv;

	if (!g_ascii_strcasecmp (name, "Culture")) {
		priv->contact_fields->culture = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "DisplayName")) {
		priv->contact_fields->display_name = e_soap_parameter_get_string_value (subparam);
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
		priv->contact_fields->has_birthday = TRUE;
		priv->contact_fields->birthday = ews_item_parse_date (subparam);
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
	} else if (!g_ascii_strcasecmp (name, "Profession")) {
		priv->contact_fields->profession = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "SpouseName")) {
		priv->contact_fields->spouse_name = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "Surname")) {
		priv->contact_fields->surname = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "GivenName")) {
		priv->contact_fields->givenname = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "MiddleName")) {
		priv->contact_fields->middlename = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "WeddingAnniversary")) {
		priv->contact_fields->has_wedding_anniversary = TRUE;
		priv->contact_fields->wedding_anniversary = ews_item_parse_date (subparam);
	} else if (!g_ascii_strcasecmp (name, "Body")) {
		/*
		 * For Exchange versions >= 2010_SP2 Notes property can be get
		 * directly from contacts:Notes. But for backward compatibility
		 * with old servers (< 2010_SP2) we prefer use item:Body.
		 */
		priv->contact_fields->notes = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "UserSMIMECertificate") ||
		   !g_ascii_strcasecmp (name, "MSExchangeCertificate")) {
		ESoapParameter *data_param;
		guchar **out_bytes;
		gsize *out_len;

		if (!g_ascii_strcasecmp (name, "UserSMIMECertificate")) {
			out_bytes = &priv->contact_fields->user_cert;
			out_len = &priv->contact_fields->user_cert_len;
		} else {
			out_bytes = &priv->contact_fields->msexchange_cert;
			out_len = &priv->contact_fields->msexchange_cert_len;
		}

		data_param = e_soap_parameter_get_first_child_by_name (subparam, "Base64Binary");
		if (data_param) {
			gchar *base64_data;

			base64_data = e_soap_parameter_get_string_value (data_param);
			if (base64_data && *base64_data) {
				*out_bytes = g_base64_decode_inplace (base64_data, out_len);
				if (!*out_len) {
					g_free (*out_bytes);

					*out_len = 0;
					*out_bytes = NULL;
				}
			} else {
				g_free (base64_data);
			}
		}
	}
}

static guint32 /* bit-or of EEwsRecurrenceDaysOfWeek */
parse_recur_days_of_week (ESoapParameter *param)
{
	struct _keys {
		const gchar *str_value;
		EEwsRecurrenceDaysOfWeek bit_value;
	} keys[] = {
		/* Do not localize, these are values used in XML */
		{ "Sunday", E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY },
		{ "Monday", E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY },
		{ "Tuesday", E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY },
		{ "Wednesday", E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY },
		{ "Thursday", E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY },
		{ "Friday", E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY },
		{ "Saturday", E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY },
		{ "Day", E_EWS_RECURRENCE_DAYS_OF_WEEK_DAY },
		{ "Weekday", E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKDAY },
		{ "WeekendDay", E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKENDDAY }
	};
	gchar *value, **split_value;
	guint32 days_of_week = 0;
	gint ii, jj;

	g_return_val_if_fail (param != NULL, E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN);

	value = e_soap_parameter_get_string_value (param);
	if (!value || !*value) {
		g_free (value);
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN;
	}

	split_value = g_strsplit (value, " ", -1);

	for (ii = 0; split_value && split_value[ii]; ii++) {
		const gchar *str = split_value[ii];

		if (!str || !*str)
			continue;

		for (jj = 0; jj < G_N_ELEMENTS (keys); jj++) {
			if (g_strcmp0 (str, keys[jj].str_value) == 0) {
				days_of_week |= keys[jj].bit_value;
				break;
			}
		}
	}

	g_strfreev (split_value);
	g_free (value);

	return days_of_week;
}

static EEwsRecurrenceDayOfWeekIndex
parse_recur_day_of_week_index (ESoapParameter *param)
{
	EEwsRecurrenceDayOfWeekIndex day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN;
	gchar *value;

	g_return_val_if_fail (param != NULL, E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN);

	value = e_soap_parameter_get_string_value (param);
	if (!value || !*value) {
		g_free (value);
		return E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN;
	}

	/* Do not localize, these are values used in XML */
	if (g_strcmp0 (value, "First") == 0)
		day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FIRST;
	else if (g_strcmp0 (value, "Second") == 0)
		day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_SECOND;
	else if (g_strcmp0 (value, "Third") == 0)
		day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_THIRD;
	else if (g_strcmp0 (value, "Fourth") == 0)
		day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FOURTH;
	else if (g_strcmp0 (value, "Last") == 0)
		day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST;

	g_free (value);

	return day_of_week_index;
}

static GDateMonth
parse_recur_month (ESoapParameter *param)
{
	GDateMonth month = G_DATE_BAD_MONTH;
	gchar *value;

	g_return_val_if_fail (param != NULL, G_DATE_BAD_MONTH);

	value = e_soap_parameter_get_string_value (param);
	if (!value || !*value) {
		g_free (value);
		return G_DATE_BAD_MONTH;
	}

	/* Do not localize, these are values used in XML */
	if (g_strcmp0 (value, "January") == 0)
		month = G_DATE_JANUARY;
	else if (g_strcmp0 (value, "February") == 0)
		month = G_DATE_FEBRUARY;
	else if (g_strcmp0 (value, "March") == 0)
		month = G_DATE_MARCH;
	else if (g_strcmp0 (value, "April") == 0)
		month = G_DATE_APRIL;
	else if (g_strcmp0 (value, "May") == 0)
		month = G_DATE_MAY;
	else if (g_strcmp0 (value, "June") == 0)
		month = G_DATE_JUNE;
	else if (g_strcmp0 (value, "July") == 0)
		month = G_DATE_JULY;
	else if (g_strcmp0 (value, "August") == 0)
		month = G_DATE_AUGUST;
	else if (g_strcmp0 (value, "September") == 0)
		month = G_DATE_SEPTEMBER;
	else if (g_strcmp0 (value, "October") == 0)
		month = G_DATE_OCTOBER;
	else if (g_strcmp0 (value, "November") == 0)
		month = G_DATE_NOVEMBER;
	else if (g_strcmp0 (value, "December") == 0)
		month = G_DATE_DECEMBER;

	g_free (value);

	return month;
}

static GDateWeekday
parse_recur_first_day_of_week (ESoapParameter *param)
{
	GDateWeekday first_day_of_week = G_DATE_BAD_WEEKDAY;
	gchar *value;

	g_return_val_if_fail (param != NULL, G_DATE_BAD_WEEKDAY);

	value = e_soap_parameter_get_string_value (param);
	if (!value || !*value) {
		g_free (value);
		return G_DATE_BAD_WEEKDAY;
	}

	/* Do not localize, these are values used in XML */
	if (g_strcmp0 (value, "Sunday") == 0)
		first_day_of_week = G_DATE_SUNDAY;
	else if (g_strcmp0 (value, "Monday") == 0)
		first_day_of_week = G_DATE_MONDAY;
	else if (g_strcmp0 (value, "Tuesday") == 0)
		first_day_of_week = G_DATE_TUESDAY;
	else if (g_strcmp0 (value, "Wednesday") == 0)
		first_day_of_week = G_DATE_WEDNESDAY;
	else if (g_strcmp0 (value, "Thursday") == 0)
		first_day_of_week = G_DATE_THURSDAY;
	else if (g_strcmp0 (value, "Friday") == 0)
		first_day_of_week = G_DATE_FRIDAY;
	else if (g_strcmp0 (value, "Saturday") == 0)
		first_day_of_week = G_DATE_SATURDAY;

	g_free (value);

	return first_day_of_week;
}

static void
parse_recurrence_field (EEwsItem *item,
			ESoapParameter *param)
{
	ESoapParameter *subparam, *subparam1;

	g_return_if_fail (item != NULL);
	g_return_if_fail (param != NULL);

	item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
	item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;

	if ((subparam = e_soap_parameter_get_first_child_by_name (param, "RelativeYearlyRecurrence")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_RELATIVE_YEARLY;
		item->priv->recurrence.recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN;
		item->priv->recurrence.recur.relative_yearly.day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN;
		item->priv->recurrence.recur.relative_yearly.month = G_DATE_BAD_MONTH;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "DaysOfWeek");
		if (subparam1) {
			item->priv->recurrence.recur.relative_yearly.days_of_week = parse_recur_days_of_week (subparam1);

			if (item->priv->recurrence.recur.relative_yearly.days_of_week == E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "DayOfWeekIndex");
		if (subparam1) {
			item->priv->recurrence.recur.relative_yearly.day_of_week_index = parse_recur_day_of_week_index (subparam1);

			if (item->priv->recurrence.recur.relative_yearly.day_of_week_index == E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Month");
		if (subparam1) {
			item->priv->recurrence.recur.relative_yearly.month = parse_recur_month (subparam1);

			if (item->priv->recurrence.recur.relative_yearly.month == G_DATE_BAD_MONTH)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "AbsoluteYearlyRecurrence")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_ABSOLUTE_YEARLY;
		item->priv->recurrence.recur.absolute_yearly.day_of_month = 0;
		item->priv->recurrence.recur.absolute_yearly.month = G_DATE_BAD_MONTH;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "DayOfMonth");
		if (subparam1) {
			item->priv->recurrence.recur.absolute_yearly.day_of_month = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.absolute_yearly.day_of_month < 1 ||
			    item->priv->recurrence.recur.absolute_yearly.day_of_month > 31)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Month");
		if (subparam1) {
			item->priv->recurrence.recur.absolute_yearly.month = parse_recur_month (subparam1);

			if (item->priv->recurrence.recur.absolute_yearly.month == G_DATE_BAD_MONTH)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "RelativeMonthlyRecurrence")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_RELATIVE_MONTHLY;
		item->priv->recurrence.recur.relative_monthly.interval = 0;
		item->priv->recurrence.recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN;
		item->priv->recurrence.recur.relative_monthly.day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.relative_monthly.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.relative_monthly.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "DaysOfWeek");
		if (subparam1) {
			item->priv->recurrence.recur.relative_monthly.days_of_week = parse_recur_days_of_week (subparam1);

			if (item->priv->recurrence.recur.relative_monthly.days_of_week == E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "DayOfWeekIndex");
		if (subparam1) {
			item->priv->recurrence.recur.relative_monthly.day_of_week_index = parse_recur_day_of_week_index (subparam1);

			if (item->priv->recurrence.recur.relative_monthly.day_of_week_index == E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "AbsoluteMonthlyRecurrence")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_ABSOLUTE_MONTHLY;
		item->priv->recurrence.recur.absolute_monthly.interval = 0;
		item->priv->recurrence.recur.absolute_monthly.day_of_month = 0;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.absolute_monthly.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.absolute_monthly.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "DayOfMonth");
		if (subparam1) {
			item->priv->recurrence.recur.absolute_monthly.day_of_month = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.absolute_monthly.day_of_month < 1 ||
			    item->priv->recurrence.recur.absolute_monthly.day_of_month > 31)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "WeeklyRecurrence")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_WEEKLY;
		item->priv->recurrence.recur.weekly.interval = 0;
		item->priv->recurrence.recur.weekly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN;
		item->priv->recurrence.recur.weekly.first_day_of_week = G_DATE_BAD_WEEKDAY;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.weekly.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.weekly.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "DaysOfWeek");
		if (subparam1) {
			item->priv->recurrence.recur.weekly.days_of_week = parse_recur_days_of_week (subparam1);

			if (item->priv->recurrence.recur.weekly.days_of_week == E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "FirstDayOfWeek");
		if (subparam1) {
			item->priv->recurrence.recur.weekly.first_day_of_week = parse_recur_first_day_of_week (subparam1);

			if (item->priv->recurrence.recur.weekly.first_day_of_week == G_DATE_BAD_WEEKDAY)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			/* It's okay, because 2007 doesn't support it */
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "DailyRecurrence")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_DAILY;
		item->priv->recurrence.recur.interval = 0;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "DailyRegeneration")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_DAILY_REGENERATION;
		item->priv->recurrence.recur.interval = 0;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "WeeklyRegeneration")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_WEEKLY_REGENERATION;
		item->priv->recurrence.recur.interval = 0;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "MonthlyRegeneration")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_MONTHLY_REGENERATION;
		item->priv->recurrence.recur.interval = 0;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "YearlyRegeneration")) != NULL) {
		item->priv->recurrence.type = E_EWS_RECURRENCE_YEARLY_REGENERATION;
		item->priv->recurrence.recur.interval = 0;

		subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "Interval");
		if (subparam1) {
			item->priv->recurrence.recur.interval = e_soap_parameter_get_int_value (subparam1);

			if (item->priv->recurrence.recur.interval < 1)
				item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		} else {
			item->priv->recurrence.type = E_EWS_RECURRENCE_UNKNOWN;
		}
	}

	if (item->priv->recurrence.type != E_EWS_RECURRENCE_UNKNOWN) {
		if ((subparam = e_soap_parameter_get_first_child_by_name (param, "NoEndRecurrence")) != NULL) {
			subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "StartDate");
			if (subparam1) {
				item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_NO_END;
				item->priv->recurrence.utc_start_date = ews_item_parse_date (subparam1);

				if (item->priv->recurrence.utc_start_date == (time_t) -1)
					item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			}
		} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "EndDateRecurrence")) != NULL) {
			item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_DATE;

			subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "StartDate");
			if (subparam1) {
				item->priv->recurrence.utc_start_date = ews_item_parse_date (subparam1);

				if (item->priv->recurrence.utc_start_date == (time_t) -1)
					item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			} else {
				item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			}

			subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "EndDate");
			if (subparam1) {
				item->priv->recurrence.end.utc_end_date = ews_item_parse_date (subparam1);

				if (item->priv->recurrence.end.utc_end_date == (time_t) -1)
					item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			} else {
				item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			}
		} else if ((subparam = e_soap_parameter_get_first_child_by_name (param, "NumberedRecurrence")) != NULL) {
			item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_NUMBERED;

			subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "StartDate");
			if (subparam1) {
				item->priv->recurrence.utc_start_date = ews_item_parse_date (subparam1);

				if (item->priv->recurrence.utc_start_date == (time_t) -1)
					item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			} else {
				item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			}

			subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "NumberOfOccurrences");
			if (subparam1) {
				item->priv->recurrence.end.number_of_occurrences = e_soap_parameter_get_int_value (subparam1);

				if (item->priv->recurrence.end.number_of_occurrences < 1)
					item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			} else {
				item->priv->recurrence.end_type = E_EWS_RECURRENCE_END_UNKNOWN;
			}
		}
	}

	g_warn_if_fail (
		(item->priv->recurrence.type == E_EWS_RECURRENCE_UNKNOWN &&  item->priv->recurrence.end_type == E_EWS_RECURRENCE_END_UNKNOWN) ||
		(item->priv->recurrence.type != E_EWS_RECURRENCE_UNKNOWN &&  item->priv->recurrence.end_type != E_EWS_RECURRENCE_END_UNKNOWN));
}

static gchar *
strip_html_tags (const gchar *html_text)
{
	gssize haystack_len = strlen (html_text);
	gchar *plain_text = g_malloc (haystack_len + 1);
	gchar *start = g_strstr_len (html_text, haystack_len, "<body"),
		*end = g_strstr_len (html_text, haystack_len, "</body"),
		*i, *j;

	if (!start || !end) {
		g_free (plain_text);
		return g_strdup (html_text);
	}

	i = start;
	while (i < end && *i != '>') {
		i++;
	}

	for (j = plain_text; i < end; i++) {
		if (*i == '&') {
			gchar *from = i;

			while (i < end && *i != ';' && *i != '<' && *i != '>')
				i++;

			if (i >= end)
				break;

			if (*i != ';')
				i = from;
			else
				continue;
		}

		if (*i == '<') {
			while (i < end && *i != '>')
				i++;

			if (i >= end)
				break;
		} else {
			*j = *i;
			j++;
		}
	}

	*j = '\0';

	return plain_text;
}

static void
parse_task_field (EEwsItem *item,
                  const gchar *name,
                  ESoapParameter *subparam)
{
	EEwsItemPrivate *priv = item->priv;
	gchar *value = NULL;

	if (!g_ascii_strcasecmp (name, "Status")) {
		priv->task_fields->status = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "PercentComplete")) {
		priv->task_fields->percent_complete = e_soap_parameter_get_string_value (subparam);
	} else if (!g_ascii_strcasecmp (name, "DueDate")) {
		priv->task_fields->due_date = ews_item_parse_date (subparam);
		priv->task_fields->has_due_date = TRUE;
	} else if (!g_ascii_strcasecmp (name, "StartDate")) {
		priv->task_fields->start_date = ews_item_parse_date (subparam);
		priv->task_fields->has_start_date = TRUE;
	} else if (!g_ascii_strcasecmp (name, "CompleteDate")) {
		priv->task_fields->complete_date = ews_item_parse_date (subparam);
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
		if (!g_ascii_strcasecmp (priv->task_fields->delegator, "")) {
			g_free (priv->task_fields->delegator);
			priv->task_fields->delegator = NULL;
		}
	} else if (!g_ascii_strcasecmp (name, "Recurrence")) {
		parse_recurrence_field (item, subparam);
	}
}

static gboolean
e_ews_item_set_from_soap_parameter (EEwsItem *item,
                                    ESoapParameter *param)
{
	EEwsItemPrivate *priv = item->priv;
	ESoapParameter *subparam, *node = NULL, *attach_id;
	const gchar *name;

	g_return_val_if_fail (param != NULL, FALSE);

	name = e_soap_parameter_get_name (param);

	/*We get two types of response for items from server like below from two apis
	 *  Syncfolderitems			and  		Finditem
	 * <m:Changes>							<t:Items>
 *          <t:Create>							  <t:Contact>
 *            <t:Contact>						    <t:ItemId Id="AS4AUn=" ChangeKey="fsVU4==" />
 *              <t:ItemId Id="AAA=" ChangeKey="NAgws"/>			  </t:Contact>
 *            </t:Contact>						  <t:Contact>
 *          </t:Create>							    <t:ItemId Id="AS4BS=" ChangeKey="fjidU4==" />
	 *    <t:Contact>						  </t:Contact>
 *              <t:ItemId Id="ABB=" ChangeKey="GCDab"/>			  ...
 *            </t:Contact>						</t:Items>
	 *  </t:Create>
	 *  ...
	 * </m:Changes> 
	 * So check param is the node we want to use, by comparing name or is it child of the param */

	if (!g_ascii_strcasecmp (name, "Message") || (node = e_soap_parameter_get_first_child_by_name (param, "Message"))) {
		priv->item_type = E_EWS_ITEM_TYPE_MESSAGE;
		subparam = e_soap_parameter_get_first_child_by_name (node ? node : param, "ItemClass");
		if (subparam) {
			gchar *folder_class = e_soap_parameter_get_string_value (subparam);

			if (g_strcmp0 (folder_class, "IPM.StickyNote") == 0) {
				priv->item_type = E_EWS_ITEM_TYPE_MEMO;
				priv->task_fields = g_new0 (struct _EEwsTaskFields, 1);
				priv->task_fields->has_due_date = FALSE;
				priv->task_fields->has_start_date = FALSE;
				priv->task_fields->has_complete_date = FALSE;
			}

			g_free (folder_class);
		}
	} else if (!g_ascii_strcasecmp (name, "PostItem") || (node = e_soap_parameter_get_first_child_by_name (param, "PostItem")))
		priv->item_type = E_EWS_ITEM_TYPE_POST_ITEM;
	else if (!g_ascii_strcasecmp (name, "CalendarItem") || (node = e_soap_parameter_get_first_child_by_name (param, "CalendarItem")))
		priv->item_type = E_EWS_ITEM_TYPE_EVENT;
	else if (!g_ascii_strcasecmp (name, "Contact") || (node = e_soap_parameter_get_first_child_by_name (param, "Contact"))) {
		priv->item_type = E_EWS_ITEM_TYPE_CONTACT;
		priv->contact_fields = g_new0 (struct _EEwsContactFields, 1);
	} else if (!g_ascii_strcasecmp (name, "DistributionList") || (node = e_soap_parameter_get_first_child_by_name (param, "DistributionList")))
		priv->item_type = E_EWS_ITEM_TYPE_GROUP;
	else if (!g_ascii_strcasecmp (name, "MeetingMessage") || (node = e_soap_parameter_get_first_child_by_name (param, "MeetingMessage")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_MESSAGE;
	else if (!g_ascii_strcasecmp (name, "MeetingRequest") || (node = e_soap_parameter_get_first_child_by_name (param, "MeetingRequest")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_REQUEST;
	else if (!g_ascii_strcasecmp (name, "MeetingResponse") || (node = e_soap_parameter_get_first_child_by_name (param, "MeetingResponse")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_RESPONSE;
	else if (!g_ascii_strcasecmp (name, "MeetingCancellation") || (node = e_soap_parameter_get_first_child_by_name (param, "MeetingCancellation")))
		priv->item_type = E_EWS_ITEM_TYPE_MEETING_CANCELLATION;
	else if (!g_ascii_strcasecmp (name, "Task") || (node = e_soap_parameter_get_first_child_by_name (param, "Task"))) {
		priv->item_type = E_EWS_ITEM_TYPE_TASK;
		priv->task_fields = g_new0 (struct _EEwsTaskFields, 1);
		priv->task_fields->has_due_date = FALSE;
		priv->task_fields->has_start_date = FALSE;
		priv->task_fields->has_complete_date = FALSE;
	} else if (!g_ascii_strcasecmp (name, "Item") || (node = e_soap_parameter_get_first_child_by_name (param, "Item")))
		priv->item_type = E_EWS_ITEM_TYPE_GENERIC_ITEM;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "AttachmentId"))) {
		priv->attachment_id = g_new0 (EwsId, 1);
		priv->attachment_id->id = e_soap_parameter_get_property (node, "Id");
		priv->attachment_id->change_key = e_soap_parameter_get_property (node, "ChangeKey");
	} else if ((node = e_soap_parameter_get_first_child_by_name (param, "ItemId"))) {
		/*Spesial case when we are facing  <ReadFlagChange> during sync folders*/
		priv->item_id = g_new0 (EwsId, 1);
		priv->item_id->id = e_soap_parameter_get_property (node, "Id");
		priv->item_id->change_key = e_soap_parameter_get_property (node, "ChangeKey");
		return TRUE;
	} else {
		g_warning ("Unable to find the Item type \n");
		return FALSE;
	}

	attach_id = e_soap_parameter_get_first_child_by_name (param, "AttachmentId");
	if (attach_id) {
		priv->attachment_id = g_new0 (EwsId, 1);
		priv->attachment_id->id = e_soap_parameter_get_property (attach_id, "Id");
		priv->attachment_id->change_key = e_soap_parameter_get_property (attach_id, "ChangeKey");
	}

	if (!node)
		node = param;

	for (subparam = e_soap_parameter_get_first_child (node);
		subparam != NULL;
		subparam = e_soap_parameter_get_next_child (subparam)) {
		ESoapParameter *subparam1;
		gchar *value = NULL;

		name = e_soap_parameter_get_name (subparam);

		/* The order is maintained according to the order in soap response */
		if (!g_ascii_strcasecmp (name, "MimeContent")) {
			gchar *charset;
			guchar *data;
			gsize data_len = 0;

			value = e_soap_parameter_get_string_value (subparam);
			data = g_base64_decode (value, &data_len);
			if (!data || !data_len) {
				g_free (value);
				g_free (data);
				return FALSE;
			}

			charset = e_soap_parameter_get_property (subparam, "CharacterSet");
			if (g_strcmp0 (charset, "UTF-8") == 0 &&
			    !g_utf8_validate ((const gchar *) data, data_len, NULL)) {
				gchar *tmp;

				tmp = e_util_utf8_data_make_valid ((const gchar *) data, data_len);
				if (tmp) {
					g_free (data);
					data = (guchar *) tmp;
				}
			}
			g_free (charset);

			priv->mime_content = (gchar *) data;

			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "ItemId")) {
			priv->item_id = g_new0 (EwsId, 1);
			priv->item_id->id = e_soap_parameter_get_property (subparam, "Id");
			priv->item_id->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
		} else if (!g_ascii_strcasecmp (name, "Subject")) {
			priv->subject = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "InternetMessageHeaders")) {
			for (subparam1 = e_soap_parameter_get_first_child_by_name (subparam, "InternetMessageHeader");
			     subparam1;
			     subparam1 = e_soap_parameter_get_next_child (subparam1)) {
				gchar *str = e_soap_parameter_get_property (subparam1, "HeaderName");

				if (g_strcmp0 (str, "Date") == 0) {
					priv->date_header = e_soap_parameter_get_string_value (subparam1);
					g_free (str);
					break;
				}

				g_free (str);
			}
		} else if (!g_ascii_strcasecmp (name, "DateTimeReceived")) {
			priv->date_received = ews_item_parse_date (subparam);
		} else if (!g_ascii_strcasecmp (name, "Size")) {
			priv->size = e_soap_parameter_get_int_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "Categories")) {
			parse_categories (priv, subparam);
		} else if (!g_ascii_strcasecmp (name, "Importance")) {
			priv->importance = parse_importance (subparam);
		} else if (!g_ascii_strcasecmp (name, "InReplyTo")) {
			priv->in_replyto = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "DateTimeSent")) {
			priv->date_sent = ews_item_parse_date (subparam);
		} else if (!g_ascii_strcasecmp (name, "DateTimeCreated")) {
			priv->date_created = ews_item_parse_date (subparam);
		} else if (!g_ascii_strcasecmp (name, "LastModifiedTime")) {
			priv->last_modified_time = ews_item_parse_date (subparam);
		} else if (!g_ascii_strcasecmp (name, "HasAttachments")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->has_attachments = (!g_ascii_strcasecmp (value, "true"));
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "Attachments")) {
			process_attachments_list (priv, subparam);
		} else if (priv->item_type == E_EWS_ITEM_TYPE_CONTACT)
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
		} else if (!g_ascii_strcasecmp (name, "Preview")) {
			priv->preview = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "UID")) {
			priv->uid = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "IsRead")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->is_read = (!g_ascii_strcasecmp (value, "true"));
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "TimeZone")) {
			priv->timezone = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "ReminderIsSet")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->reminder_is_set = (!g_ascii_strcasecmp (value, "true"));
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "ReminderDueBy")) {
			priv->reminder_due_by = ews_item_parse_date (subparam);
		} else if (!g_ascii_strcasecmp (name, "ReminderMinutesBeforeStart")) {
			priv->reminder_minutes_before_start = e_soap_parameter_get_int_value (subparam);
		} else if (priv->item_type == E_EWS_ITEM_TYPE_TASK || priv->item_type == E_EWS_ITEM_TYPE_MEMO) {
			parse_task_field (item, name, subparam);
			/* fields below are not relevant for task, so skip them */
		} else if (!g_ascii_strcasecmp (name, "References")) {
			priv->references = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "ExtendedProperty")) {
			parse_extended_property (priv, subparam);
		} else if (!g_ascii_strcasecmp (name, "ModifiedOccurrences")) {
			process_modified_occurrences (priv, subparam);
		} else if (!g_ascii_strcasecmp (name, "IsMeeting")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->is_meeting = (!g_ascii_strcasecmp (value, "true"));
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "IsResponseRequested")) {
			value = e_soap_parameter_get_string_value (subparam);
			priv->is_response_requested = (!g_ascii_strcasecmp (value, "true"));
			g_free (value);
		} else if (!g_ascii_strcasecmp (name, "MyResponseType")) {
			g_free (priv->my_response_type);
			priv->my_response_type = e_soap_parameter_get_string_value (subparam);
		} else if (!g_ascii_strcasecmp (name, "RequiredAttendees")) {
			process_attendees (priv, subparam, "Required");
		} else if (!g_ascii_strcasecmp (name, "OptionalAttendees")) {
			process_attendees (priv, subparam, "Optional");
		} else if (!g_ascii_strcasecmp (name, "Resources")) {
			process_attendees (priv, subparam, "Resource");
		} else if (!g_ascii_strcasecmp (name, "AssociatedCalendarItemId")) {
			priv->calendar_item_accept_id = g_new0 (EwsId, 1);
			priv->calendar_item_accept_id->id = e_soap_parameter_get_property (subparam, "Id");
			priv->calendar_item_accept_id->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
		} else if (!g_ascii_strcasecmp (name, "Start")) {
			priv->calendar_start = ews_item_parse_date (subparam);
		} else if (!g_ascii_strcasecmp (name, "End")) {
			priv->calendar_end = ews_item_parse_date (subparam);
		} else if (!g_ascii_strcasecmp (name, "StartTimeZone")) {
			priv->start_timezone = e_soap_parameter_get_property (subparam, "Id");
		} else if (!g_ascii_strcasecmp (name, "EndTimeZone")) {
			priv->end_timezone = e_soap_parameter_get_property (subparam, "Id");
		} else if (!g_ascii_strcasecmp (name, "Body")) {
			const gchar *body_type;

			priv->body = e_soap_parameter_get_string_value (subparam);

			body_type = e_soap_parameter_get_property (subparam, "BodyType");

			if (g_strcmp0 (body_type, "HTML") == 0)
				priv->body_type = E_EWS_BODY_TYPE_HTML;
			else if (g_strcmp0 (body_type, "Text") == 0)
				priv->body_type = E_EWS_BODY_TYPE_TEXT;
			else
				priv->body_type = E_EWS_BODY_TYPE_ANY;
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

EEwsItem *
e_ews_item_new_from_error (const GError *error)
{
	EEwsItem *item;

	g_return_val_if_fail (error != NULL, NULL);

	item = g_object_new (E_TYPE_EWS_ITEM, NULL);
	e_ews_item_set_error (item, error);

	return item;
}

EEwsItemType
e_ews_item_get_item_type (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), E_EWS_ITEM_TYPE_UNKNOWN);

	return item->priv->item_type;
}

void
e_ews_item_set_item_type (EEwsItem *item,
                          EEwsItemType new_type)
{
	g_return_if_fail (E_IS_EWS_ITEM (item));

	/* once the type is set to error type it stays as error type */
	if (item->priv->item_type != E_EWS_ITEM_TYPE_ERROR)
		item->priv->item_type = new_type;
}

const GError *
e_ews_item_get_error (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->error;
}

void
e_ews_item_set_error (EEwsItem *item,
                      const GError *error)
{
	GError *new_error;

	g_return_if_fail (E_IS_EWS_ITEM (item));

	if (error)
		new_error = g_error_copy (error);
	else
		new_error = NULL;

	g_clear_error (&item->priv->error);
	item->priv->error = new_error;

	if (item->priv->error)
		e_ews_item_set_item_type (item, E_EWS_ITEM_TYPE_ERROR);
}

const gchar *
e_ews_item_get_subject (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->subject;
}

void
e_ews_item_set_subject (EEwsItem *item,
                        const gchar *new_subject)
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
e_ews_item_set_mime_content (EEwsItem *item,
                             const gchar *new_mime_content)
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

const EwsId *
e_ews_item_get_attachment_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsId *) item->priv->attachment_id;
}

gsize
e_ews_item_get_size (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->size;
}

const gchar *
e_ews_item_get_msg_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->msg_id;
}

const gchar *
e_ews_item_get_preview (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->preview;
}

const gchar *
e_ews_item_get_uid (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const gchar *) item->priv->uid;
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

const gchar *
e_ews_item_get_date_header (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->date_header;
}

time_t
e_ews_item_get_date_received (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->date_received;
}

time_t
e_ews_item_get_date_sent (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->date_sent;
}

time_t
e_ews_item_get_date_created (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->date_created;
}

time_t
e_ews_item_get_last_modified_time (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->last_modified_time;
}

gboolean
e_ews_item_has_attachments (EEwsItem *item,
                            gboolean *has_attachments)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*has_attachments = item->priv->has_attachments;

	return TRUE;
}

gboolean
e_ews_item_is_read (EEwsItem *item,
                    gboolean *read)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*read = item->priv->is_read;

	return TRUE;
}

gboolean
e_ews_item_get_is_meeting (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	return item->priv->is_meeting;
}

gboolean
e_ews_item_get_is_response_requested (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	return item->priv->is_response_requested;
}

gboolean
e_ews_item_is_forwarded (EEwsItem *item,
                         gboolean *forwarded)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*forwarded = (item->priv->mapi_icon_index == 0x106);

	return TRUE;
}

gboolean
e_ews_item_is_answered (EEwsItem *item,
                        gboolean *answered)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	*answered = (item->priv->mapi_icon_index == 0x105);

	return TRUE;
}

guint32
e_ews_item_get_message_flags (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), 0);

	return item->priv->mapi_message_flags;
}

const GSList *
e_ews_item_get_to_recipients (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const GSList *)	item->priv->to_recipients;
}

const GSList *
e_ews_item_get_cc_recipients (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const GSList *)	item->priv->cc_recipients;
}

const GSList *
e_ews_item_get_bcc_recipients (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const GSList *)	item->priv->bcc_recipients;
}

const EwsMailbox *
e_ews_item_get_sender (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsMailbox *) item->priv->sender;
}

const EwsMailbox *
e_ews_item_get_from (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsMailbox *) item->priv->from;
}

const GSList *
e_ews_item_get_categories (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->categories;
}

EwsImportance
e_ews_item_get_importance (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), EWS_ITEM_LOW);

	return item->priv->importance;
}

const gchar *
e_ews_item_get_iana_start_time_zone (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->iana_start_time_zone;
}

const gchar *
e_ews_item_get_iana_end_time_zone (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->iana_end_time_zone;
}

const gchar *
e_ews_item_get_event_url (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->event_url;
}

EwsMailbox *
e_ews_item_mailbox_from_soap_param (ESoapParameter *param)
{
	EwsMailbox *mb;
	ESoapParameter *subparam;

	mb = g_new0 (EwsMailbox, 1);

	subparam = e_soap_parameter_get_first_child_by_name (param, "Name");
	if (subparam)
		mb->name = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "EmailAddress");
	if (subparam)
		mb->email = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "RoutingType");
	if (subparam)
		mb->routing_type = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "MailboxType");
	if (subparam)
		mb->mailbox_type = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (param, "ItemId");
	if (subparam) {
		EwsId *id = g_new0 (EwsId, 1);
		id->id = e_soap_parameter_get_property (subparam, "Id");
		id->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
		mb->item_id = id;
	}

	if (!mb->email && !mb->name) {
		e_ews_mailbox_free (mb);
		mb = NULL;
	}

	return mb;
}

void
e_ews_mailbox_free (EwsMailbox *mailbox)
{
	if (!mailbox)
		return;

	g_free (mailbox->name);
	g_free (mailbox->email);
	g_free (mailbox->routing_type);
	g_free (mailbox->mailbox_type);

	if (mailbox->item_id) {
		g_free (mailbox->item_id->id);
		g_free (mailbox->item_id->change_key);
		g_free (mailbox->item_id);
	}

	g_free (mailbox);
}

const GSList *
e_ews_item_get_modified_occurrences (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->modified_occurrences;
}

GSList *
e_ews_item_get_attachments_ids (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->attachments_ids;
}

const gchar *
e_ews_item_get_extended_tag (EEwsItem *item,
			     guint32 prop_tag)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	if (!item->priv->mapi_extended_tags)
		return NULL;

	return g_hash_table_lookup (item->priv->mapi_extended_tags, GUINT_TO_POINTER (prop_tag));
}

const gchar *
e_ews_item_get_extended_distinguished_tag (EEwsItem *item,
					   const gchar *set_id,
					   guint32 prop_id)
{
	GHashTable *set_tags;

	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	if (!item->priv->mapi_extended_sets)
		return NULL;

	set_tags = g_hash_table_lookup (item->priv->mapi_extended_sets, set_id);
	if (!set_tags)
		return NULL;

	return g_hash_table_lookup (set_tags, GUINT_TO_POINTER (prop_id));
}

gboolean
e_ews_item_get_extended_property_as_boolean (EEwsItem *item,
					     const gchar *set_id,
					     guint32 prop_id_or_tag,
					     gboolean *found)
{
	const gchar *value;

	value = e_ews_item_get_extended_property_as_string (item, set_id, prop_id_or_tag, found);
	if (!value)
		return FALSE;

	if (g_str_equal (value, "true"))
		return TRUE;

	if (g_str_equal (value, "false"))
		return FALSE;

	if (found)
		*found = FALSE;

	return FALSE;
}

gint
e_ews_item_get_extended_property_as_int (EEwsItem *item,
					 const gchar *set_id,
					 guint32 prop_id_or_tag,
					 gboolean *found)
{
	const gchar *value;

	value = e_ews_item_get_extended_property_as_string (item, set_id, prop_id_or_tag, found);
	if (!value)
		return 0;

	return strtol (value, NULL, 0);
}

gdouble
e_ews_item_get_extended_property_as_double (EEwsItem *item,
					    const gchar *set_id,
					    guint32 prop_id_or_tag,
					    gboolean *found)
{
	const gchar *value;

	value = e_ews_item_get_extended_property_as_string (item, set_id, prop_id_or_tag, found);
	if (!value)
		return 0.0;

	return g_ascii_strtod (value, NULL);
}

const gchar *
e_ews_item_get_extended_property_as_string (EEwsItem *item,
					    const gchar *set_id,
					    guint32 prop_id_or_tag,
					    gboolean *found)
{
	const gchar *value;

	if (set_id)
		value = e_ews_item_get_extended_distinguished_tag (item, set_id, prop_id_or_tag);
	else
		value = e_ews_item_get_extended_tag (item, prop_id_or_tag);

	if (found)
		*found = value != NULL;

	return value;
}

time_t
e_ews_item_get_extended_property_as_time (EEwsItem *item,
					  const gchar *set_id,
					  guint32 prop_id_or_tag,
					  gboolean *found)
{
	const gchar *value;
	GTimeVal tv;

	value = e_ews_item_get_extended_property_as_string (item, set_id, prop_id_or_tag, found);
	if (!value)
		return (time_t) 0;

	if (g_time_val_from_iso8601 (value, &tv))
		return tv.tv_sec;

	if (found)
		*found = FALSE;

	return (time_t) 0;
}

gchar *
e_ews_embed_attachment_id_in_uri (const gchar *olduri,
                                  const gchar *attach_id)
{
	gchar *tmpdir, *tmpfilename, *filename, *dirname, *name;

	tmpfilename = g_filename_from_uri (olduri, NULL, NULL);
	g_return_val_if_fail (tmpfilename != NULL, NULL);

	name = g_path_get_basename (tmpfilename);
	tmpdir = g_path_get_dirname (tmpfilename);

	dirname = g_build_filename (tmpdir, attach_id, NULL);
	if (g_mkdir (dirname, 0775) == -1) {
		g_warning ("Failed create directory to place file in [%s]: %s\n", dirname, g_strerror (errno));
	}

	filename = g_build_filename (dirname, name, NULL);
	if (g_rename (tmpfilename, filename) != 0) {
		g_warning ("Failed to move attachment cache file [%s -> %s]: %s\n", tmpfilename, filename, g_strerror (errno));
	}

	g_free (tmpfilename);
	g_free (tmpdir);
	g_free (dirname);
	g_free (name);

	tmpfilename = g_filename_to_uri (filename, NULL, NULL);

	g_free (filename);

	return tmpfilename;
}

EEwsAttachmentInfo *
e_ews_dump_file_attachment_from_soap_parameter (ESoapParameter *param,
                                                const gchar *cache,
                                                const gchar *comp_uid)
{
	ESoapParameter *subparam;
	const gchar *param_name, *tmpfilename;
	gchar *name = NULL, *value, *filename, *dirname;
	guchar *content = NULL;
	gsize data_len = 0;
	gchar *tmpdir;
	EEwsAttachmentInfo *info;

	g_return_val_if_fail (param != NULL, NULL);

	/* Parse element, look for filename and content */
	for (subparam = e_soap_parameter_get_first_child (param); subparam != NULL; subparam = e_soap_parameter_get_next_child (subparam)) {
		param_name = e_soap_parameter_get_name (subparam);

		if (g_ascii_strcasecmp (param_name, "Name") == 0) {
			g_free (name);
			name = e_soap_parameter_get_string_value (subparam);
		} else if (g_ascii_strcasecmp (param_name, "Content") == 0) {
			g_free (content);
			value = e_soap_parameter_get_string_value (subparam);
			content = g_base64_decode (value, &data_len);
			g_free (value);
		}
	}

	/* Make sure we have needed data */
	if (!content || !name) {
		g_free (name);
		g_free (content);
		return NULL;
	}

	if (cache && content && g_file_test ((const gchar *) content, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS)) {
		gchar *uri;

		info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_URI);

		tmpfilename = (gchar *) content;
		tmpdir = g_path_get_dirname (tmpfilename);

		dirname = g_build_filename (tmpdir, comp_uid, NULL);
		if (g_mkdir_with_parents (dirname, 0775) == -1) {
			g_warning ("Failed create directory to place file in [%s]: %s\n", dirname, g_strerror (errno));
		}

		filename = g_build_filename (dirname, name, NULL);
		if (g_rename (tmpfilename, filename) != 0) {
			g_warning ("Failed to move attachment cache file [%s -> %s]: %s\n",
					tmpfilename, filename, g_strerror (errno));
		}

		g_free (dirname);
		g_free (tmpdir);
		g_free (name);
		g_free (content);

		/* Return URI to saved file */
		uri = g_filename_to_uri (filename, NULL, NULL);
		e_ews_attachment_info_set_uri (info, uri);
		g_free (filename);
		g_free (uri);
	} else {
		info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_INLINED);
		e_ews_attachment_info_set_inlined_data (info, content, data_len);
		e_ews_attachment_info_set_prefer_filename (info, name);
	}
	return info;
}

EEwsAttachmentInfo *
e_ews_item_dump_mime_content (EEwsItem *item,
                              const gchar *cache)
{
	EEwsAttachmentInfo *info;
	gchar *filename, *surename, *dirname;
	gchar *tmpdir, *uri;
	const gchar *tmpfilename;

	g_return_val_if_fail (item->priv->mime_content != NULL, NULL);
	g_return_val_if_fail (g_file_test ((const gchar *) item->priv->mime_content, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS), NULL);

	tmpfilename = (gchar *) item->priv->mime_content;
	tmpdir = g_path_get_dirname (tmpfilename);

	dirname = g_build_filename (tmpdir, "XXXXXX", NULL);
	if (!g_mkdtemp (dirname)) {
		g_warning ("Failed to create directory for attachment cache '%s': %s", dirname, g_strerror (errno));

		g_free (tmpdir);
		g_free (dirname);

		return NULL;
	}

	surename = g_uri_escape_string (item->priv->subject, "", TRUE);
	filename = g_build_filename (dirname, surename, NULL);

	if (g_rename ((const gchar *) item->priv->mime_content, filename) != 0) {
		g_warning ("Failed to move attachment cache file '%s': %s", filename, g_strerror (errno));

		g_free (tmpdir);
		g_free (dirname);
		g_free (filename);
		g_free (surename);

		return NULL;
	}

	uri = g_filename_to_uri (filename, NULL, NULL);

	info = e_ews_attachment_info_new (E_EWS_ATTACHMENT_INFO_TYPE_URI);
	e_ews_attachment_info_set_uri (info, uri);

	g_free (uri);
	g_free (filename);
	g_free (dirname);
	g_free (tmpdir);
	g_free (surename);

	/* Return URI to saved file */
	return info;
}

const gchar *
e_ews_item_get_my_response_type (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->my_response_type;
}

const GSList *
e_ews_item_get_attendees (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->attendees;
}

const EwsId *
e_ews_item_get_calendar_item_accept_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return (const EwsId *) item->priv->calendar_item_accept_id;
}

gboolean
e_ews_item_get_reminder_is_set (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);

	return item->priv->reminder_is_set;
}

time_t
e_ews_item_get_reminder_due_by (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->reminder_due_by;
}

/* -1 when not set, but should really check also
   e_ews_item_get_reminder_is_set() before using the value */
gint
e_ews_item_get_reminder_minutes_before_start (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->reminder_minutes_before_start;
}

/* the out_recurrence is filled only if the function returns TRUE */
gboolean
e_ews_item_get_recurrence (EEwsItem *item,
			   EEwsRecurrence *out_recurrence)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);
	g_return_val_if_fail (out_recurrence != NULL, -1);

	if (item->priv->recurrence.type == E_EWS_RECURRENCE_UNKNOWN ||
	    item->priv->recurrence.end_type == E_EWS_RECURRENCE_END_UNKNOWN)
		return FALSE;

	*out_recurrence = item->priv->recurrence;

	return TRUE;
}

const gchar *
e_ews_item_get_fileas (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->fileas;
}

const EwsCompleteName *
e_ews_item_get_complete_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	if (!item->priv->contact_fields->complete_name && (
	    item->priv->contact_fields->surname ||
	    item->priv->contact_fields->middlename ||
	    item->priv->contact_fields->givenname)) {
		EwsCompleteName *cn;

		cn = g_new0 (EwsCompleteName, 1);

		cn->first_name = g_strdup (item->priv->contact_fields->givenname);
		cn->middle_name = g_strdup (item->priv->contact_fields->middlename);
		cn->last_name = g_strdup (item->priv->contact_fields->surname);

		item->priv->contact_fields->complete_name = cn;
	}

	return (const EwsCompleteName *) item->priv->contact_fields->complete_name;
}

const gchar *
e_ews_item_get_display_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return item->priv->contact_fields->display_name;
}

GHashTable *
e_ews_item_get_email_addresses (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return item->priv->contact_fields->email_addresses;
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
e_ews_item_get_email_address (EEwsItem *item,
                              const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
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
e_ews_item_get_physical_address (EEwsItem *item,
                                 const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
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
e_ews_item_get_phone_number (EEwsItem *item,
                             const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
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
e_ews_item_get_im_address (EEwsItem *item,
                           const gchar *field)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	if (item->priv->contact_fields->im_addresses)
		return g_hash_table_lookup (item->priv->contact_fields->im_addresses, field);

	return NULL;
}

const gchar *
e_ews_item_get_company_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->company_name;
}

const gchar *
e_ews_item_get_department (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->department;
}

const gchar *
e_ews_item_get_job_title (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->job_title;
}

const gchar *
e_ews_item_get_assistant_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->assistant_name;
}

const gchar *
e_ews_item_get_manager (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->manager;
}

const gchar *
e_ews_item_get_office_location (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->office_location;
}

const gchar *
e_ews_item_get_business_homepage (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->business_homepage;
}

const gchar *
e_ews_item_get_profession (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->profession;
}

const gchar *
e_ews_item_get_spouse_name (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->spouse_name;
}

const gchar *
e_ews_item_get_surname (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return (const gchar *) item->priv->contact_fields->surname;
}

const gchar *
e_ews_item_get_givenname (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return item->priv->contact_fields->givenname;
}

const gchar *
e_ews_item_get_middlename (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return item->priv->contact_fields->middlename;
}

const gchar *
e_ews_item_get_notes (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);

	return item->priv->contact_fields->notes;
}

const guchar *
e_ews_item_get_user_certificate (EEwsItem *item,
				 gsize *out_len)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);
	g_return_val_if_fail (out_len != NULL, NULL);

	*out_len = item->priv->contact_fields->user_cert_len;

	return item->priv->contact_fields->user_cert;
}

const guchar *
e_ews_item_get_msexchange_certificate (EEwsItem *item,
				       gsize *out_len)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->contact_fields != NULL, NULL);
	g_return_val_if_fail (out_len != NULL, NULL);

	*out_len = item->priv->contact_fields->msexchange_cert_len;

	return item->priv->contact_fields->msexchange_cert;
}

time_t
e_ews_item_get_birthday (EEwsItem *item,
			 gboolean *out_exists)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);
	g_return_val_if_fail (item->priv->contact_fields != NULL, -1);

	if (out_exists)
		*out_exists = item->priv->contact_fields->has_birthday;

	return item->priv->contact_fields->birthday;
}

time_t
e_ews_item_get_wedding_anniversary (EEwsItem *item,
				    gboolean *out_exists)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);
	g_return_val_if_fail (item->priv->contact_fields != NULL, -1);

	if (out_exists)
		*out_exists = item->priv->contact_fields->has_wedding_anniversary;

	return item->priv->contact_fields->wedding_anniversary;
}

const gchar *
e_ews_item_get_status (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->status;
}

const gchar *
e_ews_item_get_percent_complete (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->percent_complete;
}

time_t
e_ews_item_get_due_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);
	g_return_val_if_fail (item->priv->task_fields != NULL, -1);

	return item->priv->task_fields->due_date;
}

time_t
e_ews_item_get_start_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);
	g_return_val_if_fail (item->priv->task_fields != NULL, -1);

	return item->priv->task_fields->start_date;
}

time_t
e_ews_item_get_complete_date (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);
	g_return_val_if_fail (item->priv->task_fields != NULL, -1);

	return item->priv->task_fields->complete_date;
}

const gchar *
e_ews_item_get_sensitivity (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->sensitivity;
}

const gchar *
e_ews_item_get_body (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	if (item->priv->body)
		return item->priv->body;

	return item->priv->task_fields ? item->priv->task_fields->body : NULL;
}

EEwsBodyType
e_ews_item_get_body_type (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), E_EWS_BODY_TYPE_ANY);

	if (item->priv->body)
		return item->priv->body_type;

	return E_EWS_BODY_TYPE_ANY;
}

const gchar *
e_ews_item_get_owner (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->owner;
}

const gchar *
e_ews_item_get_delegator (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);
	g_return_val_if_fail (item->priv->task_fields != NULL, NULL);

	return item->priv->task_fields->delegator;
}

gboolean
e_ews_item_task_has_start_date (EEwsItem *item,
                                gboolean *has_date)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);
	g_return_val_if_fail (item->priv->task_fields != NULL, FALSE);

	*has_date =  item->priv->task_fields->has_start_date;

	return TRUE;
}

gboolean
e_ews_item_task_has_due_date (EEwsItem *item,
                              gboolean *has_date)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);
	g_return_val_if_fail (item->priv->task_fields != NULL, FALSE);

	*has_date =  item->priv->task_fields->has_due_date;

	return TRUE;
}

gboolean
e_ews_item_task_has_complete_date (EEwsItem *item,
                                   gboolean *has_date)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), FALSE);
	g_return_val_if_fail (item->priv->task_fields != NULL, FALSE);

	*has_date =  item->priv->task_fields->has_complete_date;

	return TRUE;
}

const gchar *
e_ews_item_get_tzid (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	/* can be NULL */
	return item->priv->timezone;
}

const gchar *
e_ews_item_get_start_tzid (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	/* can be NULL */
	return item->priv->start_timezone;
}

const gchar *
e_ews_item_get_end_tzid (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	/* can be NULL */
	return item->priv->end_timezone;
}

time_t
e_ews_item_get_start (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->calendar_start;
}

time_t
e_ews_item_get_end (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), -1);

	return item->priv->calendar_end;
}

const gchar *
e_ews_item_get_contact_photo_id (EEwsItem *item)
{
	g_return_val_if_fail (E_IS_EWS_ITEM (item), NULL);

	return item->priv->contact_photo_id;
}

/* free returned pointer with e_ews_permission_free() */
EEwsPermission *
e_ews_permission_new (EEwsPermissionUserType user_type,
                      const gchar *display_name,
                      const gchar *primary_smtp,
                      const gchar *sid,
                      guint32 rights)
{
	EEwsPermission *perm;

	perm = g_new0 (EEwsPermission, 1);
	perm->user_type = user_type;
	perm->display_name = g_strdup (display_name);
	perm->primary_smtp = g_strdup (primary_smtp);
	perm->sid = g_strdup (sid);
	perm->rights = rights;

	return perm;
}

void
e_ews_permission_free (EEwsPermission *perm)
{
	if (!perm)
		return;

	g_free (perm->display_name);
	g_free (perm->primary_smtp);
	g_free (perm->sid);
	g_free (perm);
}

static void
ews_level_rights_converter (const gchar **plevel_name,
                            guint32 *prights,
                            gboolean level_to_rights)
{
	struct _known {
		const gchar *level_name;
		guint32 rights;
	} known[] = {
		{ "None", 0 },
		{ "Owner",	E_EWS_PERMISSION_BIT_READ_ANY |
			E_EWS_PERMISSION_BIT_CREATE |
			E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER |
			E_EWS_PERMISSION_BIT_EDIT_OWNED |
			E_EWS_PERMISSION_BIT_EDIT_ANY |
			E_EWS_PERMISSION_BIT_DELETE_OWNED |
			E_EWS_PERMISSION_BIT_DELETE_ANY |
			E_EWS_PERMISSION_BIT_FOLDER_OWNER |
			E_EWS_PERMISSION_BIT_FOLDER_CONTACT |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "PublishingEditor",
			E_EWS_PERMISSION_BIT_READ_ANY |
			E_EWS_PERMISSION_BIT_CREATE |
			E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER |
			E_EWS_PERMISSION_BIT_EDIT_OWNED |
			E_EWS_PERMISSION_BIT_EDIT_ANY |
			E_EWS_PERMISSION_BIT_DELETE_OWNED |
			E_EWS_PERMISSION_BIT_DELETE_ANY |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "Editor",
			E_EWS_PERMISSION_BIT_READ_ANY |
			E_EWS_PERMISSION_BIT_CREATE |
			E_EWS_PERMISSION_BIT_EDIT_OWNED |
			E_EWS_PERMISSION_BIT_EDIT_ANY |
			E_EWS_PERMISSION_BIT_DELETE_OWNED |
			E_EWS_PERMISSION_BIT_DELETE_ANY |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "PublishingAuthor",
			E_EWS_PERMISSION_BIT_READ_ANY |
			E_EWS_PERMISSION_BIT_CREATE |
			E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER |
			E_EWS_PERMISSION_BIT_EDIT_OWNED |
			E_EWS_PERMISSION_BIT_DELETE_OWNED |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "Author",
			E_EWS_PERMISSION_BIT_READ_ANY |
			E_EWS_PERMISSION_BIT_CREATE |
			E_EWS_PERMISSION_BIT_EDIT_OWNED |
			E_EWS_PERMISSION_BIT_DELETE_OWNED |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "NoneditingAuthor",
			E_EWS_PERMISSION_BIT_READ_ANY |
			E_EWS_PERMISSION_BIT_CREATE |
			E_EWS_PERMISSION_BIT_DELETE_OWNED |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "Reviewer",
			E_EWS_PERMISSION_BIT_READ_ANY |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "Contributor",
			E_EWS_PERMISSION_BIT_CREATE |
			E_EWS_PERMISSION_BIT_FOLDER_VISIBLE },
		{ "FreeBusyTimeOnly",
			E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE },
		{ "FreeBusyTimeAndSubjectAndLocation",
			E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED }

	};
	gint ii;
	guint32 rights;

	g_return_if_fail (plevel_name != NULL);
	g_return_if_fail (prights != NULL);

	rights = (*prights) & ~(E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE | E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED);

	for (ii = 0; ii < G_N_ELEMENTS (known); ii++) {
		if (level_to_rights) {
			if (g_strcmp0 (*plevel_name, known[ii].level_name) == 0) {
				*prights = known[ii].rights;
				return;
			}
		} else {
			if (*prights == known[ii].rights || (rights && rights == known[ii].rights)) {
				*plevel_name = known[ii].level_name;
				return;
			}
		}
	}

	/* here lefts only "Custom" */
	if (level_to_rights)
		*prights = 0;
	else
		*plevel_name = "Custom";
}

/* converts user rights to level name suitable for PermissionLevel/CalendarPermissionLevel */
const gchar *
e_ews_permission_rights_to_level_name (guint32 rights)
{
	const gchar *level_name = NULL;

	ews_level_rights_converter (&level_name, &rights, FALSE);

	return level_name;
}

/* converts PermissionLevel/CalendarPermissionLevel name to user rights */
guint32
e_ews_permission_level_name_to_rights (const gchar *level_name)
{
	guint32 rights = 0;

	ews_level_rights_converter (&level_name, &rights, TRUE);

	return rights;
}

static EEwsPermission *
ews_permissions_parse (ESoapParameter *param)
{
	EEwsPermission *res;
	ESoapParameter *node, *subnode;
	EEwsPermissionUserType user_type;
	gchar *value, *display_name = NULL, *primary_smtp = NULL, *sid = NULL;
	guint32 rights = 0;

	g_return_val_if_fail (param != NULL, NULL);

	node = e_soap_parameter_get_first_child_by_name (param, "UserId");
	if (!node)
		return NULL;

	subnode = e_soap_parameter_get_first_child_by_name (node, "DistinguishedUser");
	if (subnode) {
		value = e_soap_parameter_get_string_value (subnode);
		if (g_strcmp0 (value, "Anonymous") == 0) {
			user_type = E_EWS_PERMISSION_USER_TYPE_ANONYMOUS;
		} else if (g_strcmp0 (value, "Default") == 0) {
			user_type = E_EWS_PERMISSION_USER_TYPE_DEFAULT;
		} else {
			g_free (value);
			return NULL;
		}

		g_free (value);
	} else {
		user_type = E_EWS_PERMISSION_USER_TYPE_REGULAR;
	}

	subnode = e_soap_parameter_get_first_child_by_name (node, "SID");
	if (subnode)
		sid = e_soap_parameter_get_string_value (subnode);

	subnode = e_soap_parameter_get_first_child_by_name (node, "PrimarySmtpAddress");
	if (subnode)
		primary_smtp = e_soap_parameter_get_string_value (subnode);

	subnode = e_soap_parameter_get_first_child_by_name (node, "DisplayName");
	if (subnode)
		display_name = e_soap_parameter_get_string_value (subnode);

	node = e_soap_parameter_get_first_child_by_name (param, "PermissionLevel");
	if (!node)
		node = e_soap_parameter_get_first_child_by_name (param, "CalendarPermissionLevel");

	if (node) {
		value = e_soap_parameter_get_string_value (node);
		rights = e_ews_permission_level_name_to_rights (value);
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "CanCreateItems");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "true") == 0)
			rights |= E_EWS_PERMISSION_BIT_CREATE;
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "CanCreateSubFolders");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "true") == 0)
			rights |= E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER;
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "IsFolderOwner");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "true") == 0)
			rights |= E_EWS_PERMISSION_BIT_FOLDER_OWNER;
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "IsFolderVisible");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "true") == 0)
			rights |= E_EWS_PERMISSION_BIT_FOLDER_VISIBLE;
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "IsFolderContact");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "true") == 0)
			rights |= E_EWS_PERMISSION_BIT_FOLDER_CONTACT;
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "EditItems");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "None") == 0)
			rights |= 0;
		else if (g_strcmp0 (value, "Owned") == 0)
			rights |= E_EWS_PERMISSION_BIT_EDIT_OWNED;
		else if (g_strcmp0 (value, "All") == 0)
			rights |= E_EWS_PERMISSION_BIT_EDIT_ANY;
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "DeleteItems");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "None") == 0)
			rights |= 0;
		else if (g_strcmp0 (value, "Owned") == 0)
			rights |= E_EWS_PERMISSION_BIT_DELETE_OWNED;
		else if (g_strcmp0 (value, "All") == 0)
			rights |= E_EWS_PERMISSION_BIT_DELETE_ANY;
		g_free (value);
	}

	node = e_soap_parameter_get_first_child_by_name (param, "ReadItems");
	if (node) {
		value = e_soap_parameter_get_string_value (node);
		if (g_strcmp0 (value, "None") == 0)
			rights |= 0;
		else if (g_strcmp0 (value, "TimeOnly") == 0)
			rights |= E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE;
		else if (g_strcmp0 (value, "TimeAndSubjectAndLocation") == 0)
			rights |= E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED;
		else if (g_strcmp0 (value, "FullDetails") == 0)
			rights |= E_EWS_PERMISSION_BIT_READ_ANY;
		g_free (value);
	}

	res = e_ews_permission_new (user_type, display_name, primary_smtp, sid, rights);

	g_free (display_name);
	g_free (primary_smtp);
	g_free (sid);

	return res;
}

/* Returns GSList of EEwsPermission * objects, as read from @param.
 * Returned GSList should be freed with e_ews_permissions_free()
 * when done with it. Returns NULL when no permissions recognized
 * from @param.
*/
GSList *
e_ews_permissions_from_soap_param (ESoapParameter *param)
{
	GSList *perms = NULL;
	ESoapParameter *node;
	const gchar *name;

	g_return_val_if_fail (param != NULL, NULL);

	name = e_soap_parameter_get_name (param);
	if (g_ascii_strcasecmp (name, "Permissions") == 0 ||
	    g_ascii_strcasecmp (name, "CalendarPermissions") == 0) {
		node = param;
	} else {
		node = e_soap_parameter_get_first_child_by_name (param, "Permissions");
		if (!node)
			node = e_soap_parameter_get_first_child_by_name (param, "CalendarPermissions");
		if (!node)
			return NULL;
	}

	for (node = e_soap_parameter_get_first_child (node);
	     node;
	     node = e_soap_parameter_get_next_child (node)) {
		name = e_soap_parameter_get_name (node);
		if (g_ascii_strcasecmp (name, "Permission") == 0 ||
		    g_ascii_strcasecmp (name, "CalendarPermission") == 0) {
			EEwsPermission *perm;

			perm = ews_permissions_parse (node);
			if (perm) {
				perms = g_slist_prepend (perms, perm);
			}
		}
	}

	return perms ? g_slist_reverse (perms) : NULL;
}

void
e_ews_permissions_free (GSList *permissions)
{
	g_slist_free_full (permissions, (GDestroyNotify) e_ews_permission_free);
}

/* strips ex_address by its LDAP-like part, returning position in ex_address where
   common name begins; returns whole ex_address, if not found */
const gchar *
e_ews_item_util_strip_ex_address (const gchar *ex_address)
{
	const gchar *ptr;

	if (!ex_address)
		return ex_address;

	ptr = strrchr (ex_address, '/');
	if (ptr && g_ascii_strncasecmp (ptr, "/cn=", 4) == 0) {
		return ptr + 4;
	}

	return ex_address;
}

EwsId *
e_ews_id_copy (const EwsId *ews_id)
{
	EwsId *copy;

	if (!ews_id)
		return NULL;

	copy = g_new0 (EwsId, 1);
	copy->id = g_strdup (ews_id->id);
	copy->change_key = g_strdup (ews_id->change_key);

	return copy;
}

void
e_ews_id_free (EwsId *ews_id)
{
	if (ews_id) {
		g_free (ews_id->id);
		g_free (ews_id->change_key);
		g_free (ews_id);
	}
}
