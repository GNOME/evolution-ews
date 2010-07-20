/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  JP Rosevear <jpr@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
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

#ifndef E_EWS_ITEM_H
#define E_EWS_ITEM_H

#include "soup-soap-message.h"
#include "soup-soap-response.h"
#include "e-ews-recur-utils.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_ITEM            (e_ews_item_get_type ())
#define E_EWS_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_ITEM, EEwsItem))
#define E_EWS_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_ITEM, EEwsItemClass))
#define E_IS_GW_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_ITEM))
#define E_IS_GW_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_ITEM))

typedef struct _EEwsItem        EEwsItem;
typedef struct _EEwsItemClass   EEwsItemClass;
typedef struct _EEwsItemPrivate EEwsItemPrivate;

typedef enum {
	E_EWS_ITEM_TYPE_MAIL,
	E_EWS_ITEM_TYPE_APPOINTMENT,
	E_EWS_ITEM_TYPE_TASK,
	E_EWS_ITEM_TYPE_CONTACT,
	E_EWS_ITEM_TYPE_GROUP,
	E_EWS_ITEM_TYPE_ORGANISATION,
	E_EWS_ITEM_TYPE_RESOURCE,
	E_EWS_ITEM_TYPE_CATEGORY,
	E_EWS_ITEM_TYPE_NOTIFICATION,
	E_EWS_ITEM_TYPE_NOTE,
	E_EWS_ITEM_TYPE_UNKNOWN

} EEwsItemType;

typedef enum {
	E_EWS_ITEM_CHANGE_TYPE_ADD,
	E_EWS_ITEM_CHANGE_TYPE_UPDATE,
	E_EWS_ITEM_CHANGE_TYPE_DELETE,
	E_EWS_ITEM_CHNAGE_TYPE_UNKNOWN

} EEwsItemChangeType;

typedef enum {
	E_EWS_ITEM_STAT_ACCEPTED = 1<<0,
	E_EWS_ITEM_STAT_COMPLETED = 1<<1,
	E_EWS_ITEM_STAT_DELEGATED = 1<<2,
	E_EWS_ITEM_STAT_DELETED   = 1<<3,
	E_EWS_ITEM_STAT_FORWARDED = 1<<4,
	E_EWS_ITEM_STAT_OPENED    = 1<<5,
	E_EWS_ITEM_STAT_READ      = 1<<6,
	E_EWS_ITEM_STAT_REPLIED   = 1<<7,
	E_EWS_ITEM_STAT_DECLINED  = 1<<8,
	E_EWS_ITEM_STAT_NONE      = 1<<31
} EEwsItemStatus;

struct _EEwsItem {
	GObject parent;
	EEwsItemPrivate *priv;
};

struct _EEwsItemClass {
	GObjectClass parent_class;
};

/* structures defined to hold contact item fields */
typedef struct {
	gchar *name_prefix;
	gchar *first_name;
	gchar *middle_name;
	gchar *last_name;
	gchar *name_suffix;
} FullName;

typedef struct {
	gchar *street_address;
	gchar *location;
	gchar *city;
	gchar *state;
	gchar *postal_code;
	gchar *country;
} PostalAddress;

typedef struct {

	gchar *service;
	gchar *address;
}IMAddress;

typedef struct {
	gchar *id;
	gchar *email;
	gchar *name;
} EGroupMember;

typedef struct {
	gchar *email;
	gchar *display_name;
} EEwsItemOrganizer;

typedef struct {
	gchar *id;
	gchar *name;
	gchar *item_reference;
	gchar *contentid;
	gchar *contentType;
	gint size;
	gchar *date;
	gchar *data;
	gboolean hidden;
} EEwsItemAttachment;

typedef enum {
	E_EWS_ITEM_NOTIFY_NONE,
	E_EWS_ITEM_NOTIFY_MAIL
} EEwsItemReturnNotify;

