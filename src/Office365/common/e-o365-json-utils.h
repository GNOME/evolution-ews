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
#define EO365Attendee			JsonObject
#define EO365Calendar			JsonObject
#define EO365CalendarGroup		JsonObject
#define EO365Category			JsonObject
#define EO365Contact			JsonObject
#define EO365Date			gint
#define EO365DateTimeWithZone		JsonObject
#define EO365EmailAddress		JsonObject
#define EO365Event			JsonObject
#define EO365Folder			JsonObject
#define EO365FollowupFlag		JsonObject
#define EO365InternetMessageHeader	JsonObject
#define EO365ItemBody			JsonObject
#define EO365Location			JsonObject
#define EO365MailFolder			JsonObject
#define EO365MailMessage		JsonObject
#define EO365OnlineMeetingInfo		JsonObject
#define EO365OutlookGeoCoordinates	JsonObject
#define EO365PatternedRecurrence	JsonObject
#define EO365Phone			JsonObject
#define EO365PhysicalAddress		JsonObject
#define EO365Recipient			JsonObject
#define EO365RecurrencePattern		JsonObject
#define EO365RecurrenceRange		JsonObject
#define EO365ResponseStatus		JsonObject

typedef enum _EO365AttachmentDataType {
	E_O365_ATTACHMENT_DATA_TYPE_NOT_SET,
	E_O365_ATTACHMENT_DATA_TYPE_UNKNOWN,
	E_O365_ATTACHMENT_DATA_TYPE_FILE,
	E_O365_ATTACHMENT_DATA_TYPE_ITEM,
	E_O365_ATTACHMENT_DATA_TYPE_REFERENCE
} EO365AttachmentDataType;

typedef enum _EO365AttendeeType {
	E_O365_ATTENDEE_NOT_SET,
	E_O365_ATTENDEE_UNKNOWN,
	E_O365_ATTENDEE_REQUIRED,
	E_O365_ATTENDEE_OPTIONAL,
	E_O365_ATTENDEE_RESOURCE
} EO365AttendeeType;

typedef enum _EO365CalendarColorType {
	E_O365_CALENDAR_COLOR_NOT_SET		= -3,
	E_O365_CALENDAR_COLOR_UNKNOWN		= -2,
	E_O365_CALENDAR_COLOR_AUTO		= -1,
	E_O365_CALENDAR_COLOR_LIGHT_BLUE	= 0,
	E_O365_CALENDAR_COLOR_LIGHT_GREEN	= 1,
	E_O365_CALENDAR_COLOR_LIGHT_ORANGE	= 2,
	E_O365_CALENDAR_COLOR_LIGHT_GRAY	= 3,
	E_O365_CALENDAR_COLOR_LIGHT_YELLOW	= 4,
	E_O365_CALENDAR_COLOR_LIGHT_TEAL	= 5,
	E_O365_CALENDAR_COLOR_LIGHT_PINK	= 6,
	E_O365_CALENDAR_COLOR_LIGHT_BROWN	= 7,
	E_O365_CALENDAR_COLOR_LIGHT_RED		= 8,
	E_O365_CALENDAR_COLOR_MAX_COLOR		= 9
} EO365CalendarColorType;

typedef enum _EO365DayOfWeekType {
	E_O365_DAY_OF_WEEK_NOT_SET,
	E_O365_DAY_OF_WEEK_UNKNOWN,
	E_O365_DAY_OF_WEEK_SUNDAY,
	E_O365_DAY_OF_WEEK_MONDAY,
	E_O365_DAY_OF_WEEK_TUESDAY,
	E_O365_DAY_OF_WEEK_WEDNESDAY,
	E_O365_DAY_OF_WEEK_THURSDAY,
	E_O365_DAY_OF_WEEK_FRIDAY,
	E_O365_DAY_OF_WEEK_SATURDAY
} EO365DayOfWeekType;

typedef enum _EO365EventTypeType {
	E_O365_EVENT_TYPE_NOT_SET,
	E_O365_EVENT_TYPE_UNKNOWN,
	E_O365_EVENT_TYPE_SINGLE_INSTANCE,
	E_O365_EVENT_TYPE_OCCURRENCE,
	E_O365_EVENT_TYPE_EXCEPTION,
	E_O365_EVENT_TYPE_SERIES_MASTER
} EO365EventTypeType;

