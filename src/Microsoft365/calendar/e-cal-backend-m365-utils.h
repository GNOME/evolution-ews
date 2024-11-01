/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_BACKEND_M365_UTILS_H
#define E_CAL_BACKEND_M365_UTILS_H

#include <glib.h>
#include <gio/gio.h>
#include <libecal/libecal.h>

#include "common/e-m365-connection.h"

G_BEGIN_DECLS

ICalComponent *
e_cal_backend_m365_utils_json_to_ical	(EM365Connection *cnc,
					 const gchar *group_id,
					 const gchar *folder_id,
					 const gchar *attachments_dir,
					 ETimezoneCache *timezone_cache,
					 ICalComponentKind kind,
					 JsonObject *m365_object,
					 GCancellable *cancellable,
					 GError **error);
JsonBuilder *
e_cal_backend_m365_utils_ical_to_json	(EM365Connection *cnc,
					 const gchar *group_id,
					 const gchar *folder_id,
					 ETimezoneCache *timezone_cache,
					 ICalComponentKind kind,
					 ICalComponent *new_comp,
					 ICalComponent *old_comp, /* nullable */
					 GCancellable *cancellable,
					 GError **error);
gboolean
e_cal_backend_m365_utils_ical_to_json_2nd_go
					(EM365Connection *cnc,
					 const gchar *group_id,
					 const gchar *folder_id,
					 ETimezoneCache *timezone_cache,
					 ICalComponentKind kind,
					 ICalComponent *new_comp,
					 ICalComponent *old_comp, /* nullable */
					 const gchar *m365_id,
					 GCancellable *cancellable,
					 GError **error);

G_END_DECLS

#endif /* E_CAL_BACKEND_M365_UTILS_H */
