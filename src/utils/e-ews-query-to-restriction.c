/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *    Punit Jain <jpunit@novell.com>
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
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <camel/camel.h>
#include <libedata-cal/libedata-cal.h>

#include "e-ews-query-to-restriction.h"

#include "server/e-ews-message.h"

#define d(x) x

#define WRITE_CONTAINS_MESSAGE(msg, mode, compare, uri, val) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "Contains", NULL, NULL); \
		e_soap_message_add_attribute (msg, "ContainmentMode", mode, NULL, NULL); \
		e_soap_message_add_attribute (msg, "ContainmentComparison", compare, NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", uri); \
		e_ews_message_write_string_parameter_with_attribute (msg, "Constant", NULL, NULL, "Value", val); \
		e_soap_message_end_element (msg); \
	} G_STMT_END

#define WRITE_CONTAINS_MESSAGE_INDEXED(msg, mode, compare, uri, index, val) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "Contains", NULL, NULL); \
		e_soap_message_add_attribute (msg, "ContainmentMode", mode, NULL, NULL); \
		e_soap_message_add_attribute (msg, "ContainmentComparison", compare, NULL, NULL); \
		e_soap_message_start_element (msg, "IndexedFieldURI", NULL, NULL); \
		e_soap_message_add_attribute (msg, "FieldURI", uri, NULL, NULL); \
		e_soap_message_add_attribute (msg, "FieldIndex", index, NULL, NULL); \
		e_soap_message_end_element (msg); \
		e_ews_message_write_string_parameter_with_attribute (msg, "Constant", NULL, NULL, "Value", val); \
		e_soap_message_end_element (msg); \
	} G_STMT_END

#define WRITE_EXISTS_MESSAGE(msg, uri) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "Exists", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", uri);\
		e_soap_message_end_element (msg); \
	} G_STMT_END

#define WRITE_GREATER_THAN_OR_EQUAL_TO_MESSAGE(msg, uri, val) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "IsGreaterThanOrEqualTo", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", uri); \
		e_soap_message_start_element (msg, "FieldURIOrConstant", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "Constant", NULL, NULL, "Value", val); \
		e_soap_message_end_element (msg); \
		e_soap_message_end_element (msg); \
	} G_STMT_END

#define WRITE_LESS_THAN_OR_EQUAL_TO_MESSAGE(msg, uri, val) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "IsLessThanOrEqualTo", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", uri); \
		e_soap_message_start_element (msg, "FieldURIOrConstant", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "Constant", NULL, NULL, "Value", val); \
		e_soap_message_end_element (msg); \
		e_soap_message_end_element (msg); \
	} G_STMT_END

#define WRITE_GREATER_THAN_MESSAGE(msg, uri, val) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "IsGreaterThan", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", uri); \
		e_soap_message_start_element (msg, "FieldURIOrConstant", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "Constant", NULL, NULL, "Value", val); \
		e_soap_message_end_element (msg); \
		e_soap_message_end_element (msg); \
	} G_STMT_END

#define WRITE_LESS_THAN_MESSAGE(msg, uri, val) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "IsLessThan", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", uri); \
		e_soap_message_start_element (msg, "FieldURIOrConstant", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "Constant", NULL, NULL, "Value", val); \
		e_soap_message_end_element (msg); \
		e_soap_message_end_element (msg); \
	} G_STMT_END

