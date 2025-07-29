/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_JSON_UTILS_H
#define E_M365_JSON_UTILS_H

#include <time.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

/* Just for better readability */
#define EM365Attachment			JsonObject
#define EM365Attendee			JsonObject
#define EM365AutomaticRepliesSetting	JsonObject
#define EM365Calendar			JsonObject
#define EM365CalendarGroup		JsonObject
#define EM365CalendarPermission		JsonObject
#define EM365Category			JsonObject
#define EM365ChecklistItem		JsonObject
#define EM365Contact			JsonObject
#define EM365Date			gint
#define EM365DateTimeWithZone		JsonObject
#define EM365EmailAddress		JsonObject
#define EM365Event			JsonObject
#define EM365Folder			JsonObject
#define EM365FollowupFlag		JsonObject
#define EM365FreeBusyError		JsonObject
#define EM365InternetMessageHeader	JsonObject
#define EM365ItemBody			JsonObject
#define EM365LinkedResource		JsonObject
#define EM365Location			JsonObject
#define EM365MailboxSettings		JsonObject
#define EM365MailFolder			JsonObject
#define EM365MailMessage		JsonObject
#define EM365OnlineMeetingInfo		JsonObject
#define EM365OutlookGeoCoordinates	JsonObject
#define EM365PatternedRecurrence	JsonObject
#define EM365Phone			JsonObject
#define EM365PhysicalAddress		JsonObject
#define EM365Recipient			JsonObject
#define EM365RecurrencePattern		JsonObject
#define EM365RecurrenceRange		JsonObject
#define EM365ResponseStatus		JsonObject
#define EM365ScheduleInformation	JsonObject
#define EM365ScheduleItem		JsonObject
#define EM365ScoredEmailAddress		JsonObject
#define EM365Task			JsonObject
#define EM365TaskList			JsonObject
#define EM365TimeOfDay			gint64
#define EM365Website			JsonObject
#define EM365WorkingHours		JsonObject

#define E_M365_RECURRENCE_BLOB_NAME "Binary {00062002-0000-0000-c000-000000000046} Id 0x8216"
#define E_M365_PT_MESSAGE_SIZE_NAME "Long 0xe08" /* PidTagMessageSize */

typedef enum _EM365AttachmentDataType {
	E_M365_ATTACHMENT_DATA_TYPE_NOT_SET,
	E_M365_ATTACHMENT_DATA_TYPE_UNKNOWN,
	E_M365_ATTACHMENT_DATA_TYPE_FILE,
	E_M365_ATTACHMENT_DATA_TYPE_ITEM,
	E_M365_ATTACHMENT_DATA_TYPE_REFERENCE
} EM365AttachmentDataType;

typedef enum _EM365AttendeeType {
	E_M365_ATTENDEE_NOT_SET,
	E_M365_ATTENDEE_UNKNOWN,
	E_M365_ATTENDEE_REQUIRED,
	E_M365_ATTENDEE_OPTIONAL,
	E_M365_ATTENDEE_RESOURCE
} EM365AttendeeType;

typedef enum _EM365AutomaticRepliesStatusType {
	E_M365_AUTOMATIC_REPLIES_STATUS_NOT_SET,
	E_M365_AUTOMATIC_REPLIES_STATUS_UNKNOWN,
	E_M365_AUTOMATIC_REPLIES_STATUS_DISABLED,
	E_M365_AUTOMATIC_REPLIES_STATUS_ALWAYS_ENABLED,
	E_M365_AUTOMATIC_REPLIES_STATUS_SCHEDULED
} EM365AutomaticRepliesStatusType;

typedef enum _EM365CalendarColorType {
	E_M365_CALENDAR_COLOR_NOT_SET		= -3,
	E_M365_CALENDAR_COLOR_UNKNOWN		= -2,
	E_M365_CALENDAR_COLOR_AUTO		= -1,
	E_M365_CALENDAR_COLOR_LIGHT_BLUE	= 0,
	E_M365_CALENDAR_COLOR_LIGHT_GREEN	= 1,
	E_M365_CALENDAR_COLOR_LIGHT_ORANGE	= 2,
	E_M365_CALENDAR_COLOR_LIGHT_GRAY	= 3,
	E_M365_CALENDAR_COLOR_LIGHT_YELLOW	= 4,
	E_M365_CALENDAR_COLOR_LIGHT_TEAL	= 5,
	E_M365_CALENDAR_COLOR_LIGHT_PINK	= 6,
	E_M365_CALENDAR_COLOR_LIGHT_BROWN	= 7,
	E_M365_CALENDAR_COLOR_LIGHT_RED		= 8,
	E_M365_CALENDAR_COLOR_MAX_COLOR		= 9
} EM365CalendarColorType;

typedef enum _EM365CalendarPermissionType {
	E_M365_CALENDAR_PERMISSION_NOT_SET				 = -1,
	E_M365_CALENDAR_PERMISSION_UNKNOWN				 = 0,
	E_M365_CALENDAR_PERMISSION_NONE					 = 1 << 0,
	E_M365_CALENDAR_PERMISSION_FREE_BUSY_READ			 = 1 << 1,
	E_M365_CALENDAR_PERMISSION_LIMITED_READ				 = 1 << 2,
	E_M365_CALENDAR_PERMISSION_READ					 = 1 << 3,
	E_M365_CALENDAR_PERMISSION_WRITE				 = 1 << 4,
	E_M365_CALENDAR_PERMISSION_DELEGATE_WITHOUT_PRIVATE_EVENT_ACCESS = 1 << 5,
	E_M365_CALENDAR_PERMISSION_DELEGATE_WITH_PRIVATE_EVENT_ACCESS	 = 1 << 6,
	E_M365_CALENDAR_PERMISSION_CUSTOM				 = 1 << 7
} EM365CalendarPermissionType;

typedef enum _EM365DayOfWeekType {
	E_M365_DAY_OF_WEEK_NOT_SET,
	E_M365_DAY_OF_WEEK_UNKNOWN,
	E_M365_DAY_OF_WEEK_SUNDAY,
	E_M365_DAY_OF_WEEK_MONDAY,
	E_M365_DAY_OF_WEEK_TUESDAY,
	E_M365_DAY_OF_WEEK_WEDNESDAY,
	E_M365_DAY_OF_WEEK_THURSDAY,
	E_M365_DAY_OF_WEEK_FRIDAY,
	E_M365_DAY_OF_WEEK_SATURDAY
} EM365DayOfWeekType;

typedef enum _EM365EventTypeType {
	E_M365_EVENT_TYPE_NOT_SET,
	E_M365_EVENT_TYPE_UNKNOWN,
	E_M365_EVENT_TYPE_SINGLE_INSTANCE,
	E_M365_EVENT_TYPE_OCCURRENCE,
	E_M365_EVENT_TYPE_EXCEPTION,
	E_M365_EVENT_TYPE_SERIES_MASTER
} EM365EventTypeType;

typedef enum _EM365ExternalAudienceScopeType {
	E_M365_EXTERNAL_AUDIENCE_SCOPE_NOT_SET,
	E_M365_EXTERNAL_AUDIENCE_SCOPE_UNKNOWN,
	E_M365_EXTERNAL_AUDIENCE_SCOPE_NONE,
	E_M365_EXTERNAL_AUDIENCE_SCOPE_CONTACTS_ONLY,
	E_M365_EXTERNAL_AUDIENCE_SCOPE_ALL
} EM365ExternalAudienceScopeType;

typedef enum _EM365FollowupFlagStatusType {
	E_M365_FOLLOWUP_FLAG_STATUS_NOT_SET,
	E_M365_FOLLOWUP_FLAG_STATUS_UNKNOWN,
	E_M365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED,
	E_M365_FOLLOWUP_FLAG_STATUS_COMPLETE,
	E_M365_FOLLOWUP_FLAG_STATUS_FLAGGED
} EM365FollowupFlagStatusType;

