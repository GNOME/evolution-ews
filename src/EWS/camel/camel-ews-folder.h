/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Sivaiah Nallagatla <snallagatla@novell.com>
 * SPDX-FileContributor: parthasarathi susarla <sparthasarathi@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_FOLDER_H
#define CAMEL_EWS_FOLDER_H

#include <camel/camel.h>

#include "camel-ews-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_FOLDER \
	(camel_ews_folder_get_type ())
#define CAMEL_EWS_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_FOLDER, CamelEwsFolder))
#define CAMEL_EWS_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_FOLDER, CamelEwsFolderClass))
#define CAMEL_IS_EWS_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_FOLDER))
#define CAMEL_IS_EWS_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_FOLDER))
#define CAMEL_EWS_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_FOLDER, CamelEwsFolderClass))

G_BEGIN_DECLS

typedef struct _CamelEwsFolder CamelEwsFolder;
typedef struct _CamelEwsFolderClass CamelEwsFolderClass;
typedef struct _CamelEwsFolderPrivate CamelEwsFolderPrivate;

struct _CamelEwsFolder {
	CamelOfflineFolder parent;
	CamelEwsFolderPrivate *priv;

	CamelDataCache *cache;
};

struct _CamelEwsFolderClass {
	CamelOfflineFolderClass parent_class;
};

GType camel_ews_folder_get_type (void);

/* implemented */
CamelFolder * camel_ews_folder_new (CamelStore *store, const gchar *folder_dir, const gchar *folder_name, GCancellable *cancellable, GError **error);
void ews_update_summary ( CamelFolder *folder, GList *item_list, GCancellable *cancellable, GError **error);
void		camel_ews_folder_remove_cached_message	(CamelEwsFolder *ews_folder,
							 const gchar *uid);
gboolean	camel_ews_folder_get_apply_filters	(CamelEwsFolder *self);
void		camel_ews_folder_set_apply_filters	(CamelEwsFolder *sefl,
							 gboolean apply_filters);
gboolean	camel_ews_folder_get_check_folder	(CamelEwsFolder *self);
void		camel_ews_folder_set_check_folder	(CamelEwsFolder *self,
							 gboolean check_folder);

G_END_DECLS

#endif /* CAMEL_EWS_FOLDER_H */
