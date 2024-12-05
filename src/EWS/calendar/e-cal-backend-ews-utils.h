/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_BACKEND_EWS_UTILS_H
#define E_CAL_BACKEND_EWS_UTILS_H

#include <libecal/libecal.h>

#include "common/e-ews-connection.h"
#include "common/e-ews-item-change.h"

#include "e-cal-backend-ews.h"

G_BEGIN_DECLS

#define MINUTES_IN_HOUR 60
#define SECS_IN_MINUTE 60

typedef struct {
	EEwsConnection *connection;
	ETimezoneCache *timezone_cache;
	ICalTimezone *default_zone;
	gchar *user_email;
	gchar *response_type; /* Accept */
	GSList *users;
	ECalComponent *comp;
	ECalComponent *old_comp;
	ICalComponent *icomp;
	ICalComponent *vcalendar; /* can be NULL, parent of icomp, where timezones can be eventually found */
	gchar *item_id;
	gchar *change_key;
	EEwsItemChangeType change_type;
	gint index;
	time_t start;
	time_t end;
} EwsCalendarConvertData;

const gchar *e_ews_collect_organizer (ICalComponent *comp);
void e_ews_collect_attendees (ICalComponent *comp, GSList **required, GSList **optional, GSList **resource, gboolean *out_rsvp_requested);

void ewscal_set_timezone (ESoapRequest *request, const gchar *name, EEwsCalendarTimeZoneDefinition *tzd, const gchar *msdn_location);
void ewscal_set_meeting_timezone (ESoapRequest *request, ICalTimezone *icaltz, ICalComponent *icomp);
void ewscal_set_reccurence (ESoapRequest *request, ICalProperty *rrule, ICalTime *dtstart);
void ewscal_set_reccurence_exceptions (ESoapRequest *request, ICalComponent *comp);
gchar *e_ews_extract_attachment_id_from_uri (const gchar *uri);
void ews_set_alarm (ESoapRequest *request, ECalComponent *comp, ETimezoneCache *timezone_cache, ICalComponent *vcalendar, gboolean with_due_by);
gint ews_get_alarm (ECalComponent *comp);
void e_ews_clean_icomponent (ICalComponent *icomp);

const gchar *e_cal_backend_ews_tz_util_get_msdn_equivalent (const gchar *ical_tz_location);
const gchar *e_cal_backend_ews_tz_util_get_ical_equivalent (const gchar *msdn_tz_location);
void e_cal_backend_ews_populate_windows_zones (void);
void e_cal_backend_ews_unref_windows_zones (void);

gboolean e_cal_backend_ews_convert_calcomp_to_xml (ESoapRequest *request, gpointer user_data, GError **error);
gboolean e_cal_backend_ews_convert_component_to_updatexml (ESoapRequest *request, gpointer user_data, GError **error);
gboolean e_cal_backend_ews_clear_reminder_is_set (ESoapRequest *request, gpointer user_data, GError **error);
gboolean e_cal_backend_ews_prepare_set_free_busy_status (ESoapRequest *request,gpointer user_data, GError **error);
gboolean e_cal_backend_ews_prepare_accept_item_request (ESoapRequest *request, gpointer user_data, GError **error);

guint e_cal_backend_ews_rid_to_index (ICalTimezone *timezone, const gchar *rid, ICalComponent *comp, GError **error);

ICalTime *	e_cal_backend_ews_get_datetime_with_zone	(ETimezoneCache *timezone_cache,
								 ICalComponent *vcalendar,
								 ICalComponent *comp,
								 ICalPropertyKind prop_kind,
								 ICalTime * (* get_func) (ICalProperty *prop));

G_END_DECLS

#endif
