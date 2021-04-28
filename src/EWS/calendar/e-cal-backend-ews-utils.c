/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

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

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libecal/libecal.h>
#include <libsoup/soup-misc.h>

#include "common/e-ews-calendar-utils.h"
#include "common/e-ews-connection.h"
#include "common/e-ews-message.h"
#include "common/e-ews-item-change.h"

#include "e-cal-backend-ews-utils.h"

/*
 * A bunch of global variables used to map the ICalTimezone to MSDN[0] format.
 * Also, some auxiliar functions to translate from one tz type to another.
 *
 * [0]: http://msdn.microsoft.com/en-us/library/ms912391(v=winembedded.11).aspx
 */
static GRecMutex tz_mutex;

static GHashTable *ical_to_msdn = NULL;
static GHashTable *msdn_to_ical = NULL;
static guint tables_counter = 0;

void
e_cal_backend_ews_populate_windows_zones (void)
{
	const gchar *xpath_eval_exp;
	gchar *filename = NULL;
	xmlDocPtr doc;
	xmlXPathContextPtr xpath_ctxt;
	xmlXPathObjectPtr xpath_obj;
	xmlNodeSetPtr nodes;
	gint i, len;

	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn != NULL && msdn_to_ical != NULL) {
		g_hash_table_ref (ical_to_msdn);
		g_hash_table_ref (msdn_to_ical);
		tables_counter++;

		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	if (g_strcmp0 (g_getenv ("EWS_INTERNAL_TEST"), "1") == 0)
		filename = g_build_filename (EXCHANGE_EWS_SRCDIR, "windowsZones.xml", NULL);
	else
		filename = g_build_filename (EXCHANGE_EWS_DATADIR, "windowsZones.xml", NULL);

	doc = xmlReadFile (filename, NULL, 0);

	if (doc == NULL) {
		g_warning (G_STRLOC "Could not map %s file.", filename);
		g_free (filename);

		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	xpath_eval_exp = "/supplementalData/windowsZones/mapTimezones/mapZone";

	xpath_ctxt = xmlXPathNewContext (doc);
	xpath_obj = xmlXPathEvalExpression (BAD_CAST xpath_eval_exp, xpath_ctxt);

	if (xpath_obj == NULL) {
		g_warning (G_STRLOC "Unable to evaluate xpath expression \"%s\".", xpath_eval_exp);
		xmlXPathFreeContext (xpath_ctxt);
		xmlFreeDoc (doc);
		g_free (filename);

		g_rec_mutex_unlock (&tz_mutex);
		return;
	}

	nodes = xpath_obj->nodesetval;
	len = nodes->nodeNr;

	msdn_to_ical = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	ical_to_msdn = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	tables_counter++;

	for (i = 0; i < len; i++) {
		xmlChar *msdn = xmlGetProp (nodes->nodeTab[i], BAD_CAST "other");
		xmlChar *ical = xmlGetProp (nodes->nodeTab[i], BAD_CAST "type");
		gchar **tokens;
		gint tokens_len;

		tokens = g_strsplit ((gchar *) ical, " ", 0);
		tokens_len = g_strv_length (tokens);
		if (tokens_len == 1) {
			if (!g_hash_table_lookup (msdn_to_ical, msdn))
				g_hash_table_insert (msdn_to_ical, g_strdup ((gchar *) msdn), g_strdup ((gchar *) ical));

			if (!g_hash_table_lookup (ical_to_msdn, ical))
				g_hash_table_insert (ical_to_msdn, g_strdup ((gchar *) ical), g_strdup ((gchar *) msdn));
		} else {
			gint j;
			for (j = 0; j < tokens_len; j++) {
				if (!g_hash_table_lookup (msdn_to_ical, msdn))
					g_hash_table_insert (msdn_to_ical, g_strdup ((gchar *) msdn), g_strdup (tokens[j]));

				if (!g_hash_table_lookup (ical_to_msdn, tokens[j]))
					g_hash_table_insert (ical_to_msdn, g_strdup (tokens[j]), g_strdup ((gchar *) msdn));
			}
		}

		g_strfreev (tokens);
		xmlFree (ical);
		xmlFree (msdn);
	}

	xmlXPathFreeObject (xpath_obj);
	xmlXPathFreeContext (xpath_ctxt);
	xmlFreeDoc (doc);
	g_free (filename);

	g_rec_mutex_unlock (&tz_mutex);
}

void
e_cal_backend_ews_unref_windows_zones (void)
{
	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn != NULL)
		g_hash_table_unref (ical_to_msdn);

	if (msdn_to_ical != NULL)
		g_hash_table_unref (msdn_to_ical);

	if (tables_counter > 0) {
		tables_counter--;

		if (tables_counter == 0) {
			ical_to_msdn = NULL;
			msdn_to_ical = NULL;
		}
	}

	g_rec_mutex_unlock (&tz_mutex);
}

const gchar *
e_cal_backend_ews_tz_util_get_msdn_equivalent (const gchar *ical_tz_location)
{
	const gchar *msdn_tz_location = NULL;

	if (!ical_tz_location || !*ical_tz_location)
		return NULL;

	g_rec_mutex_lock (&tz_mutex);
	if (ical_to_msdn == NULL) {
		g_rec_mutex_unlock (&tz_mutex);

		g_warn_if_reached ();
		return NULL;
	}

	msdn_tz_location = g_hash_table_lookup (ical_to_msdn, ical_tz_location);
	g_rec_mutex_unlock (&tz_mutex);

	return msdn_tz_location;
}

const gchar *
e_cal_backend_ews_tz_util_get_ical_equivalent (const gchar *msdn_tz_location)
{
	const gchar *ical_tz_location = NULL;

	if (!msdn_tz_location || !*msdn_tz_location)
		return NULL;

	g_rec_mutex_lock (&tz_mutex);
	if (msdn_to_ical == NULL) {
		g_rec_mutex_unlock (&tz_mutex);

		g_warn_if_reached ();
		return NULL;
	}

	ical_tz_location = g_hash_table_lookup (msdn_to_ical, msdn_tz_location);
	g_rec_mutex_unlock (&tz_mutex);

	return ical_tz_location;
}

/*
 * Iterate over the ICalComponent properties and collect attendees
 */
void
e_ews_collect_attendees (ICalComponent *comp,
                         GSList **required,
                         GSList **optional,
                         GSList **resource,
			 gboolean *out_rsvp_requested)
{
	ICalProperty *prop;
	ICalParameter *param;
	const gchar *str = NULL;
	const gchar *org_email_address = NULL;

	if (out_rsvp_requested)
		*out_rsvp_requested = TRUE;

	/* we need to know who the orgenizer is so we wont duplicate him/her */
	org_email_address = e_ews_collect_organizer (comp);

	/* iterate over every attendee property */
	for (prop = i_cal_component_get_first_property (comp, I_CAL_ATTENDEE_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, I_CAL_ATTENDEE_PROPERTY)) {

		str = i_cal_property_get_attendee (prop);

		if (!str || !*str)
			continue;

		/* figure the email address of the attendee, discard "mailto:" if it's there */
		if (!g_ascii_strncasecmp (str, "mailto:", 7))
			str = (str) + 7;

		if (!*str)
			continue;

		/* if this attenddee is the orgenizer - dont add him/her
		 in some cases there is no maito for email if meeting orginazer */
		if (g_ascii_strcasecmp (org_email_address, str) == 0)
			continue;

		/* figure type of attendee, add to relevant list */
		param = i_cal_property_get_first_parameter (prop, I_CAL_ROLE_PARAMETER);

		/*in case of new time proposal the role parameter is not a part of ical*/
		if (!param)
			continue;

		switch (i_cal_parameter_get_role (param)) {
		case I_CAL_ROLE_OPTPARTICIPANT:
			*optional = g_slist_append (*optional, (gpointer)str);

			if (out_rsvp_requested && *out_rsvp_requested) {
				ICalParameter *rsvp;

				rsvp = i_cal_property_get_first_parameter (prop, I_CAL_RSVP_PARAMETER);
				if (rsvp && i_cal_parameter_get_rsvp (rsvp) == I_CAL_RSVP_FALSE)
					*out_rsvp_requested = FALSE;
				g_clear_object (&rsvp);
			}
			break;
		case I_CAL_ROLE_CHAIR:
		case I_CAL_ROLE_REQPARTICIPANT:
			*required = g_slist_append (*required, (gpointer)str);

			if (out_rsvp_requested && *out_rsvp_requested) {
				ICalParameter *rsvp;

				rsvp = i_cal_property_get_first_parameter (prop, I_CAL_RSVP_PARAMETER);
				if (rsvp && i_cal_parameter_get_rsvp (rsvp) == I_CAL_RSVP_FALSE)
					*out_rsvp_requested = FALSE;
				g_clear_object (&rsvp);
			}
			break;
		case I_CAL_ROLE_NONPARTICIPANT:
			*resource = g_slist_append (*resource, (gpointer)str);
			break;
		case I_CAL_ROLE_X:
		case I_CAL_ROLE_NONE:
			/* Ignore these for now */
			break;
		}

		g_object_unref (param);
	}
}

