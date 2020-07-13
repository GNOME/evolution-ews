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

/* Just for better readability */
#define EO365Attachment			JsonObject
#define EO365Category			JsonObject
#define EO365Contact			JsonObject
#define EO365DateTimeWithZone		JsonObject
#define EO365EmailAddress		JsonObject
#define EO365Folder			JsonObject
#define EO365FollowupFlag		JsonObject
#define EO365InternetMessageHeader	JsonObject
#define EO365ItemBody			JsonObject
#define EO365MailFolder			JsonObject
#define EO365MailMessage		JsonObject
#define EO365PhysicalAddress		JsonObject
#define EO365Recipient			JsonObject

typedef enum _EO365AttachmentDataType {
	E_O365_ATTACHMENT_DATA_TYPE_NOT_SET,
	E_O365_ATTACHMENT_DATA_TYPE_UNKNOWN,
	E_O365_ATTACHMENT_DATA_TYPE_FILE,
	E_O365_ATTACHMENT_DATA_TYPE_ITEM,
	E_O365_ATTACHMENT_DATA_TYPE_REFERENCE
} EO365AttachmentDataType;

typedef enum _EO365FollowupFlagStatusType {
	E_O365_FOLLOWUP_FLAG_STATUS_NOT_SET,
	E_O365_FOLLOWUP_FLAG_STATUS_UNKNOWN,
	E_O365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED,
	E_O365_FOLLOWUP_FLAG_STATUS_COMPLETE,
	E_O365_FOLLOWUP_FLAG_STATUS_FLAGGED
} EO365FollowupFlagStatusType;

typedef enum _EO365ImportanceType {
	E_O365_IMPORTANCE_NOT_SET,
	E_O365_IMPORTANCE_UNKNOWN,
	E_O365_IMPORTANCE_LOW,
	E_O365_IMPORTANCE_NORMAL,
	E_O365_IMPORTANCE_HIGH
} EO365ImportanceType;

typedef enum _EO365InferenceClassificationType {
	E_O365_INFERENCE_CLASSIFICATION_NOT_SET,
	E_O365_INFERENCE_CLASSIFICATION_UNKNOWN,
	E_O365_INFERENCE_CLASSIFICATION_FOCUSED,
	E_O365_INFERENCE_CLASSIFICATION_OTHER
} EO365InferenceClassificationType;

typedef enum _EO365ItemBodyContentTypeType {
	E_O365_ITEM_BODY_CONTENT_TYPE_NOT_SET,
	E_O365_ITEM_BODY_CONTENT_TYPE_UNKNOWN,
	E_O365_ITEM_BODY_CONTENT_TYPE_TEXT,
	E_O365_ITEM_BODY_CONTENT_TYPE_HTML
} EO365ItemBodyContentTypeType;

JsonArray *	e_o365_json_get_array_member		(JsonObject *object,
							 const gchar *member_name);
void		e_o365_json_begin_array_member		(JsonBuilder *builder,
							 const gchar *member_name);
void		e_o365_json_end_array_member		(JsonBuilder *builder);
gboolean	e_o365_json_get_boolean_member		(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
void		e_o365_json_add_boolean_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gboolean value);
gdouble		e_o365_json_get_double_member		(JsonObject *object,
							 const gchar *member_name,
							 gdouble default_value);
void		e_o365_json_add_double_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gdouble value);
gint64		e_o365_json_get_int_member		(JsonObject *object,
							 const gchar *member_name,
							 gint64 default_value);
