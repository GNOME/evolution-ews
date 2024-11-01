/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_FOLDER_H
#define CAMEL_M365_FOLDER_H

#include <camel/camel.h>

#include "camel-m365-folder-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_M365_FOLDER \
	(camel_m365_folder_get_type ())
#define CAMEL_M365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_FOLDER, CamelM365Folder))
#define CAMEL_M365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_FOLDER, CamelM365FolderClass))
#define CAMEL_IS_M365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_FOLDER))
#define CAMEL_IS_M365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_FOLDER))
#define CAMEL_M365_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_FOLDER, CamelM365FolderClass))

G_BEGIN_DECLS

typedef struct _CamelM365Folder CamelM365Folder;
typedef struct _CamelM365FolderClass CamelM365FolderClass;
typedef struct _CamelM365FolderPrivate CamelM365FolderPrivate;

struct _CamelM365Folder {
	CamelOfflineFolder parent;
	CamelM365FolderPrivate *priv;
};

struct _CamelM365FolderClass {
	CamelOfflineFolderClass parent_class;
};

GType		camel_m365_folder_get_type	(void);

CamelFolder *	camel_m365_folder_new		(CamelStore *store,
						 const gchar *display_name,
						 const gchar *full_name,
						 const gchar *folder_dir,
						 GCancellable *cancellable,
						 GError **error);
const gchar *	camel_m365_folder_get_id	(CamelM365Folder *m365_folder);
gboolean	camel_m365_folder_get_apply_filters
						(CamelM365Folder *self);
void		camel_m365_folder_set_apply_filters
						(CamelM365Folder *self,
						 gboolean apply_filters);
gboolean	camel_m365_folder_get_check_folder
						(CamelM365Folder *self);
void		camel_m365_folder_set_check_folder
						(CamelM365Folder *self,
						 gboolean check_folder);

G_END_DECLS

#endif /* CAMEL_M365_FOLDER_H */
