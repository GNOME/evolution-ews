/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_EDIT_CALENDAR_PERMISSIONS_H
#define E_M365_EDIT_CALENDAR_PERMISSIONS_H

#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>

#include "common/camel-m365-settings.h"

void	e_m365_edit_calendar_permissions	(GtkWindow *parent,
						 ESourceRegistry *registry,
						 ESource *account_source,
						 ESource *source,
						 CamelM365Settings *m365_settings,
						 const gchar *group_id,
						 const gchar *calendar_id);

#endif /* E_M365_EDIT_CALENDAR_PERMISSIONS_H */
