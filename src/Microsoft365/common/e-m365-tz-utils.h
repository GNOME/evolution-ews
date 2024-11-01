/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_TZ_UTILS_H
#define E_M365_TZ_UTILS_H

#include <glib.h>
#include <libecal/libecal.h>

G_BEGIN_DECLS

void		e_m365_tz_utils_ref_windows_zones	(void);
void		e_m365_tz_utils_unref_windows_zones	(void);
const gchar *	e_m365_tz_utils_get_msdn_equivalent	(const gchar *ical_tz_location);
const gchar *	e_m365_tz_utils_get_ical_equivalent	(const gchar *msdn_tz_location);
ICalTimezone *	e_m365_tz_utils_get_user_timezone	(void);

G_END_DECLS

#endif /* E_M365_TZ_UTILS_H */
