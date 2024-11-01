/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_EDIT_FOLDER_PERMISSIONS_H
#define E_EWS_EDIT_FOLDER_PERMISSIONS_H

#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>

#include "common/e-ews-item.h"
#include "common/e-ews-folder.h"
#include "common/camel-ews-settings.h"

void	e_ews_edit_folder_permissions	(GtkWindow *parent,
					 ESourceRegistry *registry,
					 ESource *source,
					 CamelEwsSettings *ews_settings,
					 const gchar *account_name,
					 const gchar *folder_name,
					 const EwsFolderId *folder_id,
					 EEwsFolderType folder_type);

#endif /* E_EWS_EDIT_FOLDER_PERMISSIONS_H */
