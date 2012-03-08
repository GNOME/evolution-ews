/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-folder.h: class for an ews folder */

/*
 * Authors:
 *   Sivaiah Nallagatla <snallagatla@novell.com>
 *   parthasarathi susarla <sparthasarathi@novell.com>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
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

	CamelFolderSearch *search;
	CamelDataCache *cache;
};

struct _CamelEwsFolderClass {
	CamelOfflineFolderClass parent_class;
};

GType camel_ews_folder_get_type (void);

/* implemented */
CamelFolder * camel_ews_folder_new (CamelStore *store, const gchar *folder_dir, const gchar *folder_name, GCancellable *cancellable, GError **error);
void ews_update_summary ( CamelFolder *folder, GList *item_list, GCancellable *cancellable, GError **error);

G_END_DECLS

#endif /* CAMEL_EWS_FOLDER_H */
