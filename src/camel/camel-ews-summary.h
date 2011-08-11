/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors:
 *	parthasarathi susarla <sparthasarathi@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_GW_SUMMARY_H
#define CAMEL_GW_SUMMARY_H

#include <camel/camel.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-db.h>
#include <camel/camel-exception.h>
#include <camel/camel-store.h>

/* Standard GObject macros */
#define CAMEL_EWS_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_ews_summary_get_type (), CamelEwsSummary)
#define CAMEL_EWS_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_ews_summary_get_type (), CamelEwsSummaryClass)
#define CAMEL_IS_EWS_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_ews_summary_get_type ())


G_BEGIN_DECLS

typedef struct _CamelEwsSummary CamelEwsSummary;
typedef struct _CamelEwsSummaryClass CamelEwsSummaryClass;
typedef struct _CamelEwsMessageInfo CamelEwsMessageInfo;
typedef struct _CamelEwsMessageContentInfo CamelEwsMessageContentInfo;

/* extra summary flags*/
enum {
	CAMEL_GW_MESSAGE_JUNK = 1<<17,
	CAMEL_GW_MESSAGE_NOJUNK = 1<<18
};

struct _CamelEwsMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
	gint32 item_type;
	gchar *change_key;
} ;

struct _CamelEwsMessageContentInfo {
	CamelMessageContentInfo info;
} ;

struct _CamelEwsSummary {
	CamelFolderSummary parent;

	gchar *sync_state;
	gint32 version;
} ;

struct _CamelEwsSummaryClass {
	CamelFolderSummaryClass parent_class;
} ;

CamelType camel_ews_summary_get_type (void);

CamelFolderSummary *
	camel_ews_summary_new		(struct _CamelFolder *folder,
					 const gchar *filename);
gboolean
	camel_ews_update_message_info_flags
					(CamelFolderSummary *summary,
					 CamelMessageInfo *info,
					 guint32 server_flags,
					 CamelFlag *server_user_flags);
void	camel_ews_summary_add_message	(CamelFolderSummary *summary,
					 const gchar *uid,
					 CamelMimeMessage *message);
void	camel_ews_summary_add_message_info
					(CamelFolderSummary *summary,
					 guint32 server_flags,
					 CamelMessageInfo *info);
void
camel_ews_summary_delete_id		(CamelFolderSummary *summary,
					 const gchar *id);
void	ews_summary_clear		(CamelFolderSummary *summary,
					 gboolean uncache);

G_END_DECLS

#endif /* CAMEL_GW_SUMMARY_H */
