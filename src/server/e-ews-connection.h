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
#include "e-ews-proxy.h"
#include "e-ews-container.h"
#include "e-ews-item.h"
#include "e-ews-filter.h"
#include "e-ews-sendoptions.h"
#include "e-ews-recur-utils.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_CONNECTION            (e_ews_connection_get_type ())
#define E_EWS_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_CONNECTION, EEwsConnection))
#define E_EWS_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_CONNECTION, EEwsConnectionClass))
#define E_IS_GW_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_CONNECTION))
#define E_IS_GW_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_CONNECTION))

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

EEwsConnectionStatus e_ews_connection_logout (EEwsConnection *cnc);

EEwsConnectionStatus e_ews_connection_get_container_list (EEwsConnection *cnc, const gchar *top, GList **container_list);
void                e_ews_connection_free_container_list (GList *container_list);
gchar               *e_ews_connection_get_container_id (EEwsConnection *cnc, const gchar *name);
EEwsContainer * e_ews_connection_get_container (EEwsConnection *cnc, const gchar * uid);

EEwsConnectionStatus e_ews_connection_get_items (EEwsConnection *cnc, const gchar *container,
					       const gchar *view, EEwsFilter *filter, GList **list);
EEwsConnectionStatus e_ews_connection_get_deltas ( EEwsConnection *cnc, GSList **adds, GSList **deletes, GSList **updates);
EEwsConnectionStatus e_ews_connection_send_item (EEwsConnection *cnc, EEwsItem *item, GSList **id_list);
EEwsConnectionStatus e_ews_connection_remove_item (EEwsConnection *cnc, const gchar *container, const gchar *id);
EEwsConnectionStatus e_ews_connection_remove_items (EEwsConnection *cnc, const gchar *container, GList *item_ids);
EEwsConnectionStatus e_ews_connection_get_items_delta_info (EEwsConnection *cnc, const gchar *container, gdouble *first_sequence, gdouble *last_sequence, gdouble *last_po_rebuild_time);
EEwsConnectionStatus e_ews_connection_get_items_delta (EEwsConnection *cnc, const gchar *container, const gchar *view, const gchar *count, const gchar * start_sequence, GList **add_list, GList **delete_list);

const gchar         *e_ews_connection_get_uri (EEwsConnection *cnc);
const gchar         *e_ews_connection_get_session_id (EEwsConnection *cnc);
const gchar         *e_ews_connection_get_user_name (EEwsConnection *cnc);
const gchar         *e_ews_connection_get_user_email (EEwsConnection *cnc);
const gchar         *e_ews_connection_get_user_uuid (EEwsConnection *cnc);
const gchar	   *e_ews_connection_get_version (EEwsConnection *cnc);
const gchar	   *e_ews_connection_get_server_time (EEwsConnection *cnc);

time_t              e_ews_connection_get_date_from_string (const gchar *dtstring);
gchar               *e_ews_connection_format_date_string (const gchar *dtstring);

EEwsConnectionStatus e_ews_connection_create_item (EEwsConnection *cnc, EEwsItem *item, gchar ** id);
EEwsConnectionStatus e_ews_connection_get_item (EEwsConnection *cnc, const gchar *container, const gchar *id, const gchar *view, EEwsItem **item);
EEwsConnectionStatus e_ews_connection_modify_item (EEwsConnection *cnc, const gchar *id, EEwsItem *item);
EEwsConnectionStatus e_ews_connection_accept_request (EEwsConnection *cnc, const gchar *id, const gchar *accept_level, const gchar *accept_comment, const gchar *recurrence_key);
EEwsConnectionStatus e_ews_connection_decline_request (EEwsConnection *cnc, const gchar *id, const gchar *decline_comment, const gchar *recurrence_key);
EEwsConnectionStatus e_ews_connection_retract_request (EEwsConnection *cnc, const gchar *id, const gchar *comment, gboolean retract_all, gboolean resend);
EEwsConnectionStatus e_ews_connection_complete_request (EEwsConnection *cnc, const gchar *id);
EEwsConnectionStatus e_ews_connection_delegate_request (EEwsConnection *cnc, EEwsItem *item, const gchar *id, const gchar *comments_org, const gchar *comments_del, const gchar *recur_key);
EEwsConnectionStatus e_ews_connection_create_book (EEwsConnection *cnc, gchar *book_name, gchar **id);
EEwsConnectionStatus e_ews_connection_remove_book (EEwsConnection *cnc, gchar *book_uid);
EEwsConnectionStatus e_ews_connection_get_address_book_list (EEwsConnection *cnc, GList **container_list);
EEwsConnectionStatus e_ews_connection_get_address_book_id ( EEwsConnection *cnc, gchar *book_name, gchar **id , gboolean *is_writable);
EEwsConnectionStatus e_ews_connection_get_categories  (EEwsConnection *cnc, GHashTable **categories_by_id, GHashTable **categoreis_by_name);
EEwsConnectionStatus e_ews_connection_add_members (EEwsConnection *cnc, const gchar *group_id, GList *member_ids);
EEwsConnectionStatus e_ews_connection_remove_members (EEwsConnection *cnc, const gchar *group_id, GList *member_ids);
EEwsConnectionStatus e_ews_connection_get_items_from_ids (EEwsConnection *cnc, const gchar *container, const gchar *view, GPtrArray *item_ids, GList **list);