gint
ews_get_alarm (ECalComponent *comp)
{
	GSList *alarm_uids;
	ECalComponentAlarm *alarm;
	ECalComponentAlarmAction action;
	gint dur_int = 0;

	alarm_uids = e_cal_component_get_alarm_uids (comp);
	if (!alarm_uids)
		return dur_int;

	alarm = e_cal_component_get_alarm (comp, (const gchar *) (alarm_uids->data));

	action = e_cal_component_alarm_get_action (alarm);
	if (action == E_CAL_COMPONENT_ALARM_DISPLAY) {
		ECalComponentAlarmTrigger *trigger;

		trigger = e_cal_component_alarm_get_trigger (alarm);
		if (trigger && e_cal_component_alarm_trigger_get_kind (trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START) {
			dur_int = (i_cal_duration_as_int (e_cal_component_alarm_trigger_get_duration (trigger)) / SECS_IN_MINUTE) * -1;
		}
	}
	e_cal_component_alarm_free (alarm);
	g_slist_free_full (alarm_uids, g_free);

	return dur_int;
}

void
ews_set_alarm (ESoapMessage *msg,
               ECalComponent *comp,
	       ETimezoneCache *timezone_cache,
	       ICalComponent *vcalendar,
	       gboolean with_due_by)
{
	/* We know there would be only a single alarm in EWS calendar item */
	GSList *alarm_uids;
	ECalComponentAlarm *alarm;
	ECalComponentAlarmAction action;

	alarm_uids = e_cal_component_get_alarm_uids (comp);
	if (!alarm_uids)
		return;

	alarm = e_cal_component_get_alarm (comp, (const gchar *) (alarm_uids->data));

	e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "true");
	action = e_cal_component_alarm_get_action (alarm);
	if (action == E_CAL_COMPONENT_ALARM_DISPLAY) {
		ECalComponentAlarmTrigger *trigger;
		gint dur_int = 0;

		trigger = e_cal_component_alarm_get_trigger (alarm);
		if (trigger && e_cal_component_alarm_trigger_get_kind (trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START) {
			dur_int = (i_cal_duration_as_int (e_cal_component_alarm_trigger_get_duration (trigger)) / SECS_IN_MINUTE) * -1;
			e_ews_message_write_int_parameter (msg, "ReminderMinutesBeforeStart", NULL, dur_int);
			if (with_due_by) {
				ICalTime *dtstart;

				dtstart = e_cal_backend_ews_get_datetime_with_zone (timezone_cache, vcalendar, e_cal_component_get_icalcomponent (comp),
					I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);

				if (dtstart && !i_cal_time_is_null_time (dtstart)) {
					e_ews_message_write_time_parameter (msg, "ReminderDueBy", NULL,
						i_cal_time_as_timet_with_zone (dtstart, i_cal_timezone_get_utc_timezone ()));
				}

				g_clear_object (&dtstart);
			}
		}
	}
	e_cal_component_alarm_free (alarm);
	g_slist_free_full (alarm_uids, g_free);
}

static void
ewscal_set_date (ESoapMessage *msg,
                 const gchar *name,
                 ICalTime *itt)
{
	gchar *str;

	str = g_strdup_printf (
		"%04d-%02d-%02d",
		i_cal_time_get_year (itt), i_cal_time_get_month (itt), i_cal_time_get_day (itt));

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
		  ICalProperty *prop)
{
	ICalRecurrence *recur = i_cal_property_get_rrule (prop);

	e_soap_message_start_element (msg, "RelativeYearlyRecurrence", NULL, NULL);

	e_ews_message_write_string_parameter (msg, "DaysOfWeek", NULL, number_to_weekday (i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (recur, 0))));
	e_ews_message_write_string_parameter (msg, "DayOfWeekIndex", NULL, weekindex_to_ical (i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (recur, 0))));
	e_ews_message_write_string_parameter (msg, "Month", NULL, number_to_month (i_cal_recurrence_get_by_month (recur, 0)));

	e_soap_message_end_element (msg); /* "RelativeYearlyRecurrence" */
	g_clear_object (&recur);
}

