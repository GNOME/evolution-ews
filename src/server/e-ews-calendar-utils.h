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
#include <libecal/libecal.h>

#include "server/e-soap-message.h"
#include "server/e-ews-item.h"

G_BEGIN_DECLS

/* Custom property, because evolution cannot cope with it,
   but it shouldn't lost it as well */
#define X_EWS_TASK_REGENERATION "X-EWS-TASK-REGENERATION"

typedef struct _EEWSFreeBusyData {
	time_t period_start;
	time_t period_end;
	GSList *user_mails; /* gchar * */
} EEWSFreeBusyData;

gboolean	e_ews_cal_utils_prepare_free_busy_request
						(ESoapMessage *msg,
						 gpointer user_data, /* EEWSFreeBusyData * */
						 GError **error);
void		e_ews_cal_utils_set_time	(ESoapMessage *msg,
						 const gchar *name,
						 ICalTime *tt,
						 gboolean with_timezone);
gboolean	e_ews_cal_utils_set_recurrence	(ESoapMessage *msg,
						 ICalComponent *comp,
						 gboolean server_satisfies_2013,
						 GError **error);
void		e_ews_cal_utils_recurrence_to_rrule
						(EEwsItem *item,
						 ICalComponent *comp);

G_END_DECLS

#endif /* E_EWS_CALENDAR_UTILS_H */
