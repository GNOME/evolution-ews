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
#include "soup-soap-message.h"
/* #include "e-ews-proxy.h" */
/* #include "e-ews-container.h" */
/* #include "e-ews-item.h" */
/* #include "e-ews-filter.h" */
/* #include "e-ews-sendoptions.h" */
/* #include "e-ews-recur-utils.h" */

G_BEGIN_DECLS

#define E_TYPE_EWS_CONNECTION            (e_ews_connection_get_type ())
#define E_EWS_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_CONNECTION, EEwsConnection))
#define E_EWS_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_CONNECTION, EEwsConnectionClass))
#define E_IS_EWS_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_CONNECTION))
#define E_IS_EWS_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_CONNECTION))

typedef struct _EEwsConnection        EEwsConnection;
typedef struct _EEwsConnectionClass   EEwsConnectionClass;
typedef struct _EEwsConnectionPrivate EEwsConnectionPrivate;

typedef struct {
	gint status;
	gchar *description;
}EEwsConnectionErrors;

struct _EEwsConnection {
	GObject parent;
	EEwsConnectionPrivate *priv;
};

struct _EEwsConnectionClass {
	GObjectClass parent_class;
};

/* TODO:This has to go either in a generic file or specific to junk*/
typedef struct {
	gchar *id;
	gchar *match;
	gchar *matchType;
	gchar *lastUsed;
	gint version;
	gchar *modified;
} EEwsJunkEntry;

GType          e_ews_connection_get_type (void);
EEwsConnection *e_ews_connection_new (const gchar *uri, const gchar *username, const gchar *password);

EEwsConnection * e_ews_connection_new_with_error_handler (const gchar *uri, const gchar *username, const gchar *password, EEwsConnectionErrors *errors);

gchar* e_ews_autodiscover_ws_url (const gchar *username, const gchar *password, const gchar *domain);


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

#define E_EWS_CURSOR_POSITION_CURRENT "current"
#define E_EWS_CURSOR_POSITION_START "start"
#define E_EWS_CURSOR_POSITION_END "end"

SoupSoapResponse   *e_ews_connection_send_message (EEwsConnection *cnc, SoupSoapMessage *msg);
EEwsConnectionStatus e_ews_connection_parse_response_status (SoupSoapResponse *response);
const gchar         *e_ews_connection_get_error_message (EEwsConnectionStatus status);

gchar               *e_ews_connection_format_date_string (const gchar *dtstring);

EEwsConnectionStatus e_ews_connection_logout (EEwsConnection *cnc);

G_END_DECLS

#endif