typedef enum _EM365FreeBusyStatusType {
	E_M365_FREE_BUSY_STATUS_NOT_SET,
	E_M365_FREE_BUSY_STATUS_UNKNOWN,
	E_M365_FREE_BUSY_STATUS_FREE,
	E_M365_FREE_BUSY_STATUS_TENTATIVE,
	E_M365_FREE_BUSY_STATUS_BUSY,
	E_M365_FREE_BUSY_STATUS_OOF,
	E_M365_FREE_BUSY_STATUS_WORKING_ELSEWHERE
} EM365FreeBusyStatusType;

typedef enum _EM365ImportanceType {
	E_M365_IMPORTANCE_NOT_SET,
	E_M365_IMPORTANCE_UNKNOWN,
	E_M365_IMPORTANCE_LOW,
	E_M365_IMPORTANCE_NORMAL,
	E_M365_IMPORTANCE_HIGH
} EM365ImportanceType;

typedef enum _EM365InferenceClassificationType {
	E_M365_INFERENCE_CLASSIFICATION_NOT_SET,
	E_M365_INFERENCE_CLASSIFICATION_UNKNOWN,
	E_M365_INFERENCE_CLASSIFICATION_FOCUSED,
	E_M365_INFERENCE_CLASSIFICATION_OTHER
} EM365InferenceClassificationType;

typedef enum _EM365ItemBodyContentTypeType {
	E_M365_ITEM_BODY_CONTENT_TYPE_NOT_SET,
	E_M365_ITEM_BODY_CONTENT_TYPE_UNKNOWN,
	E_M365_ITEM_BODY_CONTENT_TYPE_TEXT,
	E_M365_ITEM_BODY_CONTENT_TYPE_HTML
} EM365ItemBodyContentTypeType;

typedef enum _EM365LocationType {
	E_M365_LOCATION_NOT_SET,
	E_M365_LOCATION_UNKNOWN,
	E_M365_LOCATION_DEFAULT,
	E_M365_LOCATION_CONFERENCE_ROOM,
	E_M365_LOCATION_HOME_ADDRESS,
	E_M365_LOCATION_BUSINESS_ADDRESS,
	E_M365_LOCATION_GEO_COORDINATES,
	E_M365_LOCATION_STREET_ADDRESS,
	E_M365_LOCATION_HOTEL,
	E_M365_LOCATION_RESTAURANT,
	E_M365_LOCATION_LOCAL_BUSINESS,
	E_M365_LOCATION_POSTAL_ADDRESS
} EM365LocationType;

typedef enum _EM365OnlineMeetingProviderType {
	E_M365_ONLINE_MEETING_PROVIDER_NOT_SET			= -1,
	E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN			= 0,
	E_M365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_BUSINESS	= 1 << 0,
	E_M365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_CONSUMER	= 1 << 1,
	E_M365_ONLINE_MEETING_PROVIDER_TEAMS_FOR_BUSINESS	= 1 << 2
} EM365OnlineMeetingProviderType;

typedef enum _EM365PhoneType {
	E_M365_PHONE_NOT_SET,
	E_M365_PHONE_UNKNOWN,
	E_M365_PHONE_HOME,
	E_M365_PHONE_BUSINESS,
	E_M365_PHONE_MOBILE,
	E_M365_PHONE_OTHER,
	E_M365_PHONE_ASSISTANT,
	E_M365_PHONE_HOMEFAX,
	E_M365_PHONE_BUSINESSFAX,
	E_M365_PHONE_OTHERFAX,
	E_M365_PHONE_PAGER,
	E_M365_PHONE_RADIO
} EM365PhoneType;

typedef enum _EM365RecurrencePatternType {
	E_M365_RECURRENCE_PATTERN_NOT_SET,
	E_M365_RECURRENCE_PATTERN_UNKNOWN,
	E_M365_RECURRENCE_PATTERN_DAILY,
	E_M365_RECURRENCE_PATTERN_WEEKLY,
	E_M365_RECURRENCE_PATTERN_ABSOLUTE_MONTHLY,
	E_M365_RECURRENCE_PATTERN_RELATIVE_MONTHLY,
	E_M365_RECURRENCE_PATTERN_ABSOLUTE_YEARLY,
	E_M365_RECURRENCE_PATTERN_RELATIVE_YEARLY
} EM365RecurrencePatternType;

typedef enum _EM365RecurrenceRangeType {
	E_M365_RECURRENCE_RANGE_NOT_SET,
	E_M365_RECURRENCE_RANGE_UNKNOWN,
	E_M365_RECURRENCE_RANGE_ENDDATE,
	E_M365_RECURRENCE_RANGE_NOEND,
	E_M365_RECURRENCE_RANGE_NUMBERED
} EM365RecurrenceRangeType;

typedef enum _EM365ResponseType {
	E_M365_RESPONSE_NOT_SET,
	E_M365_RESPONSE_UNKNOWN,
	E_M365_RESPONSE_NONE,
	E_M365_RESPONSE_ORGANIZER,
	E_M365_RESPONSE_TENTATIVELY_ACCEPTED,
	E_M365_RESPONSE_ACCEPTED,
	E_M365_RESPONSE_DECLINED,
	E_M365_RESPONSE_NOT_RESPONDED
} EM365ResponseType;

typedef enum _EM365SensitivityType {
	E_M365_SENSITIVITY_NOT_SET,
	E_M365_SENSITIVITY_UNKNOWN,
	E_M365_SENSITIVITY_NORMAL,
	E_M365_SENSITIVITY_PERSONAL,
	E_M365_SENSITIVITY_PRIVATE,
	E_M365_SENSITIVITY_CONFIDENTIAL
} EM365SensitivityType;

typedef enum _EM365StatusType {
	E_M365_STATUS_NOT_SET,
	E_M365_STATUS_UNKNOWN,
	E_M365_STATUS_NOT_STARTED,
	E_M365_STATUS_IN_PROGRESS,
	E_M365_STATUS_COMPLETED,
	E_M365_STATUS_WAITING_ON_OTHERS,
	E_M365_STATUS_DEFERRED
} EM365StatusType;

typedef enum _EM365WeekIndexType {
	E_M365_WEEK_INDEX_NOT_SET,
	E_M365_WEEK_INDEX_UNKNOWN,
	E_M365_WEEK_INDEX_FIRST,
	E_M365_WEEK_INDEX_SECOND,
	E_M365_WEEK_INDEX_THIRD,
	E_M365_WEEK_INDEX_FOURTH,
	E_M365_WEEK_INDEX_LAST
} EM365WeekIndexType;

typedef enum _EM365TaskListKind {
	E_M365_TASK_LIST_KIND_NOT_SET,
	E_M365_TASK_LIST_KIND_UNKNOWN,
	E_M365_TASK_LIST_KIND_NONE,
	E_M365_TASK_LIST_KIND_DEFAULT_LIST,
	E_M365_TASK_LIST_KIND_FLAGGED_EMAILS,
	E_M365_TASK_LIST_KIND_UNKNOWN_FUTURE_VALUE
} EM365TaskListKind;

typedef enum _EM365WebsiteType {
	E_M365_WEBSITE_TYPE_NOT_SET,
	E_M365_WEBSITE_TYPE_UNKNOWN,
	E_M365_WEBSITE_TYPE_OTHER,
	E_M365_WEBSITE_TYPE_HOME,
	E_M365_WEBSITE_TYPE_WORK,
	E_M365_WEBSITE_TYPE_BLOG,
	E_M365_WEBSITE_TYPE_PROFILE
} EM365WebsiteType;

const gchar *	e_m365_calendar_color_to_rgb		(EM365CalendarColorType color);
EM365CalendarColorType
		e_m365_rgb_to_calendar_color		(const gchar *rgb);

JsonArray *	e_m365_json_get_array_member		(JsonObject *object,
							 const gchar *member_name);
void		e_m365_json_begin_array_member		(JsonBuilder *builder,
							 const gchar *member_name);
