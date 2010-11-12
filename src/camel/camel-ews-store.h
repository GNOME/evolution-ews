/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-store.h : class for an groupwise store */

/*
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
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

#ifndef CAMEL_EWS_STORE_H
#define CAMEL_EWS_STORE_H

#include <camel/camel.h>

#include "camel-ews-store-summary.h"

#include <e-ews-connection.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_STORE \
	(camel_ews_store_get_type ())
#define CAMEL_EWS_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_STORE, CamelEwsStore))
#define CAMEL_EWS_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_STORE, CamelEwsStoreClass))
#define CAMEL_IS_EWS_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_STORE))
#define CAMEL_IS_EWS_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_STORE))
#define CAMEL_EWS_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_STORE, CamelEwsStoreClass))

#define GW_PARAM_FILTER_INBOX		(1 << 0)

G_BEGIN_DECLS

typedef struct _CamelEwsStore CamelEwsStore;
typedef struct _CamelEwsStoreClass CamelEwsStoreClass;
typedef struct _CamelEwsStorePrivate CamelEwsStorePrivate;

struct _CamelEwsStore {
	CamelOfflineStore parent;

	struct _CamelEwsStoreSummary *summary;

	gchar *root_container;
	CamelEwsStorePrivate *priv;
	CamelFolder *current_folder;

	/* the parameters field is not to be included not. probably for 2.6*/
	/*guint32 parameters;*/
	time_t refresh_stamp;
};

struct _CamelEwsStoreClass {
	CamelOfflineStoreClass parent_class;
};

GType camel_ews_store_get_type (void);
gchar * ews_get_name(CamelService *service, gboolean brief);

/*IMplemented*/
/* const gchar *camel_ews_store_folder_lookup (CamelEwsStore *ews_store, const gchar *container_id); */
EEwsConnection *cnc_lookup (CamelEwsStorePrivate *priv);
gchar *storage_path_lookup (CamelEwsStorePrivate *priv);
const gchar *ews_base_url_lookup (CamelEwsStorePrivate *priv);
CamelFolderInfo * create_junk_folder (CamelStore *store);
gboolean camel_ews_store_connected (CamelEwsStore *store, GCancellable *cancellable, GError **error);
gboolean ews_store_reload_folder (CamelEwsStore *store, CamelFolder *folder, guint32 flags, GCancellable *cancellable, GError **error);
void ews_store_set_current_folder (CamelEwsStore *ews_store, CamelFolder *folder);

G_END_DECLS

#endif /* CAMEL_EWS_STORE_H */
