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

#ifndef CAMEL_O365_FOLDER_H
#define CAMEL_O365_FOLDER_H

#include <camel/camel.h>

#include "camel-o365-folder-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_O365_FOLDER \
	(camel_o365_folder_get_type ())
#define CAMEL_O365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_O365_FOLDER, CamelO365Folder))
#define CAMEL_O365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_O365_FOLDER, CamelO365FolderClass))
#define CAMEL_IS_O365_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_O365_FOLDER))
#define CAMEL_IS_O365_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_O365_FOLDER))
#define CAMEL_O365_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_O365_FOLDER, CamelO365FolderClass))

G_BEGIN_DECLS

typedef struct _CamelO365Folder CamelO365Folder;
typedef struct _CamelO365FolderClass CamelO365FolderClass;
typedef struct _CamelO365FolderPrivate CamelO365FolderPrivate;

struct _CamelO365Folder {
	CamelOfflineFolder parent;
	CamelO365FolderPrivate *priv;
};

struct _CamelO365FolderClass {
	CamelOfflineFolderClass parent_class;
};

GType		camel_o365_folder_get_type	(void);

CamelFolder *	camel_o365_folder_new		(CamelStore *store,
						 const gchar *display_name,
						 const gchar *full_name,
						 const gchar *folder_dir,
						 GCancellable *cancellable,
						 GError **error);
const gchar *	camel_o365_folder_get_id	(CamelO365Folder *o365_folder);

G_END_DECLS

#endif /* CAMEL_O365_FOLDER_H */
