/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_SEARCH_USER_H
#define E_EWS_SEARCH_USER_H

#include <gtk/gtk.h>
#include "common/e-ews-connection.h"
#include "common/e-ews-item.h"

gboolean	e_ews_search_user_modal	(GtkWindow *parent,
					 EEwsConnection *conn,
					 const gchar *search_this,
					 gchar **display_name,
					 gchar **email);

#endif /* E_EWS_SEARCH_USER_H */