typedef enum _EO365FollowupFlagStatusType {
	E_O365_FOLLOWUP_FLAG_STATUS_NOT_SET,
	E_O365_FOLLOWUP_FLAG_STATUS_UNKNOWN,
	E_O365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED,
	E_O365_FOLLOWUP_FLAG_STATUS_COMPLETE,
	E_O365_FOLLOWUP_FLAG_STATUS_FLAGGED
} EO365FollowupFlagStatusType;

typedef enum _EO365FreeBusyStatusType {
	E_O365_FREE_BUSY_STATUS_NOT_SET,
	E_O365_FREE_BUSY_STATUS_UNKNOWN,
	E_O365_FREE_BUSY_STATUS_FREE,
	E_O365_FREE_BUSY_STATUS_TENTATIVE,
	E_O365_FREE_BUSY_STATUS_BUSY,
	E_O365_FREE_BUSY_STATUS_OOF,
	E_O365_FREE_BUSY_STATUS_WORKING_ELSEWHERE
} EO365FreeBusyStatusType;

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

typedef enum _EO365LocationType {
	E_O365_LOCATION_NOT_SET,
	E_O365_LOCATION_UNKNOWN,
	E_O365_LOCATION_DEFAULT,
	E_O365_LOCATION_CONFERENCE_ROOM,
	E_O365_LOCATION_HOME_ADDRESS,
	E_O365_LOCATION_BUSINESS_ADDRESS,
	E_O365_LOCATION_GEO_COORDINATES,
	E_O365_LOCATION_STREET_ADDRESS,
	E_O365_LOCATION_HOTEL,
	E_O365_LOCATION_RESTAURANT,
	E_O365_LOCATION_LOCAL_BUSINESS,
	E_O365_LOCATION_POSTAL_ADDRESS
} EO365LocationType;

typedef enum _EO365OnlineMeetingProviderType {
	E_O365_ONLINE_MEETING_PROVIDER_NOT_SET			= -1,
	E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN			= 0,
	E_O365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_BUSINESS	= 1 << 0,
	E_O365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_CONSUMER	= 1 << 1,
	E_O365_ONLINE_MEETING_PROVIDER_TEAMS_FOR_BUSINESS	= 1 << 2
} EO365OnlineMeetingProviderType;

typedef enum _EO365PhoneType {
	E_O365_PHONE_NOT_SET,
	E_O365_PHONE_UNKNOWN,
	E_O365_PHONE_HOME,
	E_O365_PHONE_BUSINESS,
	E_O365_PHONE_MOBILE,
	E_O365_PHONE_OTHER,
	E_O365_PHONE_ASSISTANT,
	E_O365_PHONE_HOMEFAX,
	E_O365_PHONE_BUSINESSFAX,
	E_O365_PHONE_OTHERFAX,
	E_O365_PHONE_PAGER,
	E_O365_PHONE_RADIO
} EO365PhoneType;

typedef enum _EO365RecurrencePatternType {
	E_O365_RECURRENCE_PATTERN_NOT_SET,
	E_O365_RECURRENCE_PATTERN_UNKNOWN,
	E_O365_RECURRENCE_PATTERN_DAILY,
	E_O365_RECURRENCE_PATTERN_WEEKLY,
	E_O365_RECURRENCE_PATTERN_ABSOLUTE_MONTHLY,
	E_O365_RECURRENCE_PATTERN_RELATIVE_MONTHLY,
	E_O365_RECURRENCE_PATTERN_ABSOLUTE_YEARLY,
	E_O365_RECURRENCE_PATTERN_RELATIVE_YEARLY
} EO365RecurrencePatternType;

typedef enum _EO365RecurrenceRangeType {
	E_O365_RECURRENCE_RANGE_NOT_SET,
	E_O365_RECURRENCE_RANGE_UNKNOWN,
	E_O365_RECURRENCE_RANGE_ENDDATE,
	E_O365_RECURRENCE_RANGE_NOEND,
	E_O365_RECURRENCE_RANGE_NUMBERED
} EO365RecurrenceRangeType;

typedef enum _EO365ResponseType {
	E_O365_RESPONSE_NOT_SET,
	E_O365_RESPONSE_UNKNOWN,
	E_O365_RESPONSE_NONE,
	E_O365_RESPONSE_ORGANIZER,
	E_O365_RESPONSE_TENTATIVELY_ACCEPTED,
	E_O365_RESPONSE_ACCEPTED,
	E_O365_RESPONSE_DECLINED,
	E_O365_RESPONSE_NOT_RESPONDED
} EO365ResponseType;

