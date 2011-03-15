/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#include <config.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <e-ews-connection.h>
#include <e-ews-message.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-time-util.h>
#include <libsoup/soup-misc.h>
#include "e-cal-backend-ews-utils.h"
#include "libedataserver/e-source-list.h"

/*
 * Iterate over the icalcomponent properties and collect attendees
 */
void e_ews_collect_attendees(icalcomponent *comp, GSList **required, GSList **optional, GSList **resource) {
	icalcomponent *inner;
	icalproperty *prop, *org_prop = NULL;
	icalparameter *param;
	const gchar *org = NULL, *str = NULL;

	/* we need to know who the orgenizer is so we wont duplicate him/her */
	org_prop = icalcomponent_get_first_property (comp, ICAL_ORGANIZER_PROPERTY);
	org = icalproperty_get_organizer(org_prop);
	if (!org) org = "";

	/* Look at the internal VEVENT component */
	inner = icalcomponent_get_inner(comp);

	/* iterate over every attendee property */
	for (prop = icalcomponent_get_first_property(comp, ICAL_ATTENDEE_PROPERTY);
		prop != NULL;
		prop = icalcomponent_get_next_property(comp, ICAL_ATTENDEE_PROPERTY)) {

		str = icalproperty_get_attendee(prop);

		/* if this attenddee is the orgenizer - dont add him/her */
		if (g_ascii_strcasecmp(org, str) == 0) continue;

		/* figure the email address of the attendee, discard "mailto:" if it's there */
		if (!g_ascii_strncasecmp (str, "mailto:", 7)) str = (str) + 7;

		/* figure type of attendee, add to relevant list */
		param = icalproperty_get_first_parameter(prop, ICAL_ROLE_PARAMETER);
		switch (icalparameter_get_role(param)) {
			case ICAL_ROLE_OPTPARTICIPANT:
				*optional = g_slist_append(*optional, (gpointer)str);
				break;
			case ICAL_ROLE_CHAIR:
			case ICAL_ROLE_REQPARTICIPANT:
				*required = g_slist_append(*required, (gpointer)str);
				break;
			case ICAL_ROLE_NONPARTICIPANT:
				*resource = g_slist_append(*resource, (gpointer)str);
				break;
		}
	}
}

void ewscal_set_time (ESoapMessage *msg, const gchar *name, icaltimetype *t)
{
	char *str;

	str = g_strdup_printf("%04d-%02d-%02dT%02d:%02d:%02d",
			      t->year, t->month, t->day,
			      t->hour, t->minute, t->second);

	e_ews_message_write_string_parameter(msg, name, NULL, str);
	g_free (str);
}

static const char *number_to_month(int num) {
	static const char *months[] = {
		"January", "February", "March", "April", "May", "June", "July",
		"August", "September", "October", "November", "December"
	};

	return months[num-1];
}

static const char *number_to_weekday(int num) {
	static const char *days[] = {
		"Sunday", "Monday", "Tuesday", "Wednesday",
		"Thursday", "Friday", "Saturday",
		"Day", "Weekday", "WeekendDay"
	};

	return days[num+1];
}

static const char *weekindex_to_ical(int index) {
	static struct {
		const char *exch;
		int index;
	} table[] = {
		{ "First", 1 },
		{ "Second", 2 },
		{ "Third", 3 },
		{ "Fourth", 4 },
		{ "Last", -1 }
	};
	int i;

	for (i = 0; i < 5; i++) {
		if (index == table[i].index)
				return table[i].exch;
	}

	return 0;
}

