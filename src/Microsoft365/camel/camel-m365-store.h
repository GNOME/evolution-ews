/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_STORE_H
#define CAMEL_M365_STORE_H

#include <camel/camel.h>

#include "common/e-m365-connection.h"
#include "camel-m365-store-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_M365_STORE \
	(camel_m365_store_get_type ())
#define CAMEL_M365_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_M365_STORE, CamelM365Store))
#define CAMEL_M365_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_M365_STORE, CamelM365StoreClass))
#define CAMEL_IS_M365_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_M365_STORE))
#define CAMEL_IS_M365_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_M365_STORE))
#define CAMEL_M365_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_M365_STORE, CamelM365StoreClass))

G_BEGIN_DECLS

typedef struct _CamelM365Store CamelM365Store;
typedef struct _CamelM365StoreClass CamelM365StoreClass;
typedef struct _CamelM365StorePrivate CamelM365StorePrivate;

struct _CamelM365Store {
	CamelOfflineStore parent;
	CamelM365StorePrivate *priv;
};

struct _CamelM365StoreClass {
	CamelOfflineStoreClass parent_class;
};

GType		camel_m365_store_get_type	(void);

CamelM365StoreSummary *
		camel_m365_store_ref_store_summary
						(CamelM365Store *m365_store);
EM365Connection *
		camel_m365_store_ref_connection	(CamelM365Store *m365_store);
gboolean	camel_m365_store_ensure_connected
						(CamelM365Store *m365_store,
						 EM365Connection **out_cnc, /* out, nullable, transfer full */
						 GCancellable *cancellable,
						 GError **error);
void		camel_m365_store_maybe_disconnect
						(CamelM365Store *m365_store,
						 GError *error);
void		camel_m365_store_connect_folder_summary
						(CamelM365Store *m365_store,
						 CamelFolderSummary *folder_summary);
void		camel_m365_store_set_has_ooo_set(CamelM365Store *self,
						 gboolean has_ooo_set);
gboolean	camel_m365_store_get_has_ooo_set(const CamelM365Store *self);
void		camel_m365_store_set_ooo_alert_state
						(CamelM365Store *self,
						 CamelM365StoreOooAlertState state);
CamelM365StoreOooAlertState
		camel_m365_store_get_ooo_alert_state
						(const CamelM365Store *self);
void		camel_m365_store_unset_oof_settings_state
						(CamelM365Store *self);

G_END_DECLS

#endif /* CAMEL_M365_STORE_H */
