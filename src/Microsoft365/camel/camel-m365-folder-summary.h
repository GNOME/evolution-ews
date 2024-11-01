/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_FOLDER_SUMMARY_H
#define CAMEL_M365_FOLDER_SUMMARY_H

#include <camel/camel.h>

#include "camel-m365-message-info.h"

#define CAMEL_M365_FOLDER_SUMMARY_VERSION (2)

/* Standard GObject macros */
#define CAMEL_TYPE_M365_FOLDER_SUMMARY \
	(camel_m365_folder_summary_get_type ())
#define CAMEL_M365_FOLDER_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_FOLDER_SUMMARY, CamelM365FolderSummary))
#define CAMEL_M365_FOLDER_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_FOLDER_SUMMARY, CamelM365FolderSummaryClass))
#define CAMEL_IS_M365_FOLDER_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_FOLDER_SUMMARY))
#define CAMEL_IS_M365_FOLDER_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_FOLDER_SUMMARY))
#define CAMEL_M365_FOLDER_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_FOLDER_SUMMARY, CamelM365FolderSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelM365FolderSummary CamelM365FolderSummary;
typedef struct _CamelM365FolderSummaryClass CamelM365FolderSummaryClass;
typedef struct _CamelM365FolderSummaryPrivate CamelM365FolderSummaryPrivate;

struct _CamelM365FolderSummary {
	CamelFolderSummary parent;
	CamelM365FolderSummaryPrivate *priv;
};

struct _CamelM365FolderSummaryClass {
	CamelFolderSummaryClass parent_class;
};

GType		camel_m365_folder_summary_get_type	(void);

CamelFolderSummary *
		camel_m365_folder_summary_new		(CamelFolder *folder);
gint		camel_m365_folder_summary_get_version	(CamelM365FolderSummary *m365_summary);
void		camel_m365_folder_summary_set_delta_link(CamelM365FolderSummary *m365_summary,
							 const gchar *delta_link);
gchar *		camel_m365_folder_summary_dup_delta_link(CamelM365FolderSummary *m365_summary);
void		camel_m365_folder_summary_clear		(CamelFolderSummary *summary);
gboolean	camel_m365_folder_summary_update_message_info_flags
							(CamelFolderSummary *summary,
							 CamelMessageInfo *info,
							 guint32 server_flags,
							 const CamelNamedFlags *server_user_flags);
gboolean	camel_m365_folder_summary_add_message	(CamelFolderSummary *summary,
							 const gchar *uid,
							 const gchar *change_key,
							 CamelMessageInfo *info,
							 CamelMimeMessage *message);

G_END_DECLS

#endif /* CAMEL_M365_FOLDER_SUMMARY_H */
