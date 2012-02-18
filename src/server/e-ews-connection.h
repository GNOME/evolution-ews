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

	void (*authenticate) (EEwsConnection *cnc);
};

enum {
	EWS_PRIORITY_LOW,
	EWS_PRIORITY_MEDIUM,
	EWS_PRIORITY_HIGH
};

typedef void (*EEwsRequestCreationCallback) (ESoapMessage *msg,
					     gpointer user_data);
typedef void (*EwsProgressFn) (gpointer object, gint percent);

typedef enum {
	EWS_SEARCH_AD,
	EWS_SEARCH_AD_CONTACTS,
	EWS_SEARCH_CONTACTS,
	EWS_SEARCH_CONTACTS_AD

} EwsContactsSearchScope;

typedef enum {
	EWS_HARD_DELETE = 1,
	EWS_SOFT_DELETE,
	EWS_MOVE_TO_DELETED_ITEMS
} EwsDeleteType;

typedef enum {
	EWS_SEND_TO_NONE = 1,
	EWS_SEND_ONLY_TO_ALL,
	EWS_SEND_TO_ALL_AND_SAVE_COPY
} EwsSendMeetingCancellationsType;

typedef enum {
	EWS_ALL_OCCURRENCES = 1,
	EWS_SPECIFIED_OCCURRENCE_ONLY
} EwsAffectedTaskOccurrencesType;

typedef struct {
	gchar *as_url;
	gchar *oab_url;

	/* all the below variables are for future use */
	gchar *oof_url;
	gpointer future1;
	gpointer future2;
} EwsUrls;

typedef struct {
	gchar *id;
	gchar *dn;
	gchar *name;
} EwsOAL;

typedef struct {
	guint32 seq;
	guint32 ver;
	guint32 size;
	guint32 uncompressed_size;
	gchar *sha;
	gchar *filename;
} EwsOALDetails;

typedef struct{
	gchar *sid;
	gchar *primary_smtp_add;
	gchar *display_name;
	gchar *distinguished_user;
	gchar *external_user;
}EwsUserId;

typedef enum{
	NONE,
	EWS_PERM_EDITOR,
	EWS_PERM_REVIEWER,
	EWS_PERM_AUTHOR,
	CUSTOM
}EwsPermissionLevel;

typedef struct{
	EwsUserId *user_id;
	EwsPermissionLevel calendar, contact, inbox, tasks, notes, journal;
	gboolean meetingcopies;
	gboolean view_priv_items;
}EwsDelegateInfo;

typedef enum {
	NORMAL_FIELD_URI,
	INDEXED_FIELD_URI,
	EXTENDED_FIELD_URI
} EwsFieldURIType;

typedef struct {
	gchar *distinguished_prop_set_id;
	gchar *prop_set_id;
	gchar *prop_tag;
	gchar *prop_name;
	gchar *prop_id;
	gchar *prop_type;	
} EwsExtendedFieldURI;

typedef struct {
	gchar *field_uri;
	gchar *field_index;
} EwsIndexedFieldURI;

typedef struct {
	gchar *field_uri;
	GSList *extended_furis;
	GSList *indexed_furis;
} EwsAdditionalProps;

typedef struct {
	gchar *order;
	gint uri_type;
	gpointer field_uri;
} EwsSortOrder;

typedef struct {
	gchar *state;		/*Out of office state*/
	gchar *ext_aud;		/*external audience*/
	time_t start_tm;	/*Start time*/
	time_t end_tm;		/*End time*/
	gchar *int_reply;	/*Internal Reply*/
	gchar *ext_reply;	/*External Reply*/
} OOFSettings;

GType		e_ews_connection_get_type	(void);
EEwsConnection *e_ews_connection_new		(const gchar *uri,
						 const gchar *username,
						 const gchar *password,
						 GCallback authenticate_cb,
						 gpointer authenticate_ctx,
						 GError **error);
EEwsConnection *e_ews_connection_find		(const gchar *uri,
						 const gchar *username);
void		e_ews_connection_authenticate	(EEwsConnection *cnc,
						 SoupAuth *auth,
						 const gchar *user,
						 const gchar *passwd,
						 GError *error);

typedef void (*EEwsAutoDiscoverCallback) (EwsUrls *urls, gpointer user_data, GError *error);
void		e_ews_autodiscover_ws_url	(EEwsAutoDiscoverCallback cb,
						 gpointer cbdata,
						 const gchar *email,
						 const gchar *password,
						 const gchar *ews_url,
						 const gchar *username);
