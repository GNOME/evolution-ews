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

#include "evolution-ews-config.h"

#include <calendar/gui/calendar-config.h>

#include "e-ews-message.h"

#include "e-ews-calendar-utils.h"

static const gchar *
number_to_weekday (gint num)
{
	static const gchar *days[] = {
		"Sunday", "Monday", "Tuesday", "Wednesday",
		"Thursday", "Friday", "Saturday",
		"Day", "Weekday", "WeekendDay"
	};

	return days[num - 1];
}

static void
ewscal_add_availability_rrule (ESoapMessage *msg,
                               icalproperty *prop)
{
	struct icalrecurrencetype recur = icalproperty_get_rrule (prop);
	gchar buffer[16];
	gint dayorder;

	dayorder = icalrecurrencetype_day_position (recur.by_day[0]);
	dayorder = dayorder % 5;
	if (dayorder < 0)
		dayorder += 5;
	dayorder += 1;

	/* expected value is 1..5, inclusive */
	snprintf (buffer, 16, "%d", dayorder);
	e_ews_message_write_string_parameter (msg, "DayOrder", NULL, buffer);

	snprintf (buffer, 16, "%d", recur.by_month[0]);
	e_ews_message_write_string_parameter (msg, "Month", NULL, buffer);

	e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));
}

static void
ewscal_add_availability_default_timechange (ESoapMessage *msg)
{

	e_soap_message_start_element (msg, "StandardTime", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "Bias", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Time", NULL, "00:00:00");
	e_ews_message_write_string_parameter (msg, "DayOrder", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Month", NULL, "0");
	e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, "Sunday");
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "DaylightTime", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "Bias", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Time", NULL, "00:00:00");
	e_ews_message_write_string_parameter (msg, "DayOrder", NULL, "0");
	e_ews_message_write_string_parameter (msg, "Month", NULL, "0");
	e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, "Sunday");
	e_soap_message_end_element (msg);
}

static void
ewscal_add_availability_timechange (ESoapMessage *msg,
                                    icalcomponent *comp,
                                    gint baseoffs)
{
	gchar buffer[16];
	icalproperty *prop;
	struct icaltimetype dtstart;
	gint utcoffs;

	/* Calculate zone Offset from BaseOffset */
	prop = icalcomponent_get_first_property (comp, ICAL_TZOFFSETTO_PROPERTY);
	if (prop) {
		utcoffs = -icalproperty_get_tzoffsetto (prop) / 60;
		utcoffs -= baseoffs;
		snprintf (buffer, 16, "%d", utcoffs);
		e_ews_message_write_string_parameter (msg, "Bias", NULL, buffer);
	}

	prop = icalcomponent_get_first_property (comp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dtstart = icalproperty_get_dtstart (prop);
		snprintf (buffer, 16, "%02d:%02d:%02d", dtstart.hour, dtstart.minute, dtstart.second);
		e_ews_message_write_string_parameter (msg, "Time", NULL, buffer);
	}

	prop = icalcomponent_get_first_property (comp, ICAL_RRULE_PROPERTY);
	if (prop)
		ewscal_add_availability_rrule (msg, prop);
}

static void
ewscal_set_availability_timezone (ESoapMessage *msg,
                                  icaltimezone *icaltz)
{
	icalcomponent *comp;
	icalproperty *prop;
	icalcomponent *xstd, *xdaylight;
	gint std_utcoffs;
	gchar *offset;

	if (!icaltz)
		return;

	comp = icaltimezone_get_component (icaltz);

	xstd = icalcomponent_get_first_component (comp, ICAL_XSTANDARD_COMPONENT);
	xdaylight = icalcomponent_get_first_component (comp, ICAL_XDAYLIGHT_COMPONENT);

	/*TimeZone is the root element of GetUserAvailabilityRequest*/
	e_soap_message_start_element (msg, "TimeZone", NULL, NULL);

	/* Fetch the timezone offsets for the standard (or only) zone.
	 * Negate it, because Exchange does it backwards */
	if (xstd) {
		prop = icalcomponent_get_first_property (xstd, ICAL_TZOFFSETTO_PROPERTY);
		std_utcoffs = -icalproperty_get_tzoffsetto (prop) / 60;
	} else
		std_utcoffs = 0;

	/* This is the overall BaseOffset tag, which the Standard and Daylight
	 * zones are offset from. It's redundant, but Exchange always sets it
	 * to the offset of the Standard zone, and the Offset in the Standard
	 * zone to zero. So try to avoid problems by doing the same. */
	offset = g_strdup_printf ("%d", std_utcoffs);
	e_ews_message_write_string_parameter (msg, "Bias", NULL, offset);
	g_free (offset);

	if (xdaylight) {
		/* Standard */
		e_soap_message_start_element (msg, "StandardTime", NULL, NULL);
		ewscal_add_availability_timechange (msg, xstd, std_utcoffs);
		e_soap_message_end_element (msg); /* "StandardTime" */

		/* DayLight */
		e_soap_message_start_element (msg, "DaylightTime", NULL, NULL);
		ewscal_add_availability_timechange (msg, xdaylight, std_utcoffs);
		e_soap_message_end_element (msg); /* "DaylightTime" */
	} else
		/* Set default values*/
		ewscal_add_availability_default_timechange (msg);

	e_soap_message_end_element (msg); /* "TimeZone" */
}

