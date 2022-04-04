/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <libecal/libecal.h>
#include <calendar/gui/calendar-config.h>

#include "e-ews-message.h"
#include "ews-errors.h"

#include "e-ews-calendar-utils.h"

static void
e_ews_cal_util_encode_regeneration (EEwsRecurrence *in_recur,
				    ICalComponent *out_comp) /* not 'out' as such */
{
	gchar chr = 0;

	g_return_if_fail (in_recur != NULL);
	g_return_if_fail (out_comp != NULL);

	switch (in_recur->type) {
	case E_EWS_RECURRENCE_DAILY_REGENERATION:
		chr = 'D';
		break;
	case E_EWS_RECURRENCE_WEEKLY_REGENERATION:
		chr = 'W';
		break;
	case E_EWS_RECURRENCE_MONTHLY_REGENERATION:
		chr = 'M';
		break;
	case E_EWS_RECURRENCE_YEARLY_REGENERATION:
		chr = 'Y';
		break;
	default:
		break;
	}

	if (chr && in_recur->end_type != E_EWS_RECURRENCE_END_UNKNOWN) {
		gchar end_chr = 0;
		gint64 end_num = 0;

		switch (in_recur->end_type) {
		case E_EWS_RECURRENCE_END_UNKNOWN:
			g_warn_if_reached ();
			break;
		case E_EWS_RECURRENCE_END_NO_END:
			end_chr = 'X';
			end_num = 0;
			break;
		case E_EWS_RECURRENCE_END_DATE:
			end_chr = 'D';
			end_num = (gint64) in_recur->end.utc_end_date;
			break;
		case E_EWS_RECURRENCE_END_NUMBERED:
			end_chr = 'N';
			end_num = (gint64) in_recur->end.number_of_occurrences;
			break;
		}

		if (end_chr) {
			gchar *value;
			gint64 start_num;

			start_num = (gint64) in_recur->utc_start_date;

			value = g_strdup_printf ("%c.%d.%c.%" G_GINT64_FORMAT ".%" G_GINT64_FORMAT,
				chr,
				in_recur->recur.interval,
				end_chr,
				start_num,
				end_num);
			e_cal_util_component_set_x_property (out_comp, X_EWS_TASK_REGENERATION, value);
			g_free (value);
		}
	} else {
		e_cal_util_component_remove_x_property (out_comp, X_EWS_TASK_REGENERATION);
	}
}

static gboolean
e_ews_cal_util_decode_regeneration_info_from_string (const gchar *value,
						     gchar *out_recur_type_chr,
						     gint *out_interval,
						     gchar *out_end_type_chr,
						     gint64 *out_start_num,
						     gint64 *out_end_num)
{
	gchar **parts;

	if (!value || !*value)
		return FALSE;

	parts = g_strsplit (value, ".", -1);

	if (g_strv_length (parts) != 5) {
		g_strfreev (parts);
		return FALSE;
	}

	if (out_recur_type_chr)
		*out_recur_type_chr = parts[0][0];

	if (out_interval)
		*out_interval = (gint) g_ascii_strtoll (parts[1], NULL, 10);

	if (out_end_type_chr)
		*out_end_type_chr = parts[2][0];

	if (out_start_num)
		*out_start_num = g_ascii_strtoll (parts[3], NULL, 10);

	if (out_end_num)
		*out_end_num = g_ascii_strtoll (parts[4], NULL, 10);

	g_strfreev (parts);

	return TRUE;
}

