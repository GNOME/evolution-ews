/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Chenthill Palanisamy <pchenthill@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_EWS_STORE_H
#define CAMEL_EWS_STORE_H

#include <camel/camel.h>
#include <camel/camel-ews-enums.h>

#include "common/e-ews-connection.h"

#include "camel-ews-store-summary.h"

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

#define EWS_PARAM_FILTER_INBOX		(1 << 0)

#define EWS_FOREIGN_FOLDER_ROOT_ID		"ForeignRoot"
#define EWS_FOREIGN_FOLDER_ROOT_DISPLAY_NAME	_("Foreign Folders")
#define EWS_PUBLIC_FOLDER_ROOT_ID		"PublicRoot"
#define EWS_PUBLIC_FOLDER_ROOT_DISPLAY_NAME	_("Public Folders")

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

GType camel_ews_store_get_type (void);
gchar *		ews_get_name	(CamelService *service, gboolean brief);
EEwsConnection *
		camel_ews_store_ref_connection	(CamelEwsStore *ews_store);

gboolean	camel_ews_store_connected	(CamelEwsStore *store,
						 GCancellable *cancellable,
						 GError **error);
void		camel_ews_store_maybe_disconnect
						(CamelEwsStore *store,
						 GError *error);
void		camel_ews_store_ensure_virtual_folders
						(CamelEwsStore *ews_store);
void		camel_ews_store_ensure_unique_path
						(CamelEwsStore *ews_store,
						 gchar **ppath);
void		camel_ews_store_update_foreign_subfolders
						(CamelEwsStore *ews_store,
						 const gchar *fid);
void		camel_ews_store_set_has_ooo_set
						(CamelEwsStore *ews_store,
						 gboolean has_ooo_set);
gboolean	camel_ews_store_get_has_ooo_set
						(const CamelEwsStore *ews_store);
void		camel_ews_store_set_ooo_alert_state
						(CamelEwsStore *ews_store,
						 CamelEwsStoreOooAlertState state);
CamelEwsStoreOooAlertState
		camel_ews_store_get_ooo_alert_state
						(const CamelEwsStore *ews_store);
void		camel_ews_store_unset_oof_settings_state
						(CamelEwsStore *ews_store);


G_END_DECLS

#endif /* CAMEL_EWS_STORE_H */