void		e_m365_json_end_array_member		(JsonBuilder *builder);
gboolean	e_m365_json_get_boolean_member		(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
void		e_m365_json_add_boolean_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gboolean value);
gdouble		e_m365_json_get_double_member		(JsonObject *object,
							 const gchar *member_name,
							 gdouble default_value);
void		e_m365_json_add_double_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gdouble value);
gint64		e_m365_json_get_int_member		(JsonObject *object,
							 const gchar *member_name,
							 gint64 default_value);
void		e_m365_json_add_int_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 gint64 value);
gboolean	e_m365_json_get_null_member		(JsonObject *object,
							 const gchar *member_name,
							 gboolean default_value);
void		e_m365_json_add_null_member		(JsonBuilder *builder,
							 const gchar *member_name);
JsonObject *	e_m365_json_get_object_member		(JsonObject *object,
							 const gchar *member_name);
void		e_m365_json_begin_object_member		(JsonBuilder *builder,
							 const gchar *member_name);
void		e_m365_json_end_object_member		(JsonBuilder *builder);
const gchar *	e_m365_json_get_string_member		(JsonObject *object,
							 const gchar *member_name,
							 const gchar *default_value);
void		e_m365_json_add_string_member		(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
void		e_m365_json_add_nonempty_string_member	(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
void		e_m365_json_add_nonempty_or_null_string_member
							(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *value);
const gchar *	e_m365_json_get_string_single_value_extended_property
							(JsonObject *object,
							 const gchar *property_name);
gint64		e_m365_json_get_integer_single_value_extended_property
							(JsonObject *object,
							 const gchar *property_name,
							 gint64 default_value);

EM365Date	e_m365_date_get				(JsonObject *object,
							 const gchar *member_name);
void		e_m365_add_date				(JsonBuilder *builder,
							 const gchar *member_name,
							 EM365Date value);
gboolean	e_m365_date_decode			(EM365Date dt,
							 gint *out_year,
							 gint *out_month,
							 gint *out_day);
EM365Date	e_m365_date_encode			(gint year,
							 gint month,
							 gint day);
EM365TimeOfDay	e_m365_time_of_day_get			(JsonObject *object,
							 const gchar *member_name);
void		e_m365_add_time_of_day			(JsonBuilder *builder,
							 const gchar *member_name,
							 EM365TimeOfDay value);
gboolean	e_m365_time_of_day_decode		(EM365TimeOfDay tod,
							 gint *out_hour,
							 gint *out_minute,
							 gint *out_second,
							 gint *out_fraction);
EM365TimeOfDay	e_m365_time_of_day_encode		(gint hour,
							 gint minute,
							 gint second,
							 gint fraction);

time_t		e_m365_get_date_time_offset_member	(JsonObject *object,
							 const gchar *member_name,
							 gboolean *out_exists);
void		e_m365_add_date_time_offset_member	(JsonBuilder *builder,
							 const gchar *member_name,
							 time_t value);

time_t		e_m365_date_time_get_date_time		(EM365DateTimeWithZone *datetime);
const gchar *	e_m365_date_time_get_time_zone		(EM365DateTimeWithZone *datetime);
void		e_m365_add_date_time			(JsonBuilder *builder,
							 const gchar *member_name,
							 time_t date_time,
							 const gchar *zone);

gboolean	e_m365_delta_is_removed_object		(JsonObject *object);

const gchar *	e_m365_category_get_display_name	(EM365Category *category);
const gchar *	e_m365_category_get_id			(EM365Category *category);
const gchar *	e_m365_category_get_color		(EM365Category *category);

const gchar *	e_m365_folder_get_id			(EM365Folder *folder);
const gchar *	e_m365_folder_get_parent_folder_id	(EM365Folder *folder);
const gchar *	e_m365_folder_get_display_name		(EM365Folder *folder);

gint32		e_m365_mail_folder_get_child_folder_count
							(EM365MailFolder *folder);
gint32		e_m365_mail_folder_get_total_item_count	(EM365MailFolder *folder);
gint32		e_m365_mail_folder_get_unread_item_count(EM365MailFolder *folder);

const gchar *	e_m365_recipient_get_name		(EM365Recipient *recipient);
const gchar *	e_m365_recipient_get_address		(EM365Recipient *recipient);
void		e_m365_add_recipient			(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *name,
							 const gchar *address);

const gchar *	e_m365_internet_message_header_get_name	(EM365InternetMessageHeader *header);
const gchar *	e_m365_internet_message_header_get_value(EM365InternetMessageHeader *header);
void		e_m365_add_internet_message_header	(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *value);

EM365DateTimeWithZone *
		e_m365_followup_flag_get_completed_date_time
							(EM365FollowupFlag *flag);
void		e_m365_followup_flag_add_completed_date_time
							(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EM365DateTimeWithZone *
		e_m365_followup_flag_get_due_date_time	(EM365FollowupFlag *flag);
void		e_m365_followup_flag_add_due_date_time	(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EM365FollowupFlagStatusType
		e_m365_followup_flag_get_flag_status	(EM365FollowupFlag *flag);
void		e_m365_followup_flag_add_flag_status	(JsonBuilder *builder,
							 EM365FollowupFlagStatusType status);
EM365DateTimeWithZone *
		e_m365_followup_flag_get_start_date_time(EM365FollowupFlag *flag);
void		e_m365_followup_flag_add_start_date_time(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);

const gchar *	e_m365_item_body_get_content		(EM365ItemBody *item_body);
EM365ItemBodyContentTypeType
		e_m365_item_body_get_content_type	(EM365ItemBody *item_body);
void		e_m365_add_item_body			(JsonBuilder *builder,
							 const gchar *member_name,
							 EM365ItemBodyContentTypeType content_type,
							 const gchar *content);

JsonArray *	e_m365_mail_message_get_bcc_recipients	(EM365MailMessage *mail); /* EM365Recipient * */
void		e_m365_mail_message_begin_bcc_recipients(JsonBuilder *builder);
void		e_m365_mail_message_end_bcc_recipients	(JsonBuilder *builder);
EM365ItemBody *	e_m365_mail_message_get_body		(EM365MailMessage *mail);
void		e_m365_mail_message_add_body		(JsonBuilder *builder,
							 EM365ItemBodyContentTypeType content_type,
							 const gchar *content);
const gchar *	e_m365_mail_message_get_body_preview	(EM365MailMessage *mail);
JsonArray *	e_m365_mail_message_get_categories	(EM365MailMessage *mail); /* const gchar * */
void		e_m365_mail_message_begin_categories	(JsonBuilder *builder);
void		e_m365_mail_message_end_categories	(JsonBuilder *builder);
void		e_m365_mail_message_add_category	(JsonBuilder *builder,
							 const gchar *category);
JsonArray *	e_m365_mail_message_get_cc_recipients	(EM365MailMessage *mail); /* EM365Recipient * */
void		e_m365_mail_message_begin_cc_recipients	(JsonBuilder *builder);
void		e_m365_mail_message_end_cc_recipients	(JsonBuilder *builder);
const gchar *	e_m365_mail_message_get_change_key	(EM365MailMessage *mail);
const gchar *	e_m365_mail_message_get_conversation_id	(EM365MailMessage *mail);
JsonObject *	e_m365_mail_message_get_conversation_index
							(EM365MailMessage *mail);
time_t		e_m365_mail_message_get_created_date_time
							(EM365MailMessage *mail);
EM365FollowupFlag *
		e_m365_mail_message_get_flag		(EM365MailMessage *mail);
void		e_m365_mail_message_begin_flag		(JsonBuilder *builder);
void		e_m365_mail_message_end_flag		(JsonBuilder *builder);
EM365Recipient *
		e_m365_mail_message_get_from		(EM365MailMessage *mail);
void		e_m365_mail_message_add_from		(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
gboolean	e_m365_mail_message_get_has_attachments	(EM365MailMessage *mail);
const gchar *	e_m365_mail_message_get_id		(EM365MailMessage *mail);
EM365ImportanceType
		e_m365_mail_message_get_importance	(EM365MailMessage *mail);
void		e_m365_mail_message_add_importance	(JsonBuilder *builder,
							 EM365ImportanceType importance);
EM365InferenceClassificationType
		e_m365_mail_message_get_inference_classification
							(EM365MailMessage *mail);
JsonArray *	e_m365_mail_message_get_internet_message_headers
							(EM365MailMessage *mail); /* EM365InternetMessageHeader * */
void		e_m365_mail_message_begin_internet_message_headers
							(JsonBuilder *builder);
void		e_m365_mail_message_end_internet_message_headers
							(JsonBuilder *builder);
const gchar *	e_m365_mail_message_get_internet_message_id
							(EM365MailMessage *mail);
void		e_m365_mail_message_add_internet_message_id
							(JsonBuilder *builder,
							 const gchar *message_id);
gboolean	e_m365_mail_message_get_is_delivery_receipt_requested
							(EM365MailMessage *mail);
void		e_m365_mail_message_add_is_delivery_receipt_requested
							(JsonBuilder *builder,
							 gboolean value);
gboolean	e_m365_mail_message_get_is_draft	(EM365MailMessage *mail);
gboolean	e_m365_mail_message_get_is_read		(EM365MailMessage *mail);
void		e_m365_mail_message_add_is_read		(JsonBuilder *builder,
							 gboolean value);
gboolean	e_m365_mail_message_get_is_read_receipt_requested
							(EM365MailMessage *mail);
void		e_m365_mail_message_add_is_read_receipt_requested
							(JsonBuilder *builder,
							 gboolean value);
time_t		e_m365_mail_message_get_last_modified_date_time
							(EM365MailMessage *mail);
const gchar *	e_m365_mail_message_get_parent_folder_id(EM365MailMessage *mail);
time_t		e_m365_mail_message_get_received_date_time
							(EM365MailMessage *mail);
void		e_m365_mail_message_add_received_date_time
							(JsonBuilder *builder,
							 time_t value);
JsonArray *	e_m365_mail_message_get_reply_to	(EM365MailMessage *mail); /* EM365Recipient * */
void		e_m365_mail_message_begin_reply_to	(JsonBuilder *builder);
void		e_m365_mail_message_end_reply_to	(JsonBuilder *builder);
EM365Recipient *e_m365_mail_message_get_sender		(EM365MailMessage *mail);
void		e_m365_mail_message_add_sender		(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
time_t		e_m365_mail_message_get_sent_date_time	(EM365MailMessage *mail);
void		e_m365_mail_message_add_sent_date_time	(JsonBuilder *builder,
							 time_t value);
const gchar *	e_m365_mail_message_get_subject		(EM365MailMessage *mail);
void		e_m365_mail_message_add_subject		(JsonBuilder *builder,
							 const gchar *subject);
JsonArray *	e_m365_mail_message_get_to_recipients	(EM365MailMessage *mail); /* EM365Recipient * */
void		e_m365_mail_message_begin_to_recipients	(JsonBuilder *builder);
void		e_m365_mail_message_end_to_recipients	(JsonBuilder *builder);
EM365ItemBody *	e_m365_mail_message_get_unique_body	(EM365MailMessage *mail);
const gchar *	e_m365_mail_message_get_web_link	(EM365MailMessage *mail);

EM365AttachmentDataType
		e_m365_attachment_get_data_type		(EM365Attachment *attachment);
void		e_m365_attachment_begin_attachment	(JsonBuilder *builder,
							 EM365AttachmentDataType data_type);
void		e_m365_attachment_end_attachment	(JsonBuilder *builder);
const gchar *	e_m365_attachment_get_content_type	(EM365Attachment *attachment);
void		e_m365_attachment_add_content_type	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_attachment_get_id		(EM365Attachment *attachment);
gboolean	e_m365_attachment_get_is_inline		(EM365Attachment *attachment);
void		e_m365_attachment_add_is_inline		(JsonBuilder *builder,
							 gboolean value);
time_t		e_m365_attachment_get_last_modified_date_time
							(EM365Attachment *attachment);
void		e_m365_attachment_add_last_modified_date_time
							(JsonBuilder *builder,
							 time_t value);
const gchar *	e_m365_attachment_get_name		(EM365Attachment *attachment);
void		e_m365_attachment_add_name		(JsonBuilder *builder,
							 const gchar *value);
gint32		e_m365_attachment_get_size		(EM365Attachment *attachment);
void		e_m365_attachment_add_size		(JsonBuilder *builder,
							 gint32 value);
const gchar *	e_m365_file_attachment_get_content_bytes(EM365Attachment *attachment); /* base64-encoded */
void		e_m365_file_attachment_add_content_bytes(JsonBuilder *builder,
							 const gchar *base64_value);
const gchar *	e_m365_file_attachment_get_content_id	(EM365Attachment *attachment);
void		e_m365_file_attachment_add_content_id	(JsonBuilder *builder,
							 const gchar *value);

const gchar *	e_m365_email_address_get_name		(EM365EmailAddress *email);
const gchar *	e_m365_email_address_get_address	(EM365EmailAddress *email);
void		e_m365_add_email_address		(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *name,
							 const gchar *address);
const gchar *	e_m365_physical_address_get_city	(EM365PhysicalAddress *address);
const gchar *	e_m365_physical_address_get_country_or_region
							(EM365PhysicalAddress *address);
const gchar *	e_m365_physical_address_get_postal_code	(EM365PhysicalAddress *address);
const gchar *	e_m365_physical_address_get_state	(EM365PhysicalAddress *address);
const gchar *	e_m365_physical_address_get_street	(EM365PhysicalAddress *address);
void		e_m365_add_physical_address		(JsonBuilder *builder,
							 const gchar *member_name,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);

const gchar *	e_m365_contact_get_id			(EM365Contact *contact);
const gchar *	e_m365_contact_get_parent_folder_id	(EM365Contact *contact);
const gchar *	e_m365_contact_get_change_key		(EM365Contact *contact);
time_t		e_m365_contact_get_created_date_time	(EM365Contact *contact);
time_t		e_m365_contact_get_last_modified_date_time
							(EM365Contact *contact);
const gchar *	e_m365_contact_get_assistant_name	(EM365Contact *contact);
void		e_m365_contact_add_assistant_name	(JsonBuilder *builder,
							 const gchar *value);
time_t		e_m365_contact_get_birthday		(EM365Contact *contact,
							 gboolean *out_exists);
void		e_m365_contact_add_birthday		(JsonBuilder *builder,
							 time_t value);
EM365PhysicalAddress *
		e_m365_contact_get_business_address	(EM365Contact *contact);
void		e_m365_contact_add_business_address	(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
const gchar *	e_m365_contact_get_business_home_page	(EM365Contact *contact);
void		e_m365_contact_add_business_home_page	(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_m365_contact_get_business_phones	(EM365Contact *contact); /* const gchar * */
void		e_m365_contact_begin_business_phones	(JsonBuilder *builder);
void		e_m365_contact_end_business_phones	(JsonBuilder *builder);
void		e_m365_contact_add_business_phone	(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_m365_contact_get_categories		(EM365Contact *contact); /* const gchar * */
void		e_m365_contact_begin_categories		(JsonBuilder *builder);
void		e_m365_contact_end_categories		(JsonBuilder *builder);
void		e_m365_contact_add_category		(JsonBuilder *builder,
							 const gchar *category);
JsonArray *	e_m365_contact_get_children		(EM365Contact *contact); /* const gchar * */
void		e_m365_contact_begin_children		(JsonBuilder *builder);
void		e_m365_contact_end_children		(JsonBuilder *builder);
void		e_m365_contact_add_child		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_company_name		(EM365Contact *contact);
void		e_m365_contact_add_company_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_department		(EM365Contact *contact);
void		e_m365_contact_add_department		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_display_name		(EM365Contact *contact);
void		e_m365_contact_add_display_name		(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_m365_contact_get_email_addresses	(EM365Contact *contact); /* EM365EmailAddress * */
void		e_m365_contact_begin_email_addresses	(JsonBuilder *builder);
void		e_m365_contact_end_email_addresses	(JsonBuilder *builder);
const gchar *	e_m365_contact_get_file_as		(EM365Contact *contact);
void		e_m365_contact_add_file_as		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_generation		(EM365Contact *contact);
void		e_m365_contact_add_generation		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_given_name		(EM365Contact *contact);
void		e_m365_contact_add_given_name		(JsonBuilder *builder,
							 const gchar *value);
EM365PhysicalAddress *
		e_m365_contact_get_home_address		(EM365Contact *contact);
void		e_m365_contact_add_home_address		(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
JsonArray *	e_m365_contact_get_home_phones		(EM365Contact *contact); /* const gchar * */
void		e_m365_contact_begin_home_phones	(JsonBuilder *builder);
void		e_m365_contact_end_home_phones		(JsonBuilder *builder);
void		e_m365_contact_add_home_phone		(JsonBuilder *builder,
							 const gchar *value);
JsonArray *	e_m365_contact_get_im_addresses		(EM365Contact *contact); /* const gchar * */
void		e_m365_contact_begin_im_addresses	(JsonBuilder *builder);
void		e_m365_contact_end_im_addresses		(JsonBuilder *builder);
void		e_m365_contact_add_im_address		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_initials		(EM365Contact *contact);
void		e_m365_contact_add_initials		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_job_title		(EM365Contact *contact);
void		e_m365_contact_add_job_title		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_manager		(EM365Contact *contact);
void		e_m365_contact_add_manager		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_middle_name		(EM365Contact *contact);
void		e_m365_contact_add_middle_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_mobile_phone		(EM365Contact *contact);
void		e_m365_contact_add_mobile_phone		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_nick_name		(EM365Contact *contact);
void		e_m365_contact_add_nick_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_office_location	(EM365Contact *contact);
void		e_m365_contact_add_office_location	(JsonBuilder *builder,
							 const gchar *value);
EM365PhysicalAddress *
		e_m365_contact_get_other_address	(EM365Contact *contact);
void		e_m365_contact_add_other_address	(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
const gchar *	e_m365_contact_get_personal_notes	(EM365Contact *contact);
void		e_m365_contact_add_personal_notes	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_profession		(EM365Contact *contact);
void		e_m365_contact_add_profession		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_spouse_name		(EM365Contact *contact);
void		e_m365_contact_add_spouse_name		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_surname		(EM365Contact *contact);
void		e_m365_contact_add_surname		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_title		(EM365Contact *contact);
void		e_m365_contact_add_title		(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_yomi_company_name	(EM365Contact *contact);
void		e_m365_contact_add_yomi_company_name	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_yomi_given_name	(EM365Contact *contact);
void		e_m365_contact_add_yomi_given_name	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_contact_get_yomi_surname		(EM365Contact *contact);
void		e_m365_contact_add_yomi_surname		(JsonBuilder *builder,
							 const gchar *value);

JsonArray *	e_m365_contact_org_get_addresses	(EM365Contact *contact); /* EM365PhysicalAddress * */
const gchar *	e_m365_contact_org_get_mail		(EM365Contact *contact);
const gchar *	e_m365_contact_org_get_mail_nickname	(EM365Contact *contact);
JsonArray *	e_m365_contact_org_get_phones		(EM365Contact *contact); /* EM365Phone * */
JsonArray *	e_m365_contact_org_get_proxy_addresses	(EM365Contact *contact); /* const gchar * */

const gchar *	e_m365_contact_user_get_about_me	(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_city		(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_country		(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_fax_number	(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_mail		(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_mail_nickname	(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_mobile_phone	(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_my_site		(EM365Contact *contact);
JsonArray *	e_m365_contact_user_get_other_mails	(EM365Contact *contact); /* const gchar * */
const gchar *	e_m365_contact_user_get_postal_code	(EM365Contact *contact);
JsonArray *	e_m365_contact_user_get_proxy_addresses	(EM365Contact *contact); /* const gchar * */
const gchar *	e_m365_contact_user_get_state		(EM365Contact *contact);
const gchar *	e_m365_contact_user_get_street_address	(EM365Contact *contact);

const gchar *	e_m365_contact_person_get_im_address	(EM365Contact *contact);
gboolean	e_m365_contact_person_get_is_favorite	(EM365Contact *contact);
const gchar *	e_m365_contact_person_get_type_class	(EM365Contact *contact);
const gchar *	e_m365_contact_person_get_type_subclass	(EM365Contact *contact);
JsonArray *	e_m365_contact_person_get_phones	(EM365Contact *contact); /* EM365Phone * */
JsonArray *	e_m365_contact_person_get_postal_addresses			 /* EM365Location * */
							(EM365Contact *contact);
JsonArray *	e_m365_contact_person_get_scored_email_addresses		 /* EM365ScoredEmailAddress * */
							(EM365Contact *contact);
const gchar *	e_m365_contact_person_get_user_principal_name
							(EM365Contact *contact);
JsonArray *	e_m365_contact_person_get_websites	(EM365Contact *contact); /* EM365Website * */
const gchar *	e_m365_contact_person_get_yomi_company	(EM365Contact *contact);

const gchar *	e_m365_scored_email_address_get_address	(EM365ScoredEmailAddress *scored_email_address);
gdouble		e_m365_scored_email_address_get_relevance_score
							(EM365ScoredEmailAddress *scored_email_address);

const gchar *	e_m365_website_get_address		(EM365Website *website);
const gchar *	e_m365_website_get_display_name		(EM365Website *website);
EM365WebsiteType
		e_m365_website_get_type			(EM365Website *website);

const gchar *	e_m365_calendar_group_get_id		(EM365CalendarGroup *group);
const gchar *	e_m365_calendar_group_get_change_key	(EM365CalendarGroup *group);
const gchar *	e_m365_calendar_group_get_class_id	(EM365CalendarGroup *group);
const gchar *	e_m365_calendar_group_get_name		(EM365CalendarGroup *group);

const gchar *	e_m365_calendar_get_id			(EM365Calendar *calendar);
const gchar *	e_m365_calendar_get_change_key		(EM365Calendar *calendar);
gboolean	e_m365_calendar_get_can_edit		(EM365Calendar *calendar);
gboolean	e_m365_calendar_get_can_share		(EM365Calendar *calendar);
gboolean	e_m365_calendar_get_can_view_private_items
							(EM365Calendar *calendar);
gboolean	e_m365_calendar_get_is_removable	(EM365Calendar *calendar);
gboolean	e_m365_calendar_get_is_tallying_responses
							(EM365Calendar *calendar);
EM365EmailAddress *
		e_m365_calendar_get_owner		(EM365Calendar *calendar);
const gchar *	e_m365_calendar_get_name		(EM365Calendar *calendar);
void		e_m365_calendar_add_name		(JsonBuilder *builder,
							 const gchar *name);
guint32		e_m365_calendar_get_allowed_online_meeting_providers /* bit-or of EM365OnlineMeetingProviderType */
							(EM365Calendar *calendar);
void		e_m365_calendar_add_allowed_online_meeting_providers
							(JsonBuilder *builder,
							 guint providers); /* bit-or of EM365OnlineMeetingProviderType */
EM365CalendarColorType
		e_m365_calendar_get_color		(EM365Calendar *calendar);
void		e_m365_calendar_add_color		(JsonBuilder *builder,
							 EM365CalendarColorType color);
const gchar *	e_m365_calendar_get_hex_color		(EM365Calendar *calendar);
EM365OnlineMeetingProviderType
		e_m365_calendar_get_default_online_meeting_provider
							(EM365Calendar *calendar);
void		e_m365_calendar_add_default_online_meeting_provider
							(JsonBuilder *builder,
							 EM365OnlineMeetingProviderType provider);
guint32		e_m365_calendar_permission_get_allowed_roles /* bit-or of EM365CalendarPermissionType */
							(EM365CalendarPermission *permission);
EM365EmailAddress *
		e_m365_calendar_permission_get_email_address
							(EM365CalendarPermission *permission);
void		e_m365_calendar_permission_add_email_address
							(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
const gchar *	e_m365_calendar_permission_get_id	(EM365CalendarPermission *permission);
gboolean	e_m365_calendar_permission_get_is_inside_organization
							(EM365CalendarPermission *permission);
void		e_m365_calendar_permission_add_is_inside_organization
							(JsonBuilder *builder,
							 gboolean value);
gboolean	e_m365_calendar_permission_get_is_removable
							(EM365CalendarPermission *permission);
void		e_m365_calendar_permission_add_is_removable
							(JsonBuilder *builder,
							 gboolean value);
EM365CalendarPermissionType
		e_m365_calendar_permission_get_role	(EM365CalendarPermission *permission);
void		e_m365_calendar_permission_add_role
							(JsonBuilder *builder,
							 EM365CalendarPermissionType value);
EM365ResponseType
		e_m365_response_status_get_response	(EM365ResponseStatus *response_status);
time_t		e_m365_response_status_get_time		(EM365ResponseStatus *response_status);

EM365ResponseStatus *
		e_m365_attendee_get_status		(EM365Attendee *attendee);
EM365AttendeeType
		e_m365_attendee_get_type		(EM365Attendee *attendee);
EM365EmailAddress *
		e_m365_attendee_get_email_address	(EM365Attendee *attendee);

gdouble		e_m365_outlook_geo_coordinates_get_accuracy
							(EM365OutlookGeoCoordinates *coords);
void		e_m365_outlook_geo_coordinates_add_accuracy
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_m365_outlook_geo_coordinates_get_altitude
							(EM365OutlookGeoCoordinates *coords);
void		e_m365_outlook_geo_coordinates_add_altitude
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_m365_outlook_geo_coordinates_get_altitude_accuracy
							(EM365OutlookGeoCoordinates *coords);
void		e_m365_outlook_geo_coordinates_add_altitude_accuracy
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_m365_outlook_geo_coordinates_get_latitude
							(EM365OutlookGeoCoordinates *coords);
void		e_m365_outlook_geo_coordinates_add_latitude
							(JsonBuilder *builder,
							 gdouble value);
gdouble		e_m365_outlook_geo_coordinates_get_longitude
							(EM365OutlookGeoCoordinates *coords);
void		e_m365_outlook_geo_coordinates_add_longitude
							(JsonBuilder *builder,
							 gdouble value);
EM365PhysicalAddress *
		e_m365_location_get_address		(EM365Location *location);
void		e_m365_location_add_address		(JsonBuilder *builder,
							 const gchar *city,
							 const gchar *country_or_region,
							 const gchar *postal_code,
							 const gchar *state,
							 const gchar *street);
EM365OutlookGeoCoordinates *
		e_m365_location_get_coordinates		(EM365Location *location);
void		e_m365_location_begin_coordinates	(JsonBuilder *builder);
void		e_m365_location_end_coordinates		(JsonBuilder *builder);
const gchar *	e_m365_location_get_display_name	(EM365Location *location);
void		e_m365_location_add_display_name	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_location_get_email_address	(EM365Location *location);
void		e_m365_location_add_email_address	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_location_get_uri			(EM365Location *location);
void		e_m365_location_add_uri			(JsonBuilder *builder,
							 const gchar *value);
EM365LocationType
		e_m365_location_get_type		(EM365Location *location);

const gchar *	e_m365_phone_get_number			(EM365Phone *phone);
EM365PhoneType	e_m365_phone_get_type			(EM365Phone *phone);

const gchar *	e_m365_online_meeting_info_get_conference_id
							(EM365OnlineMeetingInfo *meeting_info);
const gchar *	e_m365_online_meeting_info_get_join_url	(EM365OnlineMeetingInfo *meeting_info);
JsonArray *	e_m365_online_meeting_info_get_phones	(EM365OnlineMeetingInfo *meeting_info); /* EM365Phone * */
const gchar *	e_m365_online_meeting_info_get_quick_dial
							(EM365OnlineMeetingInfo *meeting_info);
JsonArray *	e_m365_online_meeting_info_get_toll_free_numbers
							(EM365OnlineMeetingInfo *meeting_info); /* gchar * */
const gchar *	e_m365_online_meeting_info_get_toll_number
							(EM365OnlineMeetingInfo *meeting_info);

EM365DayOfWeekType
		e_m365_array_get_day_of_week_element	(JsonArray *array, /* const gchar * representing EM365DayOfWeekType */
							 guint index);
gint		e_m365_recurrence_pattern_get_day_of_month
							(EM365RecurrencePattern *pattern);
void		e_m365_recurrence_pattern_add_day_of_month
							(JsonBuilder *builder,
							 gint value);
JsonArray *	e_m365_recurrence_pattern_get_days_of_week
							(EM365RecurrencePattern *pattern); /* const gchar * representing EM365DayOfWeekType, use e_m365_array_get_day_of_week_element() */
void		e_m365_recurrence_pattern_begin_days_of_week
							(JsonBuilder *builder);
void		e_m365_recurrence_pattern_end_days_of_week
							(JsonBuilder *builder);
void		e_m365_recurrence_pattern_add_day_of_week
							(JsonBuilder *builder,
							 EM365DayOfWeekType value);
EM365DayOfWeekType
		e_m365_recurrence_pattern_get_first_day_of_week
							(EM365RecurrencePattern *pattern);
void		e_m365_recurrence_pattern_add_first_day_of_week
							(JsonBuilder *builder,
							 EM365DayOfWeekType value);
EM365WeekIndexType
		e_m365_recurrence_pattern_get_index	(EM365RecurrencePattern *pattern);
void		e_m365_recurrence_pattern_add_index	(JsonBuilder *builder,
							 EM365WeekIndexType value);
gint		e_m365_recurrence_pattern_get_interval	(EM365RecurrencePattern *pattern);
void		e_m365_recurrence_pattern_add_interval	(JsonBuilder *builder,
							 gint value);
gint		e_m365_recurrence_pattern_get_month	(EM365RecurrencePattern *pattern);
void		e_m365_recurrence_pattern_add_month	(JsonBuilder *builder,
							 gint value);
EM365RecurrencePatternType
		e_m365_recurrence_pattern_get_type	(EM365RecurrencePattern *pattern);
void		e_m365_recurrence_pattern_add_type	(JsonBuilder *builder,
							 EM365RecurrencePatternType value);

EM365Date	e_m365_recurrence_range_get_end_date	(EM365RecurrenceRange *range);
void		e_m365_recurrence_range_add_end_date	(JsonBuilder *builder,
							 EM365Date value);
gint		e_m365_recurrence_range_get_number_of_occurrences
							(EM365RecurrenceRange *range);
void		e_m365_recurrence_range_add_number_of_occurrences
							(JsonBuilder *builder,
							 gint value);
const gchar *	e_m365_recurrence_range_get_recurrence_time_zone
							(EM365RecurrenceRange *range);
void		e_m365_recurrence_range_add_recurrence_time_zone
							(JsonBuilder *builder,
							 const gchar *value);
EM365Date	e_m365_recurrence_range_get_start_date	(EM365RecurrenceRange *range);
void		e_m365_recurrence_range_add_start_date	(JsonBuilder *builder,
							 EM365Date value);
EM365RecurrenceRangeType
		e_m365_recurrence_range_get_type	(EM365RecurrenceRange *range);
void		e_m365_recurrence_range_add_type	(JsonBuilder *builder,
							 EM365RecurrenceRangeType value);

EM365RecurrencePattern *
		e_m365_patterned_recurrence_get_pattern	(EM365PatternedRecurrence *patterned_recurrence);
void		e_m365_patterned_recurrence_begin_pattern
							(JsonBuilder *builder);
void		e_m365_patterned_recurrence_end_pattern
							(JsonBuilder *builder);
EM365RecurrenceRange *
		e_m365_patterned_recurrence_get_range	(EM365PatternedRecurrence *patterned_recurrence);
void		e_m365_patterned_recurrence_begin_range
							(JsonBuilder *builder);
void		e_m365_patterned_recurrence_end_range
							(JsonBuilder *builder);

const gchar *	e_m365_event_get_id			(EM365Event *event);
const gchar *	e_m365_event_get_change_key		(EM365Event *event);
JsonArray *	e_m365_event_get_attendees		(EM365Event *event); /* EM365Attendee * */
void		e_m365_event_begin_attendees		(JsonBuilder *builder);
void		e_m365_event_end_attendees		(JsonBuilder *builder);
void		e_m365_event_add_attendee		(JsonBuilder *builder,
							 EM365AttendeeType type,
							 EM365ResponseType response,
							 time_t response_time,
							 const gchar *name,
							 const gchar *address);
void		e_m365_event_add_null_attendees		(JsonBuilder *builder);
EM365ItemBody *	e_m365_event_get_body			(EM365Event *event);
void		e_m365_event_add_body			(JsonBuilder *builder,
							 EM365ItemBodyContentTypeType content_type,
							 const gchar *content);
const gchar *	e_m365_event_get_body_preview		(EM365Event *event);
JsonArray *	e_m365_event_get_categories		(EM365Event *event); /* const gchar * */
void		e_m365_event_begin_categories		(JsonBuilder *builder);
void		e_m365_event_end_categories		(JsonBuilder *builder);
void		e_m365_event_add_category		(JsonBuilder *builder,
							 const gchar *category);
time_t		e_m365_event_get_created_date_time	(EM365Event *event);
EM365DateTimeWithZone *
		e_m365_event_get_end			(EM365Event *event);
void		e_m365_event_add_end			(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
gboolean	e_m365_event_get_has_attachments	(EM365Event *event);
const gchar *	e_m365_event_get_ical_uid		(EM365Event *event);
EM365ImportanceType
		e_m365_event_get_importance		(EM365Event *event);
void		e_m365_event_add_importance		(JsonBuilder *builder,
							 EM365ImportanceType value);
gboolean	e_m365_event_get_is_all_day		(EM365Event *event);
void		e_m365_event_add_is_all_day		(JsonBuilder *builder,
							 gboolean value);
gboolean	e_m365_event_get_is_cancelled		(EM365Event *event);
gboolean	e_m365_event_get_is_online_meeting	(EM365Event *event);
void		e_m365_event_add_is_online_meeting	(JsonBuilder *builder,
							 gboolean value);
gboolean	e_m365_event_get_is_organizer		(EM365Event *event);
gboolean	e_m365_event_get_is_reminder_on		(EM365Event *event);
void		e_m365_event_add_is_reminder_on		(JsonBuilder *builder,
							 gboolean value);
time_t		e_m365_event_get_last_modified_date_time(EM365Event *event);
EM365Location *	e_m365_event_get_location		(EM365Event *event);
void		e_m365_event_begin_location		(JsonBuilder *builder);
void		e_m365_event_end_location		(JsonBuilder *builder);
void		e_m365_event_add_null_location		(JsonBuilder *builder);
JsonArray *	e_m365_event_get_locations		(EM365Event *event); /* EM365Location * */
void		e_m365_event_begin_locations		(JsonBuilder *builder);
void		e_m365_event_end_locations		(JsonBuilder *builder);
void		e_m365_event_begin_locations_location	(JsonBuilder *builder);
void		e_m365_event_end_locations_location	(JsonBuilder *builder);
EM365OnlineMeetingInfo *
		e_m365_event_get_online_meeting_info	(EM365Event *event);
EM365OnlineMeetingProviderType
		e_m365_event_get_online_meeting_provider(EM365Event *event);
void		e_m365_event_add_online_meeting_provider(JsonBuilder *builder,
							 EM365OnlineMeetingProviderType value);
const gchar *	e_m365_event_get_online_meeting_url	(EM365Event *event);
EM365Recipient *e_m365_event_get_organizer		(EM365Event *event);
void		e_m365_event_add_organizer		(JsonBuilder *builder,
							 const gchar *name,
							 const gchar *address);
void		e_m365_event_add_null_organizer		(JsonBuilder *builder);
const gchar *	e_m365_event_get_original_end_timezone	(EM365Event *event);
time_t		e_m365_event_get_original_start		(EM365Event *event);
const gchar *	e_m365_event_get_original_start_timezone(EM365Event *event);
EM365PatternedRecurrence *
		e_m365_event_get_recurrence		(EM365Event *event);
void		e_m365_event_begin_recurrence		(JsonBuilder *builder);
void		e_m365_event_end_recurrence		(JsonBuilder *builder);
void		e_m365_event_add_null_recurrence	(JsonBuilder *builder);
gint		e_m365_event_get_reminder_minutes_before_start
							(EM365Event *event);
void		e_m365_event_add_reminder_minutes_before_start
							(JsonBuilder *builder,
							 gint value);
gboolean	e_m365_event_get_response_requested	(EM365Event *event);
void		e_m365_event_add_response_requested	(JsonBuilder *builder,
							 gboolean value);
EM365ResponseStatus *
		e_m365_event_get_response_status	(EM365Event *event);
EM365SensitivityType
		e_m365_event_get_sensitivity		(EM365Event *event);
void		e_m365_event_add_sensitivity		(JsonBuilder *builder,
							 EM365SensitivityType value);
const gchar *	e_m365_event_get_series_master_id	(EM365Event *event);
EM365FreeBusyStatusType
		e_m365_event_get_show_as		(EM365Event *event);
void		e_m365_event_add_show_as		(JsonBuilder *builder,
							 EM365FreeBusyStatusType value);
EM365DateTimeWithZone *
		e_m365_event_get_start			(EM365Event *event);
void		e_m365_event_add_start			(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
const gchar *	e_m365_event_get_subject		(EM365Event *event);
void		e_m365_event_add_subject		(JsonBuilder *builder,
							 const gchar *value);
EM365EventTypeType
		e_m365_event_get_type			(EM365Event *event);
const gchar *	e_m365_event_get_web_link		(EM365Event *event);

const gchar *	e_m365_free_busy_error_get_message	(EM365FreeBusyError *fberror);
const gchar *	e_m365_free_busy_error_get_response_code(EM365FreeBusyError *fberror);

EM365DateTimeWithZone *
		e_m365_schedule_item_get_end		(EM365ScheduleItem *schitem);
gboolean	e_m365_schedule_item_get_is_private	(EM365ScheduleItem *schitem);
const gchar *	e_m365_schedule_item_get_location	(EM365ScheduleItem *schitem);
EM365DateTimeWithZone *
		e_m365_schedule_item_get_start		(EM365ScheduleItem *schitem);
EM365FreeBusyStatusType
		e_m365_schedule_item_get_status		(EM365ScheduleItem *schitem);
const gchar *	e_m365_schedule_item_get_subject	(EM365ScheduleItem *schitem);

JsonArray *	e_m365_working_hours_get_days_of_week	(EM365WorkingHours *wrkhrs); /* Use e_m365_array_get_day_of_week_element() to get the items */
EM365TimeOfDay	e_m365_working_hours_get_start_time	(EM365WorkingHours *wrkhrs);
EM365TimeOfDay	e_m365_working_hours_get_end_time	(EM365WorkingHours *wrkhrs);
const gchar *	e_m365_working_hours_get_time_zone_name	(EM365WorkingHours *wrkhrs);

const gchar *	e_m365_schedule_information_get_availability_view
							(EM365ScheduleInformation *schinfo);
EM365FreeBusyError *
		e_m365_schedule_information_get_free_busy_error
							(EM365ScheduleInformation *schinfo);
const gchar *	e_m365_schedule_information_get_schedule_id
							(EM365ScheduleInformation *schinfo);
JsonArray *	e_m365_schedule_information_get_schedule_items /* EM365ScheduleItem * */
							(EM365ScheduleInformation *schinfo);
EM365WorkingHours *
		e_m365_schedule_information_get_working_hours
							(EM365ScheduleInformation *schinfo);

const gchar *	e_m365_task_list_get_id			(EM365TaskList *list);
const gchar *	e_m365_task_list_get_display_name	(EM365TaskList *list);
void		e_m365_task_list_add_display_name	(JsonBuilder *builder,
							 const gchar *display_name);
gboolean	e_m365_task_list_get_is_owner		(EM365TaskList *list);
gboolean	e_m365_task_list_get_is_shared		(EM365TaskList *list);
EM365TaskListKind
		e_m365_task_list_get_kind		(EM365TaskList *list);

const gchar *	e_m365_task_get_id			(EM365Task *task);
void		e_m365_task_add_id			(JsonBuilder *builder,
							 const gchar *value);
EM365ItemBody *	e_m365_task_get_body			(EM365Task *task);
void		e_m365_task_add_body			(JsonBuilder *builder,
							 EM365ItemBodyContentTypeType content_type,
							 const gchar *content);
time_t		e_m365_task_get_body_last_modified_date_time
							(EM365Task *task);
void		e_m365_task_add_boady_last_modified_date_time
							(JsonBuilder *builder,
							 time_t value);
JsonArray *	e_m365_task_get_categories		(EM365Task *task); /* const gchar * */
void		e_m365_task_begin_categories		(JsonBuilder *builder);
void		e_m365_task_end_categories		(JsonBuilder *builder);
void		e_m365_task_add_category		(JsonBuilder *builder,
							 const gchar *category);
EM365DateTimeWithZone *
		e_m365_task_get_completed_date_time	(EM365Task *task);
void		e_m365_task_add_completed_date_time	(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
time_t		e_m365_task_get_created_date_time	(EM365Task *task);
void		e_m365_task_add_created_date_time	(JsonBuilder *builder,
							 time_t value);
EM365DateTimeWithZone *
		e_m365_task_get_due_date_time		(EM365Task *task);
void		e_m365_task_add_due_date_time		(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EM365ImportanceType
		e_m365_task_get_importance		(EM365Task *task);
void		e_m365_task_add_importance		(JsonBuilder *builder,
							 EM365ImportanceType value);
gboolean	e_m365_task_get_is_reminder_on		(EM365Task *task);
void		e_m365_task_add_is_reminder_on		(JsonBuilder *builder,
							 gboolean value);
time_t		e_m365_task_get_last_modified_date_time	(EM365Task *task);
const gchar *	e_m365_task_get_last_modified_as_string	(EM365Task *task);
void		e_m365_task_add_last_modified_date_time	(JsonBuilder *builder,
							 time_t value);
EM365PatternedRecurrence *
		e_m365_task_get_recurrence		(EM365Task *task);
void		e_m365_task_begin_recurrence		(JsonBuilder *builder);
void		e_m365_task_end_recurrence		(JsonBuilder *builder);
void		e_m365_task_add_null_recurrence		(JsonBuilder *builder);
EM365DateTimeWithZone *
		e_m365_task_get_reminder_date_time	(EM365Task *task);
void		e_m365_task_add_reminder_date_time	(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EM365DateTimeWithZone *
		e_m365_task_get_start_date_time		(EM365Task *task);
void		e_m365_task_add_start_date_time		(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EM365StatusType	e_m365_task_get_status			(EM365Task *task);
void		e_m365_task_add_status			(JsonBuilder *builder,
							 EM365StatusType value);
const gchar *	e_m365_task_get_title			(EM365Task *task);
void		e_m365_task_add_title			(JsonBuilder *builder,
							 const gchar *value);

const gchar *	e_m365_checklist_item_get_id		(EM365ChecklistItem *item);
time_t		e_m365_checklist_item_get_checked_date_time
							(EM365ChecklistItem *item);
void		e_m365_checklist_item_add_checked_date_time
							(JsonBuilder *builder,
							 time_t value);
time_t		e_m365_checklist_item_get_created_date_time
							(EM365ChecklistItem *item);
void		e_m365_checklist_item_add_created_date_time
							(JsonBuilder *builder,
							 time_t value);
const gchar *	e_m365_checklist_item_get_display_name	(EM365ChecklistItem *item);
void		e_m365_checklist_item_add_display_name	(JsonBuilder *builder,
							 const gchar *value);
gboolean	e_m365_checklist_item_get_is_checked	(EM365ChecklistItem *item);
void		e_m365_checklist_item_add_is_checked	(JsonBuilder *builder,
							 gboolean value);

const gchar *	e_m365_linked_resource_get_id		(EM365LinkedResource *resource);
const gchar *	e_m365_linked_resource_get_application_name
							(EM365LinkedResource *resource);
void		e_m365_linked_resource_add_application_name
							(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_linked_resource_get_display_name	(EM365LinkedResource *resource);
void		e_m365_linked_resource_add_display_name	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_linked_resource_get_external_id	(EM365LinkedResource *resource);
void		e_m365_linked_resource_add_external_id	(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_linked_resource_get_web_url	(EM365LinkedResource *resource);
void		e_m365_linked_resource_add_web_url	(JsonBuilder *builder,
							 const gchar *value);
EM365AutomaticRepliesSetting *
		e_m365_mailbox_settings_get_automatic_replies_setting
							(EM365MailboxSettings *malbox_settings);
void		e_m365_begin_mailbox_settings		(JsonBuilder *builder);
void		e_m365_end_mailbox_settings		(JsonBuilder *builder);

void		e_m365_begin_automatic_replies_setting	(JsonBuilder *builder);
void		e_m365_end_automatic_replies_setting	(JsonBuilder *builder);
EM365ExternalAudienceScopeType
		e_m365_automatic_replies_setting_get_external_audience
							(EM365AutomaticRepliesSetting *automatic_replies_setting);
void		e_m365_automatic_replies_setting_add_external_audience
							(JsonBuilder *builder,
							 EM365ExternalAudienceScopeType value);
const gchar *	e_m365_automatic_replies_setting_get_external_reply_message
							(EM365AutomaticRepliesSetting *automatic_replies_setting);
void		e_m365_automatic_replies_setting_add_external_reply_message
							(JsonBuilder *builder,
							 const gchar *value);
const gchar *	e_m365_automatic_replies_setting_get_internal_reply_message
							(EM365AutomaticRepliesSetting *automatic_replies_setting);
void		e_m365_automatic_replies_setting_add_internal_reply_message
							(JsonBuilder *builder,
							 const gchar *value);
EM365DateTimeWithZone *
		e_m365_automatic_replies_setting_get_scheduled_end_date_time
							(EM365AutomaticRepliesSetting *automatic_replies_setting);
void		e_m365_automatic_replies_setting_add_scheduled_end_date_time
							(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EM365DateTimeWithZone *
		e_m365_automatic_replies_setting_get_scheduled_start_date_time
							(EM365AutomaticRepliesSetting *automatic_replies_setting);
void		e_m365_automatic_replies_setting_add_scheduled_start_date_time
							(JsonBuilder *builder,
							 time_t date_time,
							 const gchar *zone);
EM365AutomaticRepliesStatusType
		e_m365_automatic_replies_setting_get_status
							(EM365AutomaticRepliesSetting *automatic_replies_setting);
void		e_m365_automatic_replies_setting_add_status
							(JsonBuilder *builder,
							 EM365AutomaticRepliesStatusType value);

G_END_DECLS

#endif /* E_M365_JSON_UTILS_H */
