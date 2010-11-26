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

#ifndef E_EWS_CONNECTION_H
#define E_EWS_CONNECTION_H

#include <glib-object.h>
#include <libsoup/soup.h>
#include <libedataserver/e-soap-message.h>

G_BEGIN_DECLS

#define E_TYPE_EWS_CONNECTION            (e_ews_connection_get_type ())
#define E_EWS_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_CONNECTION, EEwsConnection))
#define E_EWS_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_CONNECTION, EEwsConnectionClass))
#define E_IS_EWS_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_CONNECTION))
#define E_IS_EWS_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_CONNECTION))

typedef struct _EEwsConnection        EEwsConnection;
typedef struct _EEwsConnectionClass   EEwsConnectionClass;
typedef struct _EEwsConnectionPrivate EEwsConnectionPrivate;

struct _EEwsConnection {
	GObject parent;
	EEwsConnectionPrivate *priv;
};

struct _EEwsConnectionClass {
	GObjectClass parent_class;

	void	(*next_request) (EEwsConnection *cnc);
	void	(*shutdown)	(EEwsConnection *cnc);
};

/* Operations on the store (folder_tree) will have highest priority as we know for sure they are sync
   and user triggered. */
enum {
	EWS_PRIORITY_CREATE_FOLDER = 200,
	EWS_PRIORITY_DELETE_FOLDER = 200,
	EWS_PRIORITY_RENAME_FOLDER = 200,
	EWS_PRIORITY_MANAGE_SUBSCRIPTION = 200,
	EWS_PRIORITY_SYNC_CHANGES = 150,
	EWS_PRIORITY_EXPUNGE = 150,
	EWS_PRIORITY_GET_MESSAGE = 100,
	EWS_PRIORITY_REFRESH_INFO = 0,
	EWS_PRIORITY_NOOP = 0,
	EWS_PRIORITY_NEW_MESSAGES = 0,
	EWS_PRIORITY_APPEND_MESSAGE = -60,
	EWS_PRIIORITY_COPY_MESSAGE = -60,
	EWS_PRIORITY_LIST = -80,
	EWS_PRIORITY_IDLE = -100,
	EWS_PRIORITY_SYNC_MESSAGE = -120
};

typedef enum {
	E_EWS_CONNECTION_STATUS_OK,
	E_EWS_CONNECTION_STATUS_INVALID_CONNECTION,
	E_EWS_CONNECTION_STATUS_INVALID_OBJECT,
	E_EWS_CONNECTION_STATUS_INVALID_RESPONSE,
	E_EWS_CONNECTION_STATUS_NO_RESPONSE,
	E_EWS_CONNECTION_STATUS_OBJECT_NOT_FOUND,
	E_EWS_CONNECTION_STATUS_UNKNOWN_USER,
	E_EWS_CONNECTION_STATUS_BAD_PARAMETER,
	E_EWS_CONNECTION_STATUS_ITEM_ALREADY_ACCEPTED,
	E_EWS_CONNECTION_STATUS_REDIRECT,
	E_EWS_CONNECTION_STATUS_OTHER,
	E_EWS_CONNECTION_STATUS_UNKNOWN,
	E_EWS_CONNECTION_STATUS_INVALID_PASSWORD = 53273,
	E_EWS_CONNECTION_STATUS_OVER_QUOTA = 58652
} EEwsConnectionStatus;

GType          e_ews_connection_get_type (void);
EEwsConnection *e_ews_connection_new (const gchar *uri, const gchar *username, const gchar *password, GError **error);
gchar* e_ews_autodiscover_ws_url (const gchar *email, const gchar *password, GError **error);
void e_ews_connection_create_folder (EEwsConnection *cnc);
void e_ews_connection_sync_folder_hierarchy (EEwsConnection *cnc, const gchar *sync_state, GList **folder_list);
void e_ews_connection_find_item (EEwsConnection *cnc, const gchar *folder_name);

G_END_DECLS

#endif