static void
ewscal_add_timechange (ESoapMessage *msg,
		       ICalComponent *comp,
		       gint baseoffs)
{
	gchar buffer[16], *offset;
	const gchar *tzname;
	ICalProperty *prop;
	ICalTime *dtstart;
	gint utcoffs;

	prop = i_cal_component_get_first_property (comp, I_CAL_TZNAME_PROPERTY);
	if (prop) {
		tzname = i_cal_property_get_tzname (prop);
		e_soap_message_add_attribute (msg, "TimeZoneName", tzname, NULL, NULL);
		g_object_unref (prop);
	}

	/* Calculate zone Offset from BaseOffset */
	prop = i_cal_component_get_first_property (comp, I_CAL_TZOFFSETTO_PROPERTY);
	if (prop) {
		ICalDuration *duration;

		utcoffs = -i_cal_property_get_tzoffsetto (prop);
		utcoffs -= baseoffs;
		duration = i_cal_duration_new_from_int (utcoffs);
		offset = i_cal_duration_as_ical_string (duration);
		e_ews_message_write_string_parameter (msg, "Offset", NULL, offset);

		g_clear_object (&duration);
		g_object_unref (prop);
		free (offset);
	}

	prop = i_cal_component_get_first_property (comp, I_CAL_RRULE_PROPERTY);
	if (prop) {
		ewscal_add_rrule (msg, prop);
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (comp, I_CAL_DTSTART_PROPERTY);
	if (prop) {
		dtstart = i_cal_property_get_dtstart (prop);
		snprintf (buffer, 16, "%02d:%02d:%02d", i_cal_time_get_hour (dtstart), i_cal_time_get_minute (dtstart), i_cal_time_get_second (dtstart));
		e_ews_message_write_string_parameter (msg, "Time", NULL, buffer);
		g_clear_object (&dtstart);
		g_object_unref (prop);
	}
}

static void
ewscal_set_absolute_date_transitions (ESoapMessage *msg,
				      GSList *absolute_date_transitions)
{
	GSList *l;

	if (absolute_date_transitions == NULL)
		return;

	for (l = absolute_date_transitions; l != NULL; l = l->next) {
		EEwsCalendarAbsoluteDateTransition *adt = l->data;

		e_soap_message_start_element (msg, "AbsoluteDateTransition", NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"To", NULL, adt->to->value,
			"Kind", adt->to->kind);
		e_ews_message_write_string_parameter (msg, "DateTime", NULL, adt->date_time);

		e_soap_message_end_element (msg); /* "AbsoluteDateTransition" */
	}
}

static void
ewscal_set_recurring_day_transitions (ESoapMessage *msg,
				      GSList *recurring_day_transitions)
{
	GSList *l;

	if (recurring_day_transitions == NULL)
		return;

	for (l = recurring_day_transitions; l != NULL; l = l->next) {
		EEwsCalendarRecurringDayTransition *rdt = l->data;

		e_soap_message_start_element (msg, "RecurringDayTransition", NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"To", NULL, rdt->to->value,
			"Kind", rdt->to->kind);
		e_ews_message_write_string_parameter (msg, "TimeOffset", NULL, rdt->time_offset);
		e_ews_message_write_string_parameter (msg, "Month", NULL, rdt->month);
		e_ews_message_write_string_parameter (msg, "DayOfWeek", NULL, rdt->day_of_week);
		e_ews_message_write_string_parameter (msg, "Occurrence", NULL, rdt->occurrence);

		e_soap_message_end_element (msg); /* "RecurringDayTransition" */
	}
}

static void
ewscal_set_recurring_date_transitions (ESoapMessage *msg,
				       GSList *recurring_date_transitions)
{
	GSList *l;

	if (recurring_date_transitions == NULL)
		return;

	for (l = recurring_date_transitions; l != NULL; l = l->next) {
		EEwsCalendarRecurringDateTransition *rdt = l->data;

		e_soap_message_start_element (msg, "RecurringDateTransition", NULL, NULL);

		e_ews_message_write_string_parameter_with_attribute (
			msg,
			"To", NULL, rdt->to->value,
			"Kind", rdt->to->kind);
		e_ews_message_write_string_parameter (msg, "TimeOffset", NULL, rdt->time_offset);
		e_ews_message_write_string_parameter (msg, "Month", NULL, rdt->month);
		e_ews_message_write_string_parameter (msg, "Day", NULL, rdt->day);

		e_soap_message_end_element (msg); /* "RecurringDateTransition" */
	}
}

void
ewscal_set_timezone (ESoapMessage *msg,
		     const gchar *name,
		     EEwsCalendarTimeZoneDefinition *tzd)
{
	GSList *l;

	if (name == NULL || tzd == NULL)
		return;

	e_soap_message_start_element (msg, name, NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", tzd->id, NULL, NULL);
	e_soap_message_add_attribute (msg, "Name", tzd->name, NULL, NULL);

	e_soap_message_start_element (msg, "Periods", NULL, NULL);
	for (l = tzd->periods; l != NULL; l = l->next) {
		EEwsCalendarPeriod *period = l->data;

		e_soap_message_start_element (msg, "Period", NULL, NULL);
		e_soap_message_add_attribute (msg, "Bias", period->bias, NULL, NULL);
		e_soap_message_add_attribute (msg, "Name", period->name, NULL, NULL);
		e_soap_message_add_attribute (msg, "Id", period->id, NULL, NULL);
		e_soap_message_end_element (msg); /* "Period" */
	}
	e_soap_message_end_element (msg); /* "Periods" */

	e_soap_message_start_element (msg, "TransitionsGroups", NULL, NULL);
	for (l = tzd->transitions_groups; l != NULL; l = l->next) {
		EEwsCalendarTransitionsGroup *tg = l->data;

		e_soap_message_start_element (msg, "TransitionsGroup", NULL, NULL);
		e_soap_message_add_attribute (msg, "Id", tg->id, NULL, NULL);

		if (tg->transition != NULL) {
			e_soap_message_start_element (msg, "Transition", NULL, NULL);
			e_ews_message_write_string_parameter_with_attribute (
				msg,
				"To", NULL, tg->transition->value,
				"Kind", tg->transition->kind);
			e_soap_message_end_element (msg); /* "Transition" */
		}

		ewscal_set_absolute_date_transitions (msg, tg->absolute_date_transitions);
		ewscal_set_recurring_day_transitions (msg, tg->recurring_day_transitions);
		ewscal_set_recurring_date_transitions (msg, tg->recurring_date_transitions);

		e_soap_message_end_element (msg); /* "TransitionsGroup" */
	}
	e_soap_message_end_element (msg); /* "TransitionsGroups" */

	e_soap_message_start_element (msg, "Transitions", NULL, NULL);
	e_soap_message_start_element (msg, "Transition", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (
		msg,
		"To", NULL, tzd->transitions->transition->value,
		"Kind", tzd->transitions->transition->kind);
	e_soap_message_end_element (msg); /* "Transition" */
	ewscal_set_absolute_date_transitions (msg, tzd->transitions->absolute_date_transitions);
	ewscal_set_recurring_day_transitions (msg, tzd->transitions->recurring_day_transitions);
	ewscal_set_recurring_date_transitions (msg, tzd->transitions->recurring_date_transitions);
	e_soap_message_end_element (msg); /* "Transitions" */

	e_soap_message_end_element (msg); /* "StartTimeZone" */
}

void
ewscal_set_meeting_timezone (ESoapMessage *msg,
			     ICalTimezone *icaltz)
{
	ICalComponent *comp;
	ICalComponent *xstd, *xdaylight;
	ICalDuration *duration;
	const gchar *location;
	gint std_utcoffs;
	gchar *offset;

	if (!icaltz)
		return;

	comp = i_cal_timezone_get_component (icaltz);
	if (!comp)
		return;

	/* Exchange needs a BaseOffset, followed by either *both*
	 * Standard and Daylight zones, or neither of them. If there's
	 * more than one STANDARD or DAYLIGHT component in the VTIMEZONE,
	 * we ignore the extra. So fully-specified timezones including
	 * historical DST rules cannot be handled by Exchange. */

	/* FIXME: Walk through them all to find the *latest* ones, like
	 * i_cal_timezone_get_tznames_from_vtimezone() does. */
	xstd = i_cal_component_get_first_component (comp, I_CAL_XSTANDARD_COMPONENT);
	xdaylight = i_cal_component_get_first_component (comp, I_CAL_XDAYLIGHT_COMPONENT);

	/* If there was only a DAYLIGHT component, swap them over and pretend
	 * it was the STANDARD component. We're only going to give the server
	 * the BaseOffset anyway. */
	if (!xstd) {
		xstd = xdaylight;
		xdaylight = NULL;
	}

	/* Find a suitable string to use for the TimeZoneName */
	location = i_cal_timezone_get_location (icaltz);
	if (!location)
		location = i_cal_timezone_get_tzid (icaltz);
	if (!location)
		location = i_cal_timezone_get_tznames (icaltz);

	e_soap_message_start_element (msg, "MeetingTimeZone", NULL, NULL);
	e_soap_message_add_attribute (msg, "TimeZoneName", location, NULL, NULL);

	/* Fetch the timezone offsets for the standard (or only) zone.
	 * Negate it, because Exchange does it backwards */
	if (xstd) {
		ICalProperty *prop;

		prop = i_cal_component_get_first_property (xstd, I_CAL_TZOFFSETTO_PROPERTY);
		std_utcoffs = -i_cal_property_get_tzoffsetto (prop);
		g_object_unref (prop);
	} else {
		/* UTC has no properties at all, so just set manually */
		std_utcoffs = 0;
	}

	/* This is the overall BaseOffset tag, which the Standard and Daylight
	 * zones are offset from. It's redundant, but Exchange always sets it
	 * to the offset of the Standard zone, and the Offset in the Standard
	 * zone to zero. So try to avoid problems by doing the same. */
	duration = i_cal_duration_new_from_int (std_utcoffs);
	offset = i_cal_duration_as_ical_string (duration);
	e_ews_message_write_string_parameter (msg, "BaseOffset", NULL, offset);
	g_clear_object (&duration);
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

	g_clear_object (&comp);
	g_clear_object (&xstd);
	g_clear_object (&xdaylight);
}

void
ewscal_set_reccurence (ESoapMessage *msg,
		       ICalProperty *rrule,
		       ICalTime *dtstart)
{
	gchar buffer[256];
	gint i, len;

	/* MSDN reference: http://msdn.microsoft.com/en-us/library/aa580471%28v=EXCHG.80%29.aspx
	 */
	ICalRecurrence *recur = i_cal_property_get_rrule (rrule);

	if (!recur)
		return;

	e_soap_message_start_element (msg, "Recurrence", NULL, NULL);

	switch (i_cal_recurrence_get_freq (recur)) {
		case I_CAL_DAILY_RECURRENCE:
			e_soap_message_start_element (msg, "DailyRecurrence", NULL, NULL);
			snprintf (buffer, 32, "%d", i_cal_recurrence_get_interval (recur));
			e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);
			e_soap_message_end_element (msg); /* "DailyRecurrence" */
			break;

		case I_CAL_WEEKLY_RECURRENCE:
			e_soap_message_start_element (msg, "WeeklyRecurrence", NULL, NULL);

			snprintf (buffer, 32, "%d", i_cal_recurrence_get_interval (recur));
			e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

			len = snprintf (
				buffer, 256, "%s",
				number_to_weekday (i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (recur, 0))));
			for (i = 1; i_cal_recurrence_get_by_day (recur, i) != I_CAL_RECURRENCE_ARRAY_MAX; i++) {
				len += snprintf (
					buffer + len, 256 - len, " %s",
					number_to_weekday (i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (recur, i))));
			}
			e_ews_message_write_string_parameter (msg, "DaysOfWeek", NULL, buffer);

			e_soap_message_end_element (msg); /* "WeeklyRecurrence" */
			break;

		case I_CAL_MONTHLY_RECURRENCE:
			if (i_cal_recurrence_get_by_month_day (recur, 0) == I_CAL_RECURRENCE_ARRAY_MAX) {
				e_soap_message_start_element (msg, "RelativeMonthlyRecurrence", NULL, NULL);

				/* For now this is what got implemented since this is the only
				 relative monthly recurrence evolution can set.
				 TODO: extend the code with all possible monthly recurrence settings */
				snprintf (buffer, 32, "%d", i_cal_recurrence_get_interval (recur));
				e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

				e_ews_message_write_string_parameter (
					msg, "DaysOfWeek", NULL,
					number_to_weekday (i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (recur, 0))));

				e_ews_message_write_string_parameter (msg, "DayOfWeekIndex", NULL, weekindex_to_ical (
					i_cal_recurrence_get_by_set_pos (recur, 0) == 5 ? -1 : i_cal_recurrence_get_by_set_pos (recur, 0)));

				e_soap_message_end_element (msg); /* "RelativeMonthlyRecurrence" */
			} else {
				e_soap_message_start_element (msg, "AbsoluteMonthlyRecurrence", NULL, NULL);

				snprintf (buffer, 256, "%d", i_cal_recurrence_get_interval (recur));
				e_ews_message_write_string_parameter (msg, "Interval", NULL, buffer);

				snprintf (buffer, 256, "%d", i_cal_recurrence_get_by_month_day (recur, 0) == -1 ? 31 : i_cal_recurrence_get_by_month_day (recur, 0));
				e_ews_message_write_string_parameter (msg, "DayOfMonth", NULL, buffer);

				e_soap_message_end_element (msg); /* "AbsoluteMonthlyRecurrence" */
			}
			break;

		case I_CAL_YEARLY_RECURRENCE:
			#if 0 /* FIXME */
			if (is_relative) {
				ewscal_add_rrule (msg, rrule);

			} else
			#endif
			{
				e_soap_message_start_element (msg, "AbsoluteYearlyRecurrence", NULL, NULL);

				/* work according to RFC5545 §3.3.10
				 * dtstart is the default, give preference to by_month & by_month_day if they are set
				 */
				if (i_cal_recurrence_get_by_month_day (recur, 0) != I_CAL_RECURRENCE_ARRAY_MAX) {
					snprintf (buffer, 256, "%d", i_cal_recurrence_get_by_month_day (recur, 0) == -1 ? 31 : i_cal_recurrence_get_by_month_day (recur, 0));
				} else {
					snprintf (buffer, 256, "%d", i_cal_time_get_day (dtstart));
				}
				e_ews_message_write_string_parameter (msg, "DayOfMonth", NULL, buffer);

				if (i_cal_recurrence_get_by_month (recur, 0) != I_CAL_RECURRENCE_ARRAY_MAX) {
					snprintf (buffer, 256, "%d", i_cal_recurrence_get_by_month_day (recur, 0));
					e_ews_message_write_string_parameter (
						msg, "Month", NULL,
						number_to_month (i_cal_recurrence_get_by_month (recur, 0)));
				} else {
					e_ews_message_write_string_parameter (
						msg, "Month", NULL,
						number_to_month (i_cal_time_get_month (dtstart)));
				}

				e_soap_message_end_element (msg); /* "AbsoluteYearlyRecurrence" */

			}
			break;

		case I_CAL_SECONDLY_RECURRENCE:
		case I_CAL_MINUTELY_RECURRENCE:
		case I_CAL_HOURLY_RECURRENCE:
		default:
			/* TODO: remove the "Recurrence" element somehow */
			g_warning ("EWS cant handle recurrence with frequency higher than DAILY\n");
			goto exit;
	}

	if (i_cal_recurrence_get_count (recur) > 0) {
		e_soap_message_start_element (msg, "NumberedRecurrence", NULL, NULL);
		ewscal_set_date (msg, "StartDate", dtstart);
		snprintf (buffer, 32, "%d", i_cal_recurrence_get_count (recur));
		e_ews_message_write_string_parameter (msg, "NumberOfOccurrences", NULL, buffer);
		e_soap_message_end_element (msg); /* "NumberedRecurrence" */
	} else {
		ICalTime *until;

		until = i_cal_recurrence_get_until (recur);

		if (until && !i_cal_time_is_null_time (until)) {
			e_soap_message_start_element (msg, "EndDateRecurrence", NULL, NULL);
			ewscal_set_date (msg, "StartDate", dtstart);
			ewscal_set_date (msg, "EndDate", until);
			e_soap_message_end_element (msg); /* "EndDateRecurrence" */
		} else {
			e_soap_message_start_element (msg, "NoEndRecurrence", NULL, NULL);
			ewscal_set_date (msg, "StartDate", dtstart);
			e_soap_message_end_element (msg); /* "NoEndRecurrence" */
		}

		g_clear_object (&until);
	}

