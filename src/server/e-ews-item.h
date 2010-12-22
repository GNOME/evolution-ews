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

#include <libedataserver/e-soap-message.h>
#include <libedataserver/e-soap-response.h>

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
	E_EWS_ITEM_TYPE_MESSAGE,
	E_EWS_ITEM_TYPE_CALENDAR_ITEM,
	E_EWS_ITEM_TYPE_CONTACT,
	E_EWS_ITEM_TYPE_GROUP,
	E_EWS_ITEM_TYPE_MEETING_MESSAGE,
	E_EWS_ITEM_TYPE_MEETING_REQUEST,
	E_EWS_ITEM_TYPE_MEETING_RESPONSE,
	E_EWS_ITEM_TYPE_MEETING_CANCELLATION,
	E_EWS_ITEM_TYPE_TASK,
	E_EWS_ITEM_TYPE_GENERIC_ITEM,
	E_EWS_ITEM_TYPE_UNKNOWN
} EEwsItemType;

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

GType       e_ews_item_get_type (void);
EEwsItem    *e_ews_item_new_from_soap_parameter (ESoapParameter *param);

EEwsItemType e_ews_item_get_item_type (EEwsItem *item);
void        e_ews_item_set_item_type (EEwsItem *item, EEwsItemType new_type);
const gchar *e_ews_item_get_subject (EEwsItem *item);
void        e_ews_item_set_subject (EEwsItem *item, const gchar *new_subject);
const gchar *e_ews_item_get_mime_content (EEwsItem *item);
void        e_ews_item_set_mime_content (EEwsItem *item, const gchar *new_mime_content);
const EwsId *e_ews_item_get_id (EEwsItem *item);

G_END_DECLS

#endif
