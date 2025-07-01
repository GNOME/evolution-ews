/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_STORE_SUMMARY_H
#define CAMEL_EWS_STORE_SUMMARY_H

#include <camel/camel.h>

#include "common/e-ews-enums.h"

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_STORE_SUMMARY \
	(camel_ews_store_summary_get_type ())
#define CAMEL_EWS_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummary))
#define CAMEL_EWS_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummaryClass))
#define CAMEL_IS_EWS_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY))
#define CAMEL_IS_EWS_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_STORE_SUMMARY))
#define CAMEL_EWS_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_STORE_SUMMARY, CamelEwsStoreSummaryClass))

/* the last possible value from CAMEL_FOLDER_TYPE_MASK range */
#define CAMEL_EWS_FOLDER_TYPE_JOURNAL \
	(((CAMEL_FOLDER_TYPE_MASK >> CAMEL_FOLDER_TYPE_BIT) - 1) << \
	CAMEL_FOLDER_TYPE_BIT)

G_BEGIN_DECLS

typedef struct _CamelEwsCategory {
	gchar *guid;
	gchar *name;
	gchar *color_def;
} CamelEwsCategory;

typedef struct _CamelEwsStoreSummary CamelEwsStoreSummary;
typedef struct _CamelEwsStoreSummaryClass CamelEwsStoreSummaryClass;
typedef struct _CamelEwsStoreSummaryPrivate CamelEwsStoreSummaryPrivate;

struct _CamelEwsStoreSummary {
	GObject parent;
	CamelEwsStoreSummaryPrivate *priv;
};

struct _CamelEwsStoreSummaryClass {
	GObjectClass parent_class;
};

GType		camel_ews_store_summary_get_type	(void);

CamelEwsStoreSummary *
		camel_ews_store_summary_new	(const gchar *path);
gboolean	camel_ews_store_summary_load	(CamelEwsStoreSummary *ews_summary,
						 GError **error);
gboolean	camel_ews_store_summary_save	(CamelEwsStoreSummary *ews_summary,
						 GError **error);
gboolean	camel_ews_store_summary_clear	(CamelEwsStoreSummary *ews_summary);
gboolean	camel_ews_store_summary_remove	(CamelEwsStoreSummary *ews_summary);
void		camel_ews_store_summary_rebuild_hashes
						(CamelEwsStoreSummary *ews_summary);

void		camel_ews_store_summary_set_folder_name
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 const gchar *display_name);
void		camel_ews_store_summary_set_parent_folder_id
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 const gchar *parent_id);
void		camel_ews_store_summary_set_change_key
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 const gchar *change_key);
void		camel_ews_store_summary_set_sync_state
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 const gchar *sync_state);
void		camel_ews_store_summary_set_folder_flags
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 guint64 flags);
void		camel_ews_store_summary_set_folder_unread
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 guint64 unread);
void		camel_ews_store_summary_set_folder_total
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 guint64 total);
void		camel_ews_store_summary_set_folder_type
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 EEwsFolderType folder_type);
void		camel_ews_store_summary_set_foreign
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 gboolean is_foreign);
void		camel_ews_store_summary_set_foreign_subfolders
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 gboolean foreign_subfolders);
void		camel_ews_store_summary_set_public
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 gboolean is_public);

gchar *	camel_ews_store_summary_get_folder_name
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *camel_ews_store_summary_get_folder_full_name
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *	camel_ews_store_summary_get_parent_folder_id
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *	camel_ews_store_summary_get_change_key
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
gchar *	camel_ews_store_summary_get_sync_state
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
guint64		camel_ews_store_summary_get_folder_flags
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
guint64		camel_ews_store_summary_get_folder_unread
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
guint64		camel_ews_store_summary_get_folder_total
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
EEwsFolderType	camel_ews_store_summary_get_folder_type
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
gboolean	camel_ews_store_summary_get_foreign
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
gboolean	camel_ews_store_summary_get_foreign_subfolders
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);
gboolean	camel_ews_store_summary_get_public
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);

GSList *	camel_ews_store_summary_get_folders
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *prefix,
						 gboolean only_direct_subfolders);
GSList *	camel_ews_store_summary_get_foreign_folders
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *prefix);

void		camel_ews_store_summary_store_string_val
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *key,
						 const gchar *value);

gchar *	camel_ews_store_summary_get_string_val
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *key,
						 GError **error);

gboolean	camel_ews_store_summary_remove_folder
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 GError **error);

void		camel_ews_store_summary_new_folder
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_id,
						 const gchar *parent_fid,
						 const gchar *change_key,
						 const gchar *display_name,
						 EEwsFolderType folder_type,
						 guint64 folder_flags,
						 guint64 total,
						 gboolean foreign,
						 gboolean public_folder);

gchar *		camel_ews_store_summary_get_folder_id_from_name
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *folder_name);

gchar *		camel_ews_store_summary_get_folder_id_from_folder_type
						(CamelEwsStoreSummary *ews_summary,
						 guint64 folder_type);

gboolean	camel_ews_store_summary_has_folder
						(CamelEwsStoreSummary *ews_summary,
						 const gchar *id);
GHashTable *	camel_ews_store_summary_get_categories /* gchar *guid ~> CamelEwsCategory * */
						(CamelEwsStoreSummary *ews_summary);
void		camel_ews_store_summary_set_categories
						(CamelEwsStoreSummary *ews_summary,
						 GHashTable *categories); /* gchar *guid ~> CamelEwsCategory * */

CamelEwsCategory *
		camel_ews_category_new		(const gchar *guid,
						 const gchar *name,
						 const gchar *color_def);
void		camel_ews_category_free		(gpointer ptr); /* CamelEwsCategory * */

G_END_DECLS

#endif /* CAMEL_EWS_STORE_SUMMARY_H */
