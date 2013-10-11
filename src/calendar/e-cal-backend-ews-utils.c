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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libecal/libecal.h>
#include <libsoup/soup-misc.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-message.h"

#include "e-cal-backend-ews-utils.h"

/*
 * Iterate over the icalcomponent properties and collect attendees
 */
void
e_ews_collect_attendees (icalcomponent *comp,
                         GSList **required,
                         GSList **optional,
                         GSList **resource)
{
	icalproperty *prop;
	icalparameter *param;
	const gchar *str = NULL;
	const gchar *org_email_address = NULL;

	/* we need to know who the orgenizer is so we wont duplicate him/her */
	org_email_address = e_ews_collect_organizer (comp);

	/* iterate over every attendee property */
	for (prop = icalcomponent_get_first_property (comp, ICAL_ATTENDEE_PROPERTY);
		prop != NULL;
		prop = icalcomponent_get_next_property (comp, ICAL_ATTENDEE_PROPERTY)) {

		str = icalproperty_get_attendee (prop);

		if (!str || !*str)
			continue;

		/* figure the email address of the attendee, discard "mailto:" if it's there */
		if (!g_ascii_strncasecmp (str, "mailto:", 7))
			str = (str) + 7;

		if (!*str)
			continue;

		/* if this attenddee is the orgenizer - dont add him/her
		 in some cases there is no maito for email if meeting orginazer */
		if (g_ascii_strcasecmp (org_email_address, str) == 0) continue;

		/* figure type of attendee, add to relevant list */
		param = icalproperty_get_first_parameter (prop, ICAL_ROLE_PARAMETER);

		/*in case of new time proposal the role parameter is not a part of ical*/
		if (!param) continue;

		switch (icalparameter_get_role (param)) {
		case ICAL_ROLE_OPTPARTICIPANT:
			*optional = g_slist_append (*optional, (gpointer)str);
			break;
		case ICAL_ROLE_CHAIR:
		case ICAL_ROLE_REQPARTICIPANT:
			*required = g_slist_append (*required, (gpointer)str);
			break;
		case ICAL_ROLE_NONPARTICIPANT:
			*resource = g_slist_append (*resource, (gpointer)str);
			break;
		case ICAL_ROLE_X:
		case ICAL_ROLE_NONE:
			/* Ignore these for now */
			break;
		}
	}

	if (*required == NULL && *optional == NULL && *resource == NULL && org_email_address != NULL)
		*required = g_slist_prepend (*required, (gpointer) org_email_address);
}

gint
ews_get_alarm (ECalComponent *comp)
{
	GList *alarm_uids = e_cal_component_get_alarm_uids (comp);
	ECalComponentAlarm *alarm = e_cal_component_get_alarm (comp, (const gchar *) (alarm_uids->data));
	ECalComponentAlarmAction action;
	ECalComponentAlarmTrigger trigger;
	gint dur_int = 0;

	e_cal_component_alarm_get_action (alarm, &action);
	if (action == E_CAL_COMPONENT_ALARM_DISPLAY) {
		e_cal_component_alarm_get_trigger (alarm, &trigger);
		switch (trigger.type) {
		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
			dur_int = ((icaldurationtype_as_int (trigger.u.rel_duration)) / SECS_IN_MINUTE) * -1;
			break;
		default:
			break;
		}
	}
	e_cal_component_alarm_free (alarm);
	cal_obj_uid_list_free (alarm_uids);
	return dur_int;
}

void
ews_set_alarm (ESoapMessage *msg,
               ECalComponent *comp)
{
	/* We know there would be only a single alarm in EWS calendar item */
	GList *alarm_uids = e_cal_component_get_alarm_uids (comp);
	ECalComponentAlarm *alarm = e_cal_component_get_alarm (comp, (const gchar *) (alarm_uids->data));
	ECalComponentAlarmAction action;

	e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "true");
	e_cal_component_alarm_get_action (alarm, &action);
	if (action == E_CAL_COMPONENT_ALARM_DISPLAY) {
		ECalComponentAlarmTrigger trigger;
		gchar buf[20];
		gint dur_int = 0;
		e_cal_component_alarm_get_trigger (alarm, &trigger);
		switch (trigger.type) {
		case E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START:
			dur_int = ((icaldurationtype_as_int (trigger.u.rel_duration)) / SECS_IN_MINUTE) * -1;
			snprintf (buf, 20, "%d", dur_int);
			e_ews_message_write_string_parameter (msg, "ReminderMinutesBeforeStart", NULL, buf);
			break;
		default:
			break;
		}
	}
	e_cal_component_alarm_free (alarm);
	cal_obj_uid_list_free (alarm_uids);

}