static void
e_ews_cal_util_decode_regeneration (ICalComponent *in_comp,
				    EEwsRecurrence *out_recur)
{
	gchar *value;
	gchar recur_type_chr = 0, end_type_chr = 0;
	gint interval = 0;
	gint64 start_num = 0, end_num = 0;

	g_return_if_fail (in_comp != NULL);
	g_return_if_fail (out_recur != NULL);

	value = e_cal_util_component_dup_x_property (in_comp, X_EWS_TASK_REGENERATION);

	if (e_ews_cal_util_decode_regeneration_info_from_string (value, &recur_type_chr, &interval, &end_type_chr, &start_num, &end_num)) {
		EEwsRecurrenceType recur_type = E_EWS_RECURRENCE_UNKNOWN;
		EEwsRecurrenceEndType end_type = E_EWS_RECURRENCE_END_UNKNOWN;

		switch (recur_type_chr) {
		case 'D':
			recur_type = E_EWS_RECURRENCE_DAILY_REGENERATION;
			break;
		case 'W':
			recur_type = E_EWS_RECURRENCE_WEEKLY_REGENERATION;
			break;
		case 'M':
			recur_type = E_EWS_RECURRENCE_MONTHLY_REGENERATION;
			break;
		case 'Y':
			recur_type = E_EWS_RECURRENCE_YEARLY_REGENERATION;
			break;
		}

		switch (end_type_chr) {
		case 'X':
			end_type = E_EWS_RECURRENCE_END_NO_END;
			break;
		case 'D':
			end_type = E_EWS_RECURRENCE_END_DATE;
			break;
		case 'N':
			end_type = E_EWS_RECURRENCE_END_NUMBERED;
			break;
		}

		if (recur_type != E_EWS_RECURRENCE_UNKNOWN && end_type != E_EWS_RECURRENCE_END_UNKNOWN &&
		    start_num > 0 && (end_type == E_EWS_RECURRENCE_END_NO_END || end_num > 0) && interval > 0) {
			out_recur->type = recur_type;
			out_recur->recur.interval = interval;
			out_recur->end_type = end_type;
			out_recur->utc_start_date = (time_t) start_num;

			if (end_type == E_EWS_RECURRENCE_END_DATE)
				out_recur->end.utc_end_date = (time_t) end_num;
			else if (end_type == E_EWS_RECURRENCE_END_NUMBERED)
				out_recur->end.number_of_occurrences = (gint) end_num;
		}
	} else if (value && *value) {
		g_warning ("%s: Failed to decode value '%s'", G_STRFUNC, value);
	}

	g_free (value);
}

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
                               ICalProperty *prop)
{
	ICalRecurrence *recur = i_cal_property_get_rrule (prop);
	gchar buffer[16];
	gint dayorder;

	dayorder = i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (recur, 0));
	dayorder = dayorder % 5;
	if (dayorder < 0)
		dayorder += 5;
	dayorder += 1;

	/* expected value is 1..5, inclusive */
	snprintf (buffer, 16, "%d", dayorder);
	e_ews_message_write_string_parameter (msg, "DayOrder", NULL, buffer);

	snprintf (buffer, 16, "%d", i_cal_recurrence_get_by_month (recur, 0));
	e_ews_message_write_string_parameter (msg, "Month", NULL, buffer);

	e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, number_to_weekday (i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (recur, 0))));

	g_clear_object (&recur);
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
                                    ICalComponent *comp,
                                    gint baseoffs)
{
	gchar buffer[16];
	ICalProperty *prop;
	ICalTime *dtstart;
	gint utcoffs;

	/* Calculate zone Offset from BaseOffset */
	prop = i_cal_component_get_first_property (comp, I_CAL_TZOFFSETTO_PROPERTY);
	if (prop) {
		utcoffs = -i_cal_property_get_tzoffsetto (prop) / 60;
		utcoffs -= baseoffs;
		snprintf (buffer, 16, "%d", utcoffs);
		e_ews_message_write_string_parameter (msg, "Bias", NULL, buffer);
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (comp, I_CAL_DTSTART_PROPERTY);
	if (prop) {
		dtstart = i_cal_property_get_dtstart (prop);
		snprintf (buffer, 16, "%02d:%02d:%02d", i_cal_time_get_hour (dtstart), i_cal_time_get_minute (dtstart), i_cal_time_get_second (dtstart));
		e_ews_message_write_string_parameter (msg, "Time", NULL, buffer);
		g_object_unref (dtstart);
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (comp, I_CAL_RRULE_PROPERTY);
	if (prop) {
		ewscal_add_availability_rrule (msg, prop);
		g_object_unref (prop);
	}
}

static void
ewscal_set_availability_timezone (ESoapMessage *msg,
                                  ICalTimezone *icaltz)
{
	ICalComponent *comp;
	ICalComponent *xstd, *xdaylight;
	gint std_utcoffs;
	gchar *offset;

	if (!icaltz)
		return;

	comp = i_cal_timezone_get_component (icaltz);

	if (comp) {
		xstd = i_cal_component_get_first_component (comp, I_CAL_XSTANDARD_COMPONENT);
		xdaylight = i_cal_component_get_first_component (comp, I_CAL_XDAYLIGHT_COMPONENT);
	} else {
		xstd = NULL;
		xdaylight = NULL;
	}

	/*TimeZone is the root element of GetUserAvailabilityRequest*/
	e_soap_message_start_element (msg, "TimeZone", NULL, NULL);

	/* Fetch the timezone offsets for the standard (or only) zone.
	 * Negate it, because Exchange does it backwards */
	if (xstd) {
		ICalProperty *prop;

		prop = i_cal_component_get_first_property (xstd, I_CAL_TZOFFSETTO_PROPERTY);
		if (prop) {
			std_utcoffs = -i_cal_property_get_tzoffsetto (prop) / 60;
			g_object_unref (prop);
		} else {
			std_utcoffs = 0;
		}
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

	g_clear_object (&comp);
	g_clear_object (&xstd);
	g_clear_object (&xdaylight);
}

gboolean
e_ews_cal_utils_prepare_free_busy_request (ESoapMessage *msg,
					   gpointer user_data,
					   GError **error)
{
	const EEWSFreeBusyData *fbdata = user_data;
	ICalTime *t_start, *t_end;
	ICalTimezone *utc_zone = i_cal_timezone_get_utc_timezone ();
	GSList *link;

	g_return_val_if_fail (fbdata != NULL, FALSE);

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

	t_start = i_cal_time_new_from_timet_with_zone (fbdata->period_start, 0, utc_zone);
	t_end = i_cal_time_new_from_timet_with_zone (fbdata->period_end, 0, utc_zone);

	e_soap_message_start_element (msg, "TimeWindow", NULL, NULL);
	e_ews_cal_utils_set_time (msg, "StartTime", t_start, FALSE);
	e_ews_cal_utils_set_time (msg, "EndTime", t_end, FALSE);
	e_soap_message_end_element (msg); /* "TimeWindow" */

	g_clear_object (&t_start);
	g_clear_object (&t_end);

	e_ews_message_write_string_parameter (msg, "MergedFreeBusyIntervalInMinutes", NULL, "60");
	e_ews_message_write_string_parameter (msg, "RequestedView", NULL, "DetailedMerged");

	e_soap_message_end_element (msg); /* "FreeBusyViewOptions" */

	return TRUE;
}

void
e_ews_cal_utils_set_time (ESoapMessage *msg,
			  const gchar *name,
			  ICalTime *tt,
			  gboolean with_timezone)
{
	ICalTime *local_tt = NULL;
	gchar *str;
	gchar *tz_ident = NULL;

	g_return_if_fail (tt != NULL);

	if (with_timezone) {
		ICalTimezone *zone = i_cal_time_get_timezone (tt);

		if (i_cal_time_is_utc (tt) || i_cal_time_is_date (tt) || !zone || zone == i_cal_timezone_get_utc_timezone ()) {
			tz_ident = g_strdup ("Z");
		} else {
			gint offset, is_daylight, hrs, mins;

			offset = i_cal_timezone_get_utc_offset (
				i_cal_timezone_get_utc_timezone (), tt, &is_daylight);

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

	if (i_cal_time_is_date (tt)) {
		ICalTimezone *cfg_zone;

		cfg_zone = calendar_config_get_icaltimezone ();
		local_tt = i_cal_time_new_from_timet_with_zone (i_cal_time_as_timet_with_zone (tt, cfg_zone), FALSE, i_cal_timezone_get_utc_timezone ());
		tt = local_tt;
	}

	str = g_strdup_printf (
		"%04d-%02d-%02dT%02d:%02d:%02d%s",
		i_cal_time_get_year (tt),
		i_cal_time_get_month (tt),
		i_cal_time_get_day (tt),
		i_cal_time_get_hour (tt),
		i_cal_time_get_minute (tt),
		i_cal_time_get_second (tt),
		tz_ident ? tz_ident : "");

	e_ews_message_write_string_parameter (msg, name, NULL, str);

	g_clear_object (&local_tt);
	g_free (tz_ident);
	g_free (str);
}

static gint
e_ews_cal_util_recurrence_count_by_xxx_and_free (GArray *array)
{
	guint ii;

	if (!array)
		return 0;

	for (ii = 0; ii < array->len; ii++) {
		if (g_array_index (array, gshort, ii) == I_CAL_RECURRENCE_ARRAY_MAX)
			break;
	}

	g_array_unref (array);

	return ii;
}

static EEwsRecurrenceDaysOfWeek
e_ews_cal_util_month_index_to_days_of_week (gint month_index)
{
	switch (month_index) {
	case 0:
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY;
	case 1:
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY;
	case 2:
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY;
	case 3:
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY;
	case 4:
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY;
	case 5:
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
	case 6:
		return E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;
	default:
		g_warning ("%s: What is month_index:%d for the last day?", G_STRFUNC, month_index);
		break;
	}

	return E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN;
}

enum month_num_options {
	MONTH_NUM_INVALID = -1,
	MONTH_NUM_FIRST,
	MONTH_NUM_SECOND,
	MONTH_NUM_THIRD,
	MONTH_NUM_FOURTH,
	MONTH_NUM_FIFTH,
	MONTH_NUM_LAST,
	MONTH_NUM_DAY,
	MONTH_NUM_OTHER
};

enum month_day_options {
	MONTH_DAY_NTH,
	MONTH_DAY_MON,
	MONTH_DAY_TUE,
	MONTH_DAY_WED,
	MONTH_DAY_THU,
	MONTH_DAY_FRI,
	MONTH_DAY_SAT,
	MONTH_DAY_SUN
};

static EEwsRecurrenceDayOfWeekIndex
e_ews_cal_util_month_num_to_day_of_week_index (gint month_num)
{
	switch (month_num) {
	case MONTH_NUM_FIRST:
		return E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FIRST;
	case MONTH_NUM_SECOND:
		return E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_SECOND;
	case MONTH_NUM_THIRD:
		return E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_THIRD;
	case MONTH_NUM_FOURTH:
		return E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FOURTH;
	case MONTH_NUM_FIFTH:
	case MONTH_NUM_LAST:
		return E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST;
	default:
		break;
	}

	return E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN;
}

static gboolean
e_ews_cal_utils_convert_recurrence (ICalComponent *icomp,
				    EEwsRecurrence *out_recur,
				    GError **error)
{
	ICalProperty *prop;
	ICalRecurrence *rrule = NULL;
	gint n_by_second, n_by_minute, n_by_hour;
	gint n_by_day, n_by_month_day, n_by_year_day;
	gint n_by_week_no, n_by_month, n_by_set_pos;

	g_return_val_if_fail (icomp != NULL, FALSE);
	g_return_val_if_fail (out_recur != NULL, FALSE);

	prop = i_cal_component_get_first_property (icomp, I_CAL_RRULE_PROPERTY);
	if (!prop) {
		e_ews_cal_util_decode_regeneration (icomp, out_recur);
		return TRUE;
	}

	switch (i_cal_component_isa (icomp)) {
	case I_CAL_VEVENT_COMPONENT:
	case I_CAL_VTODO_COMPONENT:
	case I_CAL_VJOURNAL_COMPONENT:
		break;
	default:
		g_object_unref (prop);
		return TRUE;
	}

	out_recur->type = E_EWS_RECURRENCE_UNKNOWN;
	out_recur->end_type = E_EWS_RECURRENCE_END_UNKNOWN;

	if (i_cal_component_count_properties (icomp, I_CAL_RRULE_PROPERTY) != 1 ||
	    i_cal_component_count_properties (icomp, I_CAL_RDATE_PROPERTY) != 0 ||
	    i_cal_component_count_properties (icomp, I_CAL_EXRULE_PROPERTY) != 0)
		goto custom;

	rrule = i_cal_property_get_rrule (prop);

	switch (i_cal_recurrence_get_freq (rrule)) {
	case I_CAL_DAILY_RECURRENCE:
	case I_CAL_WEEKLY_RECURRENCE:
	case I_CAL_MONTHLY_RECURRENCE:
	case I_CAL_YEARLY_RECURRENCE:
		break;
	default:
		goto custom;
	}

	n_by_second = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_second_array (rrule));
	n_by_minute = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_minute_array (rrule));
	n_by_hour = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_hour_array (rrule));
	n_by_day = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_day_array (rrule));
	n_by_month_day = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_month_day_array (rrule));
	n_by_year_day = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_year_day_array (rrule));
	n_by_week_no = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_week_no_array (rrule));
	n_by_month = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_month_array (rrule));
	n_by_set_pos = e_ews_cal_util_recurrence_count_by_xxx_and_free (i_cal_recurrence_get_by_set_pos_array (rrule));

	if (n_by_second != 0 ||
	    n_by_minute != 0 ||
	    n_by_hour != 0)
		goto custom;

	switch (i_cal_recurrence_get_freq (rrule)) {
	case I_CAL_DAILY_RECURRENCE:
		if (n_by_day != 0 ||
		    n_by_month_day != 0 ||
		    n_by_year_day != 0 ||
		    n_by_week_no != 0 ||
		    n_by_month != 0 ||
		    n_by_set_pos != 0)
			goto custom;

		if (i_cal_recurrence_get_interval (rrule) > 0) {
			out_recur->type = E_EWS_RECURRENCE_DAILY;
			out_recur->recur.interval = i_cal_recurrence_get_interval (rrule);
		}
		break;

	case I_CAL_WEEKLY_RECURRENCE: {
		gint ii, ndays;
		guint8 day_mask;

		if (n_by_month_day != 0 ||
		    n_by_year_day != 0 ||
		    n_by_week_no != 0 ||
		    n_by_month != 0 ||
		    n_by_set_pos != 0)
			goto custom;

		day_mask = 0;

		for (ii = 0; ii < 8 && i_cal_recurrence_get_by_day (rrule, ii) != I_CAL_RECURRENCE_ARRAY_MAX; ii++) {
			ICalRecurrenceWeekday weekday;
			gint pos;

			weekday = i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (rrule, ii));
			pos = i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (rrule, ii));

			if (pos != 0)
				goto custom;

			switch (weekday) {
			case I_CAL_SUNDAY_WEEKDAY:
				day_mask |= 1 << 0;
				break;

			case I_CAL_MONDAY_WEEKDAY:
				day_mask |= 1 << 1;
				break;

			case I_CAL_TUESDAY_WEEKDAY:
				day_mask |= 1 << 2;
				break;

			case I_CAL_WEDNESDAY_WEEKDAY:
				day_mask |= 1 << 3;
				break;

			case I_CAL_THURSDAY_WEEKDAY:
				day_mask |= 1 << 4;
				break;

			case I_CAL_FRIDAY_WEEKDAY:
				day_mask |= 1 << 5;
				break;

			case I_CAL_SATURDAY_WEEKDAY:
				day_mask |= 1 << 6;
				break;

			default:
				break;
			}
		}

		if (ii == 0) {
			ICalTime *dtstart;

			dtstart = i_cal_component_get_dtstart (icomp);

			ii = dtstart ? i_cal_time_day_of_week (dtstart) : 0;
			if (ii >= 1)
				day_mask |= 1 << (ii - 1);
			g_clear_object (&dtstart);
		}

		ndays = 0;

		for (ii = 0; ii < 7; ii++) {
			if ((day_mask & (1 << ii)) != 0)
				ndays++;
		}

		out_recur->type = E_EWS_RECURRENCE_WEEKLY;
		out_recur->recur.weekly.interval = i_cal_recurrence_get_interval (rrule);
		out_recur->recur.weekly.days_of_week = 0;
		out_recur->recur.weekly.first_day_of_week = G_DATE_BAD_WEEKDAY;

		for (ii = 0; ii < 7 && ndays; ii++) {
			if ((day_mask & (1 << ii)) != 0) {
				switch (ii) {
				case 0:
					out_recur->recur.weekly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY;
					break;
				case 1:
					out_recur->recur.weekly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY;
					break;
				case 2:
					out_recur->recur.weekly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY;
					break;
				case 3:
					out_recur->recur.weekly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY;
					break;
				case 4:
					out_recur->recur.weekly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY;
					break;
				case 5:
					out_recur->recur.weekly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
					break;
				case 6:
					out_recur->recur.weekly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;
					break;
				default:
					g_warn_if_reached ();
					break;
				}
			}
		}
		break;
	}

	case I_CAL_MONTHLY_RECURRENCE: {
		gint month_index = 1;
		enum month_day_options month_day;
		enum month_num_options month_num;

		if (n_by_year_day != 0 ||
		    n_by_week_no != 0 ||
		    n_by_month != 0 ||
		    n_by_set_pos > 1)
			goto custom;

		if (n_by_month_day == 1) {
			gint nth;

			if (n_by_set_pos != 0)
				goto custom;

			nth = i_cal_recurrence_get_by_month_day (rrule, 0);
			if (nth < 1 && nth != -1)
				goto custom;

			if (nth == -1) {
				ICalTime *dtstart;

				dtstart = i_cal_component_get_dtstart (icomp);

				month_index = dtstart ? i_cal_time_get_day (dtstart) : 0;
				month_num = MONTH_NUM_LAST;

				g_clear_object (&dtstart);
			} else {
				month_index = nth;
				month_num = MONTH_NUM_DAY;
			}
			month_day = MONTH_DAY_NTH;

		} else if (n_by_day == 1) {
			ICalRecurrenceWeekday weekday;
			gint pos;

			weekday = i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (rrule, 0));
			pos = i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (rrule, 0));

			if (pos == 0) {
				if (n_by_set_pos != 1)
					goto custom;
				pos = i_cal_recurrence_get_by_set_pos (rrule, 0);
			} else if (pos < 0) {
				goto custom;
			}

			switch (weekday) {
			case I_CAL_MONDAY_WEEKDAY:
				month_day = MONTH_DAY_MON;
				break;

			case I_CAL_TUESDAY_WEEKDAY:
				month_day = MONTH_DAY_TUE;
				break;

			case I_CAL_WEDNESDAY_WEEKDAY:
				month_day = MONTH_DAY_WED;
				break;

			case I_CAL_THURSDAY_WEEKDAY:
				month_day = MONTH_DAY_THU;
				break;

			case I_CAL_FRIDAY_WEEKDAY:
				month_day = MONTH_DAY_FRI;
				break;

			case I_CAL_SATURDAY_WEEKDAY:
				month_day = MONTH_DAY_SAT;
				break;

			case I_CAL_SUNDAY_WEEKDAY:
				month_day = MONTH_DAY_SUN;
				break;

			default:
				goto custom;
			}

			if (pos == -1)
				month_num = MONTH_NUM_LAST;
			else
				month_num = pos - 1;
		} else if (n_by_day > 1 && n_by_set_pos == 1 && n_by_month_day == 0) {
			gint ii, pos;

			pos = i_cal_recurrence_get_by_set_pos (rrule, 0);
			if (pos == -1)
				month_num = MONTH_NUM_LAST;
			else
				month_num = pos - 1;

			out_recur->type = E_EWS_RECURRENCE_RELATIVE_MONTHLY;
			out_recur->recur.relative_monthly.interval = i_cal_recurrence_get_interval (rrule);
			out_recur->recur.relative_monthly.days_of_week = 0;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);

			for (ii = 0; i_cal_recurrence_get_by_day (rrule, ii) != I_CAL_RECURRENCE_ARRAY_MAX; ii++) {
				ICalRecurrenceWeekday weekday;

				weekday = i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (rrule, ii));
				pos = i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (rrule, ii));

				if (pos != 0)
					goto custom;

				switch (weekday) {
				case I_CAL_SUNDAY_WEEKDAY:
					out_recur->recur.relative_monthly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY;
					break;
				case I_CAL_MONDAY_WEEKDAY:
					out_recur->recur.relative_monthly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY;
					break;
				case I_CAL_TUESDAY_WEEKDAY:
					out_recur->recur.relative_monthly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY;
					break;
				case I_CAL_WEDNESDAY_WEEKDAY:
					out_recur->recur.relative_monthly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY;
					break;
				case I_CAL_THURSDAY_WEEKDAY:
					out_recur->recur.relative_monthly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY;
					break;
				case I_CAL_FRIDAY_WEEKDAY:
					out_recur->recur.relative_monthly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
					break;
				case I_CAL_SATURDAY_WEEKDAY:
					out_recur->recur.relative_monthly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;
					break;
				default:
					break;
				}
			}

			break;
		} else {
			goto custom;
		}

		out_recur->type = E_EWS_RECURRENCE_RELATIVE_MONTHLY;
		out_recur->recur.relative_monthly.interval = i_cal_recurrence_get_interval (rrule);
		out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN;
		out_recur->recur.relative_monthly.day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN;

		switch (month_day) {
		case MONTH_DAY_NTH:
			if (month_num == MONTH_NUM_LAST) {
				out_recur->recur.relative_monthly.days_of_week = e_ews_cal_util_month_index_to_days_of_week (month_index);
				out_recur->recur.relative_monthly.day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST;
			} else { /* month_num = MONTH_NUM_DAY; */
				out_recur->type = E_EWS_RECURRENCE_ABSOLUTE_MONTHLY;
				out_recur->recur.absolute_monthly.interval = i_cal_recurrence_get_interval (rrule);
				out_recur->recur.absolute_monthly.day_of_month = month_index;
			}
			break;
		case MONTH_DAY_MON:
			out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
			break;
		case MONTH_DAY_TUE:
			out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
			break;
		case MONTH_DAY_WED:
			out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
			break;
		case MONTH_DAY_THU:
			out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
			break;
		case MONTH_DAY_FRI:
			out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
			break;
		case MONTH_DAY_SAT:
			out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
			break;
		case MONTH_DAY_SUN:
			out_recur->recur.relative_monthly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY;
			out_recur->recur.relative_monthly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
			break;
		}

		break;
	}

	case I_CAL_YEARLY_RECURRENCE: {
		gint month_index = 1;
		enum month_day_options month_day = MONTH_DAY_NTH;
		enum month_num_options month_num = MONTH_NUM_DAY;

		if (n_by_year_day != 0 ||
		    n_by_week_no != 0 ||
		    n_by_set_pos > 1 ||
		    i_cal_recurrence_get_interval (rrule) > 1)
			goto custom;

		if (n_by_month_day == 1) {
			gint nth;

			if (n_by_set_pos != 0)
				goto custom;

			nth = i_cal_recurrence_get_by_month_day (rrule, 0);
			if (nth < 1 && nth != -1)
				goto custom;

			if (nth == -1) {
				ICalTime *dtstart;

				dtstart = i_cal_component_get_dtstart (icomp);

				month_index = dtstart ? i_cal_time_get_day (dtstart) : 0;
				month_num = MONTH_NUM_LAST;

				g_clear_object (&dtstart);
			} else {
				month_index = nth;
				month_num = MONTH_NUM_DAY;
			}
			month_day = MONTH_DAY_NTH;
		}

		if (n_by_day == 1) {
			ICalRecurrenceWeekday weekday;
			gint pos;

			weekday = i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (rrule, 0));
			pos = i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (rrule, 0));

			if (pos == 0) {
				if (n_by_set_pos != 1)
					goto custom;
				pos = i_cal_recurrence_get_by_set_pos (rrule, 0);
			} else if (pos < 0) {
				goto custom;
			}

			switch (weekday) {
			case I_CAL_MONDAY_WEEKDAY:
				month_day = MONTH_DAY_MON;
				break;

			case I_CAL_TUESDAY_WEEKDAY:
				month_day = MONTH_DAY_TUE;
				break;

			case I_CAL_WEDNESDAY_WEEKDAY:
				month_day = MONTH_DAY_WED;
				break;

			case I_CAL_THURSDAY_WEEKDAY:
				month_day = MONTH_DAY_THU;
				break;

			case I_CAL_FRIDAY_WEEKDAY:
				month_day = MONTH_DAY_FRI;
				break;

			case I_CAL_SATURDAY_WEEKDAY:
				month_day = MONTH_DAY_SAT;
				break;

			case I_CAL_SUNDAY_WEEKDAY:
				month_day = MONTH_DAY_SUN;
				break;

			default:
				goto custom;
			}

			if (pos == -1)
				month_num = MONTH_NUM_LAST;
			else
				month_num = pos - 1;
		}

		out_recur->type = E_EWS_RECURRENCE_RELATIVE_YEARLY;
		out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN;
		out_recur->recur.relative_yearly.day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN;
		out_recur->recur.relative_yearly.month = i_cal_recurrence_get_by_month (rrule, 0);

		if (n_by_day > 1 &&
		    n_by_month == 1 &&
		    n_by_set_pos == 1 &&
		    n_by_month_day == 0) {
			gint ii, pos;

			pos = i_cal_recurrence_get_by_set_pos (rrule, 0);
			if (pos == -1)
				month_num = MONTH_NUM_LAST;
			else
				month_num = pos - 1;

			out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);

			for (ii = 0; i_cal_recurrence_get_by_day (rrule, ii) != I_CAL_RECURRENCE_ARRAY_MAX; ii++) {
				ICalRecurrenceWeekday weekday;

				weekday = i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (rrule, ii));
				pos = i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (rrule, ii));

				if (pos != 0)
					goto custom;

				switch (weekday) {
				case I_CAL_SUNDAY_WEEKDAY:
					out_recur->recur.relative_yearly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY;
					break;
				case I_CAL_MONDAY_WEEKDAY:
					out_recur->recur.relative_yearly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY;
					break;
				case I_CAL_TUESDAY_WEEKDAY:
					out_recur->recur.relative_yearly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY;
					break;
				case I_CAL_WEDNESDAY_WEEKDAY:
					out_recur->recur.relative_yearly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY;
					break;
				case I_CAL_THURSDAY_WEEKDAY:
					out_recur->recur.relative_yearly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY;
					break;
				case I_CAL_FRIDAY_WEEKDAY:
					out_recur->recur.relative_yearly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
					break;
				case I_CAL_SATURDAY_WEEKDAY:
					out_recur->recur.relative_yearly.days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;
					break;
				default:
					break;
				}
			}
		} else if (n_by_month == 1 &&
			   n_by_day == 1 &&
			   n_by_month_day == 0) {
			switch (month_day) {
			case MONTH_DAY_NTH:
				if (month_num == MONTH_NUM_LAST) {
					out_recur->recur.relative_yearly.days_of_week = e_ews_cal_util_month_index_to_days_of_week (month_index);
					out_recur->recur.relative_yearly.day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST;
				} else { /* month_num = MONTH_NUM_DAY; */
					out_recur->type = E_EWS_RECURRENCE_ABSOLUTE_YEARLY;
					out_recur->recur.absolute_yearly.day_of_month = month_index;
					out_recur->recur.absolute_yearly.month = i_cal_recurrence_get_by_month (rrule, 0);
				}
				break;
			case MONTH_DAY_MON:
				out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY;
				out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
				break;
			case MONTH_DAY_TUE:
				out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY;
				out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
				break;
			case MONTH_DAY_WED:
				out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY;
				out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
				break;
			case MONTH_DAY_THU:
				out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY;
				out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
				break;
			case MONTH_DAY_FRI:
				out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
				out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
				break;
			case MONTH_DAY_SAT:
				out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;
				out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
				break;
			case MONTH_DAY_SUN:
				out_recur->recur.relative_yearly.days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY;
				out_recur->recur.relative_yearly.day_of_week_index = e_ews_cal_util_month_num_to_day_of_week_index (month_num);
				break;
			}
		} else if (n_by_month == 1 &&
			   n_by_month_day == 1 &&
			   n_by_day == 0) {
			if (month_num == MONTH_NUM_LAST) {
				out_recur->recur.relative_yearly.days_of_week = e_ews_cal_util_month_index_to_days_of_week (month_index);
				out_recur->recur.relative_yearly.day_of_week_index = E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST;
			} else { /* month_num = MONTH_NUM_DAY; */
				out_recur->type = E_EWS_RECURRENCE_ABSOLUTE_YEARLY;
				out_recur->recur.absolute_yearly.day_of_month = month_index;
				out_recur->recur.absolute_yearly.month = i_cal_recurrence_get_by_month (rrule, 0);
			}
		} else if (n_by_day == 0 &&
			   n_by_month_day == 0 &&
			   n_by_month == 0) {
			ICalTime *dtstart;

			dtstart = i_cal_component_get_dtstart (icomp);
			if (!dtstart || i_cal_time_is_null_time (dtstart)) {
				g_clear_object (&dtstart);
				goto custom;
			}

			out_recur->type = E_EWS_RECURRENCE_ABSOLUTE_YEARLY;
			out_recur->recur.absolute_yearly.day_of_month = i_cal_time_get_day (dtstart);
			out_recur->recur.absolute_yearly.month = i_cal_time_get_month (dtstart);

			g_object_unref (dtstart);
		} else {
			goto custom;
		}
		} break;

	default:
		break;
	}

	if (out_recur->type != E_EWS_RECURRENCE_UNKNOWN) {
		ICalTime *dtstart;

		dtstart = i_cal_component_get_dtstart (icomp);

		out_recur->utc_start_date = i_cal_time_as_timet_with_zone (dtstart, i_cal_timezone_get_utc_timezone ());
		g_clear_object (&dtstart);

		if (i_cal_recurrence_get_count (rrule)) {
			out_recur->end_type = E_EWS_RECURRENCE_END_NUMBERED;
			out_recur->end.number_of_occurrences = i_cal_recurrence_get_count (rrule);
		} else {
			ICalTime *until;

			until = i_cal_recurrence_get_until (rrule);
			if (until && !i_cal_time_is_null_time (until) && i_cal_time_get_year (until)) {
				i_cal_time_set_is_date (until, TRUE);

				out_recur->end_type = E_EWS_RECURRENCE_END_DATE;
				out_recur->end.utc_end_date = i_cal_time_as_timet_with_zone (until, i_cal_timezone_get_utc_timezone ());
			} else {
				out_recur->end_type = E_EWS_RECURRENCE_END_NO_END;
			}

			g_clear_object (&until);
		}
	}

	g_clear_object (&rrule);
	g_clear_object (&prop);

	return TRUE;

 custom:

	g_clear_object (&rrule);
	g_clear_object (&prop);

	g_set_error (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_CALENDARINVALIDRECURRENCE,
		_("Cannot store this recurrence. Change it to simple single daily, weekly, monthly or yearly recurrence without exceptions and with start date."));

	return FALSE;
}