exit:
	e_soap_message_end_element (msg); /* "Recurrence" */
	g_object_unref (recur);
}

static ICalTime *
icomponent_get_datetime (ICalComponent *comp,
			 ICalProperty *prop)
{
	/* Extract datetime with proper timezone */
	ICalParameter *param;
	ICalValue *value;
	ICalTime *ret;

	value = i_cal_property_get_value (prop);
	ret = i_cal_value_get_datetime (value);
	g_clear_object (&value);

	if ((param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER)) != NULL) {
		const gchar *tzid = i_cal_parameter_get_tzid (param);
		ICalTimezone *tz = NULL;
		ICalComponent *cc, *next_cc;

		for (cc = g_object_ref (comp);
		     cc;
		     g_object_unref (cc), cc = next_cc) {
			tz = i_cal_component_get_timezone (cc, tzid);
			if (tz) {
				g_object_unref (cc);
				break;
			}

			next_cc = i_cal_component_get_parent (cc);
		}

		if (!tz) {
			tz = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
			if (tz)
				g_object_ref (tz);
		}

		if (tz)
			i_cal_time_set_timezone (ret, tz);

		g_object_unref (param);
		g_clear_object (&tz);
	}

	return ret;
}

void
ewscal_set_reccurence_exceptions (ESoapMessage *msg,
				  ICalComponent *comp)
{
	ICalProperty *exdate;

	/* Make sure we have at least 1 excluded occurrence */
	exdate = i_cal_component_get_first_property (comp, I_CAL_EXDATE_PROPERTY);
	if (!exdate)
		return;

	e_soap_message_start_element (msg, "DeletedOccurrences", NULL, NULL);

	for (; exdate; g_object_unref (exdate), exdate = i_cal_component_get_next_property (comp, I_CAL_EXDATE_PROPERTY)) {
		ICalTime *exdatetime = icomponent_get_datetime (comp, exdate);

		e_soap_message_start_element (msg, "DeletedOccurrence", NULL, NULL);

		ewscal_set_date (msg, "Start", exdatetime);

		e_soap_message_end_element (msg); /* "DeletedOccurrence" */

		g_clear_object (&exdatetime);
	}

	e_soap_message_end_element (msg); /* "DeletedOccurrences" */
}

/*
 * get meeting organizer e-mail address
 */
const gchar *
e_ews_collect_organizer (ICalComponent *comp)
{
	ICalProperty *org_prop = NULL;
	const gchar *org = NULL;
	const gchar *org_email_address = NULL;

	org_prop = i_cal_component_get_first_property (comp, I_CAL_ORGANIZER_PROPERTY);
	if (!org_prop)
		return NULL;

	org = i_cal_property_get_organizer (org_prop);
	if (!org) {
		g_object_unref (org_prop);
		return NULL;
	}

	if (g_ascii_strncasecmp (org, "mailto:", 7) == 0)
		org = org + 7;

	org_email_address = org;

	if (org_email_address && !*org_email_address)
		org_email_address = NULL;

	g_object_unref (org_prop);

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
e_ews_clean_icomponent (ICalComponent *icomp)
{
	e_cal_util_component_remove_x_property (icomp, "X-EVOLUTION-ITEMID");
	e_cal_util_component_remove_x_property (icomp, "X-EVOLUTION-CHANGEKEY");
}

static void
add_attendees_list_to_message (ESoapMessage *msg,
                               const gchar *listname,
                               GSList *list)
{
	GSList *item;

	e_soap_message_start_element (msg, listname, NULL, NULL);

	for (item = list; item != NULL; item = item->next) {
		e_soap_message_start_element (msg, "Attendee", NULL, NULL);
		e_soap_message_start_element (msg, "Mailbox", NULL, NULL);

		e_ews_message_write_string_parameter (msg, "EmailAddress", NULL, item->data);

		e_soap_message_end_element (msg); /* "Mailbox" */
		e_soap_message_end_element (msg); /* "Attendee" */
	}

	e_soap_message_end_element (msg);
}

static void
convert_sensitivity_calcomp_to_xml (ESoapMessage *msg,
				    ICalComponent *icomp)
{
	ICalProperty *prop;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (icomp != NULL);

	prop = i_cal_component_get_first_property (icomp, I_CAL_CLASS_PROPERTY);
	if (prop) {
		ICalProperty_Class classify = i_cal_property_get_class (prop);
		if (classify == I_CAL_CLASS_PUBLIC) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Normal");
		} else if (classify == I_CAL_CLASS_PRIVATE) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Private");
		} else if (classify == I_CAL_CLASS_CONFIDENTIAL) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Personal");
		}
		g_object_unref (prop);
	}
}

static void
convert_categories_calcomp_to_xml (ESoapMessage *msg,
				   ECalComponent *comp,
				   ICalComponent *icomp)
{
	GSList *categ_list, *citer;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (icomp != NULL);

	if (comp) {
		g_object_ref (comp);
	} else {
		ICalComponent *clone = i_cal_component_clone (icomp);

		comp = e_cal_component_new_from_icalcomponent (clone);
		if (!comp)
			return;
	}

	categ_list = e_cal_component_get_categories_list (comp);

	g_object_unref (comp);

	if (!categ_list)
		return;

	/* Categories cannot be empty, thus first verify they are not */
	for (citer = categ_list; citer; citer = g_slist_next (citer)) {
		const gchar *category = citer->data;

		if (category && *category)
			break;
	}

	if (citer) {
		e_soap_message_start_element (msg, "Categories", NULL, NULL);

		for (citer = categ_list; citer; citer = g_slist_next (citer)) {
			const gchar *category = citer->data;

			if (!category || !*category)
				continue;

			e_ews_message_write_string_parameter (msg, "String", NULL, category);
		}

		e_soap_message_end_element (msg); /* Categories */
	}

	g_slist_free_full (categ_list, g_free);
}

static gboolean
check_is_all_day_event (const ICalTime *dtstart,
			ICalTimezone *zone_start,
			const ICalTime *dtend,
			ICalTimezone *zone_end)
{
	gint64 secs_start, secs_end;

	if (i_cal_time_is_date (dtstart) && i_cal_time_is_date (dtend))
		return TRUE;

	secs_start = (gint64) (zone_start ? i_cal_time_as_timet_with_zone (dtstart, zone_start) : i_cal_time_as_timet (dtstart));
	secs_end = (gint64) (zone_end ? i_cal_time_as_timet_with_zone (dtend, zone_end) : i_cal_time_as_timet (dtend));

	/* takes whole day(s) and starts on midnight in the zone_start */
	return ((secs_end - secs_start) > 0) && ((secs_end - secs_start) % (24 * 60 * 60)) == 0 && (secs_start % 24 * 60 * 60) == 0;
}

