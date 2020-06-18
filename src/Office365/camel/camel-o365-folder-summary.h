/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CAMEL_O365_FOLDER_SUMMARY_H
#define CAMEL_O365_FOLDER_SUMMARY_H

#include <camel/camel.h>

#include "camel-o365-message-info.h"

#define CAMEL_O365_FOLDER_SUMMARY_VERSION (1)

/* Standard GObject macros */
#define CAMEL_TYPE_O365_FOLDER_SUMMARY \
	(camel_o365_folder_summary_get_type ())
#define CAMEL_O365_FOLDER_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_O365_FOLDER_SUMMARY, CamelO365FolderSummary))
#define CAMEL_O365_FOLDER_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_O365_FOLDER_SUMMARY, CamelO365FolderSummaryClass))
#define CAMEL_IS_O365_FOLDER_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_O365_FOLDER_SUMMARY))
#define CAMEL_IS_O365_FOLDER_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_O365_FOLDER_SUMMARY))
#define CAMEL_O365_FOLDER_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_O365_FOLDER_SUMMARY, CamelO365FolderSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelO365FolderSummary CamelO365FolderSummary;
typedef struct _CamelO365FolderSummaryClass CamelO365FolderSummaryClass;
typedef struct _CamelO365FolderSummaryPrivate CamelO365FolderSummaryPrivate;

struct _CamelO365FolderSummary {
	CamelFolderSummary parent;
	CamelO365FolderSummaryPrivate *priv;
};

struct _CamelO365FolderSummaryClass {
	CamelFolderSummaryClass parent_class;
};

GType		camel_o365_folder_summary_get_type	(void);

CamelFolderSummary *
		camel_o365_folder_summary_new		(CamelFolder *folder);
gint		camel_o365_folder_summary_get_version	(CamelO365FolderSummary *o365_summary);
void		camel_o365_folder_summary_set_delta_link(CamelO365FolderSummary *o365_summary,
							 const gchar *delta_link);
gchar *		camel_o365_folder_summary_dup_delta_link(CamelO365FolderSummary *o365_summary);
void		camel_o365_folder_summary_clear		(CamelFolderSummary *summary);
gboolean	camel_o365_folder_summary_update_message_info_flags
							(CamelFolderSummary *summary,
							 CamelMessageInfo *info,
							 guint32 server_flags,
							 const CamelNamedFlags *server_user_flags);
gboolean	camel_o365_folder_summary_add_message	(CamelFolderSummary *summary,
							 const gchar *uid,
							 const gchar *change_key,
							 CamelMessageInfo *info,
							 CamelMimeMessage *message);

G_END_DECLS

#endif /* CAMEL_O365_FOLDER_SUMMARY_H */
