/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_BACKEND_EWS_M365_H
#define E_CAL_BACKEND_EWS_M365_H

#include <libecal/libecal.h>
#include <libedataserver/libedataserver.h>

#include "common/e-ews-connection.h"

G_BEGIN_DECLS

gboolean	ecb_ews_save_as_online_meeting_sync	(ESourceRegistry *registry,
							 EEwsConnection *ews_cnc,
							 ETimezoneCache *timezone_cache,
							 ECalComponent *comp,
							 gchar **out_new_uid,
							 GCancellable *cancellable,
							 GError **error);

G_END_DECLS

#endif /* E_CAL_BACKEND_EWS_M365_H */