static gboolean
convert_vevent_calcomp_to_xml (ESoapMessage *msg,
                               gpointer user_data,
			       GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	ICalComponent *icomp = convert_data->icomp;
	ECalComponent *comp;
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	ICalTime *dtstart, *dtend;
	ICalTimezone *tzid_start, *tzid_end;
	ICalProperty *prop;
	gboolean has_alarms, satisfies, rsvp_requested = TRUE, is_all_day_event;
	const gchar *ical_location_start, *ical_location_end, *value;
	const gchar *msdn_location_start, *msdn_location_end;

	comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icomp));
	if (!comp)
		return FALSE;

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa564690.aspx */

	/* Prepare CalendarItem node in the SOAP message */
	e_soap_message_start_element (msg, "CalendarItem", NULL, NULL);

	/* subject */
	value = i_cal_component_get_summary (icomp);
	if (value)
		e_ews_message_write_string_parameter (msg, "Subject", NULL, value);

	convert_sensitivity_calcomp_to_xml (msg, icomp);

	/* description */
	value = i_cal_component_get_description (icomp);
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, value, "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, comp, icomp);

	/* set alarms */
	has_alarms = e_cal_component_has_alarms (comp);
	if (has_alarms)
		ews_set_alarm (msg, comp, convert_data->timezone_cache, convert_data->vcalendar, FALSE);
	else
		e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false");

	/* start time, end time and meeting time zone */
	dtstart = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);
	tzid_start = dtstart ? i_cal_time_get_timezone (dtstart) : NULL;
	if (!tzid_start)
		tzid_start = convert_data->default_zone;
	ical_location_start = i_cal_timezone_get_location (tzid_start);

	dtend = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DTEND_PROPERTY, i_cal_property_get_dtend);
	tzid_end = dtend ? i_cal_time_get_timezone (dtend) : NULL;
	if (!tzid_end)
		tzid_end = convert_data->default_zone;
	ical_location_end = i_cal_timezone_get_location (tzid_end);

	satisfies = e_ews_connection_satisfies_server_version (convert_data->connection, E_EWS_EXCHANGE_2010);
	if (satisfies && ical_location_start != NULL && ical_location_end != NULL) {
		/* set iana timezone info as an extended property */
		e_ews_message_add_extended_property_distinguished_name_string (
			msg,
			"PublicStrings",
			"EvolutionEWSStartTimeZone",
			ical_location_start);

		e_ews_message_add_extended_property_distinguished_name_string (
			msg,
			"PublicStrings",
			"EvolutionEWSEndTimeZone",
			ical_location_end);
	}

	is_all_day_event = check_is_all_day_event (dtstart, tzid_start, dtend, tzid_end);

	e_ews_cal_utils_set_time (msg, "Start", dtstart, is_all_day_event && i_cal_time_is_date (dtstart));

	/* Cover components without DTEND */
	if (dtend && i_cal_time_is_valid_time (dtend) &&
	    !i_cal_time_is_null_time (dtend))
		e_ews_cal_utils_set_time (msg, "End", dtend, is_all_day_event && i_cal_time_is_date (dtend));
	else
		e_ews_cal_utils_set_time (msg, "End", dtstart, is_all_day_event && i_cal_time_is_date (dtstart));

	/* We have to do the time zone(s) later, or the server rejects the request */

	/* All day event ? */
	if (is_all_day_event)
		e_ews_message_write_string_parameter (msg, "IsAllDayEvent", NULL, "true");

	/*freebusy*/
	prop = i_cal_component_get_first_property (icomp, I_CAL_TRANSP_PROPERTY);
	if (!prop || i_cal_property_get_transp (prop) == I_CAL_TRANSP_TRANSPARENT)
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus", NULL, "Free");
	else
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus", NULL, "Busy");
	g_clear_object (&prop);

	/* location */
	value = i_cal_component_get_location (icomp);
	if (value)
		e_ews_message_write_string_parameter (msg, "Location", NULL, value);

	/* collect attendees */
	e_ews_collect_attendees (icomp, &required, &optional, &resource, &rsvp_requested);

	e_ews_message_write_string_parameter (msg, "IsResponseRequested", NULL, rsvp_requested ? "true" : "false");

	if (required != NULL) {
		add_attendees_list_to_message (msg, "RequiredAttendees", required);
		g_slist_free (required);
	}
	if (optional != NULL) {
		add_attendees_list_to_message (msg, "OptionalAttendees", optional);
		g_slist_free (optional);
	}
	if (resource != NULL) {
		add_attendees_list_to_message (msg, "Resources", resource);
		g_slist_free (resource);
	}
	/* end of attendees */

	/* Recurrence */
	prop = i_cal_component_get_first_property (icomp, I_CAL_RRULE_PROPERTY);
	if (prop) {
		ewscal_set_reccurence (msg, prop, dtstart);
		g_object_unref (prop);
	}

	msdn_location_start = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_start);
	msdn_location_end = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_end);
	satisfies = e_ews_connection_satisfies_server_version (convert_data->connection, E_EWS_EXCHANGE_2010);

	if (satisfies && msdn_location_start != NULL && msdn_location_end != NULL) {
		GSList *msdn_locations = NULL;
		GSList *tzds = NULL;

		msdn_locations = g_slist_append (msdn_locations, (gchar *) msdn_location_start);
		msdn_locations = g_slist_append (msdn_locations, (gchar *) msdn_location_end);

		if (e_ews_connection_get_server_time_zones_sync (
				convert_data->connection,
				EWS_PRIORITY_MEDIUM,
				msdn_locations,
				&tzds,
				NULL,
				NULL)) {
			ewscal_set_timezone (msg, "StartTimeZone", tzds->data);
			ewscal_set_timezone (msg, "EndTimeZone", tzds->data);
		}

		g_slist_free (msdn_locations);
		g_slist_free_full (tzds, (GDestroyNotify) e_ews_calendar_time_zone_definition_free);
	} else {
		e_ews_message_replace_server_version (msg, E_EWS_EXCHANGE_2007_SP1);

		ewscal_set_meeting_timezone (msg, tzid_start);
	}

	e_soap_message_end_element (msg); /* "CalendarItem" */

	g_clear_object (&dtstart);
	g_clear_object (&dtend);
	g_object_unref (comp);

	return TRUE;
}

static const gchar *
ews_priority_to_string (gint priority)
{
	if (priority <= 3)
		return "High";

	if (priority >= 7)
		return "Low";

	return "Normal";
}

static gboolean
convert_vtodo_calcomp_to_xml (ESoapMessage *msg,
                              gpointer user_data,
			      GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	ICalComponent *icomp = convert_data->icomp;
	ICalProperty *prop;
	ICalTime *dt;
	gint value;
	gchar buffer[16];
	gboolean success;
	/* gboolean has_alarms; */

	e_soap_message_start_element (msg, "Task", NULL, NULL);

	e_ews_message_write_string_parameter (msg, "Subject", NULL, i_cal_component_get_summary (icomp));

	convert_sensitivity_calcomp_to_xml (msg, icomp);

	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, i_cal_component_get_description (icomp), "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, NULL, icomp);

	prop = i_cal_component_get_first_property (icomp, I_CAL_PRIORITY_PROPERTY);
	if (prop) {
		gint priority;

		priority = i_cal_property_get_priority (prop);
		e_ews_message_write_string_parameter (msg, "Importance", NULL, ews_priority_to_string (priority));
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_DUE_PROPERTY);
	if (prop) {
		dt = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DUE_PROPERTY, i_cal_property_get_due);
		e_ews_cal_utils_set_time (msg, "DueDate", dt, TRUE);
		g_clear_object (&dt);
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = i_cal_property_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_write_string_parameter (msg, "PercentComplete", NULL, buffer);
		g_object_unref (prop);
	}

	success = e_ews_cal_utils_set_recurrence (msg, icomp, FALSE, error);

	prop = i_cal_component_get_first_property (icomp, I_CAL_DTSTART_PROPERTY);
	if (prop) {
		dt = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);
		e_ews_cal_utils_set_time (msg, "StartDate", dt, TRUE);
		g_clear_object (&dt);
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_STATUS_PROPERTY);
	if (prop) {
		switch (i_cal_property_get_status (prop)) {
		case I_CAL_STATUS_INPROCESS:
			e_ews_message_write_string_parameter (msg, "Status", NULL, "InProgress");
			break;
		case I_CAL_STATUS_COMPLETED:
			e_ews_message_write_string_parameter (msg, "Status", NULL, "Completed");
			break;
		default:
			break;
		}
		g_object_unref (prop);
	}

	/* has_alarms = e_cal_util_component_has_property (icomp, I_CAL_VALARM_COMPONENT);
	if (has_alarms) {
		ECalComponent *comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icomp));

		if (comp && e_cal_component_has_alarms (comp)) {
			ews_set_alarm (msg, comp, convert_data->timezone_cache, convert_data->vcalendar, TRUE);
		} else {
			has_alarms = FALSE;
		}

		g_clear_object (&comp);
	}

	if (!has_alarms)
		e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false"); */

	e_soap_message_end_element (msg); /* "Task" */

	return success;
}

static gboolean
convert_vjournal_calcomp_to_xml (ESoapMessage *msg,
				 gpointer user_data,
				 GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	ICalComponent *icomp = convert_data->icomp;
	const gchar *text;

	e_soap_message_start_element (msg, "Message", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "ItemClass", NULL, "IPM.StickyNote");

	e_ews_message_write_string_parameter (msg, "Subject", NULL, i_cal_component_get_summary (icomp));

	convert_sensitivity_calcomp_to_xml (msg, icomp);

	text = i_cal_component_get_description (icomp);
	if (!text || !*text)
		text = i_cal_component_get_summary (icomp);
	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, text, "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, NULL, icomp);

	e_soap_message_end_element (msg); /* Message */

	return TRUE;
}

gboolean
e_cal_backend_ews_convert_calcomp_to_xml (ESoapMessage *msg,
					  gpointer user_data,
					  GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	gboolean success = FALSE;

	switch (i_cal_component_isa (convert_data->icomp)) {
	case I_CAL_VEVENT_COMPONENT:
		success = convert_vevent_calcomp_to_xml (msg, convert_data, error);
		break;
	case I_CAL_VTODO_COMPONENT:
		success = convert_vtodo_calcomp_to_xml (msg, convert_data, error);
		break;
	case I_CAL_VJOURNAL_COMPONENT:
		success = convert_vjournal_calcomp_to_xml (msg, convert_data, error);
		break;
	default:
		g_warn_if_reached ();
		break;
	}

	return success;
}

static void
convert_component_categories_to_updatexml (ECalComponent *comp,
					   ESoapMessage *msg,
					   const gchar *base_elem_name)
{
	GSList *categ_list, *citer;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (msg != NULL);
	g_return_if_fail (base_elem_name != NULL);

	categ_list = e_cal_component_get_categories_list (comp);

	e_ews_message_start_set_item_field (msg, "Categories", "item", base_elem_name);
	e_soap_message_start_element (msg, "Categories", NULL, NULL);

	for (citer = categ_list; citer; citer = g_slist_next (citer)) {
		const gchar *category = citer->data;

		if (!category || !*category)
			continue;

		e_ews_message_write_string_parameter (msg, "String", NULL, category);
	}

	e_soap_message_end_element (msg); /* Categories */
	e_ews_message_end_set_item_field (msg);

	g_slist_free_full (categ_list, g_free);
}