typedef enum _EO365SensitivityType {
	E_O365_SENSITIVITY_NOT_SET,
	E_O365_SENSITIVITY_UNKNOWN,
	E_O365_SENSITIVITY_NORMAL,
	E_O365_SENSITIVITY_PERSONAL,
	E_O365_SENSITIVITY_PRIVATE,
	E_O365_SENSITIVITY_CONFIDENTIAL
} EO365SensitivityType;

typedef enum _EO365WeekIndexType {
	E_O365_WEEK_INDEX_NOT_SET,
	E_O365_WEEK_INDEX_UNKNOWN,
	E_O365_WEEK_INDEX_FIRST,
	E_O365_WEEK_INDEX_SECOND,
	E_O365_WEEK_INDEX_THIRD,
	E_O365_WEEK_INDEX_FOURTH,
	E_O365_WEEK_INDEX_LAST
} EO365WeekIndexType;

const gchar *	e_o365_calendar_color_to_rgb		(EO365CalendarColorType color);
EO365CalendarColorType
		e_o365_rgb_to_calendar_color		(const gchar *rgb);

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

EO365Date	e_o365_date_get				(JsonObject *object,
							 const gchar *member_name);
void		e_o365_add_date				(JsonBuilder *builder,
							 const gchar *member_name,
							 EO365Date value);
gboolean	e_o365_date_decode			(EO365Date dt,
							 guint *out_year,
							 guint *out_month,
							 guint *out_day);
EO365Date	e_o365_date_encode			(guint year,
							 guint month,
							 guint day);

time_t		e_o365_get_date_time_offset_member	(JsonObject *object,
							 const gchar *member_name);
void		e_o365_add_date_time_offset_member	(JsonBuilder *builder,
							 const gchar *member_name,
							 time_t value);

time_t		e_o365_date_time_get_date_time		(EO365DateTimeWithZone *datetime);
const gchar *	e_o365_date_time_get_time_zone		(EO365DateTimeWithZone *datetime);
void		e_o365_add_date_time			(JsonBuilder *builder,
							 const gchar *member_name,
							 time_t date_time,
							 const gchar *zone);

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
							 const gchar *member_name,
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

const gchar *	e_o365_calendar_group_get_id		(EO365CalendarGroup *group);
const gchar *	e_o365_calendar_group_get_change_key	(EO365CalendarGroup *group);
const gchar *	e_o365_calendar_group_get_class_id	(EO365CalendarGroup *group);
const gchar *	e_o365_calendar_group_get_name		(EO365CalendarGroup *group);

const gchar *	e_o365_calendar_get_id			(EO365Calendar *calendar);
const gchar *	e_o365_calendar_get_change_key		(EO365Calendar *calendar);
gboolean	e_o365_calendar_get_can_edit		(EO365Calendar *calendar);
gboolean	e_o365_calendar_get_can_share		(EO365Calendar *calendar);
gboolean	e_o365_calendar_get_can_view_private_items
							(EO365Calendar *calendar);
gboolean	e_o365_calendar_get_is_removable	(EO365Calendar *calendar);
gboolean	e_o365_calendar_get_is_tallying_responses
							(EO365Calendar *calendar);
EO365EmailAddress *
		e_o365_calendar_get_owner		(EO365Calendar *calendar);
const gchar *	e_o365_calendar_get_name		(EO365Calendar *calendar);
void		e_o365_calendar_add_name		(JsonBuilder *builder,
							 const gchar *name);
guint32		e_o365_calendar_get_allowed_online_meeting_providers /* bit-or of EO365OnlineMeetingProviderType */
							(EO365Calendar *calendar);
void		e_o365_calendar_add_allowed_online_meeting_providers
							(JsonBuilder *builder,
							 guint providers); /* bit-or of EO365OnlineMeetingProviderType */
EO365CalendarColorType
		e_o365_calendar_get_color		(EO365Calendar *calendar);
void		e_o365_calendar_add_color		(JsonBuilder *builder,
							 EO365CalendarColorType color);
EO365OnlineMeetingProviderType
		e_o365_calendar_get_default_online_meeting_provider
							(EO365Calendar *calendar);
