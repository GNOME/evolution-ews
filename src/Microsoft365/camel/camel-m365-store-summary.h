/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_STORE_SUMMARY_H
#define CAMEL_M365_STORE_SUMMARY_H

#include <camel/camel.h>

#include "common/e-m365-enums.h"

/* Standard GObject macros */
#define CAMEL_TYPE_M365_STORE_SUMMARY \
	(camel_m365_store_summary_get_type ())
#define CAMEL_M365_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_STORE_SUMMARY, CamelM365StoreSummary))
#define CAMEL_M365_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_STORE_SUMMARY, CamelM365StoreSummaryClass))
#define CAMEL_IS_M365_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_STORE_SUMMARY))
#define CAMEL_IS_M365_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_STORE_SUMMARY))
#define CAMEL_M365_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_STORE_SUMMARY, CamelM365StoreSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelM365StoreSummary CamelM365StoreSummary;
typedef struct _CamelM365StoreSummaryClass CamelM365StoreSummaryClass;
typedef struct _CamelM365StoreSummaryPrivate CamelM365StoreSummaryPrivate;

struct _CamelM365StoreSummary {
	GObject parent;
	CamelM365StoreSummaryPrivate *priv;
};

struct _CamelM365StoreSummaryClass {
	GObjectClass parent_class;
};

GType		camel_m365_store_summary_get_type	(void);

CamelM365StoreSummary *
		camel_m365_store_summary_new		(const gchar *filename);
gboolean	camel_m365_store_summary_load		(CamelM365StoreSummary *store_summary,
							 GError **error);
gboolean	camel_m365_store_summary_save		(CamelM365StoreSummary *store_summary,
							 GError **error);
void		camel_m365_store_summary_clear		(CamelM365StoreSummary *store_summary);
void		camel_m365_store_summary_lock		(CamelM365StoreSummary *store_summary);
void		camel_m365_store_summary_unlock		(CamelM365StoreSummary *store_summary);
void		camel_m365_store_summary_rebuild_hashes	(CamelM365StoreSummary *store_summary);
void		camel_m365_store_summary_set_delta_link	(CamelM365StoreSummary *store_summary,
							 const gchar *delta_link);
gchar *		camel_m365_store_summary_dup_delta_link	(CamelM365StoreSummary *store_summary);
gboolean	camel_m365_store_summary_has_folder	(CamelM365StoreSummary *store_summary,
							 const gchar *id);
gboolean	camel_m365_store_summary_has_full_name	(CamelM365StoreSummary *store_summary,
							 const gchar *full_name);
void		camel_m365_store_summary_remove_folder	(CamelM365StoreSummary *store_summary,
							 const gchar *id);
void		camel_m365_store_summary_set_folder	(CamelM365StoreSummary *store_summary,
							 gboolean with_hashes_update,
							 const gchar *id,
							 const gchar *parent_id,
							 const gchar *display_name,
							 gint32 total_count,
							 gint32 unread_count,
							 guint32 flags,
							 EM365FolderKind kind,
							 gboolean is_foreign,
							 gboolean is_public);
void		camel_m365_store_summary_update_folder	(CamelM365StoreSummary *store_summary,
							 gboolean with_hashes_update,
							 const gchar *id,
							 const gchar *parent_id,
							 const gchar *display_name,
							 gint32 total_count,
							 gint32 unread_count,
							 gint32 children_count);
gboolean	camel_m365_store_summary_get_folder	(CamelM365StoreSummary *store_summary,
							 const gchar *id,
							 gchar **out_full_name,
							 gchar **out_display_name,
							 gchar **out_parent_id,
							 gint32 *out_total_count,
							 gint32 *out_unread_count,
							 guint32 *out_flags,
							 EM365FolderKind *out_kind,
							 gboolean *out_is_foreign,
							 gboolean *out_is_public);
gchar *		camel_m365_store_summary_dup_folder_full_name
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
gchar *		camel_m365_store_summary_dup_folder_id_for_full_name
							(CamelM365StoreSummary *store_summary,
							 const gchar *full_name);
gchar *		camel_m365_store_summary_dup_folder_id_for_type
							(CamelM365StoreSummary *store_summary,
							 guint32 folder_type); /* The CAMEL_FOLDER_TYPE_... of the CamelFolderInfoFlags */
gboolean	camel_m365_store_summary_set_folder_display_name
							(CamelM365StoreSummary *store_summary,
							 const gchar *id,
							 const gchar *display_name,
							 gboolean with_hashes_update);
gchar *		camel_m365_store_summary_dup_folder_display_name
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
void		camel_m365_store_summary_set_folder_parent_id
							(CamelM365StoreSummary *store_summary,
							 const gchar *id,
							 const gchar *parent_id);
gchar *		camel_m365_store_summary_dup_folder_parent_id
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
void		camel_m365_store_summary_set_folder_total_count
							(CamelM365StoreSummary *store_summary,
							 const gchar *id,
							 gint32 total_count);
gint32		camel_m365_store_summary_get_folder_total_count
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
void		camel_m365_store_summary_set_folder_unread_count
							(CamelM365StoreSummary *store_summary,
							 const gchar *id,
							 gint32 unread_count);
gint32		camel_m365_store_summary_get_folder_unread_count
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
void		camel_m365_store_summary_set_folder_flags
							(CamelM365StoreSummary *store_summary,
							 const gchar *id,
							 guint32 flags);
guint32		camel_m365_store_summary_get_folder_flags
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
guint32		camel_m365_store_summary_get_folder_flags_for_full_name
							(CamelM365StoreSummary *store_summary,
							 const gchar *full_name);
EM365FolderKind	camel_m365_store_summary_get_folder_kind(CamelM365StoreSummary *store_summary,
							 const gchar *id);
gboolean	camel_m365_store_summary_get_folder_is_foreign
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
gboolean	camel_m365_store_summary_get_folder_is_public
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
GSList *	camel_m365_store_summary_list_folder_ids(CamelM365StoreSummary *store_summary); /* gchar *folder_id */
CamelFolderInfo *
		camel_m365_store_summary_build_folder_info_for_id
							(CamelM365StoreSummary *store_summary,
							 const gchar *id);
CamelFolderInfo *
		camel_m365_store_summary_build_folder_info
							(CamelM365StoreSummary *store_summary,
							 const gchar *top,
							 gboolean recursive);
void		camel_m365_store_summary_connect_folder_summary
							(CamelM365StoreSummary *store_summary,
							 CamelFolderSummary *folder_summary);

typedef struct _CamelM365Category {
	gchar *id;
	gchar *display_name;
	gchar *color;
} CamelM365Category;

GHashTable *	camel_m365_store_summary_get_categories /* gchar *id ~> CamelM365Category * */
						(CamelM365StoreSummary *store_summary);
void		camel_m365_store_summary_set_categories
						(CamelM365StoreSummary *store_summary,
						 GHashTable *categories); /* gchar *id ~> CamelM365Category * */

CamelM365Category *
		camel_m365_category_new		(const gchar *id,
						 const gchar *display_name,
						 const gchar *color);
void		camel_m365_category_free	(gpointer ptr); /* CamelM365Category * */

G_END_DECLS

#endif /* CAMEL_M365_STORE_SUMMARY_H */