void		e_o365_json_add_int_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gint64 value);
gboolean	e_o365_json_get_null_member		(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
void		e_o365_json_add_null_member		(JsonBuilder *builder,
							 const gchar *member_name);
JsonObject *	e_o365_json_get_object_member		(JsonObject *object,
							 const gchar *member_name);
void		e_o365_json_begin_object_member		(JsonBuilder *builder,
							 const gchar *member_name);
void		e_o365_json_end_object_member		(JsonBuilder *builder);
const gchar *	e_o365_json_get_string_member		(JsonObject *object,
							 const gchar *member_name,
							 const gchar *default_value);
void		e_o365_json_add_string_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
void		e_o365_json_add_nonempty_string_member	(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
void		e_o365_json_add_nonempty_or_null_string_member
							(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);

time_t		e_o365_get_date_time_offset_member	(JsonObject *object,
							 const gchar *member_name);
void		e_o365_add_date_time_offset_member	(JsonBuilder *builder,
							 const gchar *member_name,
							 time_t value);

gboolean	e_o365_delta_is_removed_object		(JsonObject *object);

const gchar *	e_o365_category_get_display_name	(EO365Category *category);
const gchar *	e_o365_category_get_id			(EO365Category *category);
const gchar *	e_o365_category_get_color		(EO365Category *category);

const gchar *	e_o365_folder_get_id			(EO365Folder *folder);
const gchar *	e_o365_folder_get_parent_folder_id	(EO365Folder *folder);
const gchar *	e_o365_folder_get_display_name		(EO365Folder *folder);

gint32		e_o365_mail_folder_get_child_folder_count
							(EO365MailFolder *folder);
gint32		e_o365_mail_folder_get_total_item_count	(EO365MailFolder *folder);
gint32		e_o365_mail_folder_get_unread_item_count(EO365MailFolder *folder);

const gchar *	e_o365_recipient_get_name		(EO365Recipient *recipient);
const gchar *	e_o365_recipient_get_address		(EO365Recipient *recipient);
void		e_o365_add_recipient			(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *name,
							 const gchar *address);

time_t		e_o365_date_time_get_date_time		(EO365DateTimeWithZone *datetime);
const gchar *	e_o365_date_time_get_time_zone		(EO365DateTimeWithZone *datetime);
void		e_o365_add_date_time			(JsonBuilder *builder,
							 const gchar *member_name,
							 time_t date_time,
							 const gchar *zone);

const gchar *	e_o365_internet_message_header_get_name	(EO365InternetMessageHeader *header);
const gchar *	e_o365_internet_message_header_get_value(EO365InternetMessageHeader *header);
void		e_o365_add_internet_message_header	(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *value);

EO365DateTimeWithZone *
		e_o365_followup_flag_get_completed_date_time
							(EO365FollowupFlag *flag);
void		e_o365_followup_flag_add_completed_date_time
							(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EO365DateTimeWithZone *
		e_o365_followup_flag_get_due_date_time	(EO365FollowupFlag *flag);
void		e_o365_followup_flag_add_due_date_time	(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EO365FollowupFlagStatusType
		e_o365_followup_flag_get_flag_status	(EO365FollowupFlag *flag);
void		e_o365_followup_flag_add_flag_status	(JsonBuilder *builder,
							 EO365FollowupFlagStatusType status);
EO365DateTimeWithZone *
		e_o365_followup_flag_get_start_date_time(EO365FollowupFlag *flag);
void		e_o365_followup_flag_add_start_date_time(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);

const gchar *	e_o365_item_body_get_content		(EO365ItemBody *item_body);
EO365ItemBodyContentTypeType
		e_o365_item_body_get_content_type	(EO365ItemBody *item_body);
void		e_o365_add_item_body			(JsonBuilder *builder,
							 const gchar *member_name,
							 EO365ItemBodyContentTypeType content_type,
							 const gchar *content);

JsonArray *	e_o365_mail_message_get_bcc_recipients	(EO365MailMessage *mail); /* EO365Recipient * */
void		e_o365_mail_message_begin_bcc_recipients(JsonBuilder *builder);
void		e_o365_mail_message_end_bcc_recipients	(JsonBuilder *builder);
EO365ItemBody *	e_o365_mail_message_get_body		(EO365MailMessage *mail);
void		e_o365_mail_message_add_body		(JsonBuilder *builder,
							 EO365ItemBodyContentTypeType content_type,
							 const gchar *content);
const gchar *	e_o365_mail_message_get_body_preview	(EO365MailMessage *mail);
JsonArray *	e_o365_mail_message_get_categories	(EO365MailMessage *mail); /* const gchar * */
void		e_o365_mail_message_begin_categories	(JsonBuilder *builder);
void		e_o365_mail_message_end_categories	(JsonBuilder *builder);
void		e_o365_mail_message_add_category	(JsonBuilder *builder,
							 const gchar *category);
JsonArray *	e_o365_mail_message_get_cc_recipients	(EO365MailMessage *mail); /* EO365Recipient * */
void		e_o365_mail_message_begin_cc_recipients	(JsonBuilder *builder);
void		e_o365_mail_message_end_cc_recipients	(JsonBuilder *builder);
const gchar *	e_o365_mail_message_get_change_key	(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_conversation_id	(EO365MailMessage *mail);
JsonObject *	e_o365_mail_message_get_conversation_index
							(EO365MailMessage *mail);
time_t		e_o365_mail_message_get_created_date_time
							(EO365MailMessage *mail);
EO365FollowupFlag *
		e_o365_mail_message_get_flag		(EO365MailMessage *mail);
void		e_o365_mail_message_begin_flag		(JsonBuilder *builder);
void		e_o365_mail_message_end_flag		(JsonBuilder *builder);
EO365Recipient *
		e_o365_mail_message_get_from		(EO365MailMessage *mail);
void		e_o365_mail_message_add_from		(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
gboolean	e_o365_mail_message_get_has_attachments	(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_id		(EO365MailMessage *mail);
EO365ImportanceType
		e_o365_mail_message_get_importance	(EO365MailMessage *mail);
void		e_o365_mail_message_add_importance	(JsonBuilder *builder,
							 EO365ImportanceType importance);
EO365InferenceClassificationType
		e_o365_mail_message_get_inference_classification
							(EO365MailMessage *mail);
JsonArray *	e_o365_mail_message_get_internet_message_headers
							(EO365MailMessage *mail); /* EO365InternetMessageHeader * */
void		e_o365_mail_message_begin_internet_message_headers
							(JsonBuilder *builder);
void		e_o365_mail_message_end_internet_message_headers
							(JsonBuilder *builder);
const gchar *	e_o365_mail_message_get_internet_message_id
							(EO365MailMessage *mail);
void		e_o365_mail_message_add_internet_message_id
							(JsonBuilder *builder,
							 const gchar *message_id);
gboolean	e_o365_mail_message_get_is_delivery_receipt_requested
							(EO365MailMessage *mail);
void		e_o365_mail_message_add_is_delivery_receipt_requested
							(JsonBuilder *builder,
							 gboolean value);
gboolean	e_o365_mail_message_get_is_draft	(EO365MailMessage *mail);
gboolean	e_o365_mail_message_get_is_read		(EO365MailMessage *mail);
void		e_o365_mail_message_add_is_read		(JsonBuilder *builder,
							 gboolean value);
gboolean	e_o365_mail_message_get_is_read_receipt_requested
							(EO365MailMessage *mail);
void		e_o365_mail_message_add_is_read_receipt_requested
							(JsonBuilder *builder,
							 gboolean value);
time_t		e_o365_mail_message_get_last_modified_date_time
							(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_parent_folder_id(EO365MailMessage *mail);
time_t		e_o365_mail_message_get_received_date_time
							(EO365MailMessage *mail);
void		e_o365_mail_message_add_received_date_time
							(JsonBuilder *builder,
							 time_t value);
JsonArray *	e_o365_mail_message_get_reply_to	(EO365MailMessage *mail); /* EO365Recipient * */
void		e_o365_mail_message_begin_reply_to	(JsonBuilder *builder);
void		e_o365_mail_message_end_reply_to	(JsonBuilder *builder);
EO365Recipient *e_o365_mail_message_get_sender		(EO365MailMessage *mail);
void		e_o365_mail_message_add_sender		(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
time_t		e_o365_mail_message_get_sent_date_time	(EO365MailMessage *mail);
void		e_o365_mail_message_add_sent_date_time	(JsonBuilder *builder,
							 time_t value);
const gchar *	e_o365_mail_message_get_subject		(EO365MailMessage *mail);
void		e_o365_mail_message_add_subject		(JsonBuilder *builder,
							 const gchar *subject);
JsonArray *	e_o365_mail_message_get_to_recipients	(EO365MailMessage *mail); /* EO365Recipient * */
void		e_o365_mail_message_begin_to_recipients	(JsonBuilder *builder);
void		e_o365_mail_message_end_to_recipients	(JsonBuilder *builder);
EO365ItemBody *	e_o365_mail_message_get_unique_body	(EO365MailMessage *mail);
const gchar *	e_o365_mail_message_get_web_link	(EO365MailMessage *mail);

EO365AttachmentDataType
		e_o365_attachment_get_data_type		(EO365Attachment *attachment);
void		e_o365_attachment_begin_attachment	(JsonBuilder *builder,
							 EO365AttachmentDataType data_type);
void		e_o365_attachment_end_attachment	(JsonBuilder *builder);
const gchar *	e_o365_attachment_get_content_type	(EO365Attachment *attachment);
void		e_o365_attachment_add_content_type	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_attachment_get_id		(EO365Attachment *attachment);
gboolean	e_o365_attachment_get_is_inline		(EO365Attachment *attachment);
void		e_o365_attachment_add_is_inline		(JsonBuilder *builder,
							 gboolean value);
time_t		e_o365_attachment_get_last_modified_date_time
							(EO365Attachment *attachment);
void		e_o365_attachment_add_last_modified_date_time
							(JsonBuilder *builder,
							 time_t value);
const gchar *	e_o365_attachment_get_name		(EO365Attachment *attachment);
void		e_o365_attachment_add_name		(JsonBuilder *builder,
							 const gchar *value);
gint32		e_o365_attachment_get_size		(EO365Attachment *attachment);
void		e_o365_attachment_add_size		(JsonBuilder *builder,
							 gint32 value);
const gchar *	e_o365_file_attachment_get_content_bytes(EO365Attachment *attachment); /* base64-encoded */
void		e_o365_file_attachment_add_content_bytes(JsonBuilder *builder,
							 const gchar *base64_value);
const gchar *	e_o365_file_attachment_get_content_id	(EO365Attachment *attachment);
void		e_o365_file_attachment_add_content_id	(JsonBuilder *builder,
							 const gchar *value);

const gchar *	e_o365_email_address_get_name		(EO365EmailAddress *email);
const gchar *	e_o365_email_address_get_address	(EO365EmailAddress *email);
void		e_o365_add_email_address		(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
const gchar *	e_o365_physical_address_get_city	(EO365PhysicalAddress *address);
const gchar *	e_o365_physical_address_get_country_or_region
							(EO365PhysicalAddress *address);
const gchar *	e_o365_physical_address_get_postal_code	(EO365PhysicalAddress *address);
const gchar *	e_o365_physical_address_get_state	(EO365PhysicalAddress *address);
const gchar *	e_o365_physical_address_get_street	(EO365PhysicalAddress *address);
void		e_o365_add_physical_address		(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);

const gchar *	e_o365_contact_get_id			(EO365Contact *contact);
const gchar *	e_o365_contact_get_parent_folder_id	(EO365Contact *contact);
const gchar *	e_o365_contact_get_change_key		(EO365Contact *contact);
time_t		e_o365_contact_get_created_date_time	(EO365Contact *contact);
time_t		e_o365_contact_get_last_modified_date_time
							(EO365Contact *contact);
const gchar *	e_o365_contact_get_assistant_name	(EO365Contact *contact);
void		e_o365_contact_add_assistant_name	(JsonBuilder *builder,
							 const gchar *value);
time_t		e_o365_contact_get_birthday		(EO365Contact *contact);
void		e_o365_contact_add_birthday		(JsonBuilder *builder,
							 time_t value);
EO365PhysicalAddress *
		e_o365_contact_get_business_address	(EO365Contact *contact);
void		e_o365_contact_add_business_address	(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
const gchar *	e_o365_contact_get_business_home_page	(EO365Contact *contact);
void		e_o365_contact_add_business_home_page	(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_o365_contact_get_business_phones	(EO365Contact *contact); /* const gchar * */
void		e_o365_contact_begin_business_phones	(JsonBuilder *builder);
void		e_o365_contact_end_business_phones	(JsonBuilder *builder);
void		e_o365_contact_add_business_phone	(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_o365_contact_get_categories		(EO365Contact *contact); /* const gchar * */
void		e_o365_contact_begin_categories		(JsonBuilder *builder);
void		e_o365_contact_end_categories		(JsonBuilder *builder);
void		e_o365_contact_add_category		(JsonBuilder *builder,
							 const gchar *category);
JsonArray *	e_o365_contact_get_children		(EO365Contact *contact); /* const gchar * */
void		e_o365_contact_begin_children		(JsonBuilder *builder);
void		e_o365_contact_end_children		(JsonBuilder *builder);
void		e_o365_contact_add_child		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_company_name		(EO365Contact *contact);
void		e_o365_contact_add_company_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_department		(EO365Contact *contact);
void		e_o365_contact_add_department		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_display_name		(EO365Contact *contact);
void		e_o365_contact_add_display_name		(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_o365_contact_get_email_addresses	(EO365Contact *contact); /* EO365EmailAddress * */
void		e_o365_contact_begin_email_addresses	(JsonBuilder *builder);
void		e_o365_contact_end_email_addresses	(JsonBuilder *builder);
const gchar *	e_o365_contact_get_file_as		(EO365Contact *contact);
void		e_o365_contact_add_file_as		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_generation		(EO365Contact *contact);
void		e_o365_contact_add_generation		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_given_name		(EO365Contact *contact);
void		e_o365_contact_add_given_name		(JsonBuilder *builder,
							 const gchar *value);
EO365PhysicalAddress *
		e_o365_contact_get_home_address		(EO365Contact *contact);
void		e_o365_contact_add_home_address		(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
JsonArray *	e_o365_contact_get_home_phones		(EO365Contact *contact); /* const gchar * */
void		e_o365_contact_begin_home_phones	(JsonBuilder *builder);
void		e_o365_contact_end_home_phones		(JsonBuilder *builder);
void		e_o365_contact_add_home_phone		(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_o365_contact_get_im_addresses		(EO365Contact *contact); /* const gchar * */
void		e_o365_contact_begin_im_addresses	(JsonBuilder *builder);
void		e_o365_contact_end_im_addresses		(JsonBuilder *builder);
void		e_o365_contact_add_im_address		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_initials		(EO365Contact *contact);
void		e_o365_contact_add_initials		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_job_title		(EO365Contact *contact);
void		e_o365_contact_add_job_title		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_manager		(EO365Contact *contact);
void		e_o365_contact_add_manager		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_middle_name		(EO365Contact *contact);
void		e_o365_contact_add_middle_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_mobile_phone		(EO365Contact *contact);
void		e_o365_contact_add_mobile_phone		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_nick_name		(EO365Contact *contact);
void		e_o365_contact_add_nick_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_office_location	(EO365Contact *contact);
void		e_o365_contact_add_office_location	(JsonBuilder *builder,
							 const gchar *value);
EO365PhysicalAddress *
		e_o365_contact_get_other_address	(EO365Contact *contact);
void		e_o365_contact_add_other_address	(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
const gchar *	e_o365_contact_get_personal_notes	(EO365Contact *contact);
void		e_o365_contact_add_personal_notes	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_profession		(EO365Contact *contact);
void		e_o365_contact_add_profession		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_spouse_name		(EO365Contact *contact);
void		e_o365_contact_add_spouse_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_surname		(EO365Contact *contact);
void		e_o365_contact_add_surname		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_title		(EO365Contact *contact);
void		e_o365_contact_add_title		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_yomi_company_name	(EO365Contact *contact);
void		e_o365_contact_add_yomi_company_name	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_yomi_given_name	(EO365Contact *contact);
void		e_o365_contact_add_yomi_given_name	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_contact_get_yomi_surname		(EO365Contact *contact);
void		e_o365_contact_add_yomi_surname		(JsonBuilder *builder,
							 const gchar *value);

G_END_DECLS

#endif /* E_O365_JSON_UTILS_H */