void
e_ews_cal_utils_prepare_free_busy_request (ESoapMessage *msg,
					   gpointer user_data)
{
	const EEWSFreeBusyData *fbdata = user_data;
	icaltimetype t_start, t_end;
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();
	GSList *link;

	g_return_if_fail (fbdata != NULL);

	ewscal_set_availability_timezone (msg, utc_zone);

	e_soap_message_start_element (msg, "MailboxDataArray", "messages", NULL);

	for (link = (GSList *) fbdata->user_mails; link; link = g_slist_next (link)) {
		const gchar *mail = link->data;

		e_soap_message_start_element (msg, "MailboxData", NULL, NULL);

		e_soap_message_start_element (msg, "Email", NULL, NULL);
		e_ews_message_write_string_parameter (msg, "Address", NULL, mail);
		e_soap_message_end_element (msg); /* "Email" */

		e_ews_message_write_string_parameter (msg, "AttendeeType", NULL, "Required");
		e_ews_message_write_string_parameter (msg, "ExcludeConflicts", NULL, "false");

		e_soap_message_end_element (msg); /* "MailboxData" */
	}

	e_soap_message_end_element (msg); /* "MailboxDataArray" */

	e_soap_message_start_element (msg, "FreeBusyViewOptions", NULL, NULL);

	e_soap_message_start_element (msg, "TimeWindow", NULL, NULL);
	t_start = icaltime_from_timet_with_zone (fbdata->period_start, 0, utc_zone);
	t_end = icaltime_from_timet_with_zone (fbdata->period_end, 0, utc_zone);
	e_ews_cal_utils_set_time (msg, "StartTime", &t_start, FALSE);
	e_ews_cal_utils_set_time (msg, "EndTime", &t_end, FALSE);
	e_soap_message_end_element (msg); /* "TimeWindow" */

	e_ews_message_write_string_parameter (msg, "MergedFreeBusyIntervalInMinutes", NULL, "60");
	e_ews_message_write_string_parameter (msg, "RequestedView", NULL, "DetailedMerged");

	e_soap_message_end_element (msg); /* "FreeBusyViewOptions" */
}

void
e_ews_cal_utils_set_time (ESoapMessage *msg,
			  const gchar *name,
			  icaltimetype *tt,
			  gboolean with_timezone)
{
	struct icaltimetype local_tt;
	gchar *str;
	gchar *tz_ident = NULL;

	g_return_if_fail (tt != NULL);

	if (with_timezone) {
		if (icaltime_is_utc (*tt) || !tt->zone || tt->zone == icaltimezone_get_utc_timezone () || tt->is_date) {
			tz_ident = g_strdup ("Z");
		} else {
			gint offset, is_daylight, hrs, mins;

			offset = icaltimezone_get_utc_offset (
				icaltimezone_get_utc_timezone (), tt, &is_daylight);

			offset = offset * (-1);
			hrs = offset / 60;
			mins = offset % 60;

			if (hrs < 0)
				hrs *= -1;
			if (mins < 0)
				mins *= -1;

			tz_ident = g_strdup_printf ("%s%02d:%02d", offset > 0 ? "+" : "-", hrs, mins);
		}
	}

	if (tt->is_date) {
		local_tt = *tt;
		local_tt.zone = calendar_config_get_icaltimezone ();
		local_tt = icaltime_from_timet_with_zone (icaltime_as_timet_with_zone (local_tt, local_tt.zone), FALSE, icaltimezone_get_utc_timezone ());
		tt = &local_tt;
	}

	str = g_strdup_printf (
		"%04d-%02d-%02dT%02d:%02d:%02d%s",
		tt->year, tt->month, tt->day,
		tt->hour, tt->minute, tt->second,
		tz_ident ? tz_ident : "");

	e_ews_message_write_string_parameter (msg, name, NULL, str);

	g_free (tz_ident);
	g_free (str);
}