void		e_ews_connection_set_mailbox	(EEwsConnection *cnc,
						 const gchar *email);

void		ews_user_id_free		(EwsUserId *id);

/* Sync folder items */
void		e_ews_connection_sync_folder_items_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *sync_state,
						 const gchar *fid,
						 const gchar *default_props,
						 const gchar *additional_props,
						 guint max_entries,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_sync_folder_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
					 	 gchar **sync_state,
						 gboolean *includes_last_item,
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GError **error);
gboolean	e_ews_connection_sync_folder_items
						(EEwsConnection *cnc,
						 gint pri,
						 gchar **sync_state,
						 const gchar *fid,
						 const gchar *default_props,
						 const gchar *additional_props,
						 guint max_entries,
						 gboolean *includes_last_item,
						 GSList **items_created,
						 GSList **items_updated,
						 GSList **items_deleted,
						 GCancellable *cancellable,
						 GError **error);

typedef void	(*EwsConvertQueryCallback) (ESoapMessage *msg,
						 const gchar *query,
						 EwsFolderType type);

void		e_ews_connection_find_folder_items_start
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *fid,
						 const gchar *props,
						 EwsAdditionalProps *add_props,
						 EwsSortOrder *sort_order,
						 const gchar *query,
						 EwsFolderType type,
						 EwsConvertQueryCallback convert_query_cb,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_find_folder_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 gboolean *includes_last_item,
						 GSList **items,
						 GError **error);

gboolean	e_ews_connection_find_folder_items
						(EEwsConnection *cnc,
						 gint pri,
						 EwsFolderId *fid,
						 const gchar *default_props,
						 EwsAdditionalProps *add_props,
						 EwsSortOrder *sort_order,
						 const gchar *query,
						 EwsFolderType type,
						 gboolean *includes_last_item,
						 GSList **items,
						 EwsConvertQueryCallback convert_query_cb,
						 GCancellable *cancellable,
						 GError **error);


/* Get folder items */
void		e_ews_connection_get_items_start
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 const gchar *default_props,
						 const gchar *additional_props,
						 gboolean include_mime,
						 const gchar *mime_directory,
						 GAsyncReadyCallback cb,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_get_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **items,
						 GError **error);

gboolean	e_ews_connection_get_items	(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 const gchar *default_props,
						 const gchar *additional_props,
						 gboolean include_mime,
						 const gchar *mime_directory,
						 GSList **items,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);
/* Delete folder items */
void		e_ews_connection_delete_items_start	(EEwsConnection *cnc,
					 gint pri,
					 GSList *ids,
					 EwsDeleteType delete_type,
					 EwsSendMeetingCancellationsType send_cancels,
					 EwsAffectedTaskOccurrencesType affected_tasks,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data);

void		e_ews_connection_delete_item_start	(EEwsConnection *cnc,
					 gint pri,
					 EwsId *id,
					 guint index,
					 EwsDeleteType delete_type,
					 EwsSendMeetingCancellationsType send_cancels,
					 EwsAffectedTaskOccurrencesType affected_tasks,
					 GAsyncReadyCallback cb,
					 GCancellable *cancellable,
					 gpointer user_data);

gboolean	e_ews_connection_delete_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);

gboolean	e_ews_connection_delete_items	(EEwsConnection *cnc,
						 gint pri,
						 GSList *ids,
						 EwsDeleteType delete_type,
						 EwsSendMeetingCancellationsType send_cancels,
						 EwsAffectedTaskOccurrencesType affected_tasks,
						 GCancellable *cancellable,
						 GError **error);

gboolean
e_ews_connection_delete_item	(EEwsConnection *cnc,
				 gint pri,
				 EwsId *id,
				 guint index,
				 EwsDeleteType delete_type,
				 EwsSendMeetingCancellationsType send_cancels,
				 EwsAffectedTaskOccurrencesType affected_tasks,
				 GCancellable *cancellable,
				 GError **error);

/* Update folder items */
void		e_ews_connection_update_items_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *conflict_res,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const gchar *folder_id,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_update_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **ids,
						 GError **error);

gboolean	e_ews_connection_update_items	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *conflict_res,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const gchar *folder_id,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GSList **ids,
						 GCancellable *cancellable,
						 GError **error);
/* Create folder items */
void		e_ews_connection_create_items_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const gchar *folder_id,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_create_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **ids,
						 GError **error);

gboolean	e_ews_connection_create_items	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *msg_disposition,
						 const gchar *send_invites,
						 const gchar *folder_id,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GSList **ids,
						 GCancellable *cancellable,
						 GError **error);