static void
e_ews_cal_utils_write_days_of_week (ESoapMessage *msg,
				    guint32 days_of_week)
{
	GString *value;
	guint32 weekdays, weekenddays;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	if (days_of_week == E_EWS_RECURRENCE_DAYS_OF_WEEK_UNKNOWN)
		return;

	weekdays = E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY |
		E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY |
		E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY |
		E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY |
		E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
	weekenddays = E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY |
		E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;

	if ((days_of_week & weekdays) == weekdays && !(days_of_week & ~weekdays))
		days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKDAY;
	else if ((days_of_week & weekenddays) == weekenddays && !(days_of_week & ~weekenddays))
		days_of_week = E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKENDDAY;

	/* Do not localize, these are values used in XML */
	if ((days_of_week & E_EWS_RECURRENCE_DAYS_OF_WEEK_DAY) != 0) {
		value = g_string_new ("Day");
	} else if ((days_of_week & E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKDAY) != 0) {
		value = g_string_new ("Weekday");
	} else if ((days_of_week & E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKENDDAY) != 0) {
		value = g_string_new ("WeekendDay");
	} else {
		struct _bits {
			EEwsRecurrenceDaysOfWeek n_bit;
			const gchar *value;
		} bits[] = {
			{ E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY, "Sunday" },
			{ E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY, "Monday" },
			{ E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY, "Tuesday" },
			{ E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY, "Wednesday" },
			{ E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY, "Thursday" },
			{ E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY, "Friday" },
			{ E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY, "Saturday" }
		};
		gint ii;

		value = g_string_new ("");

		for (ii = 0; ii < G_N_ELEMENTS (bits); ii++) {
			if ((days_of_week & bits[ii].n_bit) != 0) {
				if (value->len)
					g_string_append_c (value, ' ');

				g_string_append (value, bits[ii].value);
			}
		}
	}

	if (value->len) {
		e_soap_message_start_element (msg, "DaysOfWeek", NULL, NULL);
		e_soap_message_write_string (msg, value->str);
		e_soap_message_end_element (msg); /* DaysOfWeek */
	}

	g_string_free (value, TRUE);
}

