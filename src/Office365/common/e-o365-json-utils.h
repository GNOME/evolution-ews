/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef E_O365_JSON_UTILS_H
#define E_O365_JSON_UTILS_H

#include <time.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

typedef enum _EO365InferenceClassificationType {
	E_O365_INFERENCE_CLASSIFICATION_NOT_SET,
	E_O365_INFERENCE_CLASSIFICATION_UNKNOWN,
	E_O365_INFERENCE_CLASSIFICATION_FOCUSED,
	E_O365_INFERENCE_CLASSIFICATION_OTHER
} EO365InferenceClassificationType;

typedef enum _EO365ImportanceType {
	E_O365_IMPORTANCE_NOT_SET,
	E_O365_IMPORTANCE_UNKNOWN,
	E_O365_IMPORTANCE_LOW,
	E_O365_IMPORTANCE_NORMAL,
	E_O365_IMPORTANCE_HIGH
} EO365ImportanceType;

typedef enum _EO365FollowupFlagStatusType {
	E_O365_FOLLOWUP_FLAG_STATUS_NOT_SET,
	E_O365_FOLLOWUP_FLAG_STATUS_UNKNOWN,
	E_O365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED,
	E_O365_FOLLOWUP_FLAG_STATUS_COMPLETE,
	E_O365_FOLLOWUP_FLAG_STATUS_FLAGGED
} EO365FollowupFlagStatusType;

typedef enum _EO365ItemBodyContentTypeType {
	E_O365_ITEM_BODY_CONTENT_TYPE_NOT_SET,
	E_O365_ITEM_BODY_CONTENT_TYPE_UNKNOWN,
	E_O365_ITEM_BODY_CONTENT_TYPE_TEXT,
	E_O365_ITEM_BODY_CONTENT_TYPE_HTML
} EO365ItemBodyContentTypeType;

/* Just for better readability */
#define EO365MailFolder JsonObject
#define EO365Recipient JsonObject
#define EO365DateTimeWithZone JsonObject
#define EO365FollowupFlag JsonObject
#define EO365InternetMessageHeader JsonObject
#define EO365ItemBody JsonObject
#define EO365MailMessage JsonObject

JsonArray *	e_o365_json_get_array_member		(JsonObject *object,
							 const gchar *member_name);
gboolean	e_o365_json_get_boolean_member		(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
gdouble		e_o365_json_get_double_member		(JsonObject *object,
							 const gchar *member_name,
							 gdouble default_value);
gint64		e_o365_json_get_int_member		(JsonObject *object,
							 const gchar *member_name,
							 gint64 default_value);
gboolean	e_o365_json_get_null_member		(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
JsonObject *	e_o365_json_get_object_member		(JsonObject *object,
							 const gchar *member_name);
const gchar *	e_o365_json_get_string_member		(JsonObject *object,
							 const gchar *member_name,
							 const gchar *default_value);

time_t		e_o365_get_date_time_offset_member	(JsonObject *object,
							 const gchar *member_name);

gboolean	e_o365_delta_is_removed_object		(JsonObject *object);

const gchar *	e_o365_mail_folder_get_display_name	(EO365MailFolder *folder);
const gchar *	e_o365_mail_folder_get_id		(EO365MailFolder *folder);
const gchar *	e_o365_mail_folder_get_parent_folder_id	(EO365MailFolder *folder);
gint32		e_o365_mail_folder_get_child_folder_count
							(EO365MailFolder *folder);
gint32		e_o365_mail_folder_get_total_item_count	(EO365MailFolder *folder);
gint32		e_o365_mail_folder_get_unread_item_count(EO365MailFolder *folder);

const gchar *	e_o365_recipient_get_address		(EO365Recipient *recipient);
const gchar *	e_o365_recipient_get_name		(EO365Recipient *recipient);

time_t		e_o365_date_time_get_date_time		(EO365DateTimeWithZone *datetime);
const gchar *	e_o365_date_time_get_time_zone		(EO365DateTimeWithZone *datetime);

const gchar *	e_o365_internet_message_header_get_name	(EO365InternetMessageHeader *header);
const gchar *	e_o365_internet_message_header_get_value(EO365InternetMessageHeader *header);

EO365DateTimeWithZone *
		e_o365_followup_flag_get_completed_date_time
							(EO365FollowupFlag *flag);
EO365DateTimeWithZone *
		e_o365_followup_flag_get_due_date_time	(EO365FollowupFlag *flag);
EO365FollowupFlagStatusType
		e_o365_followup_flag_get_flag_status	(EO365FollowupFlag *flag);
EO365DateTimeWithZone *
		e_o365_followup_flag_get_start_date_time(EO365FollowupFlag *flag);

const gchar *	e_o365_item_body_get_content		(EO365ItemBody *item_body);
EO365ItemBodyContentTypeType
		e_o365_item_body_get_content_type	(EO365ItemBody *item_body);

JsonArray *	e_o365_mail_message_get_bcc_recipients	(EO365MailMessage *mail); /* EO365Recipient * */
EO365ItemBody *	e_o365_mail_message_get_body		(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_body_preview	(EO365MailMessage *mail);
JsonArray *	e_o365_mail_message_get_categories	(EO365MailMessage *mail); /* const gchar * */
JsonArray *	e_o365_mail_message_get_cc_recipients	(EO365MailMessage *mail); /* EO365Recipient * */
const gchar *	e_o365_mail_message_get_change_key	(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_conversation_id	(EO365MailMessage *mail);
JsonObject *	e_o365_mail_message_get_conversation_index
							(EO365MailMessage *mail);
time_t		e_o365_mail_message_get_created_date_time
							(EO365MailMessage *mail);
EO365FollowupFlag *
		e_o365_mail_message_get_flag		(EO365MailMessage *mail);
EO365Recipient *
		e_o365_mail_message_get_from		(EO365MailMessage *mail);
gboolean	e_o365_mail_message_get_has_attachments	(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_id		(EO365MailMessage *mail);
EO365ImportanceType
		e_o365_mail_message_get_importance	(EO365MailMessage *mail);
EO365InferenceClassificationType
		e_o365_mail_message_get_inference_classification
							(EO365MailMessage *mail);
JsonArray *	e_o365_mail_message_get_internet_message_headers
							(EO365MailMessage *mail); /* EO365InternetMessageHeader * */
const gchar *	e_o365_mail_message_get_internet_message_id
							(EO365MailMessage *mail);
gboolean	e_o365_mail_message_get_is_delivery_receipt_requested
							(EO365MailMessage *mail);
gboolean	e_o365_mail_message_get_is_draft	(EO365MailMessage *mail);
gboolean	e_o365_mail_message_get_is_read		(EO365MailMessage *mail);
gboolean	e_o365_mail_message_get_is_read_receipt_requested
							(EO365MailMessage *mail);
time_t		e_o365_mail_message_get_last_modified_date_time
							(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_parent_folder_id(EO365MailMessage *mail);
time_t		e_o365_mail_message_get_received_date_time
							(EO365MailMessage *mail);
JsonArray *	e_o365_mail_message_get_reply_to	(EO365MailMessage *mail); /* EO365Recipient * */
EO365Recipient *e_o365_mail_message_get_sender		(EO365MailMessage *mail);
time_t		e_o365_mail_message_get_sent_date_time	(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_subject		(EO365MailMessage *mail);
JsonArray *	e_o365_mail_message_get_to_recipients	(EO365MailMessage *mail); /* EO365Recipient * */
EO365ItemBody *	e_o365_mail_message_get_unique_body	(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_web_link	(EO365MailMessage *mail);

G_END_DECLS

#endif /* E_O365_JSON_UTILS_H */
