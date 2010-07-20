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

#ifndef E_EWS_CONTAINER_H
#define E_EWS_CONTAINER_H

#include "soup-soap-response.h"
#include "soup-soap-message.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_CONTAINER            (e_ews_container_get_type ())
#define E_EWS_CONTAINER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_CONTAINER, EEwsContainer))
#define E_EWS_CONTAINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_CONTAINER, EEwsContainerClass))
#define E_IS_GW_CONTAINER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_CONTAINER))
#define E_IS_GW_CONTAINER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_CONTAINER))

typedef struct _EShUsers            EShUsers;
typedef struct _EEwsContainer        EEwsContainer;
typedef struct _EEwsContainerClass   EEwsContainerClass;
typedef struct _EEwsContainerPrivate EEwsContainerPrivate;

struct _EEwsContainer {
	GObject parent;
	EEwsContainerPrivate *priv;
};

struct _EEwsContainerClass {
	GObjectClass parent_class;
};

struct _EShUsers {
	gchar *email;
	gint rights;
};

typedef enum {
	E_EWS_CONTAINER_TYPE_ROOT,
	E_EWS_CONTAINER_TYPE_INBOX,
	E_EWS_CONTAINER_TYPE_SENT,
	E_EWS_CONTAINER_TYPE_CALENDAR,
	E_EWS_CONTAINER_TYPE_CONTACTS,
	E_EWS_CONTAINER_TYPE_DOCUMENTS,
	E_EWS_CONTAINER_TYPE_QUERY,
	E_EWS_CONTAINER_TYPE_CHECKLIST,
	E_EWS_CONTAINER_TYPE_DRAFT,
	E_EWS_CONTAINER_TYPE_CABINET,
	E_EWS_CONTAINER_TYPE_TRASH,
	E_EWS_CONTAINER_TYPE_JUNK,
	E_EWS_CONTAINER_TYPE_FOLDER

} EEwsContainerType;

GType         e_ews_container_get_type (void);
EEwsContainer *e_ews_container_new_from_soap_parameter (SoupSoapParameter *param);
gboolean      e_ews_container_set_from_soap_parameter (EEwsContainer *container,
						      SoupSoapParameter *param);
const gchar   *e_ews_container_get_name (EEwsContainer *container);
void          e_ews_container_set_name (EEwsContainer *container, const gchar *new_name);
const gchar   *e_ews_container_get_id (EEwsContainer *container);
void          e_ews_container_set_id (EEwsContainer *container, const gchar *new_id);
const gchar   *e_ews_container_get_parent_id (EEwsContainer *container);
void	      e_ews_container_set_parent_id (EEwsContainer *container, const gchar *parent_id);
guint32       e_ews_container_get_total_count (EEwsContainer *container);
guint32       e_ews_container_get_unread_count (EEwsContainer *container);
gboolean      e_ews_container_get_is_writable (EEwsContainer *container);
void          e_ews_container_set_is_writable (EEwsContainer *container, gboolean writable);
gboolean     e_ews_container_get_is_frequent_contacts (EEwsContainer *container);
void         e_ews_container_set_is_frequent_contacts (EEwsContainer *container, gboolean is_frequent_contacts);
gboolean    e_ews_container_is_root (EEwsContainer *container);
const gchar *  e_ews_container_get_owner(EEwsContainer *container);
const gchar *  e_ews_container_get_modified(EEwsContainer *container);
gint           e_ews_container_get_sequence(EEwsContainer *container);
gboolean      e_ews_container_get_is_shared_by_me(EEwsContainer *container);
gboolean      e_ews_container_get_is_shared_to_me(EEwsContainer *container);
gint	      e_ews_container_get_rights(EEwsContainer *container, gchar *email);
EEwsContainerType e_ews_container_get_container_type (EEwsContainer *container);
void	      e_ews_container_get_user_list(EEwsContainer *container, GList **user_list);
void	      e_ews_container_form_message (SoupSoapMessage *msg, gchar *id, GList *new_list, const gchar *sub, const gchar *mesg, gint flag);
gboolean e_ews_container_get_is_system_folder (EEwsContainer *container);
void e_ews_container_set_is_system_folder (EEwsContainer *container, gboolean is_system_folder);

G_END_DECLS

#endif
