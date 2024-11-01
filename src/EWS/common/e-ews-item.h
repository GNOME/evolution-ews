/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_ITEM_H
#define E_EWS_ITEM_H

#include "e-soap-request.h"
#include "e-soap-response.h"
#include "e-ews-request.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_ITEM            (e_ews_item_get_type ())
#define E_EWS_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_ITEM, EEwsItem))
#define E_EWS_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_ITEM, EEwsItemClass))
#define E_IS_EWS_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_ITEM))
#define E_IS_EWS_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_ITEM))

typedef struct _EEwsItem        EEwsItem;
typedef struct _EEwsItemClass   EEwsItemClass;
typedef struct _EEwsItemPrivate EEwsItemPrivate;

typedef enum {
	E_EWS_BODY_TYPE_ANY,
	E_EWS_BODY_TYPE_BEST,
	E_EWS_BODY_TYPE_HTML,
	E_EWS_BODY_TYPE_TEXT
} EEwsBodyType;

typedef enum {
	E_EWS_ITEM_TYPE_UNKNOWN,
	E_EWS_ITEM_TYPE_MESSAGE,
	E_EWS_ITEM_TYPE_POST_ITEM,
	E_EWS_ITEM_TYPE_EVENT,
	E_EWS_ITEM_TYPE_CONTACT,
	E_EWS_ITEM_TYPE_GROUP,
	E_EWS_ITEM_TYPE_MEETING_MESSAGE,
	E_EWS_ITEM_TYPE_MEETING_REQUEST,
	E_EWS_ITEM_TYPE_MEETING_RESPONSE,
	E_EWS_ITEM_TYPE_MEETING_CANCELLATION,
	E_EWS_ITEM_TYPE_TASK,
	E_EWS_ITEM_TYPE_MEMO,
	E_EWS_ITEM_TYPE_GENERIC_ITEM,
	E_EWS_ITEM_TYPE_ERROR
} EEwsItemType;

typedef enum {
	EWS_ITEM_LOW,
	EWS_ITEM_NORMAL,
	EWS_ITEM_HIGH
} EwsImportance;

struct _EEwsItem {
	GObject parent;
	EEwsItemPrivate *priv;
};

struct _EEwsItemClass {
	GObjectClass parent_class;
};

typedef struct {
	gchar *id;
	gchar *change_key;
} EwsId;

typedef struct {
	gchar *name;
	gchar *email;
	gchar *routing_type;
	gchar *mailbox_type;
	EwsId *item_id;
} EwsMailbox;

typedef struct {
	EwsMailbox *mailbox;
	gchar *attendeetype;
	gchar *responsetype;
} EwsAttendee;

typedef struct {
	gchar *title;
	gchar *first_name;
	gchar *middle_name;
	gchar *last_name;
	gchar *suffix;
	gchar *initials;
	gchar *full_name;
	gchar *nick_name;
	gchar *yomi_first_name;
	gchar *yomi_last_name;
} EwsCompleteName;

typedef struct {
	gchar *street;
	gchar *city;
	gchar *state;
	gchar *country;
	gchar *postal_code;
} EwsAddress;

typedef struct {
	gchar *filename;
	gchar *mime_type;
	gsize length;
	gchar *data;
} EEwsAttachmentInline;

typedef enum {
	E_EWS_ATTACHMENT_INFO_TYPE_INLINED,
	E_EWS_ATTACHMENT_INFO_TYPE_URI
} EEwsAttachmentInfoType;

typedef struct {
	EEwsAttachmentInfoType type;
	union {
		EEwsAttachmentInline inlined;
		gchar *uri;
	} data;
	gchar *prefer_filename;
	gchar *id;
} EEwsAttachmentInfo;

