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

#ifndef CAMEL_O365_STORE_H
#define CAMEL_O365_STORE_H

#include <camel/camel.h>

#include "common/e-o365-connection.h"
#include "camel-o365-store-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_O365_STORE \
	(camel_o365_store_get_type ())
#define CAMEL_O365_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_O365_STORE, CamelO365Store))
#define CAMEL_O365_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_O365_STORE, CamelO365StoreClass))
#define CAMEL_IS_O365_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_O365_STORE))
#define CAMEL_IS_O365_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_O365_STORE))
#define CAMEL_O365_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_O365_STORE, CamelO365StoreClass))

G_BEGIN_DECLS

typedef struct _CamelO365Store CamelO365Store;
typedef struct _CamelO365StoreClass CamelO365StoreClass;
typedef struct _CamelO365StorePrivate CamelO365StorePrivate;

struct _CamelO365Store {
	CamelOfflineStore parent;
	CamelO365StorePrivate *priv;
};

struct _CamelO365StoreClass {
	CamelOfflineStoreClass parent_class;
};

GType		camel_o365_store_get_type	(void);

CamelO365StoreSummary *
		camel_o365_store_ref_store_summary
						(CamelO365Store *store);
EO365Connection *
		camel_o365_store_ref_connection	(CamelO365Store *o365_store);
gboolean	camel_o365_store_ensure_connected
						(CamelO365Store *store,
						 EO365Connection **out_cnc, /* out, nullable, trasnfer full */
						 GCancellable *cancellable,
						 GError **error);
void		camel_o365_store_maybe_disconnect
						(CamelO365Store *store,
						 const GError *error);
void		camel_o365_store_connect_folder_summary
						(CamelO365Store *store,
						 CamelFolderSummary *folder_summary);

G_END_DECLS

#endif /* CAMEL_O365_STORE_H */
