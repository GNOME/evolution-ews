/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef E_CAL_BACKEND_EWS_UTILS_H
#define E_CAL_BACKEND_EWS_UTILS_H

#include <e-ews-connection.h>
#include <libecal/e-cal-component.h>
#include <e-cal-backend-ews.h>

G_BEGIN_DECLS

void e_ews_collect_attendees(icalcomponent *comp, GSList **required, GSList **optional, GSList **resource);

icaltimezone *icalcomponent_extract_timezone(icalcomponent *comp);

void e_ews_set_start_time(ESoapMessage *msg, icalcomponent *icalcomp, icaltimezone *tz);
void e_ews_set_end_time(ESoapMessage *msg, icalcomponent *icalcomp, icaltimezone *tz);

void e_ews_set_meeting_timezone(ESoapMessage *msg, icaltimezone *icaltz);

G_END_DECLS

#endif