void		e_o365_calendar_add_default_online_meeting_provider
							(JsonBuilder *builder,
							 EO365OnlineMeetingProviderType provider);
EO365ResponseType
		e_o365_response_status_get_response	(EO365ResponseStatus *response_status);
time_t		e_o365_response_status_get_time		(EO365ResponseStatus *response_status);

EO365ResponseStatus *
		e_o365_attendee_get_status		(EO365Attendee *attendee);
EO365AttendeeType
		e_o365_attendee_get_type		(EO365Attendee *attendee);
EO365EmailAddress *
		e_o365_attendee_get_email_address	(EO365Attendee *attendee);

gdouble		e_o365_outlook_geo_coordinates_get_accuracy
							(EO365OutlookGeoCoordinates *coords);
void		e_o365_outlook_geo_coordinates_add_accuracy
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_o365_outlook_geo_coordinates_get_altitude
							(EO365OutlookGeoCoordinates *coords);
void		e_o365_outlook_geo_coordinates_add_altitude
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_o365_outlook_geo_coordinates_get_altitude_accuracy
							(EO365OutlookGeoCoordinates *coords);
void		e_o365_outlook_geo_coordinates_add_altitude_accuracy
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_o365_outlook_geo_coordinates_get_latitude
							(EO365OutlookGeoCoordinates *coords);
void		e_o365_outlook_geo_coordinates_add_latitude
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_o365_outlook_geo_coordinates_get_longitude
							(EO365OutlookGeoCoordinates *coords);
void		e_o365_outlook_geo_coordinates_add_longitude
							(JsonBuilder *builder,
							 gdouble value);
EO365PhysicalAddress *
		e_o365_location_get_address		(EO365Location *location);