void
ewscal_set_time (ESoapMessage *msg,
                 const gchar *name,
                 icaltimetype *t,
                 gboolean with_timezone)
{
	gchar *str;
	gchar *tz_ident = NULL;

	if (with_timezone) {
		if (t->is_utc || !t->zone || t->zone == icaltimezone_get_utc_timezone ()) {
			tz_ident = g_strdup ("Z");
		} else {
			gint offset, is_daylight, hrs, mins;

			offset = icaltimezone_get_utc_offset (
				icaltimezone_get_utc_timezone (), t, &is_daylight);

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

	str = g_strdup_printf (
		"%04d-%02d-%02dT%02d:%02d:%02d%s",
		t->year, t->month, t->day,
		t->hour, t->minute, t->second,
		tz_ident ? tz_ident : "");

	e_ews_message_write_string_parameter (msg, name, NULL, str);

	g_free (tz_ident);
	g_free (str);
}

static void
ewscal_set_date (ESoapMessage *msg,
                 const gchar *name,
                 icaltimetype *t)
{
	gchar *str;

	str = g_strdup_printf (
		"%04d-%02d-%02d",
		t->year, t->month, t->day);

	e_ews_message_write_string_parameter (msg, name, NULL, str);
	g_free (str);
}

static const gchar *number_to_month (gint num) {
	static const gchar *months[] = {
		"January", "February", "March", "April", "May", "June", "July",
		"August", "September", "October", "November", "December"
	};

	return months[num - 1];
}

static const gchar *number_to_weekday (gint num) {
	static const gchar *days[] = {
		"Sunday", "Monday", "Tuesday", "Wednesday",
		"Thursday", "Friday", "Saturday",
		"Day", "Weekday", "WeekendDay"
	};

	return days[num - 1];
}

static const gchar *weekindex_to_ical (gint index) {
	static struct {
		const gchar *exch;
		gint index;
	} table[] = {
		{ "First", 1 },
		{ "Second", 2 },
		{ "Third", 3 },
		{ "Fourth", 4 },
		{ "Fifth", 5 },
		{ "Last", -1 }
	};
	gint i;

	for (i = 0; i < 6; i++) {
		if (index == table[i].index)
				return table[i].exch;
	}

	return 0;
}

static void
ewscal_add_rrule (ESoapMessage *msg,
                  icalproperty *prop)
{
	struct icalrecurrencetype recur = icalproperty_get_rrule (prop);

	e_soap_message_start_element (msg, "RelativeYearlyRecurrence", NULL, NULL);

	e_ews_message_write_string_parameter (msg, "DaysOfWeek", NULL, number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));
	e_ews_message_write_string_parameter (msg, "DayOfWeekIndex", NULL, weekindex_to_ical (icalrecurrencetype_day_position (recur.by_day[0])));
	e_ews_message_write_string_parameter (msg, "Month", NULL, number_to_month (recur.by_month[0]));

	e_soap_message_end_element (msg); /* "RelativeYearlyRecurrence" */
}

static void
ewscal_add_timechange (ESoapMessage *msg,
                       icalcomponent *comp,
                       gint baseoffs)
{
	gchar buffer[16], *offset;
	const gchar *tzname;
	icalproperty *prop;
	struct icaltimetype dtstart;
	gint utcoffs;

	prop = icalcomponent_get_first_property (comp, ICAL_TZNAME_PROPERTY);
	if (prop) {
		tzname = icalproperty_get_tzname (prop);
		e_soap_message_add_attribute (msg, "TimeZoneName", tzname, NULL, NULL);
	}

	/* Calculate zone Offset from BaseOffset */
	prop = icalcomponent_get_first_property (comp, ICAL_TZOFFSETTO_PROPERTY);
	if (prop) {
		utcoffs = -icalproperty_get_tzoffsetto (prop);
		utcoffs -= baseoffs;
		offset = icaldurationtype_as_ical_string_r (icaldurationtype_from_int (utcoffs));
		e_ews_message_write_string_parameter (msg, "Offset", NULL, offset);
		free (offset);
	}

	prop = icalcomponent_get_first_property (comp, ICAL_RRULE_PROPERTY);
	if (prop)
		ewscal_add_rrule (msg, prop);

	prop = icalcomponent_get_first_property (comp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dtstart = icalproperty_get_dtstart (prop);
		snprintf (buffer, 16, "%02d:%02d:%02d", dtstart.hour, dtstart.minute, dtstart.second);
		e_ews_message_write_string_parameter (msg, "Time", NULL, buffer);
	}
}

void
ewscal_set_timezone (ESoapMessage *msg,
                     const gchar *name,
                     icaltimezone *icaltz)
{
	icalcomponent *comp;
	icalproperty *prop;
	const gchar *location;
	icalcomponent *xstd, *xdaylight;
	gint std_utcoffs;
	gchar *offset;

	if (!icaltz)
		return;

	comp = icaltimezone_get_component (icaltz);

	/* Exchange needs a BaseOffset, followed by either *both*
	 * Standard and Daylight zones, or neither of them. If there's
	 * more than one STANDARD or DAYLIGHT component in the VTIMEZONE,
	 * we ignore the extra. So fully-specified timezones including
	 * historical DST rules cannot be handled by Exchange. */

	/* FIXME: Walk through them all to find the *latest* ones, like
	 * icaltimezone_get_tznames_from_vtimezone() does. */
	xstd = icalcomponent_get_first_component (comp, ICAL_XSTANDARD_COMPONENT);
	xdaylight = icalcomponent_get_first_component (comp, ICAL_XDAYLIGHT_COMPONENT);

	/* If there was only a DAYLIGHT component, swap them over and pretend
	 * it was the STANDARD component. We're only going to give the server
	 * the BaseOffset anyway. */
	if (!xstd) {
		xstd = xdaylight;
		xdaylight = NULL;
	}

	/* Find a suitable string to use for the TimeZoneName */
	location = icaltimezone_get_location (icaltz);
	if (!location)
		location = icaltimezone_get_tzid (icaltz);
	if (!location)
		location = icaltimezone_get_tznames (icaltz);

	e_soap_message_start_element (msg, name, NULL, NULL);
	e_soap_message_add_attribute (msg, "TimeZoneName", location, NULL, NULL);

	/* Fetch the timezone offsets for the standard (or only) zone.
	 * Negate it, because Exchange does it backwards */
	if (xstd) {
		prop = icalcomponent_get_first_property (xstd, ICAL_TZOFFSETTO_PROPERTY);
		std_utcoffs = -icalproperty_get_tzoffsetto (prop);
	} else {
		/* UTC has no properties at all, so just set manually */
		std_utcoffs = 0;
	}

	/* This is the overall BaseOffset tag, which the Standard and Daylight
	 * zones are offset from. It's redundant, but Exchange always sets it
	 * to the offset of the Standard zone, and the Offset in the Standard
	 * zone to zero. So try to avoid problems by doing the same. */
	offset = icaldurationtype_as_ical_string_r (icaldurationtype_from_int (std_utcoffs));
	e_ews_message_write_string_parameter (msg, "BaseOffset", NULL, offset);
	free (offset);

	/* Only write the full TimeChangeType information, including the
	 * recurrence rules for the DST changes, if there is more than
	 * one. */
	if (xdaylight) {
		/* Standard */
		e_soap_message_start_element (msg, "Standard", NULL, NULL);
		ewscal_add_timechange (msg, xstd, std_utcoffs);
		e_soap_message_end_element (msg); /* "Standard" */

		/* DayLight */
		e_soap_message_start_element (msg, "Daylight", NULL, NULL);
		ewscal_add_timechange (msg, xdaylight, std_utcoffs);
		e_soap_message_end_element (msg); /* "Daylight" */
	}
	e_soap_message_end_element (msg); /* "MeetingTimeZone" */
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

void
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
ewscal_set_reccurence (ESoapMessage *msg,
                       icalproperty *rrule,
                       icaltimetype *dtstart)
{
	gchar buffer[256];
	gint i, len;
	gboolean is_relative = FALSE;

	/* MSDN reference: http://msdn.microsoft.com/en-us/library/aa580471%28v=EXCHG.80%29.aspx
	 */
	struct icalrecurrencetype recur = icalproperty_get_rrule (rrule);

	e_soap_message_start_element (msg, "Recurrence", NULL, NULL);

	switch (recur.freq) {
		case ICAL_DAILY_RECURRENCE:
			e_soap_message_start_element (msg, "DailyRecurrence", NULL, NULL);
			snprintf (buffer, 32, "%d", recur.interval);
			e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);
			e_soap_message_end_element (msg); /* "DailyRecurrence" */
			break;

		case ICAL_WEEKLY_RECURRENCE:
			e_soap_message_start_element (msg, "WeeklyRecurrence", NULL, NULL);

			snprintf (buffer, 32, "%d", recur.interval);
			e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

			len = snprintf (
				buffer, 256, "%s",
				number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));
			for (i = 1; recur.by_day[i] != ICAL_RECURRENCE_ARRAY_MAX; i++) {
				len += snprintf (
					buffer + len, 256 - len, " %s",
					number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[i])));
			}
			e_ews_message_write_string_parameter (msg, "DaysOfWeek", NULL, buffer);

			e_soap_message_end_element (msg); /* "WeeklyRecurrence" */
			break;

		case ICAL_MONTHLY_RECURRENCE:
			if (recur.by_month_day[0] == ICAL_RECURRENCE_ARRAY_MAX) {
				e_soap_message_start_element (msg, "RelativeMonthlyRecurrence", NULL, NULL);

				/* For now this is what got implemented since this is the only
				 relative monthly recurrence evolution can set.
				 TODO: extend the code with all possible monthly recurrence settings */
				snprintf (buffer, 32, "%d", recur.interval);
				e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

				e_ews_message_write_string_parameter (
					msg, "DaysOfWeek", NULL,
					number_to_weekday (icalrecurrencetype_day_day_of_week (recur.by_day[0])));

				e_ews_message_write_string_parameter (msg, "DayOfWeekIndex", NULL, weekindex_to_ical ((recur.by_set_pos[0] == 5 ? -1 : recur.by_set_pos[0])));

				e_soap_message_end_element (msg); /* "RelativeMonthlyRecurrence" */

			} else {
				e_soap_message_start_element (msg, "AbsoluteMonthlyRecurrence", NULL, NULL);

				snprintf (buffer, 256, "%d", recur.interval);
				e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

				snprintf (buffer, 256, "%d", recur.by_month_day[0]);
				e_ews_message_write_string_parameter (msg, "DayOfMonth", NULL, buffer);

				e_soap_message_end_element (msg); /* "AbsoluteMonthlyRecurrence" */

			}
			break;

		case ICAL_YEARLY_RECURRENCE:
			if (is_relative) {
				ewscal_add_rrule (msg, rrule);

			} else {
				e_soap_message_start_element (msg, "AbsoluteYearlyRecurrence", NULL, NULL);

				/* work according to RFC5545 §3.3.10
				 * dtstart is the default, give preference to by_month & by_month_day if they are set
				 */
				if (recur.by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
					snprintf (buffer, 256, "%d", recur.by_month_day[0]);
				} else {
					snprintf (buffer, 256, "%d", dtstart->day);
				}
				e_ews_message_write_string_parameter (msg, "DayOfMonth", NULL, buffer);

				if (recur.by_month[0] != ICAL_RECURRENCE_ARRAY_MAX) {
					snprintf (buffer, 256, "%d", recur.by_month_day[0]);
					e_ews_message_write_string_parameter (
						msg, "Month", NULL,
						number_to_month (recur.by_month[0]));
				} else {
					e_ews_message_write_string_parameter (
						msg, "Month", NULL,
						number_to_month (dtstart->month));
				}

				e_soap_message_end_element (msg); /* "AbsoluteYearlyRecurrence" */

			}
			break;

		case ICAL_SECONDLY_RECURRENCE:
		case ICAL_MINUTELY_RECURRENCE:
		case ICAL_HOURLY_RECURRENCE:
		default:
			/* TODO: remove the "Recurrence" element somehow */
			g_warning ("EWS cant handle recurrence with frequency higher than DAILY\n");
			goto exit;
	}

	if (recur.count > 0) {
		e_soap_message_start_element (msg, "NumberedRecurrence", NULL, NULL);
		ewscal_set_date (msg, "StartDate", dtstart);
		snprintf (buffer, 32, "%d", recur.count);
		e_ews_message_write_string_parameter (msg, "NumberOfOccurrences", NULL, buffer);
		e_soap_message_end_element (msg); /* "NumberedRecurrence" */

	} else if (!icaltime_is_null_time (recur.until)) {
		e_soap_message_start_element (msg, "EndDateRecurrence", NULL, NULL);
		ewscal_set_date (msg, "StartDate", dtstart);
		ewscal_set_date (msg, "EndDate", &recur.until);
		e_soap_message_end_element (msg); /* "EndDateRecurrence" */

	} else {
		e_soap_message_start_element (msg, "NoEndRecurrence", NULL, NULL);
		ewscal_set_date (msg, "StartDate", dtstart);
		e_soap_message_end_element (msg); /* "NoEndRecurrence" */
	}