static void
convert_vevent_property_to_updatexml (ESoapMessage *msg,
                                      const gchar *name,
                                      const gchar *value,
                                      const gchar *prefix,
                                      const gchar *attr_name,
                                      const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "CalendarItem");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static gboolean
convert_vevent_component_to_updatexml (ESoapMessage *msg,
                                       gpointer user_data,
				       GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	ICalComponent *icomp = e_cal_component_get_icalcomponent (convert_data->comp);
	ICalComponent *icomp_old = e_cal_component_get_icalcomponent (convert_data->old_comp);
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	ICalTime *dtstart, *dtend, *dtstart_old, *dtend_old;
	ICalTimezone *tzid_start = NULL, *tzid_end = NULL;
	ICalProperty *prop;
	ICalPropertyTransp old_transp, new_transp;
	const gchar *org_email_address = NULL, *value = NULL, *old_value = NULL;
	const gchar *ical_location_start = NULL, *ical_location_end = NULL;
	const gchar *old_ical_location_start = NULL, *old_ical_location_end = NULL;
	const gchar *old_msdn_location_start = NULL, *old_msdn_location_end = NULL;
	const gchar *msdn_location_start = NULL, *msdn_location_end = NULL;
	gchar *rrule_value, *rrule_old_value;
	gboolean dt_start_changed = FALSE, dt_end_changed = FALSE, dt_changed;
	gboolean dt_start_changed_timezone_name = FALSE, dt_end_changed_timezone_name = FALSE;
	gboolean satisfies, rsvp_requested = TRUE, is_all_day_event = FALSE;
	gchar *recid;

	/* Modifying a recurring meeting ? */
	if (e_cal_util_component_has_property (icomp_old, I_CAL_RRULE_PROPERTY)) {
		/* A single occurrence ? */
		prop = i_cal_component_get_first_property (icomp, I_CAL_RECURRENCEID_PROPERTY);
		if (prop != NULL) {
			recid = i_cal_property_get_value_as_string (prop);
			e_ews_message_start_item_change (
				msg,
				E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM,
				convert_data->item_id,
				convert_data->change_key,
				e_cal_backend_ews_rid_to_index (
					convert_data->default_zone,
					recid,
					icomp_old,
					NULL));
			g_object_unref (prop);
			g_free (recid);
		} else {
			e_ews_message_start_item_change (
				msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
				convert_data->item_id, convert_data->change_key, 0);
		}
	} else {
		e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
			convert_data->item_id, convert_data->change_key, 0);
	}

	/* subject */
	value = i_cal_component_get_summary (icomp);
	old_value = i_cal_component_get_summary (icomp_old);
	if (g_strcmp0 (value, old_value) != 0 || (value && !old_value)) {
		convert_vevent_property_to_updatexml (msg, "Subject", value, "item", NULL, NULL);
	} else if (!value && old_value) {
		convert_vevent_property_to_updatexml (msg, "Subject", "", "item", NULL, NULL);
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_CLASS_PROPERTY);
	if (prop) {
		ICalProperty_Class classify = i_cal_property_get_class (prop);
		if (classify == I_CAL_CLASS_PUBLIC) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == I_CAL_CLASS_PRIVATE) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == I_CAL_CLASS_CONFIDENTIAL) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}

		g_object_unref (prop);
	}

	/*description*/
	value = i_cal_component_get_description (icomp);
	old_value = i_cal_component_get_description (icomp_old);
	if (g_strcmp0 (value, old_value) != 0 || (value && !old_value)) {
		convert_vevent_property_to_updatexml (msg, "Body", value, "item", "BodyType", "Text");
	} else if (!value && old_value) {
		convert_vevent_property_to_updatexml (msg, "Body", "", "item", "BodyType", "Text");
	}

	/*update alarm items*/
	if (e_cal_component_has_alarms (convert_data->comp)) {
		gint alarm, alarm_old = -1;
		gboolean has_alarms_old;

		alarm = ews_get_alarm (convert_data->comp);
		has_alarms_old = e_cal_component_has_alarms (convert_data->old_comp);
		if (has_alarms_old)
			alarm_old = ews_get_alarm (convert_data->old_comp);
		if (alarm != alarm_old || !has_alarms_old) {
			gchar buf[20];
			snprintf (buf, 20, "%d", alarm);
			convert_vevent_property_to_updatexml (msg, "ReminderIsSet", "true", "item", NULL, NULL);
			convert_vevent_property_to_updatexml (msg, "ReminderMinutesBeforeStart", buf, "item", NULL, NULL);
		}
	} else {
		convert_vevent_property_to_updatexml (msg, "ReminderIsSet", "false", "item", NULL, NULL);
	}

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "CalendarItem");

	/*location*/
	value = i_cal_component_get_location (icomp);
	old_value = i_cal_component_get_location (icomp_old);
	if (g_strcmp0 (value, old_value) != 0 || (value && !old_value)) {
		convert_vevent_property_to_updatexml (msg, "Location", value, "calendar", NULL, NULL);
	} else if (!value && old_value) {
		convert_vevent_property_to_updatexml (msg, "Location", "", "calendar", NULL, NULL);
	}

	/*freebusy*/
	prop = i_cal_component_get_first_property (icomp, I_CAL_TRANSP_PROPERTY);
	new_transp = prop ? i_cal_property_get_transp (prop) : I_CAL_TRANSP_NONE;
	g_clear_object (&prop);
	prop = i_cal_component_get_first_property (icomp_old, I_CAL_TRANSP_PROPERTY);
	old_transp = prop ? i_cal_property_get_transp (prop) : I_CAL_TRANSP_NONE;
	g_clear_object (&prop);
	if (new_transp != old_transp) {
		if (new_transp == I_CAL_TRANSP_TRANSPARENT)
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Free" , "calendar", NULL, NULL);
		else
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Busy" , "calendar", NULL, NULL);
	}

	org_email_address = e_ews_collect_organizer (icomp);
	if (org_email_address && convert_data->user_email && g_ascii_strcasecmp (org_email_address, convert_data->user_email)) {
		e_ews_message_end_item_change (msg);
		return TRUE;
	}

	/* Update other properties allowed only for meeting organizers*/
	/*meeting dates*/
	dtstart = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);
	dtstart_old = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp_old, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);
	dt_start_changed = ((!dtstart || !dtstart_old) && dtstart != dtstart_old) ||
		(dtstart && dtstart_old && i_cal_time_compare (dtstart, dtstart_old) != 0);
	tzid_start = dtstart ? i_cal_time_get_timezone (dtstart) : NULL;
	if (tzid_start) {
		ICalTimezone *zone;

		zone = dtstart_old ? i_cal_time_get_timezone (dtstart_old) : NULL;

		ical_location_start = i_cal_timezone_get_location (tzid_start);
		old_ical_location_start = zone ? i_cal_timezone_get_location (zone) : NULL;

		if (g_strcmp0 (ical_location_start, old_ical_location_start) != 0)
			dt_start_changed_timezone_name = TRUE;
	}

	dtend = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DTEND_PROPERTY, i_cal_property_get_dtend);
	dtend_old = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp_old, I_CAL_DTEND_PROPERTY, i_cal_property_get_dtend);
	dt_end_changed = ((!dtend || !dtend_old) && dtend != dtend_old) ||
		(dtend && dtend_old && i_cal_time_compare (dtend, dtend_old) != 0);
	tzid_end = dtend ? i_cal_time_get_timezone (dtend) : NULL;
	if (tzid_end) {
		ICalTimezone *zone;

		zone = dtend_old ? i_cal_time_get_timezone (dtend_old) : NULL;

		ical_location_end = i_cal_timezone_get_location (tzid_end);
		old_ical_location_end = zone ? i_cal_timezone_get_location (zone) : NULL;

		if (g_strcmp0 (ical_location_end, old_ical_location_end) != 0)
			dt_end_changed_timezone_name = TRUE;
	}

	satisfies = e_ews_connection_satisfies_server_version (convert_data->connection, E_EWS_EXCHANGE_2010);

	if (satisfies) {
		if (old_ical_location_start)
			old_msdn_location_start = e_cal_backend_ews_tz_util_get_msdn_equivalent (old_ical_location_start);

		if (ical_location_start)
			msdn_location_start = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_start);

		if (g_strcmp0 (old_msdn_location_start, msdn_location_start) != 0)
			dt_start_changed = TRUE;

		if (old_ical_location_end)
			old_msdn_location_end = e_cal_backend_ews_tz_util_get_msdn_equivalent (old_ical_location_end);

		if (ical_location_end)
			msdn_location_end = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_end);

		if (g_strcmp0 (old_msdn_location_end, msdn_location_end) != 0)
			dt_end_changed = TRUE;

		if ((dt_start_changed || dt_start_changed_timezone_name) && ical_location_start != NULL)
			e_ews_message_add_set_item_field_extended_distinguished_name_string (
				msg,
				NULL,
				"CalendarItem",
				"PublicStrings",
				"EvolutionEWSStartTimeZone",
				ical_location_start);

		if ((dt_end_changed || dt_end_changed_timezone_name) && ical_location_end != NULL)
			e_ews_message_add_set_item_field_extended_distinguished_name_string (
				msg,
				NULL,
				"CalendarItem",
				"PublicStrings",
				"EvolutionEWSEndTimeZone",
				ical_location_end);
	}

	dt_changed = dt_start_changed || dt_end_changed;

	if (dt_changed)
		is_all_day_event = check_is_all_day_event (dtstart, tzid_start, dtend, tzid_end);

	if (dt_start_changed) {
		e_ews_message_start_set_item_field (msg, "Start", "calendar","CalendarItem");
		e_ews_cal_utils_set_time (msg, "Start", dtstart, is_all_day_event && i_cal_time_is_date (dtstart));
		e_ews_message_end_set_item_field (msg);
	}

	if (dt_end_changed) {
		e_ews_message_start_set_item_field (msg, "End", "calendar", "CalendarItem");
		e_ews_cal_utils_set_time (msg, "End", dtend, is_all_day_event && i_cal_time_is_date (dtend));
		e_ews_message_end_set_item_field (msg);
	}

	/*Check for All Day Event*/
	if (dt_changed) {
		if (is_all_day_event)
			convert_vevent_property_to_updatexml (msg, "IsAllDayEvent", "true", "calendar", NULL, NULL);
		else
			convert_vevent_property_to_updatexml (msg, "IsAllDayEvent", "false", "calendar", NULL, NULL);
	}

	e_ews_collect_attendees (icomp, &required, &optional, &resource, &rsvp_requested);

	convert_vevent_property_to_updatexml (msg, "IsResponseRequested", rsvp_requested ? "true" : "false", "calendar", NULL, NULL);

	if (required != NULL) {
		e_ews_message_start_set_item_field (msg, "RequiredAttendees", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "RequiredAttendees", required);
		g_slist_free (required);

		e_ews_message_end_set_item_field (msg);
	}
	if (optional != NULL) {
		e_ews_message_start_set_item_field (msg, "OptionalAttendees", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "OptionalAttendees", optional);
		g_slist_free (optional);

		e_ews_message_end_set_item_field (msg);
	}
	if (resource != NULL) {
		e_ews_message_start_set_item_field (msg, "Resources", "calendar", "CalendarItem");

		add_attendees_list_to_message (msg, "Resources", resource);
		g_slist_free (resource);

		e_ews_message_end_set_item_field (msg);
	}

	/* Recurrence */
	rrule_value = NULL;
	rrule_old_value = NULL;
	prop = i_cal_component_get_first_property (icomp_old, I_CAL_RRULE_PROPERTY);
	if (prop) {
		rrule_old_value = i_cal_property_get_value_as_string (prop);
		g_object_unref (prop);
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_RRULE_PROPERTY);
	if (prop)
		rrule_value = i_cal_property_get_value_as_string (prop);

	if (prop && g_strcmp0 (rrule_value, rrule_old_value)) {
		e_ews_message_start_set_item_field (msg, "Recurrence", "calendar", "CalendarItem");
		ewscal_set_reccurence (msg, prop, dtstart);
		e_ews_message_end_set_item_field (msg);
	}
	g_clear_object (&prop);
	g_free (rrule_value);
	g_free (rrule_old_value);

	if (dt_changed && satisfies) {
		if (msdn_location_start != NULL || msdn_location_end != NULL) {
			GSList *msdn_locations = NULL;
			GSList *tzds = NULL;

			if (msdn_location_start != NULL)
				msdn_locations = g_slist_append (msdn_locations, (gchar *) msdn_location_start);

			if (msdn_location_end != NULL)
				msdn_locations = g_slist_append (msdn_locations, (gchar *) msdn_location_end);

			if (e_ews_connection_get_server_time_zones_sync (
				convert_data->connection,
				EWS_PRIORITY_MEDIUM,
				msdn_locations,
				&tzds,
				NULL,
				NULL)) {
				GSList *tmp;

				tmp = tzds;
				if (tzid_start != NULL) {
					e_ews_message_start_set_item_field (msg, "StartTimeZone", "calendar", "CalendarItem");
					ewscal_set_timezone (msg, "StartTimeZone", tmp->data);
					e_ews_message_end_set_item_field (msg);

					/*
					 * Exchange server is smart enough to return the list of
					 * ServerTimeZone without repeated elements
					 */
					if (tmp->next != NULL)
						tmp = tmp->next;
				}

				if (tzid_end != NULL) {
					e_ews_message_start_set_item_field (msg, "EndTimeZone", "calendar", "CalendarItem");
					ewscal_set_timezone (msg, "EndTimeZone", tmp->data);
					e_ews_message_end_set_item_field (msg);
				}
			}

			g_slist_free (msdn_locations);
			g_slist_free_full (tzds, (GDestroyNotify) e_ews_calendar_time_zone_definition_free);
		}
	} else if (dt_changed) {
		e_ews_message_replace_server_version (msg, E_EWS_EXCHANGE_2007_SP1);

		e_ews_message_start_set_item_field (msg, "MeetingTimeZone", "calendar", "CalendarItem");
		ewscal_set_meeting_timezone (msg, tzid_start ? tzid_start : convert_data->default_zone);
		e_ews_message_end_set_item_field (msg);
	}

	e_ews_message_end_item_change (msg);

	g_clear_object (&dtstart);
	g_clear_object (&dtend);
	g_clear_object (&dtstart_old);
	g_clear_object (&dtend_old);

	return TRUE;
}

