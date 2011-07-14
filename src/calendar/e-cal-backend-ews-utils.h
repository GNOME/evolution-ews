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
#include <libical/icaltime.h>
#include <libical/icaltimezone.h>

G_BEGIN_DECLS

const char *e_ews_collect_orginizer(icalcomponent *comp);
void e_ews_collect_attendees(icalcomponent *comp, GSList **required, GSList **optional, GSList **resource);

void ewscal_set_time (ESoapMessage *msg, const gchar *name, icaltimetype *t);
void ewscal_set_timezone (ESoapMessage *msg, const gchar *name, icaltimezone *icaltz);
void ewscal_set_availability_timezone (ESoapMessage *msg, icaltimezone *icaltz);
void ewscal_set_reccurence (ESoapMessage *msg, icalproperty *rrule, icaltimetype *dtstart);
void ewscal_set_reccurence_exceptions (ESoapMessage *msg, icalcomponent *comp);
void ewscal_get_attach_differences (const GSList *original, const GSList *modified, GSList **removed, GSList **added);
gchar *e_ews_extract_attachment_id_from_uri (const gchar *uri);

G_END_DECLS

#endif