typedef enum {
	E_EWS_PERMISSION_BIT_FREE_BUSY_DETAILED	= 0x00001000,
	E_EWS_PERMISSION_BIT_FREE_BUSY_SIMPLE	= 0x00000800,
	E_EWS_PERMISSION_BIT_FOLDER_VISIBLE	= 0x00000400,
	E_EWS_PERMISSION_BIT_FOLDER_CONTACT	= 0x00000200,
	E_EWS_PERMISSION_BIT_FOLDER_OWNER	= 0x00000100,
	E_EWS_PERMISSION_BIT_CREATE_SUBFOLDER	= 0x00000080,
	E_EWS_PERMISSION_BIT_DELETE_ANY		= 0x00000040,
	E_EWS_PERMISSION_BIT_EDIT_ANY		= 0x00000020,
	E_EWS_PERMISSION_BIT_DELETE_OWNED	= 0x00000010,
	E_EWS_PERMISSION_BIT_EDIT_OWNED		= 0x00000008,
	E_EWS_PERMISSION_BIT_CREATE		= 0x00000002,
	E_EWS_PERMISSION_BIT_READ_ANY		= 0x00000001
} EEwsPermissionBits;

typedef enum {
	E_EWS_PERMISSION_USER_TYPE_NONE		= 0,
	E_EWS_PERMISSION_USER_TYPE_ANONYMOUS	= 1 << 1, /* anonymous user */
	E_EWS_PERMISSION_USER_TYPE_DEFAULT	= 1 << 2, /* default rights for any users */
	E_EWS_PERMISSION_USER_TYPE_REGULAR	= 1 << 3  /* regular user, the EEwsPermission::user_smtp member is valid */
} EEwsPermissionUserType;

typedef struct {
	EEwsPermissionUserType user_type;	/* whether is distinguished name, if 'true' */
	gchar *display_name;			/* display name for a user */
	gchar *primary_smtp;			/* valid only for E_EWS_PERMISSION_USER_TYPE_REGULAR */
	gchar *sid;				/* security identifier (SID), if any */
	guint32 rights;				/* EEwsPermissionBits for the user */
} EEwsPermission;

typedef enum {
	E_EWS_RECURRENCE_UNKNOWN = 0,
	E_EWS_RECURRENCE_RELATIVE_YEARLY,
	E_EWS_RECURRENCE_ABSOLUTE_YEARLY,
	E_EWS_RECURRENCE_RELATIVE_MONTHLY,
	E_EWS_RECURRENCE_ABSOLUTE_MONTHLY,
	E_EWS_RECURRENCE_WEEKLY,
	E_EWS_RECURRENCE_DAILY,
	E_EWS_RECURRENCE_DAILY_REGENERATION,
	E_EWS_RECURRENCE_WEEKLY_REGENERATION,
	E_EWS_RECURRENCE_MONTHLY_REGENERATION,
	E_EWS_RECURRENCE_YEARLY_REGENERATION
} EEwsRecurrenceType;

typedef enum {
	E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN		= 0,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY		= 1 << 0,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY		= 1 << 1,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY		= 1 << 2,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY		= 1 << 3,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY		= 1 << 4,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY		= 1 << 5,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY		= 1 << 6,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_DAY		= 1 << 7,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKDAY		= 1 << 8,
	E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKENDDAY	= 1 << 9
} EEwsRecurrenceDaysOfWeek;

typedef enum {
	E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN = 0,
	E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FIRST,
	E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_SECOND,
	E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_THIRD,
	E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FOURTH,
	E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST
} EEwsRecurrenceDayOfWeekIndex;

typedef enum {
	E_EWS_RECURRENCE_END_UNKNOWN = 0,
	E_EWS_RECURRENCE_END_NO_END,
	E_EWS_RECURRENCE_END_DATE,
	E_EWS_RECURRENCE_END_NUMBERED
} EEwsRecurrenceEndType;

typedef struct {
	EEwsRecurrenceType type;
	union _recur {
		struct _relative_yearly {
			guint32 days_of_week; /* bit-or of EEwsRecurrenceDaysOfWeek */
			EEwsRecurrenceDayOfWeekIndex day_of_week_index;
			GDateMonth month; /* 1..12 */
		} relative_yearly;

		struct _absolute_yearly {
			gint day_of_month; /* 1..31 */
			GDateMonth month; /* 1..12 */
		} absolute_yearly;

		struct _relative_monthly {
			gint interval; /* 1..99 */
			guint32 days_of_week; /* bit-or of EEwsRecurrenceDaysOfWeek */
			EEwsRecurrenceDayOfWeekIndex day_of_week_index;
		} relative_monthly;

		struct _absolute_monthly {
			gint interval; /* 1..99 */
			gint day_of_month; /* 1..31 */
		} absolute_monthly;

		struct _weekly {
			gint interval; /* 1..99 */
			guint32 days_of_week; /* bit-or of EEwsRecurrenceDaysOfWeek */
			GDateWeekday first_day_of_week; /* Exchange 2013 server and above */
		} weekly;

		/* Used for daily, daily_regeneration, weekly_regeneration, monthly_regeneration and yearly_regeneration */
		gint interval; /* 1..999 */
	} recur;

	EEwsRecurrenceEndType end_type;
	time_t utc_start_date;
	union _end {
		time_t utc_end_date;
		gint number_of_occurrences;
	} end;
} EEwsRecurrence;