static void
convert_vtodo_property_to_updatexml (ESoapMessage *msg,
                                     const gchar *name,
                                     const gchar *value,
                                     const gchar *prefix,
                                     const gchar *attr_name,
                                     const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "Task");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static gboolean
convert_vtodo_component_to_updatexml (ESoapMessage *msg,
                                      gpointer user_data,
				      GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	ICalComponent *icomp = e_cal_component_get_icalcomponent (convert_data->comp);
	ICalProperty *prop;
	ICalTime *dt;
	gint value;
	gchar buffer[16];
	gboolean success = TRUE;

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	convert_vtodo_property_to_updatexml (msg, "Subject", i_cal_component_get_summary (icomp), "item", NULL, NULL);

	prop = i_cal_component_get_first_property (icomp, I_CAL_CLASS_PROPERTY);
	if (prop) {
		ICalProperty_Class classify = i_cal_property_get_class (prop);
		if (classify == I_CAL_CLASS_PUBLIC) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == I_CAL_CLASS_PRIVATE) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == I_CAL_CLASS_CONFIDENTIAL) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
		g_object_unref (prop);
	}

	convert_vtodo_property_to_updatexml (msg, "Body", i_cal_component_get_description (icomp), "item", "BodyType", "Text");

	prop = i_cal_component_get_first_property (icomp, I_CAL_DUE_PROPERTY);
	if (prop) {
		dt = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DUE_PROPERTY, i_cal_property_get_due);
		e_ews_message_start_set_item_field (msg, "DueDate", "task", "Task");
		e_ews_cal_utils_set_time (msg, "DueDate", dt, TRUE);
		e_ews_message_end_set_item_field (msg);
		g_object_unref (prop);
		g_clear_object (&dt);
	} else {
		e_ews_message_add_delete_item_field (msg, "DueDate", "task");
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = i_cal_property_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_start_set_item_field (msg, "PercentComplete", "task", "Task");
		e_ews_message_write_string_parameter (msg, "PercentComplete", NULL, buffer);
		e_ews_message_end_set_item_field (msg);
		g_object_unref (prop);
	}

	/* Recurrence */
	value = i_cal_component_count_properties (e_cal_component_get_icalcomponent (convert_data->old_comp), I_CAL_RRULE_PROPERTY);
	if (i_cal_component_count_properties (icomp, I_CAL_RRULE_PROPERTY) > 0 ||
	    (e_cal_util_component_has_x_property (icomp, X_EWS_TASK_REGENERATION) && value <= 0)) {
		e_ews_message_start_set_item_field (msg, "Recurrence", "task", "Task");
		success = success && e_ews_cal_utils_set_recurrence (msg, icomp, FALSE, error);
		e_ews_message_end_set_item_field (msg); /* Recurrence */
	} else if (value > 0) {
		e_ews_message_add_delete_item_field (msg, "Recurrence", "task");
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_DTSTART_PROPERTY);
	if (prop) {
		dt = e_cal_backend_ews_get_datetime_with_zone (convert_data->timezone_cache, convert_data->vcalendar, icomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart);
		e_ews_message_start_set_item_field (msg, "StartDate", "task", "Task");
		e_ews_cal_utils_set_time (msg, "StartDate", dt, TRUE);
		e_ews_message_end_set_item_field (msg);
		g_object_unref (prop);
		g_clear_object (&dt);
	} else {
		e_ews_message_add_delete_item_field (msg, "StartDate", "task");
	}

	prop = i_cal_component_get_first_property (icomp, I_CAL_STATUS_PROPERTY);
	if (prop) {
		switch (i_cal_property_get_status (prop)) {
		case I_CAL_STATUS_INPROCESS:
			convert_vtodo_property_to_updatexml (msg, "Status", "InProgress", "task", NULL, NULL);
			break;
		case I_CAL_STATUS_COMPLETED:
			convert_vtodo_property_to_updatexml (msg, "Status", "Completed", "task", NULL, NULL);
			break;
		case I_CAL_STATUS_NONE:
		case I_CAL_STATUS_NEEDSACTION:
			convert_vtodo_property_to_updatexml (msg, "Status", "NotStarted", "task", NULL, NULL);
			break;
		default:
			break;
		}
		g_object_unref (prop);
	}

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "Task");

	prop = i_cal_component_get_first_property (icomp, I_CAL_PRIORITY_PROPERTY);
	if (prop) {
		gint priority;

		priority = i_cal_property_get_priority (prop);
		convert_vtodo_property_to_updatexml (msg, "Importance", ews_priority_to_string (priority), "item", NULL, NULL);
		g_object_unref (prop);
	}

	e_ews_message_end_item_change (msg);

	return success;
}