#define WRITE_IS_EQUAL_TO_MESSAGE(msg, uri, val) \
	G_STMT_START { \
		e_soap_message_start_element (msg, "IsEqualTo", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", uri); \
		e_soap_message_start_element (msg, "FieldURIOrConstant", NULL, NULL); \
		e_ews_message_write_string_parameter_with_attribute (msg, "Constant", NULL, NULL, "Value", val); \
		e_soap_message_end_element (msg); \
		e_soap_message_end_element (msg); \
	} G_STMT_END

typedef enum {
	MATCH_CONTAINS,
	MATCH_IS,
	MATCH_BEGINS_WITH,
	MATCH_ENDS_WITH,
	MATCH_AND,
	MATCH_OR,
	MATCH_NOT
} match_type;

typedef enum {
	CONTACT_NAME,
	CONTACT_NAME_OTHER,
	CONTACT_EMAIL,
	CONTACT_IM,
	CONTACT_ADDRESS,
	CONTACT_PHONE,
	CONTACT_OTHER
} contact_type;

typedef struct ContactField {
	gboolean indexed;
	contact_type flag;
	const gchar *field_uri;
} ContactField;

static ContactField contact_field[] = {
	{FALSE, CONTACT_NAME, "contacts:DisplayName"},
	{FALSE, CONTACT_NAME, "contacts:GivenName"},
	{FALSE, CONTACT_NAME, "contacts:Nickname"},
	{FALSE, CONTACT_NAME, "contacts:Surname"},
	{FALSE, CONTACT_NAME, "contacts:MiddleName"},
	{FALSE, CONTACT_NAME_OTHER, "contacts:AssistantName"},
	{FALSE, CONTACT_NAME_OTHER, "contacts:CompanyName"},
	{FALSE, CONTACT_NAME_OTHER, "contacts:Manager"},
	{FALSE, CONTACT_NAME_OTHER, "contacts:SpouseName"},
	{FALSE, CONTACT_OTHER, "contacts:BusinessHomePage"},
	{FALSE, CONTACT_OTHER, "contacts:JobTitle"},
	{FALSE, CONTACT_OTHER, "contacts:Department"},
	{FALSE, CONTACT_OTHER, "contacts:Profession"},

	{TRUE, CONTACT_IM, "contacts:ImAddress"},
	{TRUE, CONTACT_ADDRESS, "contacts:PhysicalAddress:Street"},
	{TRUE, CONTACT_ADDRESS, "contacts:PhysicalAddress:City"},
	{TRUE, CONTACT_ADDRESS, "contacts:PhysicalAddress:State"},
	{TRUE, CONTACT_ADDRESS, "contacts:PhysicalAddress:Country"},
	{TRUE, CONTACT_ADDRESS, "contacts:PhysicalAddress:PostalCode"},
	{TRUE, CONTACT_PHONE, "contacts:PhoneNumber"},
	{TRUE, CONTACT_EMAIL, "contacts:EmailAddress"}
};

typedef struct CalendarField {
	gboolean any_field;
	const gchar *field_uri;
} CalendarField;

static CalendarField calendar_field[] = {
	{FALSE, "calendar:Start"},
	{FALSE, "calendar:End"},
	{FALSE, "calendar:OriginalStart"},
	{FALSE, "calendar:IsAllDayEvent"},
	{FALSE, "calendar:LegacyFreeBusyStatus"},
	{TRUE, "calendar:Location"},
	{FALSE, "calendar:When"},
	{FALSE, "calendar:IsMeeting"},
	{FALSE, "calendar:IsCancelled"},
	{FALSE, "calendar:IsRecurring"},
	{FALSE, "calendar:MeetingRequestWasSent"},
	{FALSE, "calendar:IsResponseRequested"},
	{FALSE, "calendar:CalendarItemType"},
	{TRUE, "calendar:Organizer"},
	{TRUE, "calendar:RequiredAttendees"},
	{TRUE, "calendar:OptionalAttendees"},
	{TRUE, "calendar:Resources"},
	{FALSE, "calendar:Duration"},
	{FALSE, "calendar:TimeZone"},
	{FALSE, "calendar:AppointmentState"},
	{FALSE, "calendar:ConferenceType"},
	{FALSE, "calendar:IsOnlineMeeting"},
	{TRUE, "calendar:MeetingWorkspaceUrl"}
};

typedef struct ItemField {
	gboolean any_field;
	const gchar *field_uri;
} ItemField;

static ItemField item_field[] = {
	{TRUE, "item:Subject"},
	{TRUE, "item:Body"},
	{FALSE, "item:HasAttachments"},
	{TRUE, "item:Categories"},
	{FALSE, "item:Importance"},
	{FALSE, "item:Sensitivity"},
	{FALSE, "item:InternetMessageHeader"}
};

struct EmailIndex {
	const gchar *field_index;
} email_index[] = {
	{"EmailAddress1"},
	{"EmailAddress2"},
	{"EmailAddress3"}
};

static ESExpResult *
e_ews_implement_contact_contains (ESExp *f,
                                  gint argc,
                                  ESExpResult **argv,
                                  gpointer data,
                                  match_type type)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc > 1 && argv[0]->type == ESEXP_RES_STRING) {
		const gchar *field;
		field = argv[0]->value.string;

		if (argv[1]->type == ESEXP_RES_STRING && argv[1]->value.string != NULL) {
			gchar *mode = NULL;

			if (type == MATCH_CONTAINS || type == MATCH_ENDS_WITH)
				mode = g_strdup ("Substring");
			else if (type == MATCH_BEGINS_WITH)
				mode = g_strdup ("Prefixed");
			else if (type == MATCH_IS)
				mode = g_strdup ("FullString");
			else
				mode = g_strdup ("Substring");

			if (!strcmp (field, "full_name")) {
				gint n = 0;
				const gchar *value;
				value = argv[1]->value.string;

				e_soap_message_start_element (msg, "Or", NULL, NULL);
				while (n < G_N_ELEMENTS (contact_field)) {
					if ((contact_field[n].flag == CONTACT_NAME) && (!contact_field[n].indexed)) {
						WRITE_CONTAINS_MESSAGE (msg, mode, "IgnoreCase", contact_field[n].field_uri, value);
					}
					n++;
				}
				e_soap_message_end_element (msg);

			} else if (!strcmp (field, "x-evolution-any-field")) {
				gint n = 0;
				const gchar *value;
				value = argv[1]->value.string;

				e_soap_message_start_element (msg, "Or", NULL, NULL);
				while (n < G_N_ELEMENTS (contact_field)) {
					if (!contact_field[n].indexed) {
						WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", contact_field[n].field_uri, value);
					} else if (contact_field[n].flag == CONTACT_EMAIL && contact_field[n].indexed) {
						gint i = 0;
						while (i < G_N_ELEMENTS (email_index)) {
							WRITE_CONTAINS_MESSAGE_INDEXED (msg, "Substring", "IgnoreCase", "contacts:EmailAddress", email_index[i].field_index, value);
							i++;
						}
					}
					n++;
				}
				e_soap_message_end_element (msg);
			} else if (!strcmp (field, "email")) {
				const gchar *value;
				gint n = 0;
				value = argv[1]->value.string;

				e_soap_message_start_element (msg, "Or", NULL, NULL);
				while (n < G_N_ELEMENTS (email_index)) {
					WRITE_CONTAINS_MESSAGE_INDEXED (msg, mode, "IgnoreCase", "contacts:EmailAddress", email_index[n].field_index, value);
					n++;
				}
				e_soap_message_end_element (msg);
			} else if (!strcmp (field, "category_list")) {
				const gchar *value;
				value = argv[1]->value.string;

				WRITE_CONTAINS_MESSAGE (msg, mode, "IgnoreCase", "item:Categories", value);
			}

			g_free (mode);
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
e_ews_func_and_or_not (ESExp *f,
                       gint argc,
                       ESExpTerm **argv,
                       gpointer data,
                       match_type type)
{
	ESExpResult *r, *r1;
	ESoapMessage *msg;
	gint i;

	msg = (ESoapMessage *) data;

	/* "and" and "or" expects atleast two arguments */

	if (argc == 0)
		goto result;

	if (type == MATCH_AND) {
		if (argc >= 2)
			e_soap_message_start_element (msg, "And", NULL, NULL);

	} else if (type == MATCH_OR) {
		if (argc >= 2)
			e_soap_message_start_element (msg, "Or", NULL, NULL);

	} else if (type == MATCH_NOT)
		e_soap_message_start_element (msg, "Not", NULL, NULL);

	for (i = 0; i < argc; i++) {
		r1 = e_sexp_term_eval (f, argv[i]);
		e_sexp_result_free (f, r1);
	}

	if (argc >= 2 || type == MATCH_NOT)
		e_soap_message_end_element (msg);

result:
	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
calendar_func_contains (ESExp *f,
                        gint argc,
                        ESExpResult **argv,
                        gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc > 1 && argv[0]->type == ESEXP_RES_STRING) {
		const gchar *field;
		field = argv[0]->value.string;

		if (argv[1]->type == ESEXP_RES_STRING && argv[1]->value.string[0] != 0) {
			if (!g_strcmp0 (field, "summary")) {
				const gchar *value;
				value = argv[1]->value.string;

				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "item:Subject", value);
			} else if (!g_strcmp0 (field, "description")) {
				const gchar *value;
				value = argv[1]->value.string;

				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "item:Body", value);
			} else if (!g_strcmp0 (field, "location")) {
				const gchar *value;
				value = argv[1]->value.string;

				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "calendar:Location", value);
			} else if (!g_strcmp0 (field, "attendee")) {
				const gchar *value;
				value = argv[1]->value.string;

				e_soap_message_start_element (msg, "Or", NULL, NULL);
				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "calendar:RequiredAttendees", value);
				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "calendar:OptionalAttendees", value);
				e_soap_message_end_element (msg);
			} else if (!g_strcmp0 (field, "organizer")) {
				const gchar *value;
				value = argv[1]->value.string;

				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "calendar:Organizer", value);
			} else if (!g_strcmp0 (field, "classification")) {
				const gchar *value;
				value = argv[1]->value.string;

				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "item:Sensitivity", value);
			} else if (!g_strcmp0 (field, "priority")) {
				const gchar *value;
				value = argv[1]->value.string;

				WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "item:Importance", value);
			} else if (!g_strcmp0 (field, "any")) {
				const gchar *value;
				gint n = 0;
				value = argv[1]->value.string;

				e_soap_message_start_element (msg, "Or", NULL, NULL);
				while (n < G_N_ELEMENTS (calendar_field)) {
					if (calendar_field[n].any_field) {
						WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", calendar_field[n].field_uri, value);
					}
					n++;
				}
				n = 0;
				while (n < G_N_ELEMENTS (item_field)) {
					if (item_field[n].any_field) {
						WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", item_field[n].field_uri, value);
					}
					n++;
				}
				e_soap_message_end_element (msg);
			}
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
calendar_func_has_categories (ESExp *f,
                              gint argc,
                              ESExpResult **argv,
                              gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc == 1 && argv[0]->type == ESEXP_RES_STRING) {
		const gchar *value;
		value = argv[0]->value.string;

		WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "item:Categories", value);
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
calendar_func_has_attachment (ESExp *f,
                              gint argc,
                              ESExpResult **argv,
                              gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc == 0) {
		WRITE_EXISTS_MESSAGE (msg, "item:HasAttachments");
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
calendar_func_has_recurrence (ESExp *f,
                              gint argc,
                              ESExpResult **argv,
                              gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc == 0) {
		WRITE_EXISTS_MESSAGE (msg, "calendar:IsRecurring");
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static gchar *
e_ews_make_timestamp (time_t when)
{
	struct tm *tm;

	tm = gmtime (&when);
	return g_strdup_printf (
		"%04d-%02d-%02dT%02d:%02d:%02dZ",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static ESExpResult *
calendar_func_occur_in_time_range (ESExp *f,
                                   gint argc,
                                   ESExpResult **argv,
                                   gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;
	gchar *start, *end;

	msg = (ESoapMessage *) data;

	if (argv[0]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (
			f, "occur-in-time-range? expects argument 1 "
			"to be a time_t");
		return NULL;
	}

	if (argv[1]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (
			f, "occur-in-time-range? expects argument 2 "
			"to be a time_t");
		return NULL;
	}

	start = e_ews_make_timestamp (argv[0]->value.time);
	end = e_ews_make_timestamp (argv[1]->value.time);

	e_soap_message_start_element (msg, "And", NULL, NULL);
	WRITE_GREATER_THAN_OR_EQUAL_TO_MESSAGE (msg, "calendar:Start", start);
	WRITE_LESS_THAN_OR_EQUAL_TO_MESSAGE (msg, "calendar:End", end);
	e_soap_message_end_element (msg);

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	g_free (start);
	g_free (end);

	return r;
}

static ESExpResult *
calendar_func_occurrences_count (ESExp *f,
                                 gint argc,
                                 ESExpResult **argv,
                                 gpointer data)
{
	ESExpResult *r;

	/*ews doesn't support restriction based on number of occurrences*/
	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
message_func_body_contains (ESExp *f,
                            gint argc,
                            ESExpResult **argv,
                            gpointer data)

{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argv[0]->type == ESEXP_RES_STRING) {
		const gchar *value;
		value = argv[0]->value.string;

		WRITE_CONTAINS_MESSAGE (msg, "Substring", "IgnoreCase", "item:Body", value);
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
common_message_func_header_contains (ESExp *f,
                                     gint argc,
                                     ESExpResult **argv,
                                     gpointer data,
                                     match_type type)

{
	ESExpResult *r;
	ESoapMessage *msg;
	gchar *mode;

	msg = (ESoapMessage *) data;

	if (type == MATCH_CONTAINS || type == MATCH_ENDS_WITH)
		mode = g_strdup ("Substring");
	else if (type == MATCH_BEGINS_WITH)
		mode = g_strdup ("Prefixed");
	else if (type == MATCH_IS)
		mode = g_strdup ("FullString");
	else
		mode = g_strdup ("Substring");

	if (argv[0]->type == ESEXP_RES_STRING) {
		const gchar *headername;
		headername = argv[0]->value.string;

		if (argv[1]->type == ESEXP_RES_STRING) {
			const gchar *value;
			value = argv[1]->value.string;

			if (!g_ascii_strcasecmp (headername, "subject")) {
				WRITE_CONTAINS_MESSAGE (msg, mode, "IgnoreCase", "item:Subject", value);
			} else if (!g_ascii_strcasecmp (headername, "from")) {
				WRITE_CONTAINS_MESSAGE (msg, mode, "IgnoreCase", "message:From", value);
			} else if (!g_ascii_strcasecmp (headername, "to")) {
				WRITE_CONTAINS_MESSAGE (msg, mode, "IgnoreCase", "message:ToRecipients", value);
			} else if (!g_ascii_strcasecmp (headername, "cc")) {
				WRITE_CONTAINS_MESSAGE (msg, mode, "IgnoreCase", "message:CcRecipients", value);
			} else if (!g_ascii_strcasecmp (headername, "bcc")) {
				WRITE_CONTAINS_MESSAGE (msg, mode, "IgnoreCase", "message:BccRecipients", value);
			}
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	g_free (mode);

	return r;
}

static ESExpResult *
message_func_header_exists (ESExp *f,
                            gint argc,
                            ESExpResult **argv,
                            gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argv[0]->type == ESEXP_RES_STRING) {
		const gchar *headername;
		headername = argv[0]->value.string;

		if (!g_ascii_strcasecmp (headername, "subject")) {
			WRITE_EXISTS_MESSAGE (msg, "item:Subject");
		} else if (!g_ascii_strcasecmp (headername, "from")) {
			WRITE_EXISTS_MESSAGE (msg, "message:From");
		} else if (!g_ascii_strcasecmp (headername, "to")) {
			WRITE_EXISTS_MESSAGE (msg, "message:ToRecipients");
		} else if (!g_ascii_strcasecmp (headername, "cc")) {
			WRITE_EXISTS_MESSAGE (msg, "message:CcRecipients");
		} else if (!g_ascii_strcasecmp (headername, "bcc")) {
			WRITE_EXISTS_MESSAGE (msg, "message:BccRecipients");
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
message_func_system_flag (ESExp *f,
                          gint argc,
                          ESExpResult **argv,
                          gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argv[0]->type == ESEXP_RES_STRING) {
		const gchar *name;
		name = argv[0]->value.string;
		if (!g_ascii_strcasecmp (name, "Attachments")) {
			WRITE_EXISTS_MESSAGE (msg, "item:HasAttachments");
		} else if (!g_ascii_strcasecmp (name, "deleted") || !g_ascii_strcasecmp (name, "junk")) {
			r = e_sexp_result_new (f, ESEXP_RES_BOOL);
			r->value.boolean = FALSE;
			return r;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;

}

static ESExpResult *
message_func_sent_date (ESExp *f,
                        gint argc,
                        ESExpResult **argv,
                        gpointer data)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	r->value.string = g_strdup ("sent-date");

	return r;
}

static ESExpResult *
message_func_received_date (ESExp *f,
                            gint argc,
                            ESExpResult **argv,
                            gpointer data)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	r->value.string = g_strdup ("received-date");

	return r;
}

static ESExpResult *
message_func_current_date (ESExp *f,
                           gint argc,
                           ESExpResult **argv,
                           gpointer data)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_INT);
	r->value.time = time (NULL);
	return r;
}

static ESExpResult *
message_func_relative_months (ESExp *f,
                              gint argc,
                              ESExpResult **argv,
                              gpointer data)
{
	ESExpResult *r;

	if (argc != 1 || argv[0]->type != ESEXP_RES_INT) {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.boolean = FALSE;

	} else {
		r = e_sexp_result_new (f, ESEXP_RES_INT);
		r->value.number = camel_folder_search_util_add_months (time (NULL), argv[0]->value.number);
	}

	return r;
}

static ESExpResult *
message_func_get_size (ESExp *f,
                       gint argc,
                       ESExpResult **argv,
                       gpointer data)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	r->value.string = g_strdup ("message-size");

	return r;
}

static ESExpResult *
func_eq (ESExp *f,
         gint argc,
         ESExpResult **argv,
         gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc != 2) {
		e_sexp_fatal_error (f, "two arguments are required for this operation");
		return NULL;
	}

	if (argv[0]->type == ESEXP_RES_STRING) {
		const gchar *name;
		gchar *field_uri = NULL;

		name = argv[0]->value.string;

		if (!g_strcmp0 (name, "sent-date")) {
			field_uri = g_strdup ("item:DateTimeSent");
		} else if (!g_strcmp0 (name, "received-date")) {
			field_uri = g_strdup ("item:DateTimeReceived");
		}

		if (field_uri && argv[1]->type == ESEXP_RES_INT && argv[1]->value.number != 0) {
			time_t time;
			gchar *date;
			time = argv[1]->value.number;
			date = e_ews_make_timestamp (time);

			WRITE_IS_EQUAL_TO_MESSAGE (msg, field_uri, date);
			g_free (date);
		}
		g_free (field_uri);
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
func_gt (ESExp *f,
         gint argc,
         ESExpResult **argv,
         gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc != 2) {
		e_sexp_fatal_error (f, "two arguments are required for this operation");
		return NULL;
	}

	if (argv[0]->type == ESEXP_RES_STRING) {
		const gchar *name;
		gchar *field_uri = NULL;
		gboolean is_time = FALSE;

		name = argv[0]->value.string;

		if (!g_strcmp0 (name, "sent-date")) {
			field_uri = g_strdup ("item:DateTimeSent");
			is_time = TRUE;
		} else if (!g_strcmp0 (name, "received-date")) {
			field_uri = g_strdup ("item:DateTimeReceived");
			is_time = TRUE;
		} else if (!g_strcmp0 (name, "message-size")) {
			field_uri = g_strdup ("item:Size");
			is_time = FALSE;
		}

		if (field_uri && argv[1]->type == ESEXP_RES_INT && argv[1]->value.number != 0) {
			if (is_time) {
				time_t time;
				gchar *date;
				time = argv[1]->value.number;
				date = e_ews_make_timestamp (time);

				WRITE_GREATER_THAN_MESSAGE (msg, field_uri, date);
				g_free (date);
			} else {
				gint value;
				gchar val_str[16];

				value = argv[1]->value.number;
				value = value * (1024); //conver kB to Bytes.
				g_sprintf (val_str, "%d", value);

				WRITE_GREATER_THAN_MESSAGE (msg, field_uri, val_str);
			}
		}
		g_free (field_uri);
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
func_lt (ESExp *f,
         gint argc,
         ESExpResult **argv,
         gpointer data)
{
	ESExpResult *r;
	ESoapMessage *msg;

	msg = (ESoapMessage *) data;

	if (argc != 2) {
		e_sexp_fatal_error (f, "two arguments are required for this operation");
		return NULL;
	}

	if (argv[0]->type == ESEXP_RES_STRING) {
		const gchar *name;
		gchar *field_uri = NULL;
		gboolean is_time = FALSE;
		name = argv[0]->value.string;

		if (!g_strcmp0 (name, "sent-date")) {
			field_uri = g_strdup ("item:DateTimeSent");
			is_time = TRUE;
		} else if (!g_strcmp0 (name, "received-date")) {
			field_uri = g_strdup ("item:DateTimeReceived");
			is_time = TRUE;
		} else if (!g_strcmp0 (name, "message-size")) {
			field_uri = g_strdup ("item:Size");
			is_time = FALSE;
		}

		if (field_uri && argv[1]->type == ESEXP_RES_INT && argv[1]->value.number != 0) {
			if (is_time) {
				time_t time;
				gchar *date;
				time = argv[1]->value.number;
				date = e_ews_make_timestamp (time);

				WRITE_LESS_THAN_MESSAGE (msg, field_uri, date);
				g_free (date);
			} else {
				gint value;
				gchar val_str[16];

				value = argv[1]->value.number;
				value = value * (1024); //conver kB to Bytes.
				g_sprintf (val_str, "%d", value);

				WRITE_LESS_THAN_MESSAGE (msg, field_uri, val_str);
			}
		}
		g_free (field_uri);
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
message_func_match_all (ESExp *f,
                        gint argc,
                        ESExpResult **argv,
                        gpointer data)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);

	return r;
}

static ESExpResult *
message_func_header_contains (ESExp *f,
                              gint argc,
                              ESExpResult **argv,
                              gpointer data)
{
	return common_message_func_header_contains (f, argc, argv, data, MATCH_CONTAINS);
}

static ESExpResult *
message_func_header_matches (ESExp *f,
                             gint argc,
                             ESExpResult **argv,
                             gpointer data)
{
	return common_message_func_header_contains (f, argc, argv, data, MATCH_IS);
}

static ESExpResult *
message_func_header_starts_with (ESExp *f,
                                 gint argc,
                                 ESExpResult **argv,
                                 gpointer data)
{
	return common_message_func_header_contains (f, argc, argv, data, MATCH_BEGINS_WITH);
}

static ESExpResult *
message_func_header_ends_with (ESExp *f,
                               gint argc,
                               ESExpResult **argv,
                               gpointer data)
{
	return common_message_func_header_contains (f, argc, argv, data, MATCH_ENDS_WITH);
}

static ESExpResult *
contact_func_contains (ESExp *f,
                       gint argc,
                       ESExpResult **argv,
                       gpointer data)
{
	return e_ews_implement_contact_contains (f, argc, argv, data, MATCH_CONTAINS);
}

static ESExpResult *
contact_func_is (ESExp *f,
                 gint argc,
                 ESExpResult **argv,
                 gpointer data)
{
	return e_ews_implement_contact_contains (f, argc, argv, data, MATCH_IS);
}

static ESExpResult *
contact_func_beginswith (ESExp *f,
                         gint argc,
                         ESExpResult **argv,
                         gpointer data)
{
	return e_ews_implement_contact_contains (f, argc, argv, data, MATCH_BEGINS_WITH);
}

static ESExpResult *
contact_func_endswith (ESExp *f,
                       gint argc,
                       ESExpResult **argv,
                       gpointer data)
{
	return e_ews_implement_contact_contains (f, argc, argv, data, MATCH_ENDS_WITH);
}

static ESExpResult *
func_or (ESExp *f,
         gint argc,
         ESExpTerm **argv,
         gpointer data)
{
	return e_ews_func_and_or_not (f, argc, argv, data, MATCH_OR);
}

static ESExpResult *
func_and (ESExp *f,
          gint argc,
          ESExpTerm **argv,
          gpointer data)
{
	return e_ews_func_and_or_not (f, argc, argv, data, MATCH_AND);
}

static ESExpResult *
func_not (ESExp *f,
          gint argc,
          ESExpTerm **argv,
          gpointer data)
{
	return e_ews_func_and_or_not (f, argc, argv, data, MATCH_NOT);
}

static struct {
	const gchar *name;
	ESExpFunc *func;
	guint immediate :1;
} contact_symbols[] = {
	{ "and", (ESExpFunc *) func_and, 1},
	{ "or", (ESExpFunc *) func_or, 1},
	{ "not", (ESExpFunc *) func_not, 1},

	{ "contains", contact_func_contains, 0 },
	{ "is", contact_func_is, 0 },
	{ "beginswith", contact_func_beginswith, 0 },
	{ "endswith", contact_func_endswith, 0 },
};

static struct {
	const gchar *name;
	ESExpFunc *func;
	guint immediate :1;
} calendar_symbols[] = {
	{ "and", (ESExpFunc *) func_and, 1},
	{ "or", (ESExpFunc *) func_or, 1},
	{ "not", (ESExpFunc *) func_not, 1},

	/* Time-related functions */
	{ "make-time", e_cal_backend_sexp_func_make_time, 0 },

	/* Component-related functions */
	{ "contains?", calendar_func_contains, 0},
	{ "has-categories?", calendar_func_has_categories, 0 },
	{ "has-attachments?", calendar_func_has_attachment, 0 },
	{ "has-recurrences?", calendar_func_has_recurrence, 0 },
	{ "occur-in-time-range?", calendar_func_occur_in_time_range, 0 },
	{ "occurrences-count?", calendar_func_occurrences_count, 0 }
};

static struct {
	const gchar *name;
	ESExpFunc *func;
	guint immediate :1;
} message_symbols[] = {
	{ "and", (ESExpFunc *) func_and, 1},
	{ "or", (ESExpFunc *) func_or, 1},
	{ "not", (ESExpFunc *) func_not, 1},

	{"=",  (ESExpFunc *) func_eq, 0},
	{">",  (ESExpFunc *) func_gt, 0},
	{"<",  (ESExpFunc *) func_lt, 0},

	{ "match-all", message_func_match_all, 0 },
	{ "body-contains", message_func_body_contains, 0 },
	{ "header-contains", message_func_header_contains, 0 },
	{ "header-matches", message_func_header_matches, 0 },
	{ "header-starts-with", message_func_header_starts_with, 0 },
	{ "header-ends-with", message_func_header_ends_with, 0 },
	{ "header-exists", message_func_header_exists, 0 },
	{ "system-flag", message_func_system_flag, 0 },
	{ "get-sent-date", message_func_sent_date, 0 },
	{ "get-received-date", message_func_received_date, 0 },
	{ "get-current-date", message_func_current_date, 0 },
	{ "get-relative-months", message_func_relative_months, 0 },
	{ "get-size", message_func_get_size, 0 },
};

static void
e_ews_convert_sexp_to_restriction (ESoapMessage *msg,
                                   const gchar *query,
                                   EEwsFolderType type)
{
	ESExp *sexp;
	ESExpResult *r;
	gint i;

	sexp = e_sexp_new ();

	if (type == E_EWS_FOLDER_TYPE_CONTACTS) {
		for (i = 0; i < G_N_ELEMENTS (contact_symbols); i++) {
			if (contact_symbols[i].immediate)
				e_sexp_add_ifunction (
					sexp, 0, contact_symbols[i].name,
					(ESExpIFunc *) contact_symbols[i].func, msg);
			else
				e_sexp_add_function (
					sexp, 0, contact_symbols[i].name,
					contact_symbols[i].func, msg);
		}

	} else if (type == E_EWS_FOLDER_TYPE_CALENDAR || type == E_EWS_FOLDER_TYPE_TASKS || type == E_EWS_FOLDER_TYPE_MEMOS) {
		for (i = 0; i < G_N_ELEMENTS (calendar_symbols); i++) {
			if (calendar_symbols[i].immediate)
				e_sexp_add_ifunction (
					sexp, 0, calendar_symbols[i].name,
					(ESExpIFunc *) calendar_symbols[i].func, msg);
			else
				e_sexp_add_function (
					sexp, 0, calendar_symbols[i].name,
					calendar_symbols[i].func, msg);
		}
	} else if (type == E_EWS_FOLDER_TYPE_MAILBOX) {
		for (i = 0; i < G_N_ELEMENTS (message_symbols); i++) {
			if (message_symbols[i].immediate)
				e_sexp_add_ifunction (
					sexp, 0, message_symbols[i].name,
					(ESExpIFunc *) message_symbols[i].func, msg);
			else
				e_sexp_add_function (
					sexp, 0, message_symbols[i].name,
					message_symbols[i].func, msg);
		}

	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);
	if (!r)
		return;

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);
}

static gboolean
e_ews_check_is_query (const gchar *query,
                      EEwsFolderType type)
{

	if (!query)
		return FALSE;

	if (type == E_EWS_FOLDER_TYPE_CONTACTS) {
		if (!g_strcmp0 (query, "(contains \"x-evolution-any-field\" \"\")"))
			return FALSE;
		else
			return TRUE;

	} else if (type == E_EWS_FOLDER_TYPE_CALENDAR || type == E_EWS_FOLDER_TYPE_TASKS || type == E_EWS_FOLDER_TYPE_MEMOS) {
		if (!g_strcmp0 (query, "(contains? \"summary\"  \"\")"))
			return FALSE;
		else
			return TRUE;
	} else if (type == E_EWS_FOLDER_TYPE_MAILBOX) {
		return TRUE;
	} else
		return FALSE;
}

void
e_ews_query_to_restriction (ESoapMessage *msg,
                            const gchar *query,
                            EEwsFolderType type)
{
	gboolean is_query;

	is_query = e_ews_check_is_query (query, type);

	if (is_query) {
		e_soap_message_start_element (msg, "Restriction", "messages", NULL);
		e_ews_convert_sexp_to_restriction (msg, query, type);
		e_soap_message_end_element (msg);
	}
	return;
}