GType		e_ews_item_get_type (void);
EEwsItem *	e_ews_item_new_from_soap_parameter
						(ESoapParameter *param);
EEwsItem *	e_ews_item_new_from_error	(const GError *error);

EEwsItemType	e_ews_item_get_item_type	(EEwsItem *item);
void		e_ews_item_set_item_type	(EEwsItem *item,
						 EEwsItemType new_type);
const GError *	e_ews_item_get_error		(EEwsItem *item);
void		e_ews_item_set_error		(EEwsItem *item,
						 const GError *error);
const gchar *	e_ews_item_get_subject		(EEwsItem *item);
void		e_ews_item_set_subject		(EEwsItem *item,
						 const gchar *new_subject);
const gchar *	e_ews_item_get_mime_content	(EEwsItem *item);
void		e_ews_item_set_mime_content	(EEwsItem *item,
						 const gchar *new_mime_content);
const EwsId *	e_ews_item_get_id		(EEwsItem *item);
const EwsId *	e_ews_item_get_attachment_id	(EEwsItem *item);
gsize		e_ews_item_get_size		(EEwsItem *item);
const gchar *	e_ews_item_get_msg_id		(EEwsItem *item);
const gchar *	e_ews_item_get_uid		(EEwsItem *item);
const gchar *	e_ews_item_get_in_replyto	(EEwsItem *item);
const gchar *	e_ews_item_get_references	(EEwsItem *item);
const gchar *	e_ews_item_get_preview		(EEwsItem *item);
const gchar *	e_ews_item_get_date_header	(EEwsItem *item);
time_t		e_ews_item_get_date_received	(EEwsItem *item);
time_t		e_ews_item_get_date_sent	(EEwsItem *item);
time_t		e_ews_item_get_date_created	(EEwsItem *item);
time_t		e_ews_item_get_last_modified_time
						(EEwsItem *item);
gboolean	e_ews_item_has_attachments	(EEwsItem *item,
						 gboolean *has_attachments);
gboolean	e_ews_item_is_read		(EEwsItem *item,
						 gboolean *is_read);
gboolean	e_ews_item_is_forwarded		(EEwsItem *item,
						 gboolean *is_forwarded);
gboolean	e_ews_item_is_answered		(EEwsItem *item,
						 gboolean *is_answered);
guint32		e_ews_item_get_message_flags	(EEwsItem *item);
const GSList *	e_ews_item_get_to_recipients	(EEwsItem *item);
const GSList *	e_ews_item_get_cc_recipients	(EEwsItem *item);
const GSList *	e_ews_item_get_bcc_recipients	(EEwsItem *item);
const EwsMailbox *
		e_ews_item_get_sender		(EEwsItem *item);
const EwsMailbox *
		e_ews_item_get_from		(EEwsItem *item);
EwsImportance
		e_ews_item_get_importance	(EEwsItem *item);
const GSList *
		e_ews_item_get_categories	(EEwsItem *item);
gboolean	e_ews_item_get_reminder_is_set	(EEwsItem *item);
time_t		e_ews_item_get_reminder_due_by	(EEwsItem *item);
gint		e_ews_item_get_reminder_minutes_before_start
						(EEwsItem *item);
gboolean	e_ews_item_get_recurrence	(EEwsItem *item,
						 EEwsRecurrence *out_recurrence);
EwsMailbox *
		e_ews_item_mailbox_from_soap_param
						(ESoapParameter *param);
void		e_ews_mailbox_free		(EwsMailbox *mailbox);

gboolean	e_ews_item_get_is_meeting	(EEwsItem *item);
gboolean	e_ews_item_get_is_response_requested
						(EEwsItem *item);
const GSList *	e_ews_item_get_modified_occurrences
						(EEwsItem *item);