static void
e_ews_cal_utils_write_day_of_week_index (ESoapMessage *msg,
					 EEwsRecurrenceDayOfWeekIndex day_of_week_index)
{
	const gchar *value = NULL;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	/* Do not localize, these are values used in XML */
	switch (day_of_week_index) {
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN:
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FIRST:
		value = "First";
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_SECOND:
		value = "Second";
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_THIRD:
		value = "Third";
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FOURTH:
		value = "Fourth";
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST:
		value = "Last";
		break;
	}

	if (value) {
		e_soap_message_start_element (msg, "DayOfWeekIndex", NULL, NULL);
		e_soap_message_write_string (msg, value);
		e_soap_message_end_element (msg); /* DayOfWeekIndex */
	}
}

static void
e_ews_cal_utils_write_month (ESoapMessage *msg,
			     GDateMonth month)
{
	const gchar *value = NULL;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));

	/* Do not localize, these are values used in XML */
	switch (month) {
	case G_DATE_BAD_MONTH:
		break;
	case  G_DATE_JANUARY:
		value = "January";
		break;
	case G_DATE_FEBRUARY:
		value = "February";
		break;
	case G_DATE_MARCH:
		value = "March";
		break;
	case G_DATE_APRIL:
		value = "April";
		break;
	case G_DATE_MAY:
		value = "May";
		break;
	case G_DATE_JUNE:
		value = "June";
		break;
	case G_DATE_JULY:
		value = "July";
		break;
	case G_DATE_AUGUST:
		value = "August";
		break;
	case G_DATE_SEPTEMBER:
		value = "September";
		break;
	case G_DATE_OCTOBER:
		value = "October";
		break;
	case G_DATE_NOVEMBER:
		value = "November";
		break;
	case G_DATE_DECEMBER:
		value = "December";
		break;
	}

	if (value) {
		e_soap_message_start_element (msg, "Month", NULL, NULL);
		e_soap_message_write_string (msg, value);
		e_soap_message_end_element (msg); /* Month */
	}
}

