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
#include <gio/gio.h>
#include <libsoup/soup.h>
#include "e-soap-message.h"
#include "ews-errors.h"
#include "e-ews-folder.h"
#include "e-ews-item.h"

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
};

enum {
	EWS_PRIORITY_LOW,
	EWS_PRIORITY_MEDIUM,
	EWS_PRIORITY_HIGH
};

GType		e_ews_connection_get_type	(void);
EEwsConnection *
		e_ews_connection_new		(const gchar *uri,
						 const gchar *username, 
						 const gchar *password, 
						 GError **error);
gchar *		e_ews_autodiscover_ws_url	(const gchar *email,
						 const gchar *password, 
						 GError **error);
/* Create Folder */
void		e_ews_connection_create_folder	(EEwsConnection *cnc, 
						 GCancellable *cancellable);

void		e_ews_connection_create_folder_start
						(EEwsConnection *cnc, 
						 gint pri, 
						 GAsyncReadyCallback cb, 
						 GCancellable *cancellable,
						 gpointer user_data);

void		e_ews_connection_create_folder_finish 
						(EEwsConnection *cnc, 
						 GAsyncResult *result, 
						 guint folder_id,
						 GError **error);

/* Sync folder items */
void		e_ews_connection_sync_folder_items_start 
						(EEwsConnection *cnc, 
						 gint pri, 
						 const gchar *sync_state, 
						 const gchar *fid,
						 const gchar *props, 
						 const gchar *additional_props, 
						 guint max_entries, 
						 GAsyncReadyCallback cb, 
						 GCancellable *cancellable,
						 gpointer user_data);

void		e_ews_connection_sync_folder_items_finish 
						(EEwsConnection *cnc, 
						 GAsyncResult *result,
					 	 gchar **sync_state, 
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GError **error);
void		e_ews_connection_sync_folder_items	
						(EEwsConnection *cnc,
						 gint pri,
						 gchar **sync_state,
						 const gchar *fid,
						 const gchar *default_props,
						 const gchar *additional_props,
						 guint max_entries,
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GCancellable *cancellable,
						 GError **error);

/* Get folder items */
void		e_ews_connection_get_items_start 
						(EEwsConnection *cnc,
						 gint pri,
						 GSList *ids,
						 const gchar *default_props,
						 const gchar *additional_props,
						 gboolean include_mime,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_get_items_finish 
						(EEwsConnection *cnc, 
						 GAsyncResult *result,
						 GSList **items,
						 GError **error);

gboolean	e_ews_connection_get_items	(EEwsConnection *cnc,
						 gint pri, 
						 GSList *ids,
						 const gchar *default_props,
						 const gchar *additional_props,
						 gboolean include_mime,
						 GSList **items,
						 GCancellable *cancellable,
						 GError **error);

/* Sync folder hierarchy */
void		e_ews_connection_sync_folder_hierarchy 
						(EEwsConnection *cnc, 
						 gint pri, 
						 gchar **sync_state, 
						 GSList **folders_created,
						 GSList **folders_updated,
						 GSList **folders_deleted,
						 GCancellable *cancellable, 
						 GError **error);
void		e_ews_connection_sync_folder_hierarchy_start 
						(EEwsConnection *cnc, 
						 gint pri, 
						 const gchar *sync_state, 
						 GAsyncReadyCallback cb, 
						 GCancellable *cancellable,
						 gpointer user_data);
void		e_ews_connection_sync_folder_hierarchy_finish 
						(EEwsConnection *cnc, 
						 GAsyncResult *result, 
						 gchar **sync_state, 
						 GSList **folders_created,
						 GSList **folders_updated,
						 GSList **folders_deleted,
						 GError **error);
void		e_ews_connection_resolve_names_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *resolve_name,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

void		e_ews_connection_resolve_names_finish 
						(EEwsConnection *cnc, 
						 GAsyncResult *result, 
						 GError **error);

G_END_DECLS

#endif