exit:
	e_soap_message_end_element (msg); /* "Recurrence" */
}

static struct icaltimetype
icalcomponent_get_datetime (icalcomponent *comp,
                            icalproperty *prop)
{
	/* Extract datetime with proper timezone */
	icalcomponent *c;
	icalparameter *param;
	struct icaltimetype ret;

	ret = icalvalue_get_datetime (icalproperty_get_value (prop));

	if ((param = icalproperty_get_first_parameter (prop, ICAL_TZID_PARAMETER)) != NULL) {
		const gchar *tzid = icalparameter_get_tzid (param);
		icaltimezone *tz = NULL;

		for (c = comp; c != NULL; c = icalcomponent_get_parent (c)) {
			tz = icalcomponent_get_timezone (c, tzid);
			if (tz != NULL)	break;
		}

		if (tz == NULL)
			tz = icaltimezone_get_builtin_timezone_from_tzid (tzid);

		if (tz != NULL)
			ret = icaltime_set_timezone (&ret, tz);
	}

	return ret;
}

void
ewscal_set_reccurence_exceptions (ESoapMessage *msg,
                                  icalcomponent *comp)
{
	icalproperty *exdate;

	/* Make sure we have at least 1 excluded occurrence */
	exdate = icalcomponent_get_first_property (comp,ICAL_EXDATE_PROPERTY);
	if (!exdate) return;

	e_soap_message_start_element (msg, "DeletedOccurrences", NULL, NULL);

	for (; exdate; exdate = icalcomponent_get_next_property (comp, ICAL_EXDATE_PROPERTY)) {
		struct icaltimetype exdatetime = icalcomponent_get_datetime (comp, exdate);

		e_soap_message_start_element (msg, "DeletedOccurrence", NULL, NULL);

		ewscal_set_date (msg, "Start", &exdatetime);

		e_soap_message_end_element (msg); /* "DeletedOccurrence" */
	}

	e_soap_message_end_element (msg); /* "DeletedOccurrences" */
}