static void
convert_vjournal_property_to_updatexml (ESoapMessage *msg,
					const gchar *name,
					const gchar *value,
					const gchar *prefix,
					const gchar *attr_name,
					const gchar *attr_value)
{
	e_ews_message_start_set_item_field (msg, name, prefix, "Message");
	e_ews_message_write_string_parameter_with_attribute (msg, name, NULL, value, attr_name, attr_value);
	e_ews_message_end_set_item_field (msg);
}

static gboolean
convert_vjournal_component_to_updatexml (ESoapMessage *msg,
					 gpointer user_data,
					 GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	ICalComponent *icomp = e_cal_component_get_icalcomponent (convert_data->comp);
	ICalProperty *prop;
	const gchar *text;

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	convert_vjournal_property_to_updatexml (msg, "ItemClass", "IPM.StickyNote", "item", NULL, NULL);
	convert_vjournal_property_to_updatexml (msg, "Subject", i_cal_component_get_summary (icomp), "item", NULL, NULL);

	prop = i_cal_component_get_first_property (icomp, I_CAL_CLASS_PROPERTY);
	if (prop) {
		ICalProperty_Class classify = i_cal_property_get_class (prop);
		if (classify == I_CAL_CLASS_PUBLIC) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == I_CAL_CLASS_PRIVATE) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == I_CAL_CLASS_CONFIDENTIAL) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
		g_object_unref (prop);
	}

	text = i_cal_component_get_description (icomp);
	if (!text || !*text)
		text = i_cal_component_get_summary (icomp);

	convert_vjournal_property_to_updatexml (msg, "Body", text, "item", "BodyType", "Text");

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "Message");

	e_ews_message_end_item_change (msg);

	return TRUE;
}

gboolean
e_cal_backend_ews_convert_component_to_updatexml (ESoapMessage *msg,
						  gpointer user_data,
						  GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;
	ICalComponent *icomp = e_cal_component_get_icalcomponent (convert_data->comp);
	gboolean success = FALSE;

	switch (i_cal_component_isa (icomp)) {
	case I_CAL_VEVENT_COMPONENT:
		success = convert_vevent_component_to_updatexml (msg, user_data, error);
		break;
	case I_CAL_VTODO_COMPONENT:
		success = convert_vtodo_component_to_updatexml (msg, user_data, error);
		break;
	case I_CAL_VJOURNAL_COMPONENT:
		success = convert_vjournal_component_to_updatexml (msg, user_data, error);
		break;
	default:
		break;
	}

	return success;
}

guint
e_cal_backend_ews_rid_to_index (ICalTimezone *timezone,
				const gchar *rid,
				ICalComponent *comp,
				GError **error)
{
	guint index = 1;
	ICalProperty *prop;
	ICalRecurrence *rrule;
	ICalTime *dtstart;
	ICalRecurIterator *ritr;
	ICalTime *next, *o_time;

	prop = i_cal_component_get_first_property (comp, I_CAL_RRULE_PROPERTY);
	if (!prop)
		return index;

	rrule = i_cal_property_get_rrule (prop);
	if (!rrule) {
		g_object_unref (prop);
		return index;
	}

	dtstart = i_cal_component_get_dtstart (comp);
	if (!dtstart) {
		g_object_unref (prop);
		g_object_unref (rrule);
		return index;
	}

	/* icomponent_get_datetime needs a fix to initialize ret.zone to NULL. If a timezone is not
	 * found in libical, it remains uninitialized in that function causing invalid read or crash. so
	 * we set the timezone as we cannot identify if it has a valid timezone or not */
	i_cal_time_set_timezone (dtstart, timezone);

	o_time = i_cal_time_new_from_string (rid);
	i_cal_time_set_timezone (o_time, timezone);

	ritr = i_cal_recur_iterator_new (rrule, dtstart);

	for (next = i_cal_recur_iterator_next (ritr);
	     next && !i_cal_time_is_null_time (next);
	     g_object_unref (next), next = i_cal_recur_iterator_next (ritr), index++) {
		if (i_cal_time_compare_date_only (o_time, next) == 0) {
			break;
		}
	}

	if (!next || i_cal_time_is_null_time (next)) {
		g_propagate_error (error,
			e_client_error_create (E_CLIENT_ERROR_OTHER_ERROR, _("Invalid occurrence ID")));
		index = 0;
	}

	g_clear_object (&prop);
	g_clear_object (&rrule);
	g_clear_object (&dtstart);
	g_clear_object (&o_time);
	g_clear_object (&next);
	g_clear_object (&ritr);

	return index;
}

gboolean
e_cal_backend_ews_clear_reminder_is_set (ESoapMessage *msg,
					 gpointer user_data,
					 GError **error)
{
	EwsCalendarConvertData *convert_data = user_data;

	e_ews_message_start_item_change (
		msg,
		convert_data->change_type,
		convert_data->item_id,
		convert_data->change_key,
		convert_data->index);

	e_ews_message_start_set_item_field (msg, "ReminderIsSet","item", "CalendarItem");

	e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false");

	e_ews_message_end_set_item_field (msg);

	e_ews_message_end_item_change (msg);

	return TRUE;
}

gboolean
e_cal_backend_ews_prepare_set_free_busy_status (ESoapMessage *msg,
						gpointer user_data,
						GError **error)
{
	EwsCalendarConvertData *data = user_data;

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM, data->item_id, data->change_key, 0);

	e_ews_message_start_set_item_field (msg, "LegacyFreeBusyStatus", "calendar", "CalendarItem");

	e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus", NULL, "Free");

	e_ews_message_end_set_item_field (msg);

	e_ews_message_end_item_change (msg);

	return TRUE;
}

gboolean
e_cal_backend_ews_prepare_accept_item_request (ESoapMessage *msg,
					       gpointer user_data,
					       GError **error)
{
	EwsCalendarConvertData *data = user_data;
	const gchar *response_type = data->response_type;

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa566464%28v=exchg.140%29.aspx
	 * Accept and Decline meeting have same method code (10032)
	 * The real status is reflected at Attendee property PARTSTAT
	 * need to find current user as attendee and make a decision what to do.
	 * Prepare AcceptItem node in the SOAP message */

	if (response_type && !g_ascii_strcasecmp (response_type, "ACCEPTED"))
		e_soap_message_start_element (msg, "AcceptItem", NULL, NULL);
	else if (response_type && !g_ascii_strcasecmp (response_type, "DECLINED"))
		e_soap_message_start_element (msg, "DeclineItem", NULL, NULL);
	else
		e_soap_message_start_element (msg, "TentativelyAcceptItem", NULL, NULL);

	e_soap_message_start_element (msg, "ReferenceItemId", NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", data->item_id, NULL, NULL);
	e_soap_message_add_attribute (msg, "ChangeKey", data->change_key, NULL, NULL);
	e_soap_message_end_element (msg); /* "ReferenceItemId" */

	/* end of "AcceptItem" */
	e_soap_message_end_element (msg);

	return TRUE;
}

ICalTime *
e_cal_backend_ews_get_datetime_with_zone (ETimezoneCache *timezone_cache,
					  ICalComponent *vcalendar,
					  ICalComponent *comp,
					  ICalPropertyKind prop_kind,
					  ICalTime * (* get_func) (ICalProperty *prop))
{
	ICalTime *dt = NULL;
	ICalTimezone *zone;
	ICalProperty *prop;
	ICalParameter *param;
	const gchar *tzid, *eqv_tzid;

	g_return_val_if_fail (E_IS_TIMEZONE_CACHE (timezone_cache), dt);
	g_return_val_if_fail (comp != NULL, dt);
	g_return_val_if_fail (get_func != NULL, dt);

	prop = i_cal_component_get_first_property (comp, prop_kind);
	if (!prop)
		return dt;

	dt = get_func (prop);

	if (!dt || !i_cal_time_is_valid_time (dt) ||
	    i_cal_time_is_null_time (dt)) {
		g_clear_object (&dt);
		g_object_unref (prop);
		return dt;
	}

	i_cal_time_set_timezone (dt, NULL);

	param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
	if (!param) {
		g_object_unref (prop);
		return dt;
	}

	tzid = i_cal_parameter_get_tzid (param);
	if (!tzid || !*tzid) {
		g_object_unref (param);
		g_object_unref (prop);
		return dt;
	}

	eqv_tzid = e_cal_backend_ews_tz_util_get_ical_equivalent (tzid);

	if (!eqv_tzid) {
		/* Unlikely to work, but just in case */
		eqv_tzid = e_cal_backend_ews_tz_util_get_msdn_equivalent (tzid);
		if (eqv_tzid)
			eqv_tzid = e_cal_backend_ews_tz_util_get_ical_equivalent (eqv_tzid);
	}

	zone = NULL;

	if (eqv_tzid)
		zone = e_timezone_cache_get_timezone (timezone_cache, eqv_tzid);

	if (!zone)
		zone = e_timezone_cache_get_timezone (timezone_cache, tzid);

	if (!zone) {
		zone = vcalendar ? i_cal_component_get_timezone (vcalendar, tzid) : NULL;

		/* Workaround Teams bug - see https://gitlab.gnome.org/GNOME/evolution-ews/-/issues/151 */
		if (!zone && vcalendar && strchr (tzid, ' ')) {
			gchar *tmp;

			tmp = g_strstrip (g_strdup (tzid));

			if (tmp && *tmp)
				zone = i_cal_component_get_timezone (vcalendar, tmp);

			g_free (tmp);
		}
	} else {
		g_object_ref (zone);
	}

	i_cal_time_set_timezone (dt, zone);

	g_clear_object (&zone);
	g_object_unref (param);
	g_object_unref (prop);

	return dt;
}
