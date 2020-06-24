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

#ifndef CAMEL_O365_STORE_SUMMARY_H
#define CAMEL_O365_STORE_SUMMARY_H

#include <camel/camel.h>

#include "common/e-o365-enums.h"

/* Standard GObject macros */
#define CAMEL_TYPE_O365_STORE_SUMMARY \
	(camel_o365_store_summary_get_type ())
#define CAMEL_O365_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_O365_STORE_SUMMARY, CamelO365StoreSummary))
#define CAMEL_O365_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_O365_STORE_SUMMARY, CamelO365StoreSummaryClass))
#define CAMEL_IS_O365_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_O365_STORE_SUMMARY))
#define CAMEL_IS_O365_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_O365_STORE_SUMMARY))
#define CAMEL_O365_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_O365_STORE_SUMMARY, CamelO365StoreSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelO365StoreSummary CamelO365StoreSummary;
typedef struct _CamelO365StoreSummaryClass CamelO365StoreSummaryClass;
typedef struct _CamelO365StoreSummaryPrivate CamelO365StoreSummaryPrivate;

struct _CamelO365StoreSummary {
	GObject parent;
	CamelO365StoreSummaryPrivate *priv;
};

struct _CamelO365StoreSummaryClass {
	GObjectClass parent_class;
};

GType		camel_o365_store_summary_get_type	(void);

CamelO365StoreSummary *
		camel_o365_store_summary_new		(const gchar *filename);
gboolean	camel_o365_store_summary_load		(CamelO365StoreSummary *store_summary,
							 GError **error);
gboolean	camel_o365_store_summary_save		(CamelO365StoreSummary *store_summary,
							 GError **error);
void		camel_o365_store_summary_clear		(CamelO365StoreSummary *store_summary);
void		camel_o365_store_summary_lock		(CamelO365StoreSummary *store_summary);
void		camel_o365_store_summary_unlock		(CamelO365StoreSummary *store_summary);
void		camel_o365_store_summary_rebuild_hashes	(CamelO365StoreSummary *store_summary);
void		camel_o365_store_summary_set_delta_link	(CamelO365StoreSummary *store_summary,
							 const gchar *delta_link);
gchar *		camel_o365_store_summary_dup_delta_link	(CamelO365StoreSummary *store_summary);
gboolean	camel_o365_store_summary_has_folder	(CamelO365StoreSummary *store_summary,
							 const gchar *id);
gboolean	camel_o365_store_summary_has_full_name	(CamelO365StoreSummary *store_summary,
							 const gchar *full_name);
void		camel_o365_store_summary_remove_folder	(CamelO365StoreSummary *store_summary,
							 const gchar *id);
void		camel_o365_store_summary_set_folder	(CamelO365StoreSummary *store_summary,
							 gboolean with_hashes_update,
							 const gchar *id,
							 const gchar *parent_id,
							 const gchar *display_name,
							 gint32 total_count,
							 gint32 unread_count,
							 guint32 flags,
							 EO365FolderKind kind,
							 gboolean is_foreign,
							 gboolean is_public);
void		camel_o365_store_summary_update_folder	(CamelO365StoreSummary *store_summary,
							 gboolean with_hashes_update,
							 const gchar *id,
							 const gchar *parent_id,
							 const gchar *display_name,
							 gint32 total_count,
							 gint32 unread_count,
							 gint32 children_count);
gboolean	camel_o365_store_summary_get_folder	(CamelO365StoreSummary *store_summary,
							 const gchar *id,
							 gchar **out_full_name,
							 gchar **out_display_name,
							 gchar **out_parent_id,
							 gint32 *out_total_count,
							 gint32 *out_unread_count,
							 guint32 *out_flags,
							 EO365FolderKind *out_kind,
							 gboolean *out_is_foreign,
							 gboolean *out_is_public);
gchar *		camel_o365_store_summary_dup_folder_full_name
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
gchar *		camel_o365_store_summary_dup_folder_id_for_full_name
							(CamelO365StoreSummary *store_summary,
							 const gchar *full_name);
gchar *		camel_o365_store_summary_dup_folder_id_for_type
							(CamelO365StoreSummary *store_summary,
							 guint32 folder_type); /* The CAMEL_FOLDER_TYPE_... of the CamelFolderInfoFlags */
gboolean	camel_o365_store_summary_set_folder_display_name
							(CamelO365StoreSummary *store_summary,
							 const gchar *id,
							 const gchar *display_name,
							 gboolean with_hashes_update);
gchar *		camel_o365_store_summary_dup_folder_display_name
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
void		camel_o365_store_summary_set_folder_parent_id
							(CamelO365StoreSummary *store_summary,
							 const gchar *id,
							 const gchar *parent_id);
gchar *		camel_o365_store_summary_dup_folder_parent_id
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
void		camel_o365_store_summary_set_folder_total_count
							(CamelO365StoreSummary *store_summary,
							 const gchar *id,
							 gint32 total_count);
gint32		camel_o365_store_summary_get_folder_total_count
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
void		camel_o365_store_summary_set_folder_unread_count
							(CamelO365StoreSummary *store_summary,
							 const gchar *id,
							 gint32 unread_count);
gint32		camel_o365_store_summary_get_folder_unread_count
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
void		camel_o365_store_summary_set_folder_flags
							(CamelO365StoreSummary *store_summary,
							 const gchar *id,
							 guint32 flags);
guint32		camel_o365_store_summary_get_folder_flags
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
guint32		camel_o365_store_summary_get_folder_flags_for_full_name
							(CamelO365StoreSummary *store_summary,
							 const gchar *full_name);
EO365FolderKind	camel_o365_store_summary_get_folder_kind(CamelO365StoreSummary *store_summary,
							 const gchar *id);
gboolean	camel_o365_store_summary_get_folder_is_foreign
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
gboolean	camel_o365_store_summary_get_folder_is_public
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
CamelFolderInfo *
		camel_o365_store_summary_build_folder_info_for_id
							(CamelO365StoreSummary *store_summary,
							 const gchar *id);
CamelFolderInfo *
		camel_o365_store_summary_build_folder_info
							(CamelO365StoreSummary *store_summary,
							 const gchar *top,
							 gboolean recursive);
void		camel_o365_store_summary_connect_folder_summary
							(CamelO365StoreSummary *store_summary,
							 CamelFolderSummary *folder_summary);
G_END_DECLS

#endif /* CAMEL_O365_STORE_SUMMARY_H */
