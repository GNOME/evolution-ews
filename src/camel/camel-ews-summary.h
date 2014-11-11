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

#ifndef CAMEL_EWS_SUMMARY_H
#define CAMEL_EWS_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_SUMMARY \
	(camel_ews_summary_get_type ())
#define CAMEL_EWS_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_SUMMARY, CamelEwsSummary))
#define CAMEL_EWS_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_SUMMARY, CamelEwsSummaryClass))
#define CAMEL_IS_EWS_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_SUMMARY))
#define CAMEL_IS_EWS_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_SUMMARY))
#define CAMEL_EWS_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_SUMMARY, CamelEwsSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelEwsSummary CamelEwsSummary;
typedef struct _CamelEwsSummaryClass CamelEwsSummaryClass;
typedef struct _CamelEwsMessageInfo CamelEwsMessageInfo;
typedef struct _CamelEwsMessageContentInfo CamelEwsMessageContentInfo;

/* extra summary flags*/
enum {
	CAMEL_EWS_MESSAGE_MSGFLAG_RN_PENDING = CAMEL_MESSAGE_FOLDER_FLAGGED << 1
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

GType camel_ews_summary_get_type (void);

CamelFolderSummary *
	camel_ews_summary_new		(struct _CamelFolder *folder);
gboolean
	camel_ews_update_message_info_flags
					(CamelFolderSummary *summary,
					 CamelMessageInfo *info,
					 guint32 server_flags,
					 CamelFlag *server_user_flags);
gboolean
	camel_ews_summary_add_message	(CamelFolderSummary *summary,
					 const gchar *uid,
					 const gchar *change_key,
					 CamelMessageInfo *info,
					 CamelMimeMessage *message);
void	ews_summary_clear		(CamelFolderSummary *summary,
					 gboolean uncache);

G_END_DECLS

#endif /* CAMEL_EWS_SUMMARY_H */