/* Sync folder hierarchy */
gboolean	e_ews_connection_sync_folder_hierarchy
						(EEwsConnection *cnc,
						 gint pri,
						 gchar **sync_state,
						 gboolean *includes_last_folder,
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
gboolean	e_ews_connection_sync_folder_hierarchy_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 gchar **sync_state,
						 gboolean *includes_last_folder,
						 GSList **folders_created,
						 GSList **folders_updated,
						 GSList **folders_deleted,
						 GError **error);
/* Resolve names */
void		e_ews_connection_resolve_names_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *resolve_name,
						 EwsContactsSearchScope scope,
						 GSList *parent_folder_ids,
						 gboolean fetch_contact_data,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);
gboolean	e_ews_connection_resolve_names_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **mailboxes,
						 GSList **contact_items,
						 gboolean *includes_last_item,
						 GError **error);
gboolean	e_ews_connection_resolve_names	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *resolve_name,
						 EwsContactsSearchScope scope,
						 GSList *parent_folder_ids,
						 gboolean fetch_contact_data,
						 GSList **mailboxes,
						 GSList **contact_items,
						 gboolean *includes_last_item,
						 GCancellable *cancellable,
						 GError **error);
/* Expand distribution lists */
void		e_ews_connection_expand_dl_start
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsMailbox *mb,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);
gboolean	e_ews_connection_expand_dl_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **mailboxes,
						 gboolean *includes_last_item,
						 GError **error);
gboolean	e_ews_connection_expand_dl	(EEwsConnection *cnc,
						 gint pri,
						 const EwsMailbox *mb,
						 GSList **mailboxes,
						 gboolean *includes_last_item,
						 GCancellable *cancellable,
						 GError **error);

/*Create Folder*/

void		e_ews_connection_create_folder_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *parent_folder_id,
						 gboolean is_distinguished_id,
						 const gchar *folder_name,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);
gboolean	e_ews_connection_create_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 EwsFolderId **folder_id,
						 GError **error);
gboolean	e_ews_connection_create_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *parent_folder_id,
						 gboolean is_distinguished_id,
						 const gchar *folder_name,
						 EwsFolderId **folder_id,
						 GCancellable *cancellable,
						 GError **error);

/*Delete Folder*/

void		e_ews_connection_delete_folder_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean is_distinguished_id,
						 const gchar *delete_type,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_delete_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);

gboolean	e_ews_connection_delete_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean is_distinguished_id,
						 const gchar *delete_type,
						 GCancellable *cancellable,
						 GError **error);

/* Update (e.g. rename) folder */
void		e_ews_connection_update_folder_start
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_update_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);

gboolean	e_ews_connection_update_folder	(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback create_cb,
						 gpointer create_user_data,
						 GCancellable *cancellable,
						 GError **error);

/* Move folder (to new parent; for rename we need update_folder) */
void		e_ews_connection_move_folder_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *to_folder,
						 const gchar *folder,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_move_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);

gboolean	e_ews_connection_move_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *to_folder,
						 const gchar *folder,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_get_folder_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_shape,
						 EwsAdditionalProps *add_props,
						 GSList *folder_ids,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_get_folder_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **folders,
						 GError **error);

gboolean	e_ews_connection_get_folder	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_shape,
						 EwsAdditionalProps *add_props,
						 GSList *folder_ids,
						 GSList **folders,
						 GCancellable *cancellable,
						 GError **error);

/* Copy and Move item */
void		e_ews_connection_move_items_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean docopy,
						 GSList *ids,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_move_items_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **items,
						 GError **error);

gboolean	e_ews_connection_move_items	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *folder_id,
						 gboolean docopy,
						 GSList *ids,
						 GSList **items_ret,
						 GCancellable *cancellable,
						 GError **error);

/* Create attachemnts and associate them with an item */
void		e_ews_connection_create_attachments_start
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsId *parent,
						 const GSList *files,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

GSList *	e_ews_connection_create_attachments_finish
						(EEwsConnection *cnc,
						 gchar **change_key,
						 GAsyncResult *result,
						 GError **error);

GSList *	e_ews_connection_create_attachments
						(EEwsConnection *cnc,
						 gint pri,
						 const EwsId *parent,
						 const GSList *files,
						 gchar **change_key,
						 GCancellable *cancellable,
						 GError **error);

/* Delete attachemnts */
void		e_ews_connection_delete_attachments_start
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);

GSList *	e_ews_connection_delete_attachments_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);

