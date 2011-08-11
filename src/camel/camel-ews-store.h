/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-store.h : class for an groupwise store */

/*
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
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

#include <e-ews-connection.h>
#include "camel-ews-store-summary.h"

/* Standard Camel Object macros - added for 2.28 compatibility */
#define CAMEL_EWS_STORE_TYPE     (camel_ews_store_get_type ())
#define CAMEL_EWS_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_EWS_STORE_TYPE, CamelEwsStore))
#define CAMEL_EWS_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_EWS_STORE_TYPE, CamelEwsStoreClass))
#define CAMEL_IS_EWS_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_EWS_STORE_TYPE))

G_BEGIN_DECLS

typedef struct _CamelEwsStore CamelEwsStore;
typedef struct _CamelEwsStoreClass CamelEwsStoreClass;
typedef struct _CamelEwsStorePrivate CamelEwsStorePrivate;

struct _CamelEwsStore {
	CamelOfflineStore parent;
	CamelEwsStorePrivate *priv;

	CamelEwsStoreSummary *summary;
	gchar *storage_path;
};

struct _CamelEwsStoreClass {
	CamelOfflineStoreClass parent_class;
};

CamelType camel_ews_store_get_type (void);
gchar *		ews_get_name	(CamelService *service, gboolean brief);
EEwsConnection *
		camel_ews_store_get_connection	(CamelEwsStore *ews_store);

gboolean	camel_ews_store_connected	(CamelEwsStore *store,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_EWS_STORE_H */
