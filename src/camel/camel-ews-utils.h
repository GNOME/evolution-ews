/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_EWS_UTILS_H
#define CAMEL_EWS_UTILS_H

#include <camel/camel.h>
#include <e-ews-connection.h>
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

/*for syncing flags back to server*/
typedef struct {
	guint32 changed;
	guint32 bits;
} flags_diff_t;

/* FIXME: deprecated
   This is used exclusively for the legacy imap cache code.  DO NOT use this in any new code */

typedef gboolean (*EPathFindFoldersCallback) (const gchar *physical_path,
					      const gchar *path,
					      gpointer user_data);

gchar *   e_path_to_physical  (const gchar *prefix, const gchar *vpath);

gboolean e_path_find_folders (const gchar *prefix,
			      EPathFindFoldersCallback callback,
			      gpointer data);

gint      e_path_rmdir        (const gchar *prefix, const gchar *vpath);

void do_flags_diff (flags_diff_t *diff, guint32 old, guint32 _new);
gchar *ews_concat ( const gchar *prefix, const gchar *suffix);
void strip_lt_gt (gchar **string, gint s_offset, gint e_offset);

void	ews_utils_sync_folders	(CamelEwsStore *ews_store, 
				 GSList *created_folders, 
				 GSList *deleted_folders, 
				 GSList *updated_folders);

CamelFolderInfo *
	camel_ews_utils_build_folder_info	(CamelEwsStore *store, 
						 const gchar *fname);

void	camel_ews_utils_sync_deleted_items	(CamelEwsFolder *ews_folder, 
						 GSList *items_deleted);
void	camel_ews_utils_sync_created_items	(CamelEwsFolder *ews_folder, 
						 GSList *items_created);
void	camel_ews_utils_sync_updated_items	(CamelEwsFolder *ews_folder, 
						 GSList *items_updated);

gboolean ews_utils_rename_folder (CamelEwsStore *store, EwsFolderType ftype,
				  const gchar *fid, const gchar *changekey, const gchar *pfid,
				  const gchar *folder_name, const gchar *display_name,
				  GError **error);

G_END_DECLS

#endif
