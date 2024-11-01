/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_CALENDAR_UTILS_H
#define E_EWS_CALENDAR_UTILS_H

#include <time.h>
#include <libecal/libecal.h>

#include "common/e-soap-request.h"
#include "common/e-ews-item.h"

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
						(ESoapRequest *request,
						 gpointer user_data, /* EEWSFreeBusyData * */
						 GError **error);
void		e_ews_cal_utils_set_time	(ESoapRequest *request,
						 const gchar *name,
						 ICalTime *tt,
						 gboolean with_timezone);
gboolean	e_ews_cal_utils_set_recurrence	(ESoapRequest *request,
						 ICalComponent *comp,
						 gboolean server_satisfies_2013,
						 GError **error);
void		e_ews_cal_utils_recurrence_to_rrule
						(EEwsItem *item,
						 ICalComponent *comp);

G_END_DECLS

#endif /* E_EWS_CALENDAR_UTILS_H */