EEwsConnectionStatus e_ews_connection_create_cursor (EEwsConnection *cnc, const gchar *container, const gchar *view, EEwsFilter *filter, gint *cursor);
EEwsConnectionStatus e_ews_connection_destroy_cursor (EEwsConnection *cnc, const gchar *container,  gint cursor);
EEwsConnectionStatus e_ews_connection_read_cursor (EEwsConnection *cnc, const gchar *container, gint cursor, gboolean forward, gint count, const gchar *cursor_seek, GList **item_list);
EEwsConnectionStatus e_ews_connection_position_cursor (EEwsConnection *cnc, const gchar *container, gint cursor, const gchar *seek, gint offset);

EEwsConnectionStatus e_ews_connection_get_quick_messages (EEwsConnection *cnc, const gchar *container, const gchar *view, gchar **start_date, const gchar *message_list, const gchar *item_types, const gchar *item_sources, gint count, GSList **item_list);

EEwsConnectionStatus e_ews_connection_create_folder(EEwsConnection *cnc, const gchar *parent_name,const gchar *folder_name, gchar **container_id);
EEwsConnectionStatus
e_ews_connection_get_attachment (EEwsConnection *cnc, const gchar *id, gint offset, gint length, const gchar **attachment, gint *attach_length);
EEwsConnectionStatus e_ews_connection_get_attachment_base64 (EEwsConnection *cnc, const gchar *id, gint offset, gint length, const gchar **attachment, gint *attach_length, gint *offset_r);
EEwsConnectionStatus e_ews_connection_add_item (EEwsConnection *cnc, const gchar *container, const gchar *id);
EEwsConnectionStatus e_ews_connection_add_items (EEwsConnection *cnc, const gchar *container, GList *item_ids);
EEwsConnectionStatus e_ews_connection_move_item (EEwsConnection *cnc, const gchar *id, const gchar *dest_container_id, const gchar *from_container_id);
EEwsConnectionStatus e_ews_connection_rename_folder (EEwsConnection *cnc, const gchar *id ,const gchar *new_name);
EEwsConnectionStatus e_ews_connection_get_settings (EEwsConnection *cnc, EEwsSendOptions **opts);
EEwsConnectionStatus e_ews_connection_modify_settings (EEwsConnection *cnc, EEwsSendOptions *opts);
EEwsConnectionStatus e_ews_connection_share_folder (EEwsConnection *cnc, gchar *id, GList *new_list, const gchar *sub, const gchar *mesg ,int flag);
EEwsConnectionStatus e_ews_connection_accept_shared_folder (EEwsConnection *cnc, gchar *folder_name, gchar *container_id, gchar *item_id, gchar *desc);
EEwsConnectionStatus e_ews_connection_purge_deleted_items (EEwsConnection *cnc);
EEwsConnectionStatus e_ews_connection_purge_selected_items (EEwsConnection *cnc, GList *item_ids);

EEwsConnectionStatus e_ews_connection_mark_read(EEwsConnection *cnc, GList *item_ids);
EEwsConnectionStatus e_ews_connection_mark_unread(EEwsConnection *cnc, GList *item_ids);
EEwsConnectionStatus e_ews_connection_reply_item (EEwsConnection *cnc, const gchar *id, const gchar *view, EEwsItem **item);
EEwsConnectionStatus e_ews_connection_forward_item (EEwsConnection *cnc, const gchar *id, const gchar *view, gboolean embed, EEwsItem **item);
EEwsConnectionStatus e_ews_connection_create_junk_entry (EEwsConnection *cnc, const gchar *value, const gchar *match_type , const gchar *list_type);
EEwsConnectionStatus e_ews_connection_get_junk_settings (EEwsConnection *cnc, gint *use_junk, gint *use_block, gint *use_pab,  gint *persistence);
EEwsConnectionStatus e_ews_connection_modify_junk_settings (EEwsConnection *cnc, gint use_junk, gint use_block, gint use_pab , gint persistence);
EEwsConnectionStatus e_ews_connection_get_junk_entries (EEwsConnection *cnc, GList **entries);
EEwsConnectionStatus  e_ews_connection_remove_junk_entry (EEwsConnection *cnc, const gchar *id);
EEwsConnectionStatus e_ews_connection_read_cal_ids (EEwsConnection *cnc, const gchar *container, gint cursor, gboolean forward, gint count, const gchar *cursor_seek, GList **list);
EEwsConnectionStatus e_ews_connection_get_proxy_access_list (EEwsConnection *cnc, GList **proxy_list);
EEwsConnectionStatus e_ews_connection_add_proxy (EEwsConnection *cnc, proxyHandler *new_proxy);
EEwsConnectionStatus e_ews_connection_remove_proxy (EEwsConnection *cnc, proxyHandler *newProxy);
EEwsConnectionStatus e_ews_connection_modify_proxy (EEwsConnection *cnc, proxyHandler *newProxy);
EEwsConnectionStatus e_ews_connection_get_proxy_list (EEwsConnection *cnc, GList **proxy_info);
EEwsConnection *e_ews_connection_get_proxy_connection (EEwsConnection *cnc1, gchar *username, const gchar *password, const gchar *proxy, int* permissions);
EEwsConnectionStatus e_ews_connection_get_all_mail_uids (EEwsConnection *cnc, const gchar *container, gint cursor, gboolean forward, gint count, const gchar *cursor_seek, GList **list);

G_END_DECLS

#endif
