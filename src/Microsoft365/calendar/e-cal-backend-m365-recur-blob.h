/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_BACKEND_M365_RECUR_BLOB_H
#define E_CAL_BACKEND_M365_RECUR_BLOB_H

#include <glib.h>
#include <libecal/libecal.h>

G_BEGIN_DECLS

gboolean	e_cal_backend_m365_decode_recur_blob
						(const gchar *base64_blob,
						 ICalComponent *icomp,
						 ICalTimezone *recur_zone,
						 GSList **out_extra_detached); /* ICalComponent * */

G_END_DECLS

#endif
