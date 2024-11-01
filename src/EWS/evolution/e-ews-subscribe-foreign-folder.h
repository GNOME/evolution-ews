/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H
#define E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H

#include <e-util/e-util.h>

#include "camel/camel-ews-store.h"
#include "common/e-ews-connection.h"
#include "common/e-ews-folder.h"

G_BEGIN_DECLS

void		e_ews_subscribe_foreign_folder	(GtkWindow *parent,
						 CamelSession *session,
						 CamelStore *store,
						 EClientCache *client_cache);

gboolean	e_ews_subscribe_foreign_folder_resolve_name_sync
						(EEwsConnection *cnc,
						 const gchar *name,
						 gchar **out_display_name,
						 gchar **out_email_address,
						 GCancellable *cancellable,
						 GError **error);

gboolean	e_ews_subscrive_foreign_folder_subscribe_sync
						(CamelEwsStore *ews_store,
						 EEwsFolder *folder,
						 const gchar *user_display_name,
						 const gchar *user_email,
						 const gchar *fallback_folder_name,
						 gboolean include_subfolders,
						 GCancellable *cancellable,
						 GError **error);
G_END_DECLS

#endif /* E_EWS_SUBSCRIBE_FOREIGN_FOLDER_H */
