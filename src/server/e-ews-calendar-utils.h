/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#ifndef E_EWS_CALENDAR_UTILS_H
#define E_EWS_CALENDAR_UTILS_H

#include <time.h>
#include <libical/ical.h>

#include "server/e-soap-message.h"

G_BEGIN_DECLS

typedef struct _EEWSFreeBusyData {
	time_t period_start;
	time_t period_end;
	GSList *user_mails; /* gchar * */
} EEWSFreeBusyData;

void		e_ews_cal_utils_prepare_free_busy_request
						(ESoapMessage *msg,
						 gpointer user_data); /* EEWSFreeBusyData * */
void		e_ews_cal_utils_set_time	(ESoapMessage *msg,
						 const gchar *name,
						 icaltimetype *tt,
						 gboolean with_timezone);

G_END_DECLS

#endif /* E_EWS_CALENDAR_UTILS_H */