typedef enum {
	E_EWS_ITEM_NONE,
	E_EWS_ITEM_DELIVERED,
	E_EWS_ITEM_DELIVERED_OPENED,
	E_EWS_ITEM_ALL
} EEwsItemTrack;

typedef struct {
	gchar *id;
	gchar *type;
	gchar *thread;
} EEwsItemLinkInfo;

typedef struct {
	gchar *item_id;
	gchar *ical_id;
	gchar *recur_key;
	gchar *start_date;
} EEwsItemCalId;

GType       e_ews_item_get_type (void);
EEwsItem    *e_ews_item_new_empty (void);
EEwsItem    *e_ews_item_new_from_soap_parameter (const gchar *email, const gchar *container, SoupSoapParameter *param);

EEwsItemType e_ews_item_get_item_type (EEwsItem *item);
void        e_ews_item_set_item_type (EEwsItem *item, EEwsItemType new_type);
const gchar *e_ews_item_get_container_id (EEwsItem *item);
void        e_ews_item_set_container_id (EEwsItem *item, const gchar *new_id);
const gchar *e_ews_item_get_icalid (EEwsItem *item);
void        e_ews_item_set_icalid (EEwsItem *item, const gchar *new_icalid);
const gchar *e_ews_item_get_id (EEwsItem *item);
void        e_ews_item_set_id (EEwsItem *item, const gchar *new_id);
gchar       *e_ews_item_get_creation_date (EEwsItem *item);
void        e_ews_item_set_creation_date (EEwsItem *item, const gchar *new_date);
gchar       *e_ews_item_get_delivered_date (EEwsItem *item);
void        e_ews_item_set_delivered_date (EEwsItem *item, const gchar *new_date);
const gchar  *e_ews_item_get_modified_date (EEwsItem *item);
void        e_ews_item_set_modified_date (EEwsItem *item, const gchar *new_date);
gchar       *e_ews_item_get_start_date (EEwsItem *item);
void        e_ews_item_set_start_date (EEwsItem *item, const gchar *new_date);
gchar       *e_ews_item_get_completed_date (EEwsItem *item);
void        e_ews_item_set_completed_date (EEwsItem *item, const gchar *new_date);
gchar       *e_ews_item_get_end_date (EEwsItem *item);
void        e_ews_item_set_end_date (EEwsItem *item, const gchar *new_date);
gchar       *e_ews_item_get_due_date (EEwsItem *item);
void        e_ews_item_set_due_date (EEwsItem *item, const gchar *new_date);
const gchar *e_ews_item_get_subject (EEwsItem *item);
void        e_ews_item_set_subject (EEwsItem *item, const gchar *new_subject);
const gchar *e_ews_item_get_message (EEwsItem *item);
void        e_ews_item_set_message (EEwsItem *item, const gchar *new_message);
const gchar *e_ews_item_get_place (EEwsItem *item);
void        e_ews_item_set_place (EEwsItem *item, const gchar *new_place);
const gchar *e_ews_item_get_security (EEwsItem *item);
void        e_ews_item_set_security (EEwsItem *item, const gchar *new_class);
gboolean    e_ews_item_get_completed (EEwsItem *item);
void        e_ews_item_set_completed (EEwsItem *item, gboolean new_completed);
gboolean    e_ews_item_get_is_allday_event (EEwsItem *item);
void	    e_ews_item_set_is_allday_event (EEwsItem *item, gboolean is_allday);
gchar *       e_ews_item_get_field_value (EEwsItem *item, const gchar *field_name);
void        e_ews_item_set_field_value (EEwsItem *item, const gchar *field_name, gchar * field_value);
GList*      e_ews_item_get_email_list (EEwsItem *item);
void        e_ews_item_set_email_list (EEwsItem *item, GList *email_list);
FullName*   e_ews_item_get_full_name (EEwsItem *item);
void        e_ews_item_set_full_name (EEwsItem *item, FullName* full_name);
GList*      e_ews_item_get_member_list (EEwsItem *item);
void        e_ews_item_set_member_list (EEwsItem *item, GList *list);
PostalAddress* e_ews_item_get_address (EEwsItem *item, const gchar *address_type);
void        e_ews_item_set_address (EEwsItem *item, const gchar *addres_type, PostalAddress *address);
GList*      e_ews_item_get_im_list (EEwsItem *item);
void        e_ews_item_set_im_list (EEwsItem *item, GList *im_list);
void        e_ews_item_set_categories (EEwsItem *item, GList *category_list);
GList*      e_ews_item_get_categories (EEwsItem *item);
void	    e_ews_item_set_to (EEwsItem *item, const gchar *to);
const gchar * e_ews_item_get_to (EEwsItem *item);
const gchar *e_ews_item_get_msg_content_type (EEwsItem *item);
guint32     e_ews_item_get_item_status (EEwsItem *item);
void	    e_ews_item_set_content_type (EEwsItem *item, const gchar *content_type);
void	    e_ews_item_set_link_info (EEwsItem *item, EEwsItemLinkInfo *info);
EEwsItemLinkInfo *e_ews_item_get_link_info (EEwsItem *item);
gchar	    *e_ews_item_get_content_type (EEwsItem *item);
const gchar *e_ews_item_get_msg_body_id (EEwsItem *item);
gint	    e_ews_item_get_mail_size (EEwsItem *item);
void e_ews_item_set_change (EEwsItem *item, EEwsItemChangeType change_type, const gchar *field_name, gpointer field_value);
gboolean e_ews_item_append_changes_to_soap_message (EEwsItem *item, SoupSoapMessage *msg);
void e_ews_item_set_category_name (EEwsItem *item, gchar *cateogry_name);
gchar * e_ews_item_get_category_name (EEwsItem *item);
void e_ews_item_set_sendoptions (EEwsItem *item, gboolean set);
void e_ews_item_set_reply_request (EEwsItem *item, gboolean set);
gboolean e_ews_item_get_reply_request (EEwsItem *item);
void e_ews_item_set_reply_within (EEwsItem *item, gchar *reply_within);
gchar *e_ews_item_get_reply_within (EEwsItem *item);
void e_ews_item_set_track_info (EEwsItem *item, EEwsItemTrack track_info);
EEwsItemTrack e_ews_item_get_track_info (EEwsItem *item);
void e_ews_item_set_autodelete (EEwsItem *item, gboolean set);
gboolean e_ews_item_get_autodelete (EEwsItem *item);
void e_ews_item_set_notify_completed (EEwsItem *item, EEwsItemReturnNotify notify);
EEwsItemReturnNotify e_ews_item_get_notify_completed (EEwsItem *item);
void e_ews_item_set_notify_accepted (EEwsItem *item, EEwsItemReturnNotify notify);
EEwsItemReturnNotify e_ews_item_get_notify_accepted (EEwsItem *item);
void e_ews_item_set_notify_declined (EEwsItem *item, EEwsItemReturnNotify notify);
EEwsItemReturnNotify e_ews_item_get_notify_declined (EEwsItem *item);
void e_ews_item_set_notify_opened (EEwsItem *item, EEwsItemReturnNotify notify);
EEwsItemReturnNotify e_ews_item_get_notify_opened (EEwsItem *item);
void e_ews_item_set_notify_deleted (EEwsItem *item, EEwsItemReturnNotify notify);
EEwsItemReturnNotify e_ews_item_get_notify_deleted (EEwsItem *item);
void e_ews_item_set_expires (EEwsItem *item, gchar *expires);
gchar *e_ews_item_get_expires (EEwsItem *item);
void e_ews_item_set_delay_until (EEwsItem *item, gchar *delay_until);
gchar *e_ews_item_get_delay_until (EEwsItem *item);
void e_ews_item_free_cal_id (EEwsItemCalId *calid);

