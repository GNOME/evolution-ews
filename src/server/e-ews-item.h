/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
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

#include "e-soap-message.h"
#include "e-soap-response.h"

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
	E_EWS_ITEM_TYPE_UNKNOWN,
	E_EWS_ITEM_TYPE_MESSAGE,
	E_EWS_ITEM_TYPE_CALENDAR_ITEM,
	E_EWS_ITEM_TYPE_CONTACT,
	E_EWS_ITEM_TYPE_GROUP,
	E_EWS_ITEM_TYPE_MEETING_MESSAGE,
	E_EWS_ITEM_TYPE_MEETING_REQUEST,
	E_EWS_ITEM_TYPE_MEETING_RESPONSE,
	E_EWS_ITEM_TYPE_MEETING_CANCELLATION,
	E_EWS_ITEM_TYPE_TASK,
	E_EWS_ITEM_TYPE_GENERIC_ITEM
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
} EwsMailbox;

GType       	e_ews_item_get_type (void);
EEwsItem *	e_ews_item_new_from_soap_parameter	
						(ESoapParameter *param);

EEwsItemType 	e_ews_item_get_item_type	(EEwsItem *item);
void		e_ews_item_set_item_type	(EEwsItem *item, 
						 EEwsItemType new_type);
const gchar *	e_ews_item_get_subject		(EEwsItem *item);
void		e_ews_item_set_subject		(EEwsItem *item, 
						 const gchar *new_subject);
const gchar *	e_ews_item_get_mime_content	(EEwsItem *item);
void		e_ews_item_set_mime_content	(EEwsItem *item, 
						 const gchar *new_mime_content);
const EwsId *	e_ews_item_get_id		(EEwsItem *item);
gsize		e_ews_item_get_size		(EEwsItem *item);
const gchar *	e_ews_item_get_msg_id		(EEwsItem *item);
const gchar *	e_ews_item_get_in_replyto	(EEwsItem *item);
const gchar *	e_ews_item_get_references	(EEwsItem *item);
time_t		e_ews_item_get_date_received	(EEwsItem *item);
time_t		e_ews_item_get_date_sent	(EEwsItem *item);
time_t		e_ews_item_get_date_created	(EEwsItem *item);
gboolean	e_ews_item_has_attachments	(EEwsItem *item, 
						 gboolean *has_attachments);
gboolean	e_ews_item_is_read		(EEwsItem *item, 
						 gboolean *is_read);
gboolean	e_ews_item_is_forwarded		(EEwsItem *item, 
						 gboolean *is_forwarded);
gboolean	e_ews_item_is_answered		(EEwsItem *item, 
						 gboolean *is_answered);
const GSList *	e_ews_item_get_to_recipients	(EEwsItem *item);
const GSList *	e_ews_item_get_cc_recipients	(EEwsItem *item);
const GSList *	e_ews_item_get_bcc_recipients	(EEwsItem *item);
const EwsMailbox *	
		e_ews_item_get_sender		(EEwsItem *item);
const EwsMailbox *	
		e_ews_item_get_from		(EEwsItem *item);
EwsImportance
		e_ews_item_get_importance	(EEwsItem *item);
EwsMailbox *
		e_ews_item_mailbox_from_soap_param	
						(ESoapParameter *param);

const GSList *	e_ews_item_get_modified_occurrences
						(EEwsItem *item);
GSList *	e_ews_item_get_attachments_ids
						(EEwsItem *item);
gchar *
e_ews_dump_file_attachment_from_soap_parameter (ESoapParameter *param, const gchar *cache);

gchar *
e_ews_item_ical_dump(EEwsItem *item);

gchar *
e_ews_item_dump_mime_content(EEwsItem *item, const gchar *cache);

G_END_DECLS

#endif
