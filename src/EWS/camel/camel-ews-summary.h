/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: parthasrathi susarla <sparthasrathi@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_SUMMARY_H
#define CAMEL_EWS_SUMMARY_H

#include <camel/camel.h>

#include "camel-ews-message-info.h"

#define CAMEL_EWS_SUMMARY_VERSION (3)

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
typedef struct _CamelEwsSummaryPrivate CamelEwsSummaryPrivate;

struct _CamelEwsSummary {
	CamelFolderSummary parent;
	CamelEwsSummaryPrivate *priv;
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
					 const CamelNamedFlags *server_user_flags);
gboolean
	camel_ews_summary_add_message	(CamelFolderSummary *summary,
					 const gchar *uid,
					 const gchar *change_key,
					 CamelMessageInfo *info,
					 CamelMimeMessage *message);
void	ews_summary_clear		(CamelFolderSummary *summary,
					 gboolean uncache);
gint32	camel_ews_summary_get_version	(CamelEwsSummary *ews_summary);
void	camel_ews_summary_set_sync_state(CamelEwsSummary *ews_summary,
					 const gchar *sync_state);
gchar *	camel_ews_summary_dup_sync_state(CamelEwsSummary *ews_summary);
guint	camel_ews_summary_get_sync_tag_stamp
					(CamelEwsSummary *ews_summary);
void	camel_ews_summary_set_sync_tag_stamp
					(CamelEwsSummary *ews_summary,
					 guint value);

G_END_DECLS

#endif /* CAMEL_EWS_SUMMARY_H */