void ewscal_set_timezone (ESoapMessage *msg, const gchar *name, icaltimezone *icaltz)
{
	icalcomponent *comp;
	icalproperty *prop;
	struct icalrecurrencetype xstd_recur, daylight_recur;
	struct icaltimetype dtstart;
	char buffer[16], *offset;
	icalcomponent *xstd, *xdaylight;

	if (!icaltz)
		return;

	comp = icaltimezone_get_component(icaltz);
	xstd = icalcomponent_get_first_component(comp, ICAL_XSTANDARD_COMPONENT);
	xdaylight = icalcomponent_get_first_component(comp, ICAL_XDAYLIGHT_COMPONENT);

	e_soap_message_start_element(msg, name, NULL, NULL);
	e_soap_message_add_attribute(msg, "TimeZoneName", icaltimezone_get_tznames(icaltz), NULL, NULL);
	e_ews_message_write_string_parameter(msg, "BaseOffset", NULL, "PT0S");

	/* Standard */
	e_soap_message_start_element(msg, "Standard", NULL, NULL);
	e_soap_message_add_attribute(msg, "TimeZoneName", icaltimezone_get_tznames(icaltz), NULL, NULL);

	prop = icalcomponent_get_first_property(xstd, ICAL_RRULE_PROPERTY);
	xstd_recur = icalproperty_get_rrule(prop);
	prop = icalcomponent_get_first_property(xstd, ICAL_DTSTART_PROPERTY);
	dtstart = icalproperty_get_dtstart(prop);
	prop = icalcomponent_get_first_property(xstd, ICAL_TZOFFSETTO_PROPERTY);
	offset = icaldurationtype_as_ical_string_r(icaldurationtype_from_int(icalproperty_get_tzoffsetto(prop)));
	e_ews_message_write_string_parameter(msg, "Offset", NULL, offset);
	free(offset);

	e_soap_message_start_element(msg, "RelativeYearlyRecurrence", NULL, NULL);

	e_ews_message_write_string_parameter(msg, "DaysOfWeek", NULL, number_to_weekday(icalrecurrencetype_day_day_of_week(xstd_recur.by_day[0]) - xstd_recur.week_start));
	e_ews_message_write_string_parameter(msg, "DayOfWeekIndex", NULL, weekindex_to_ical(icalrecurrencetype_day_position(xstd_recur.by_day[0])));
	e_ews_message_write_string_parameter(msg, "Month", NULL, number_to_month(xstd_recur.by_month[0]));

	e_soap_message_end_element(msg); /* "RelativeYearlyRecurrence" */

	snprintf(buffer, 16, "%02d:%02d:%02d", dtstart.hour, dtstart.minute, dtstart.second);
	e_ews_message_write_string_parameter(msg, "Time", NULL, buffer);

	e_soap_message_end_element(msg); /* "Standard" */

	/* DayLight */
	e_soap_message_start_element(msg, "Daylight", NULL, NULL);
	e_soap_message_add_attribute(msg, "TimeZoneName", icaltimezone_get_tznames(icaltz), NULL, NULL);

	prop = icalcomponent_get_first_property(xdaylight, ICAL_RRULE_PROPERTY);
	daylight_recur = icalproperty_get_rrule(prop);
	prop = icalcomponent_get_first_property(xdaylight, ICAL_DTSTART_PROPERTY);
	dtstart = icalproperty_get_dtstart(prop);
	prop = icalcomponent_get_first_property(xdaylight, ICAL_TZOFFSETTO_PROPERTY);
	offset = icaldurationtype_as_ical_string_r(icaldurationtype_from_int(icalproperty_get_tzoffsetto(prop)));
	e_soap_message_add_attribute(msg, "Offset", offset, NULL, NULL);
	free(offset);

	e_soap_message_start_element(msg, "RelativeYearlyRecurrence", NULL, NULL);

	e_ews_message_write_string_parameter(msg, "DaysOfWeek", NULL, number_to_weekday(icalrecurrencetype_day_day_of_week(daylight_recur.by_day[0]) - xstd_recur.week_start));
	e_ews_message_write_string_parameter(msg, "DayOfWeekIndex", NULL, weekindex_to_ical(icalrecurrencetype_day_position(daylight_recur.by_day[0])));
	e_ews_message_write_string_parameter(msg, "Month", NULL, number_to_month(daylight_recur.by_month[0]));

	e_soap_message_end_element(msg); /* "RelativeYearlyRecurrence" */

	snprintf(buffer, 16, "%02d:%02d:%02d", dtstart.hour, dtstart.minute, dtstart.second);
	e_ews_message_write_string_parameter(msg, "Time", NULL, buffer);

	e_soap_message_end_element(msg); /* "Daylight" */

	e_soap_message_end_element(msg); /* "MeetingTimeZone" */
}
