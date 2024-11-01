/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_UTILS_H
#define CAMEL_EWS_UTILS_H

#include <camel/camel.h>

#include "common/e-ews-connection.h"

#include "camel-ews-store.h"
#include "camel-ews-folder.h"

/*Headers for send options*/
#define X_SEND_OPTIONS        "X-ews-send-options"
/*General Options*/
#define X_SEND_OPT_PRIORITY   "X-ews-send-opt-priority"
#define X_SEND_OPT_SECURITY   "X-ews-send-opt-security"
#define X_REPLY_CONVENIENT    "X-reply-convenient"
#define X_REPLY_WITHIN        "X-reply-within"
#define X_EXPIRE_AFTER        "X-expire-after"
#define X_DELAY_UNTIL         "X-delay-until"

/*Status Tracking Options*/
#define X_TRACK_WHEN            "X-track-when"
#define X_AUTODELETE            "X-auto-delete"
#define X_RETURN_NOTIFY_OPEN    "X-return-notify-open"
#define X_RETURN_NOTIFY_DELETE  "X-return-notify-delete"

/* Folder types for source */
#define RECEIVED  "Mailbox"
#define SENT	  "Sent Items"
#define DRAFT	  ""
#define PERSONAL  "Cabinet"

G_BEGIN_DECLS

void		ews_utils_sync_folders		(CamelEwsStore *ews_store,
						 GSList *created_folders,
						 GSList *deleted_folders,
						 GSList *updated_folders,
						 GSList **created_folder_ids);
CamelFolderInfo *
		camel_ews_utils_build_folder_info
						(CamelEwsStore *store,
						 const gchar *fid);
void		camel_ews_utils_sync_deleted_items
						(CamelEwsFolder *ews_folder,
						 GSList *items_deleted,
						 CamelFolderChangeInfo *change_info);
void		camel_ews_utils_sync_created_items
						(CamelEwsFolder *ews_folder,
						 EEwsConnection *cnc,
						 gboolean is_drafts_folder,
						 GSList *items_created,
						 CamelFolderChangeInfo *change_info,
						 GCancellable *cancellable);
void		camel_ews_utils_sync_updated_items
						(CamelEwsFolder *ews_folder,
						 EEwsConnection *cnc,
						 gboolean is_drafts_folder,
						 GSList *items_updated,
						 CamelFolderChangeInfo *change_info,
						 GCancellable *cancellable);
GSList *	ews_utils_gather_server_user_flags
						(ESoapRequest *request,
						 CamelMessageInfo *mi);
void		ews_utils_update_followup_flags (ESoapRequest *request,
						 CamelMessageInfo *mi);
gchar *		camel_ews_utils_get_host_name	(CamelSettings *settings);
gboolean	camel_ews_utils_delete_folders_from_summary_recursive
						(CamelEwsStore *ews_store,
						 CamelFolderInfo *folder_info,
						 gboolean send_signals,
						 GError **error);
ESource *	camel_ews_utils_ref_corresponding_source
						(CamelService *service,
						 GCancellable *cancellable);
CamelMessageInfo * /* (transfer full) */
		camel_ews_utils_item_to_message_info
						(CamelEwsFolder *ews_folder,
						 EEwsConnection *cnc,
						 EEwsItem *item,
						 GCancellable *cancellable);
gboolean	camel_ews_utils_folder_is_drafts_folder
						(CamelEwsFolder *ews_folder);
void		camel_ews_utils_merge_category_list
						(CamelEwsStore *ews_store,
						 const guchar *xml_data,
						 gsize xml_data_len);

G_END_DECLS

#endif