static void
e_ews_cal_util_write_utc_date (ESoapMessage *msg,
			       const gchar *name,
			       time_t utc_date)
{
	ICalTime *itt;
	gchar *value;

	g_return_if_fail (E_IS_SOAP_MESSAGE (msg));
	g_return_if_fail (name != NULL);

	itt = i_cal_time_new_from_timet_with_zone (utc_date, 1, i_cal_timezone_get_utc_timezone ());
	value = g_strdup_printf ("%04d-%02d-%02dZ",
		i_cal_time_get_year (itt),
		i_cal_time_get_month (itt),
		i_cal_time_get_day (itt));
	g_clear_object (&itt);

	e_soap_message_start_element (msg, name, NULL, NULL);
	e_soap_message_write_string (msg, value);
	e_soap_message_end_element (msg);

	g_free (value);
}

/* Writes 'Recurrence' element into the @msg. Sets the @error only if the RRULE
   cannot be transformed into Recurrence */
gboolean
e_ews_cal_utils_set_recurrence (ESoapMessage *msg,
				ICalComponent *comp,
				gboolean server_satisfies_2013,
				GError **error)
{
	EEwsRecurrence recur;

	g_return_val_if_fail (E_IS_SOAP_MESSAGE (msg), FALSE);
	g_return_val_if_fail (comp != NULL, FALSE);

	memset (&recur, 0, sizeof (EEwsRecurrence));

	recur.type = E_EWS_RECURRENCE_UNKNOWN;
	recur.end_type = E_EWS_RECURRENCE_END_UNKNOWN;

	if (!e_ews_cal_utils_convert_recurrence (comp, &recur, error))
		return FALSE;

	if (recur.type == E_EWS_RECURRENCE_UNKNOWN ||
	    recur.end_type == E_EWS_RECURRENCE_END_UNKNOWN) {
		return TRUE;
	}

	e_soap_message_start_element (msg, "Recurrence", NULL, NULL);

	switch (recur.type) {
	case E_EWS_RECURRENCE_UNKNOWN:
		g_warn_if_reached ();
		break;
	case E_EWS_RECURRENCE_RELATIVE_YEARLY:
		e_soap_message_start_element (msg, "RelativeYearlyRecurrence", NULL, NULL);
		e_ews_cal_utils_write_days_of_week (msg, recur.recur.relative_yearly.days_of_week);
		e_ews_cal_utils_write_day_of_week_index (msg, recur.recur.relative_yearly.day_of_week_index);
		e_ews_cal_utils_write_month (msg, recur.recur.relative_yearly.month);
		e_soap_message_end_element (msg); /* RelativeYearlyRecurrence */
		break;
	case E_EWS_RECURRENCE_ABSOLUTE_YEARLY:
		e_soap_message_start_element (msg, "AbsoluteYearlyRecurrence", NULL, NULL);
		e_soap_message_start_element (msg, "DayOfMonth", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.absolute_yearly.day_of_month);
		e_soap_message_end_element (msg); /* DayOfMonth */
		e_ews_cal_utils_write_month (msg, recur.recur.absolute_yearly.month);
		e_soap_message_end_element (msg); /* AbsoluteYearlyRecurrence */
		break;
	case E_EWS_RECURRENCE_RELATIVE_MONTHLY:
		e_soap_message_start_element (msg, "RelativeMonthlyRecurrence", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.relative_monthly.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_ews_cal_utils_write_days_of_week (msg, recur.recur.relative_monthly.days_of_week);
		e_ews_cal_utils_write_day_of_week_index (msg, recur.recur.relative_monthly.day_of_week_index);
		e_soap_message_end_element (msg); /* RelativeMonthlyRecurrence */
		break;
	case E_EWS_RECURRENCE_ABSOLUTE_MONTHLY:
		e_soap_message_start_element (msg, "AbsoluteMonthlyRecurrence", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.absolute_monthly.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_soap_message_start_element (msg, "DayOfMonth", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.absolute_monthly.day_of_month);
		e_soap_message_end_element (msg); /* DayOfMonth */
		e_soap_message_end_element (msg); /* AbsoluteMonthlyRecurrence */
		break;
	case E_EWS_RECURRENCE_WEEKLY:
		e_soap_message_start_element (msg, "WeeklyRecurrence", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.weekly.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_ews_cal_utils_write_days_of_week (msg, recur.recur.weekly.days_of_week);
		if (server_satisfies_2013) {
			const gchar *value = NULL;

			switch (recur.recur.weekly.first_day_of_week) {
			case G_DATE_BAD_WEEKDAY:
				break;
			case G_DATE_MONDAY:
				value = "Monday";
				break;
			case G_DATE_TUESDAY:
				value = "Tuesday";
				break;
			case G_DATE_WEDNESDAY:
				value = "Wednesday";
				break;
			case G_DATE_THURSDAY:
				value = "Thursday";
				break;
			case G_DATE_FRIDAY:
				value = "Friday";
				break;
			case G_DATE_SATURDAY:
				value = "Saturday";
				break;
			case G_DATE_SUNDAY:
				value = "Sunday";
				break;
			}

			if (value) {
				e_soap_message_start_element (msg, "FirstDayOfWeek", NULL, NULL);
				e_soap_message_write_string (msg, value);
				e_soap_message_end_element (msg); /* FirstDayOfWeek */
			}
		}
		e_soap_message_end_element (msg); /* WeeklyRecurrence */
		break;
	case E_EWS_RECURRENCE_DAILY:
		e_soap_message_start_element (msg, "DailyRecurrence", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_soap_message_end_element (msg); /* DailyRecurrence */
		break;
	case E_EWS_RECURRENCE_DAILY_REGENERATION:
		e_soap_message_start_element (msg, "DailyRegeneration", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_soap_message_end_element (msg); /* DailyRegeneration */
		break;
	case E_EWS_RECURRENCE_WEEKLY_REGENERATION:
		e_soap_message_start_element (msg, "WeeklyRegeneration", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_soap_message_end_element (msg); /* WeeklyRegeneration */
		break;
	case E_EWS_RECURRENCE_MONTHLY_REGENERATION:
		e_soap_message_start_element (msg, "MonthlyRegeneration", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_soap_message_end_element (msg); /* MonthlyRegeneration */
		break;
	case E_EWS_RECURRENCE_YEARLY_REGENERATION:
		e_soap_message_start_element (msg, "YearlyRegeneration", NULL, NULL);
		e_soap_message_start_element (msg, "Interval", NULL, NULL);
		e_soap_message_write_int (msg, recur.recur.interval);
		e_soap_message_end_element (msg); /* Interval */
		e_soap_message_end_element (msg); /* YearlyRegeneration */
		break;
	}

	switch (recur.end_type) {
	case E_EWS_RECURRENCE_END_UNKNOWN:
		g_warn_if_reached ();
		break;
	case E_EWS_RECURRENCE_END_NO_END:
		e_soap_message_start_element (msg, "NoEndRecurrence", NULL, NULL);
		e_ews_cal_util_write_utc_date (msg, "StartDate", recur.utc_start_date);
		e_soap_message_end_element (msg); /* NoEndRecurrence */
		break;
	case E_EWS_RECURRENCE_END_DATE:
		e_soap_message_start_element (msg, "EndDateRecurrence", NULL, NULL);
		e_ews_cal_util_write_utc_date (msg, "StartDate", recur.utc_start_date);
		e_ews_cal_util_write_utc_date (msg, "EndDate", recur.end.utc_end_date);
		e_soap_message_end_element (msg); /* EndDateRecurrence */
		break;
	case E_EWS_RECURRENCE_END_NUMBERED:
		e_soap_message_start_element (msg, "NumberedRecurrence", NULL, NULL);
		e_ews_cal_util_write_utc_date (msg, "StartDate", recur.utc_start_date);
		e_soap_message_start_element (msg, "NumberOfOccurrences", NULL, NULL);
		e_soap_message_write_int (msg, recur.end.number_of_occurrences);
		e_soap_message_end_element (msg); /* NumberOfOccurrences */
		e_soap_message_end_element (msg); /* NumberedRecurrence */
		break;
	}

	e_soap_message_end_element (msg); /* Recurrence */

	return TRUE;
}

static void
e_ews_cal_utils_days_of_week_to_rrule (ICalRecurrence *rrule,
				       guint32 days_of_week)
{
	struct _bits {
		EEwsRecurrenceDaysOfWeek bit;
		ICalRecurrenceWeekday week_day;
	} bits[] = {
		{ E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY, I_CAL_SUNDAY_WEEKDAY },
		{ E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY, I_CAL_MONDAY_WEEKDAY },
		{ E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY, I_CAL_TUESDAY_WEEKDAY },
		{ E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY, I_CAL_WEDNESDAY_WEEKDAY },
		{ E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY, I_CAL_THURSDAY_WEEKDAY },
		{ E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY, I_CAL_FRIDAY_WEEKDAY },
		{ E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY, I_CAL_SATURDAY_WEEKDAY }
	};
	gint ii, idx;

	g_return_if_fail (rrule != NULL);

	if ((days_of_week & E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKDAY) != 0) {
		days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_MONDAY |
			E_EWS_RECURRENCE_DAYS_OF_WEEK_TUESDAY |
			E_EWS_RECURRENCE_DAYS_OF_WEEK_WEDNESDAY |
			E_EWS_RECURRENCE_DAYS_OF_WEEK_THURSDAY |
			E_EWS_RECURRENCE_DAYS_OF_WEEK_FRIDAY;
	} else if ((days_of_week & E_EWS_RECURRENCE_DAYS_OF_WEEK_WEEKENDDAY) != 0) {
		days_of_week |= E_EWS_RECURRENCE_DAYS_OF_WEEK_SUNDAY | E_EWS_RECURRENCE_DAYS_OF_WEEK_SATURDAY;
	}

	idx = 0;

	for (ii = 0; ii < G_N_ELEMENTS (bits); ii++) {
		if ((days_of_week & bits[ii].bit) != 0) {
			i_cal_recurrence_set_by_day (rrule, idx, bits[ii].week_day);
			idx++;
		}
	}

	i_cal_recurrence_set_by_day (rrule, idx, I_CAL_RECURRENCE_ARRAY_MAX);
}

static void
e_ews_cal_utils_day_of_week_index_to_rrule (ICalRecurrence *rrule,
					    EEwsRecurrenceDayOfWeekIndex day_of_week_index)
{
	g_return_if_fail (rrule != NULL);

	switch (day_of_week_index) {
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_UNKNOWN:
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FIRST:
		i_cal_recurrence_set_by_set_pos (rrule, 0, 1);
		i_cal_recurrence_set_by_set_pos (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_SECOND:
		i_cal_recurrence_set_by_set_pos (rrule, 0, 2);
		i_cal_recurrence_set_by_set_pos (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_THIRD:
		i_cal_recurrence_set_by_set_pos (rrule, 0, 3);
		i_cal_recurrence_set_by_set_pos (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_FOURTH:
		i_cal_recurrence_set_by_set_pos (rrule, 0, 4);
		i_cal_recurrence_set_by_set_pos (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
		break;
	case E_EWS_RECURRENCE_DAY_OF_WEEK_INDEX_LAST:
		i_cal_recurrence_set_by_set_pos (rrule, 0, -1);
		i_cal_recurrence_set_by_set_pos (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
		break;
	}
}

static void
e_ews_cal_utils_month_to_rrule (ICalRecurrence *rrule,
				GDateMonth month)
{
	g_return_if_fail (rrule != NULL);

	if (month >= G_DATE_JANUARY && month <= G_DATE_DECEMBER) {
		i_cal_recurrence_set_by_month (rrule, 0, month);
		i_cal_recurrence_set_by_month (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
	}
}

/* Does nothing when the 'item' doesn't contain any recurrence */
void
e_ews_cal_utils_recurrence_to_rrule (EEwsItem *item,
				     ICalComponent *comp)
{
	EEwsRecurrence recur;
	ICalRecurrence *rrule;
	ICalTime *recur_start, *itt;
	gboolean usable = FALSE;

	g_return_if_fail (E_IS_EWS_ITEM (item));
	g_return_if_fail (comp != NULL);

	e_cal_util_component_remove_property_by_kind (comp, I_CAL_RRULE_PROPERTY, TRUE);
	e_cal_util_component_remove_property_by_kind (comp, I_CAL_RDATE_PROPERTY, TRUE);
	e_cal_util_component_remove_property_by_kind (comp, I_CAL_EXRULE_PROPERTY, TRUE);
	e_cal_util_component_remove_property_by_kind (comp, I_CAL_EXDATE_PROPERTY, TRUE);
	e_cal_util_component_remove_x_property (comp, X_EWS_TASK_REGENERATION);

	if (!e_ews_item_get_recurrence (item, &recur))
		return;

	rrule = i_cal_recurrence_new ();
	recur_start = i_cal_time_new_from_timet_with_zone (recur.utc_start_date, 1, i_cal_timezone_get_utc_timezone ());

	switch (recur.end_type) {
	case E_EWS_RECURRENCE_END_UNKNOWN:
		break;
	case E_EWS_RECURRENCE_END_NO_END:
		itt = i_cal_time_new_null_time ();
		i_cal_recurrence_set_until (rrule, itt);
		i_cal_recurrence_set_count (rrule, 0);
		g_clear_object (&itt);
		usable = TRUE;
		break;
	case E_EWS_RECURRENCE_END_DATE:
		itt = i_cal_time_new_from_timet_with_zone (recur.end.utc_end_date, 1, i_cal_timezone_get_utc_timezone ());
		i_cal_recurrence_set_until (rrule, itt);
		i_cal_recurrence_set_count (rrule, 0);
		usable = !i_cal_time_is_null_time (itt) && i_cal_time_is_valid_time (itt);
		g_clear_object (&itt);
		break;
	case E_EWS_RECURRENCE_END_NUMBERED:
		itt = i_cal_time_new_null_time ();
		i_cal_recurrence_set_until (rrule, itt);
		i_cal_recurrence_set_count (rrule, recur.end.number_of_occurrences);
		g_clear_object (&itt);
		usable = recur.end.number_of_occurrences > 0;
		break;
	}

	if (!usable) {
		g_warning ("%s: Cannot decode end condition in received recurrence", G_STRFUNC);
		g_object_unref (recur_start);
		g_object_unref (rrule);
		return;
	}

	usable = FALSE;

	switch (recur.type) {
	case E_EWS_RECURRENCE_UNKNOWN:
		break;
	case E_EWS_RECURRENCE_RELATIVE_YEARLY:
		usable = TRUE;
		i_cal_recurrence_set_freq (rrule, I_CAL_YEARLY_RECURRENCE);
		i_cal_recurrence_set_interval (rrule, 1);
		e_ews_cal_utils_days_of_week_to_rrule (rrule, recur.recur.relative_yearly.days_of_week);
		e_ews_cal_utils_day_of_week_index_to_rrule (rrule, recur.recur.relative_yearly.day_of_week_index);
		e_ews_cal_utils_month_to_rrule (rrule, recur.recur.relative_yearly.month);
		break;
	case E_EWS_RECURRENCE_ABSOLUTE_YEARLY:
		usable = TRUE;
		i_cal_recurrence_set_freq (rrule, I_CAL_YEARLY_RECURRENCE);
		i_cal_recurrence_set_interval (rrule, 1);
		if (i_cal_time_is_valid_time (recur_start) &&
		    i_cal_time_get_month (recur_start) == recur.recur.absolute_yearly.month &&
		    i_cal_time_get_day (recur_start) == recur.recur.absolute_yearly.day_of_month) {
			/* This is how evolution uses it, derive date from the DTSTART */
		} else {
			i_cal_recurrence_set_by_month_day (rrule, 0, recur.recur.absolute_yearly.day_of_month);
			i_cal_recurrence_set_by_month_day (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
			e_ews_cal_utils_month_to_rrule (rrule, recur.recur.absolute_yearly.month);
		}
		break;
	case E_EWS_RECURRENCE_RELATIVE_MONTHLY:
		usable = TRUE;
		i_cal_recurrence_set_freq (rrule, I_CAL_MONTHLY_RECURRENCE);
		i_cal_recurrence_set_interval (rrule, recur.recur.relative_monthly.interval);
		e_ews_cal_utils_days_of_week_to_rrule (rrule, recur.recur.relative_monthly.days_of_week);
		e_ews_cal_utils_day_of_week_index_to_rrule (rrule, recur.recur.relative_monthly.day_of_week_index);
		break;
	case E_EWS_RECURRENCE_ABSOLUTE_MONTHLY:
		usable = TRUE;
		i_cal_recurrence_set_freq (rrule, I_CAL_MONTHLY_RECURRENCE);
		i_cal_recurrence_set_interval (rrule, recur.recur.absolute_monthly.interval);
		i_cal_recurrence_set_by_month_day (rrule, 0, recur.recur.absolute_monthly.day_of_month);
		i_cal_recurrence_set_by_month_day (rrule, 1, I_CAL_RECURRENCE_ARRAY_MAX);
		break;
	case E_EWS_RECURRENCE_WEEKLY:
		usable = TRUE;
		i_cal_recurrence_set_freq (rrule, I_CAL_WEEKLY_RECURRENCE);
		i_cal_recurrence_set_interval (rrule, recur.recur.weekly.interval);
		e_ews_cal_utils_days_of_week_to_rrule (rrule, recur.recur.weekly.days_of_week);
		break;
	case E_EWS_RECURRENCE_DAILY:
		usable = TRUE;
		i_cal_recurrence_set_freq (rrule, I_CAL_DAILY_RECURRENCE);
		i_cal_recurrence_set_interval (rrule, recur.recur.interval);
		break;
	case E_EWS_RECURRENCE_DAILY_REGENERATION:
	case E_EWS_RECURRENCE_WEEKLY_REGENERATION:
	case E_EWS_RECURRENCE_MONTHLY_REGENERATION:
	case E_EWS_RECURRENCE_YEARLY_REGENERATION:
		e_ews_cal_util_encode_regeneration (&recur, comp);
		g_object_unref (recur_start);
		g_object_unref (rrule);
		return;
	}

	if (usable) {
		if (!i_cal_time_is_null_time (recur_start) && i_cal_time_is_valid_time (recur_start)) {
			ICalProperty *prop;

			i_cal_component_set_dtstart (comp, recur_start);

			prop = i_cal_property_new_rrule (rrule);
			i_cal_component_take_property (comp, prop);
		} else {
			g_warning ("%s: Cannot decode start date in received recurrence", G_STRFUNC);
		}
	} else {
		g_warning ("%s: Cannot decode received recurrence", G_STRFUNC);
	}

	g_object_unref (recur_start);
	g_object_unref (rrule);
}