gchar *		e_ews_embed_attachment_id_in_uri (const gchar *olduri, const gchar *attach_id);
GSList *	e_ews_item_get_attachments_ids
						(EEwsItem *item);
const gchar *	e_ews_item_get_extended_tag	(EEwsItem *item,
						 guint32 prop_tag);
const gchar *	e_ews_item_get_extended_distinguished_tag
						(EEwsItem *item,
						 const gchar *set_id,
						 guint32 prop_id);
gboolean	e_ews_item_get_extended_property_as_boolean
						(EEwsItem *item,
						 const gchar *set_id,
						 guint32 prop_id_or_tag,
						 gboolean *found);
gint		e_ews_item_get_extended_property_as_int
						(EEwsItem *item,
						 const gchar *set_id,
						 guint32 prop_id_or_tag,
						 gboolean *found);
gdouble		e_ews_item_get_extended_property_as_double
						(EEwsItem *item,
						 const gchar *set_id,
						 guint32 prop_id_or_tag,
						 gboolean *found);
const gchar *	e_ews_item_get_extended_property_as_string
						(EEwsItem *item,
						 const gchar *set_id,
						 guint32 prop_id_or_tag,
						 gboolean *found);
time_t		e_ews_item_get_extended_property_as_time
						(EEwsItem *item,
						 const gchar *set_id,
						 guint32 prop_id_or_tag,
						 gboolean *found);

EEwsAttachmentInfo *
e_ews_dump_file_attachment_from_soap_parameter (ESoapParameter *param, const gchar *cache, const gchar *comp_uid);

EEwsAttachmentInfo *
e_ews_item_dump_mime_content (EEwsItem *item, const gchar *cache);

const gchar *	e_ews_item_get_my_response_type	(EEwsItem *item);
const GSList *	e_ews_item_get_attendees	(EEwsItem *item);

const EwsId *	e_ews_item_get_calendar_item_accept_id
						(EEwsItem *item);

EEwsAttachmentInfo *
		e_ews_attachment_info_new	(EEwsAttachmentInfoType type);
void		e_ews_attachment_info_free	(EEwsAttachmentInfo *info);
EEwsAttachmentInfoType
		e_ews_attachment_info_get_type	(EEwsAttachmentInfo *info);
const gchar *	e_ews_attachment_info_get_prefer_filename
						(EEwsAttachmentInfo *info);
void		e_ews_attachment_info_set_prefer_filename
						(EEwsAttachmentInfo *info,
						 const gchar *prefer_filename);
const gchar *	e_ews_attachment_info_get_inlined_data
						(EEwsAttachmentInfo *info,
						 gsize *len);
void		e_ews_attachment_info_set_inlined_data
						(EEwsAttachmentInfo *info,
						 const guchar *data,
						 gsize len);
const gchar *	e_ews_attachment_info_get_mime_type
						(EEwsAttachmentInfo *info);
void		e_ews_attachment_info_set_mime_type
						(EEwsAttachmentInfo *info,
						 const gchar *mime_type);
const gchar *	e_ews_attachment_info_get_filename
						(EEwsAttachmentInfo *info);
void		e_ews_attachment_info_set_filename
						(EEwsAttachmentInfo *info,
						 const gchar *filename);
const gchar *	e_ews_attachment_info_get_uri	(EEwsAttachmentInfo *info);
void		e_ews_attachment_info_set_uri	(EEwsAttachmentInfo *info,
						 const gchar *uri);
const gchar *	e_ews_attachment_info_get_id	(EEwsAttachmentInfo *info);
void		e_ews_attachment_info_set_id	(EEwsAttachmentInfo *info,
						 const gchar *id);


/* Contact fields */
const gchar *	e_ews_item_get_fileas		(EEwsItem *item);
const EwsCompleteName *
		e_ews_item_get_complete_name	(EEwsItem *item);
const gchar *	e_ews_item_get_display_name	(EEwsItem *item);
GHashTable *	e_ews_item_get_email_addresses	(EEwsItem *item);
const gchar *	e_ews_item_get_email_address	(EEwsItem *item, const gchar *type);
const EwsAddress *
		e_ews_item_get_physical_address	(EEwsItem *item, const gchar *type);
const gchar *	e_ews_item_get_phone_number	(EEwsItem *item, const gchar *type);
const gchar *	e_ews_item_get_im_address	(EEwsItem *item, const gchar *type);