GSList *	e_ews_connection_delete_attachments
						(EEwsConnection *cnc,
						 gint pri,
						 const GSList *ids,
						 GCancellable *cancellable,
						 GError **error);

/* Get attachments items */
void		e_ews_connection_get_attachments_start
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *comp_uid,
						 const GSList *ids,
						 const gchar *cache,
						 gboolean include_mime,
						 GAsyncReadyCallback cb,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 gpointer user_data);

GSList *	e_ews_connection_get_attachments_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **items,
						 GError **error);

GSList *	e_ews_connection_get_attachments
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *comp_uid,
						 GSList *ids,
						 const gchar *cache,
						 gboolean include_mime,
						 GSList **items,
						 ESoapProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_get_oal_list_start
						(EEwsConnection *cnc,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);
gboolean	e_ews_connection_get_oal_list_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **oals,
						 GError **error);
void		e_ews_connection_get_oal_detail_start
						(EEwsConnection *cnc,
						 const gchar *oal_id,
						 const gchar *oal_element,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);
gboolean	e_ews_connection_get_oal_detail_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **elements,
						 GError **error);
gboolean	e_ews_connection_get_oal_detail 
						(EEwsConnection *cnc,
						 const gchar *oal_id,
						 const gchar *oal_element,
						 GSList **elements,
						 GCancellable *cancellable,
						 GError **error);

/* Get free-busy status */
void		e_ews_connection_get_free_busy_start
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback free_busy_cb,
						 gpointer free_busy_user_data,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);
gboolean	e_ews_connection_get_free_busy_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GSList **free_busy,
						 GError **error);
gboolean	e_ews_connection_get_free_busy
						(EEwsConnection *cnc,
						 gint pri,
						 EEwsRequestCreationCallback free_busy_cb,
						 gpointer create_user_data,
						 GSList **free_busy,
						 GCancellable *cancellable,
						 GError **error);
void		e_ews_connection_download_oal_file_start
						(EEwsConnection *cnc,
						 const gchar *cache_filename,
						 GAsyncReadyCallback cb,
						 EwsProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 gpointer user_data);

gboolean	e_ews_connection_download_oal_file_finish
						(EEwsConnection *cnc,
						 GAsyncResult *result,
						 GError **error);
gboolean	e_ews_connection_download_oal_file	
						(EEwsConnection *cnc,
						 const gchar *cache_filename,
						 EwsProgressFn progress_fn,
						 gpointer progress_data,
						 GCancellable *cancellable,
						 GError **error);
/*Get Delegation*/
void e_ews_connection_get_delegate_start	(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const gchar *include_permissions,
						 GAsyncReadyCallback cb,
						 GCancellable *cancellable,
						 gpointer user_data);
gboolean e_ews_connection_get_delegate_finish	(EEwsConnection *cnc,
						 GAsyncResult *result,
						 EwsDelegateInfo **get_delegate,
						 GError **error);
gboolean e_ews_connection_get_delegate	
						(EEwsConnection *cnc,
						 gint pri,
						 const gchar *mail_id,
						 const gchar *include_permissions,
						 EwsDelegateInfo **get_delegate,
						 GCancellable *cancellable,
						 GError **error);

void		e_ews_connection_get_oof_settings_start	(EEwsConnection *cnc,
							 gint pri,
							 GAsyncReadyCallback cb,
							 GCancellable *cancellable,
							 gpointer user_data);

gboolean	e_ews_connection_get_oof_settings_finish (EEwsConnection *cnc,
							  GAsyncResult *result,
							  OOFSettings **oof_settings,
							  GError **error);

gboolean	e_ews_connection_get_oof_settings (EEwsConnection *cnc,
						   gint pri,
						   OOFSettings **oof_settings,
						   GCancellable *cancellable,
						   GError **error);

void		e_ews_connection_set_oof_settings_start	(EEwsConnection *cnc,
							 gint pri,
							 OOFSettings *oof_settings,
							 GAsyncReadyCallback cb,
							 GCancellable *cancellable,
							 gpointer user_data);

gboolean	e_ews_connection_set_oof_settings_finish (EEwsConnection *cnc,
							  GAsyncResult *result,
						 	  GError **error);

gboolean	e_ews_connection_set_oof_settings (EEwsConnection *cnc,
					 	   gint pri,
					 	   OOFSettings *oof_settings,
					 	   GCancellable *cancellable,
					 	   GError **error);

void		e_ews_connection_free_oof_settings (OOFSettings *oof_settings);

G_END_DECLS

#endif