void		e_o365_location_add_address		(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
EO365OutlookGeoCoordinates *
		e_o365_location_get_coordinates		(EO365Location *location);
void		e_o365_location_begin_coordinates	(JsonBuilder *builder);
void		e_o365_location_end_coordinates		(JsonBuilder *builder);
const gchar *	e_o365_location_get_display_name	(EO365Location *location);
void		e_o365_location_add_display_name	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_location_get_email_address	(EO365Location *location);
void		e_o365_location_add_email_address	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_o365_location_get_uri			(EO365Location *location);
void		e_o365_location_add_uri			(JsonBuilder *builder,
							 const gchar *value);
EO365LocationType
		e_o365_location_get_type		(EO365Location *location);

const gchar *	e_o365_phone_get_number			(EO365Phone *phone);
EO365PhoneType	e_o365_phone_get_type			(EO365Phone *phone);

const gchar *	e_o365_online_meeting_info_get_conference_id
							(EO365OnlineMeetingInfo *meeting_info);
const gchar *	e_o365_online_meeting_info_get_join_url	(EO365OnlineMeetingInfo *meeting_info);
JsonArray *	e_o365_online_meeting_info_get_phones	(EO365OnlineMeetingInfo *meeting_info); /* EO365Phone * */
const gchar *	e_o365_online_meeting_info_get_quick_dial
							(EO365OnlineMeetingInfo *meeting_info);
JsonArray *	e_o365_online_meeting_info_get_toll_free_numbers
							(EO365OnlineMeetingInfo *meeting_info); /* gchar * */
const gchar *	e_o365_online_meeting_info_get_toll_number
							(EO365OnlineMeetingInfo *meeting_info);

EO365DayOfWeekType
		e_o365_array_get_day_of_week_element	(JsonArray *array, /* const gchar * representing EO365DayOfWeekType */
							 guint index);
gint		e_o365_recurrence_pattern_get_day_of_month
							(EO365RecurrencePattern *pattern);
void		e_o365_recurrence_pattern_add_day_of_month
							(JsonBuilder *builder,
							 gint value);
JsonArray *	e_o365_recurrence_pattern_get_days_of_week
							(EO365RecurrencePattern *pattern); /* const gchar * representing EO365DayOfWeekType, use e_o365_array_get_day_of_week_element() */
void		e_o365_recurrence_pattern_begin_days_of_week
							(JsonBuilder *builder);
void		e_o365_recurrence_pattern_end_days_of_week
							(JsonBuilder *builder);
void		e_o365_recurrence_pattern_add_day_of_week
							(JsonBuilder *builder,
							 EO365DayOfWeekType value);
EO365DayOfWeekType
		e_o365_recurrence_pattern_get_first_day_of_week
							(EO365RecurrencePattern *pattern);
void		e_o365_recurrence_pattern_add_first_day_of_week
							(JsonBuilder *builder,
							 EO365DayOfWeekType value);
EO365WeekIndexType
		e_o365_recurrence_pattern_get_index	(EO365RecurrencePattern *pattern);
void		e_o365_recurrence_pattern_add_index	(JsonBuilder *builder,
							 EO365WeekIndexType value);
gint		e_o365_recurrence_pattern_get_interval	(EO365RecurrencePattern *pattern);
void		e_o365_recurrence_pattern_add_interval	(JsonBuilder *builder,
							 gint value);
gint		e_o365_recurrence_pattern_get_month	(EO365RecurrencePattern *pattern);
void		e_o365_recurrence_pattern_add_month	(JsonBuilder *builder,
							 gint value);
EO365RecurrencePatternType
		e_o365_recurrence_pattern_get_type	(EO365RecurrencePattern *pattern);
void		e_o365_recurrence_pattern_add_type	(JsonBuilder *builder,
							 EO365RecurrencePatternType value);

EO365Date	e_o365_recurrence_range_get_end_date	(EO365RecurrenceRange *range);
void		e_o365_recurrence_range_add_end_date	(JsonBuilder *builder,
							 EO365Date value);
gint		e_o365_recurrence_range_get_number_of_occurrences
							(EO365RecurrenceRange *range);
void		e_o365_recurrence_range_add_number_of_occurrences
							(JsonBuilder *builder,
							 gint value);
const gchar *	e_o365_recurrence_range_get_recurrence_time_zone
							(EO365RecurrenceRange *range);
void		e_o365_recurrence_range_add_recurrence_time_zone
							(JsonBuilder *builder,
							 const gchar *value);
EO365Date	e_o365_recurrence_range_get_start_date	(EO365RecurrenceRange *range);
void		e_o365_recurrence_range_add_start_date	(JsonBuilder *builder,
							 EO365Date value);
EO365RecurrenceRangeType
		e_o365_recurrence_range_get_type	(EO365RecurrenceRange *range);
void		e_o365_recurrence_range_add_type	(JsonBuilder *builder,
							 EO365RecurrenceRangeType value);

EO365RecurrencePattern *
		e_o365_patterned_recurrence_get_pattern	(EO365PatternedRecurrence *patterned_recurrence);
void		e_o365_patterned_recurrence_begin_pattern
							(JsonBuilder *builder);
void		e_o365_patterned_recurrence_end_pattern
							(JsonBuilder *builder);
EO365RecurrenceRange *
		e_o365_patterned_recurrence_get_range	(EO365PatternedRecurrence *patterned_recurrence);
void		e_o365_patterned_recurrence_begin_range
							(JsonBuilder *builder);
void		e_o365_patterned_recurrence_end_range
							(JsonBuilder *builder);

const gchar *	e_o365_event_get_id			(EO365Event *event);
const gchar *	e_o365_event_get_change_key		(EO365Event *event);
JsonArray *	e_o365_event_get_attendees		(EO365Event *event); /* EO365Attendee * */
void		e_o365_event_begin_attendees		(JsonBuilder *builder);
void		e_o365_event_end_attendees		(JsonBuilder *builder);
void		e_o365_event_add_attendee		(JsonBuilder *builder,
							 EO365AttendeeType type,
							 EO365ResponseType response,
							 const gchar *name,
							 const gchar *address);
EO365ItemBody *	e_o365_event_get_body			(EO365Event *event);
void		e_o365_event_add_body			(JsonBuilder *builder,
							 EO365ItemBodyContentTypeType content_type,
							 const gchar *content);
const gchar *	e_o365_event_get_body_preview		(EO365Event *event);
JsonArray *	e_o365_event_get_categories		(EO365Event *event); /* const gchar * */
void		e_o365_event_begin_categories		(JsonBuilder *builder);
void		e_o365_event_end_categories		(JsonBuilder *builder);
void		e_o365_event_add_category		(JsonBuilder *builder,
							 const gchar *category);
time_t		e_o365_event_get_created_date_time	(EO365Event *event);
EO365DateTimeWithZone *
		e_o365_event_get_end			(EO365Event *event);
void		e_o365_event_add_end			(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
gboolean	e_o365_event_get_has_attachments	(EO365Event *event);
const gchar *	e_o365_event_get_ical_uid		(EO365Event *event);
EO365ImportanceType
		e_o365_event_get_importance		(EO365Event *event);
void		e_o365_event_add_importance		(JsonBuilder *builder,
							 EO365ImportanceType value);
gboolean	e_o365_event_get_is_all_day		(EO365Event *event);
void		e_o365_event_add_is_all_day		(JsonBuilder *builder,
							 gboolean value);
gboolean	e_o365_event_get_is_cancelled		(EO365Event *event);
gboolean	e_o365_event_get_is_online_meeting	(EO365Event *event);
void		e_o365_event_add_is_online_meeting	(JsonBuilder *builder,
							 gboolean value);
gboolean	e_o365_event_get_is_organizer		(EO365Event *event);
gboolean	e_o365_event_get_is_reminder_on		(EO365Event *event);
void		e_o365_event_add_is_reminder_on		(JsonBuilder *builder,
							 gboolean value);
time_t		e_o365_event_get_last_modified_date_time(EO365Event *event);
EO365Location *	e_o365_event_get_location		(EO365Event *event);
void		e_o365_event_begin_location		(JsonBuilder *builder);
void		e_o365_event_end_location		(JsonBuilder *builder);
JsonArray *	e_o365_event_get_locations		(EO365Event *event); /* EO365Location * */
void		e_o365_event_begin_locations		(JsonBuilder *builder);
void		e_o365_event_end_locations		(JsonBuilder *builder);
void		e_o365_event_begin_locations_location	(JsonBuilder *builder);
void		e_o365_event_end_locations_location	(JsonBuilder *builder);
EO365OnlineMeetingInfo *
		e_o365_event_get_online_meeting_info	(EO365Event *event);
EO365OnlineMeetingProviderType
		e_o365_event_get_online_meeting_provider(EO365Event *event);
void		e_o365_event_add_online_meeting_provider(JsonBuilder *builder,
							 EO365OnlineMeetingProviderType value);
const gchar *	e_o365_event_get_online_meeting_url	(EO365Event *event);
EO365Recipient *e_o365_event_get_organizer		(EO365Event *event);
void		e_o365_event_add_organizer		(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
const gchar *	e_o365_event_get_original_end_timezone	(EO365Event *event);
time_t		e_o365_event_get_original_start		(EO365Event *event);
const gchar *	e_o365_event_get_original_start_timezone(EO365Event *event);
EO365PatternedRecurrence *
		e_o365_event_get_recurrence		(EO365Event *event);
void		e_o365_event_begin_recurrence		(JsonBuilder *builder);
void		e_o365_event_end_recurrence		(JsonBuilder *builder);
void		e_o365_event_add_null_recurrence	(JsonBuilder *builder);
gint		e_o365_event_get_reminder_minutes_before_start
							(EO365Event *event);
void		e_o365_event_add_reminder_minutes_before_start
							(JsonBuilder *builder,
							 gint value);
gboolean	e_o365_event_get_response_requested	(EO365Event *event);
void		e_o365_event_add_response_requested	(JsonBuilder *builder,
							 gboolean value);
EO365ResponseStatus *
		e_o365_event_get_response_status	(EO365Event *event);
EO365SensitivityType
		e_o365_event_get_sensitivity		(EO365Event *event);
void		e_o365_event_add_sensitivity		(JsonBuilder *builder,
							 EO365SensitivityType value);
const gchar *	e_o365_event_get_series_master_id	(EO365Event *event);
EO365FreeBusyStatusType
		e_o365_event_get_show_as		(EO365Event *event);
void		e_o365_event_add_show_as		(JsonBuilder *builder,
							 EO365FreeBusyStatusType value);
EO365DateTimeWithZone *
		e_o365_event_get_start			(EO365Event *event);
void		e_o365_event_add_start			(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
const gchar *	e_o365_event_get_subject		(EO365Event *event);
void		e_o365_event_add_subject		(JsonBuilder *builder,
							 const gchar *value);
EO365EventTypeType
		e_o365_event_get_type			(EO365Event *event);
const gchar *	e_o365_event_get_web_link		(EO365Event *event);

G_END_DECLS

#endif /* E_O365_JSON_UTILS_H */