void
ewscal_get_attach_differences (const GSList *original,
                               const GSList *modified,
                               GSList **removed,
                               GSList **added)
{
	gboolean flag;
	GSList *i, *i_next, *j, *j_next, *original_copy, *modified_copy;
	original_copy = g_slist_copy ((GSList *) original);
	modified_copy = g_slist_copy ((GSList *) modified);

	for (j = modified_copy; j; j = j_next) {
		j_next = j->next;

		for (i = original_copy, flag = FALSE; !flag && i; i = i_next) {
			i_next = i->next;

			if (g_strcmp0 (j->data, i->data) == 0) {
				/* Remove from the lists attachments that are on both */
				original_copy = g_slist_delete_link (original_copy, i);
				modified_copy = g_slist_delete_link (modified_copy, j);
				flag = TRUE;
			}
		}
	}

	*removed = original_copy;
	*added = modified_copy;
}

/*
 * get meeting organizer e-mail address
 */
const gchar *
e_ews_collect_organizer (icalcomponent *comp)
{
	icalproperty *org_prop = NULL;
	const gchar *org = NULL;
	const gchar *org_email_address = NULL;

	org_prop = icalcomponent_get_first_property (comp, ICAL_ORGANIZER_PROPERTY);
	org = icalproperty_get_organizer (org_prop);
	if (!org)
		org = "";

	if (g_ascii_strncasecmp (org, "MAILTO:", 7) == 0)
		org_email_address = (org) + 7;
	else
		org_email_address = org;

	if (org_email_address && !*org_email_address)
		org_email_address = NULL;

	return org_email_address;
}

gchar *
e_ews_extract_attachment_id_from_uri (const gchar *uri)
{
	gchar *attachment_id, *filepath = g_filename_from_uri (uri, NULL, NULL);
	gchar **dirs = g_strsplit (filepath, "/", 0);
	gint n = 0;

	while (dirs[n]) n++;

	attachment_id = g_strdup (dirs[n - 1]);

	g_strfreev (dirs);

	return attachment_id;
}

void
e_ews_clean_icalcomponent (icalcomponent *icalcomp)
{
	icalproperty *prop, *item_id_prop = NULL, *changekey_prop = NULL;

	prop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (prop) {
		const gchar *x_name = icalproperty_get_x_name (prop);
		if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-ITEMID"))
			item_id_prop = prop;
		 else if (!g_ascii_strcasecmp (x_name, "X-EVOLUTION-CHANGEKEY"))
			changekey_prop = prop;

		prop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	if (item_id_prop != NULL)
		icalcomponent_remove_property (icalcomp, item_id_prop);

	if (changekey_prop != NULL)
		icalcomponent_remove_property (icalcomp, changekey_prop);
}