#define E_EWS_ITEM_CLASSIFICATION_PUBLIC       "Public"
#define E_EWS_ITEM_CLASSIFICATION_PRIVATE      "Private"
#define E_EWS_ITEM_CLASSIFICATION_CONFIDENTIAL "Confidential"

const gchar *e_ews_item_get_classification (EEwsItem *item);
void        e_ews_item_set_classification (EEwsItem *item, const gchar *new_class);

#define E_EWS_ITEM_ACCEPT_LEVEL_BUSY          "Busy"
#define E_EWS_ITEM_ACCEPT_LEVEL_OUT_OF_OFFICE "OutOfOffice"
#define E_EWS_ITEM_ACCEPT_LEVEL_FREE	     "Free"

const gchar *e_ews_item_get_accept_level (EEwsItem *item);
void        e_ews_item_set_accept_level (EEwsItem *item, const gchar *new_level);

#define E_EWS_ITEM_PRIORITY_HIGH     "High"
#define E_EWS_ITEM_PRIORITY_STANDARD "Standard"
#define E_EWS_ITEM_PRIORITY_LOW      "Low"

const gchar *e_ews_item_get_priority (EEwsItem *item);
void        e_ews_item_set_priority (EEwsItem *item, const gchar *new_priority);

const gchar *e_ews_item_get_task_priority (EEwsItem *item);
void        e_ews_item_set_task_priority (EEwsItem *item, const gchar *new_priority);