const gchar *	e_ews_item_get_company_name	(EEwsItem *item);
const gchar *	e_ews_item_get_department	(EEwsItem *item);
const gchar *	e_ews_item_get_job_title	(EEwsItem *item);
const gchar *	e_ews_item_get_assistant_name	(EEwsItem *item);
const gchar *	e_ews_item_get_manager		(EEwsItem *item);
const gchar *	e_ews_item_get_office_location	(EEwsItem *item);
const gchar *	e_ews_item_get_business_homepage
						(EEwsItem *item);
time_t		e_ews_item_get_birthday		(EEwsItem *item,
						 gboolean *out_exists);
time_t		e_ews_item_get_wedding_anniversary
						(EEwsItem *item,
						 gboolean *out_exists);
const gchar *	e_ews_item_get_profession	(EEwsItem *item);
const gchar *	e_ews_item_get_spouse_name	(EEwsItem *item);
const gchar *	e_ews_item_get_culture		(EEwsItem *item);
const gchar *	e_ews_item_get_surname		(EEwsItem *item);
const gchar *	e_ews_item_get_givenname	(EEwsItem *item);
const gchar *	e_ews_item_get_middlename	(EEwsItem *item);
const gchar *	e_ews_item_get_notes		(EEwsItem *item);
const guchar *	e_ews_item_get_user_certificate	(EEwsItem *item,
						 gsize *out_len);
const guchar *	e_ews_item_get_msexchange_certificate
						(EEwsItem *item,
						 gsize *out_len);

/*Task fields*/
const gchar *	e_ews_item_get_status		(EEwsItem *item);
const gchar *	e_ews_item_get_percent_complete (EEwsItem *item);
const gchar *	e_ews_item_get_sensitivity	(EEwsItem *item);
const gchar *	e_ews_item_get_body		(EEwsItem *item);
EEwsBodyType	e_ews_item_get_body_type	(EEwsItem *item);
const gchar *	e_ews_item_get_owner		(EEwsItem *item);
const gchar *	e_ews_item_get_delegator	(EEwsItem *item);
time_t		e_ews_item_get_due_date		(EEwsItem *item);
time_t		e_ews_item_get_start_date	(EEwsItem *item);
time_t		e_ews_item_get_complete_date	(EEwsItem *item);
gboolean	e_ews_item_task_has_start_date	(EEwsItem *item,
						 gboolean *has_date);
gboolean	e_ews_item_task_has_due_date	(EEwsItem *item,
						 gboolean *has_date);
gboolean	e_ews_item_task_has_complete_date
						(EEwsItem * item,
						 gboolean * has_date);
const gchar *	e_ews_item_get_tzid		(EEwsItem *item);
const gchar *	e_ews_item_get_start_tzid	(EEwsItem *item);
const gchar *	e_ews_item_get_end_tzid		(EEwsItem *item);
time_t		e_ews_item_get_start		(EEwsItem *item);
time_t		e_ews_item_get_end		(EEwsItem *item);
const gchar *	e_ews_item_get_contact_photo_id	(EEwsItem *item);
const gchar *	e_ews_item_get_iana_start_time_zone
						(EEwsItem *item);
const gchar *	e_ews_item_get_iana_end_time_zone
						(EEwsItem *item);
const gchar *	e_ews_item_get_event_url	(EEwsItem *item);

/* Folder Permissions */
EEwsPermission *e_ews_permission_new		(EEwsPermissionUserType user_type,
						 const gchar *display_name,
						 const gchar *primary_smtp,
						 const gchar *sid,
						 guint32 rights);
void		e_ews_permission_free		(EEwsPermission *perm);

const gchar *	e_ews_permission_rights_to_level_name
						(guint32 rights);
guint32		e_ews_permission_level_name_to_rights
						(const gchar *level_name);

GSList *	e_ews_permissions_from_soap_param
						(ESoapParameter *param);
void		e_ews_permissions_free (GSList *permissions);

/* Utility functions */
const gchar *	e_ews_item_util_strip_ex_address
						(const gchar *ex_address);
EwsId *		e_ews_id_copy			(const EwsId *ews_id);
void		e_ews_id_free			(EwsId *ews_id);

G_END_DECLS

#endif