GSList *e_ews_item_get_recipient_list (EEwsItem *item);
void e_ews_item_set_recipient_list (EEwsItem *item, GSList *new_recipient_list);

EEwsItemOrganizer *e_ews_item_get_organizer (EEwsItem *item);
void e_ews_item_set_organizer (EEwsItem  *item, EEwsItemOrganizer *organizer);

GSList * e_ews_item_get_attach_id_list (EEwsItem *item);
void e_ews_item_set_attach_id_list (EEwsItem *item, GSList *attach_list);

GSList *e_ews_item_get_recurrence_dates (EEwsItem *item);
void e_ews_item_set_recurrence_dates (EEwsItem  *item, GSList *new_recurrence_dates);

GSList *e_ews_item_get_exdate_list (EEwsItem *item);
void e_ews_item_set_exdate_list (EEwsItem  *item, GSList *new_exdate_list);

void e_ews_item_set_rrule (EEwsItem *item, EEwsItemRecurrenceRule *rrule);
EEwsItemRecurrenceRule *e_ews_item_get_rrule (EEwsItem *item);

gint e_ews_item_get_recurrence_key (EEwsItem *item);
void e_ews_item_set_recurrence_key (EEwsItem *item, gint recurrence_key);

void e_ews_item_set_source (EEwsItem *item, const gchar *source);

gint e_ews_item_get_trigger (EEwsItem *item);
void e_ews_item_set_trigger (EEwsItem *item, gint trigger);

gboolean e_ews_item_has_attachment (EEwsItem *item);

gboolean e_ews_item_is_from_internet (EEwsItem *item);

const gchar *e_ews_item_get_parent_thread_ids (EEwsItem *item);
const gchar * e_ews_item_get_message_id (EEwsItem *item);

typedef struct {
	gchar *email;
	gchar *display_name;
	gboolean status_enabled;
	gchar *delivered_date;
	gchar *opened_date;
	gchar *accepted_date;
	gchar *deleted_date;
	gchar *declined_date;
	gchar *completed_date;
	gchar *undelivered_date;
	enum {
		E_EWS_ITEM_RECIPIENT_TO,
		E_EWS_ITEM_RECIPIENT_CC,
		E_EWS_ITEM_RECIPIENT_BC,
		E_EWS_ITEM_RECIPIENT_NONE
	} type;

	EEwsItemStatus status;
} EEwsItemRecipient;

gboolean    e_ews_item_append_to_soap_message (EEwsItem *item, SoupSoapMessage *msg);
void e_ews_item_add_distribution_to_soap_message (EEwsItem *item, SoupSoapMessage *msg);
G_END_DECLS

#endif
