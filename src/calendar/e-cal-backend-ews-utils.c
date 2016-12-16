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

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libecal/libecal.h>
#include <libsoup/soup-misc.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-message.h"
#include "server/e-ews-item-change.h"

#include "e-cal-backend-ews-utils.h"

/*
 * A bunch of global variables used to map the icaltimezone to MSDN[0] format.
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

	g_return_val_if_fail (ical_tz_location != NULL, NULL);

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

	g_return_val_if_fail (msdn_tz_location != NULL, NULL);

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

	e_soap_message_start_element (msg, "MeetingTimeZone", NULL, NULL);
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

				snprintf (buffer, 256, "%d", recur.by_month_day[0] == -1 ? 31 : recur.by_month_day[0]);
				e_ews_message_write_string_parameter (msg, "DayOfMonth", NULL, buffer);

				e_soap_message_end_element (msg); /* "AbsoluteMonthlyRecurrence" */

			}
			break;

		case ICAL_YEARLY_RECURRENCE:
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
				if (recur.by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
					snprintf (buffer, 256, "%d", recur.by_month_day[0] == -1 ? 31 : recur.by_month_day[0]);
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
		return NULL;

	if (g_ascii_strncasecmp (org, "MAILTO:", 7) == 0)
		org = org + 7;

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
				    icalcomponent *icalcomp)
{
	icalproperty *prop;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (icalcomp != NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Normal");
		} else if (classify == ICAL_CLASS_PRIVATE) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Private");
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			e_ews_message_write_string_parameter (msg, "Sensitivity", NULL, "Personal");
		}
	}
}

static void
convert_categories_calcomp_to_xml (ESoapMessage *msg,
				   ECalComponent *comp,
				   icalcomponent *icalcomp)
{
	GSList *categ_list, *citer;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (icalcomp != NULL);

	if (comp) {
		g_object_ref (comp);
	} else {
		icalcomponent *clone = icalcomponent_new_clone (icalcomp);

		comp = e_cal_component_new ();
		if (!e_cal_component_set_icalcomponent (comp, clone)) {
			icalcomponent_free (clone);
			g_object_unref (comp);

			return;
		}
	}

	e_cal_component_get_categories_list (comp, &categ_list);

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

	e_cal_component_free_categories_list (categ_list);
}

static gboolean
check_is_all_day_event (const struct icaltimetype dtstart,
			icaltimezone *zone_start,
			const struct icaltimetype dtend,
			icaltimezone *zone_end)
{
	gint64 secs_start, secs_end;

	if (icaltime_is_date (dtstart) && icaltime_is_date (dtend))
		return TRUE;

	secs_start = (gint64) (zone_start ? icaltime_as_timet_with_zone (dtstart, zone_start) : icaltime_as_timet (dtstart));
	secs_end = (gint64) (zone_end ? icaltime_as_timet_with_zone (dtend, zone_end) : icaltime_as_timet (dtend));

	/* takes whole day(s) and starts on midnight in the zone_start */
	return ((secs_end - secs_start) % (24 * 60 * 60)) == 0 && (secs_start % 24 * 60 * 60) == 0;
}

static void
convert_vevent_calcomp_to_xml (ESoapMessage *msg,
                               gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = convert_data->icalcomp;
	ECalComponent *comp = e_cal_component_new ();
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend;
	icaltimezone *tzid_start, *tzid_end;
	icalproperty *prop;
	gboolean has_alarms, satisfies;
	const gchar *ical_location_start, *ical_location_end, *value;
	const gchar *msdn_location_start, *msdn_location_end;

	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* FORMAT OF A SAMPLE SOAP MESSAGE: http://msdn.microsoft.com/en-us/library/aa564690.aspx */

	/* Prepare CalendarItem node in the SOAP message */
	e_soap_message_start_element (msg, "CalendarItem", NULL, NULL);

	/* subject */
	value = icalcomponent_get_summary (icalcomp);
	if (value)
		e_ews_message_write_string_parameter (msg, "Subject", NULL, value);

	convert_sensitivity_calcomp_to_xml (msg, icalcomp);

	/* description */
	value = icalcomponent_get_description (icalcomp);
	if (value)
		e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, value, "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, comp, icalcomp);

	/* set alarms */
	has_alarms = e_cal_component_has_alarms (comp);
	if (has_alarms)
		ews_set_alarm (msg, comp);
	else
		e_ews_message_write_string_parameter (msg, "ReminderIsSet", NULL, "false");

	/* start time, end time and meeting time zone */
	dtstart = icalcomponent_get_dtstart (icalcomp);
	tzid_start = (icaltimezone *) (dtstart.zone ? dtstart.zone : convert_data->default_zone);
	ical_location_start = icaltimezone_get_location (tzid_start);

	dtend = icalcomponent_get_dtend (icalcomp);
	tzid_end = (icaltimezone *) (dtend.zone ? dtend.zone : convert_data->default_zone);
	ical_location_end = icaltimezone_get_location (tzid_end);

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

	ewscal_set_time (msg, "Start", &dtstart, FALSE);
	ewscal_set_time (msg, "End", &dtend, FALSE);
	/* We have to do the time zone(s) later, or the server rejects the request */

	/* All day event ? */
	if (check_is_all_day_event (dtstart, tzid_start, dtend, tzid_end))
		e_ews_message_write_string_parameter (msg, "IsAllDayEvent", NULL, "true");

	/*freebusy*/
	prop = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
	if (!g_strcmp0 (icalproperty_get_value_as_string (prop), "TRANSPARENT"))
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus",NULL,"Free");
	else
		e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus",NULL,"Busy");

	/* location */
	value = icalcomponent_get_location (icalcomp);
	if (value)
		e_ews_message_write_string_parameter (msg, "Location", NULL, value);

	/* collect attendees */
	e_ews_collect_attendees (icalcomp, &required, &optional, &resource);

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
	prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL) {
		ewscal_set_reccurence (msg, prop, &dtstart);
	}

	msdn_location_start = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_start);
	msdn_location_end = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_end);
	satisfies = e_ews_connection_satisfies_server_version (convert_data->connection, E_EWS_EXCHANGE_2010);

	/* We have to cast these because libical puts a const pointer into the
	 * icaltimetype, but its basic read-only icaltimezone_foo() functions
	 * take a non-const pointer! */
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
}

static void
convert_vtodo_calcomp_to_xml (ESoapMessage *msg,
                              gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = convert_data->icalcomp;
	icalproperty *prop;
	icaltimetype dt;
	gint value;
	gchar buffer[16];

	e_soap_message_start_element (msg, "Task", NULL, NULL);

	e_ews_message_write_string_parameter (msg, "Subject", NULL, icalcomponent_get_summary (icalcomp));

	convert_sensitivity_calcomp_to_xml (msg, icalcomp);

	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, icalcomponent_get_description (icalcomp), "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, NULL, icalcomp);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DUE_PROPERTY);
	if (prop) {
		dt = icalproperty_get_due (prop);
		ewscal_set_time (msg, "DueDate", &dt, TRUE);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = icalproperty_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_write_string_parameter (msg, "PercentComplete", NULL, buffer);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dt = icalproperty_get_dtstart (prop);
		ewscal_set_time (msg, "StartDate", &dt, TRUE);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_STATUS_PROPERTY);
	if (prop) {
		switch (icalproperty_get_status (prop)) {
		case ICAL_STATUS_INPROCESS:
			e_ews_message_write_string_parameter (msg, "Status", NULL, "InProgress");
			break;
		case ICAL_STATUS_COMPLETED:
			e_ews_message_write_string_parameter (msg, "Status", NULL, "Completed");
			break;
		default:
			break;
		}
	}

	e_soap_message_end_element (msg); /* "Task" */
}

static void
convert_vjournal_calcomp_to_xml (ESoapMessage *msg,
				 gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = convert_data->icalcomp;
	const gchar *text;

	e_soap_message_start_element (msg, "Message", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "ItemClass", NULL, "IPM.StickyNote");

	e_ews_message_write_string_parameter (msg, "Subject", NULL, icalcomponent_get_summary (icalcomp));

	convert_sensitivity_calcomp_to_xml (msg, icalcomp);

	text = icalcomponent_get_description (icalcomp);
	if (!text || !*text)
		text = icalcomponent_get_summary (icalcomp);
	e_ews_message_write_string_parameter_with_attribute (msg, "Body", NULL, text, "BodyType", "Text");

	convert_categories_calcomp_to_xml (msg, NULL, icalcomp);

	e_soap_message_end_element (msg); /* Message */
}

void
e_cal_backend_ews_convert_calcomp_to_xml (ESoapMessage *msg,
					  gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;

	switch (icalcomponent_isa (convert_data->icalcomp)) {
	case ICAL_VEVENT_COMPONENT:
		convert_vevent_calcomp_to_xml (msg, convert_data);
		break;
	case ICAL_VTODO_COMPONENT:
		convert_vtodo_calcomp_to_xml (msg, convert_data);
		break;
	case ICAL_VJOURNAL_COMPONENT:
		convert_vjournal_calcomp_to_xml (msg, convert_data);
		break;
	default:
		g_warn_if_reached ();
		break;
	}
}

static void
convert_component_categories_to_updatexml (ECalComponent *comp,
					   ESoapMessage *msg,
					   const gchar *base_elem_name)
{
	GSList *categ_list = NULL, *citer;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (msg != NULL);
	g_return_if_fail (base_elem_name != NULL);

	e_cal_component_get_categories_list (comp, &categ_list);

	/* Categories cannot be empty, thus first verify they are not */

	for (citer = categ_list; citer; citer = g_slist_next (citer)) {
		const gchar *category = citer->data;

		if (category && *category)
			break;
	}

	if (citer) {
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
	} else {
		e_ews_message_add_delete_item_field (msg, "Categories", "item");
	}

	e_cal_component_free_categories_list (categ_list);
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

static void
convert_vevent_component_to_updatexml (ESoapMessage *msg,
                                       gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);
	icalcomponent *icalcomp_old = e_cal_component_get_icalcomponent (convert_data->old_comp);
	GSList *required = NULL, *optional = NULL, *resource = NULL;
	icaltimetype dtstart, dtend, dtstart_old, dtend_old;
	icaltimezone *tzid_start = NULL, *tzid_end = NULL;
	icalproperty *prop, *transp;
	const gchar *org_email_address = NULL, *value = NULL, *old_value = NULL;
	const gchar *ical_location_start = NULL, *ical_location_end = NULL;
	const gchar *old_ical_location_start = NULL, *old_ical_location_end = NULL;
	const gchar *old_msdn_location_start = NULL, *old_msdn_location_end = NULL;
	const gchar *msdn_location_start = NULL, *msdn_location_end = NULL;
	gboolean has_alarms, has_alarms_old;
	gboolean dt_start_changed = FALSE, dt_end_changed = FALSE, dt_changed;
	gboolean dt_start_changed_timezone_name = FALSE, dt_end_changed_timezone_name = FALSE;
	gboolean satisfies;
	gint alarm = 0, alarm_old = 0;
	gchar *recid;
	GError *error = NULL;

	/* Modifying a recurring meeting ? */
	if (icalcomponent_get_first_property (icalcomp_old, ICAL_RRULE_PROPERTY) != NULL) {
		/* A single occurrence ? */
		prop = icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY);
		if (prop != NULL) {
			recid = icalproperty_get_value_as_string_r (prop);
			e_ews_message_start_item_change (
				msg,
				E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM,
				convert_data->item_id,
				convert_data->change_key,
				e_cal_backend_ews_rid_to_index (
					convert_data->default_zone,
					recid,
					icalcomp_old,
					&error));
			g_free (recid);
		} else {
			e_ews_message_start_item_change (
				msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
				convert_data->item_id, convert_data->change_key, 0);
		}
	} else e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	/* subject */
	value = icalcomponent_get_summary (icalcomp);
	old_value = icalcomponent_get_summary (icalcomp_old);
	if ((value && old_value && g_ascii_strcasecmp (value, old_value)) ||
	 (value && old_value == NULL)) {
		convert_vevent_property_to_updatexml (msg, "Subject", value, "item", NULL, NULL);
	} else if (!value && old_value)
		convert_vevent_property_to_updatexml (msg, "Subject", "", "item", NULL, NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_PRIVATE) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			convert_vevent_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
	}

	/*description*/
	value = icalcomponent_get_description (icalcomp);
	old_value = icalcomponent_get_description (icalcomp_old);
	if ((value && old_value && g_ascii_strcasecmp (value, old_value)) ||
	 (value && old_value == NULL)) {
		convert_vevent_property_to_updatexml (msg, "Body", value, "item", "BodyType", "Text");
	} else if (!value && old_value)
		convert_vevent_property_to_updatexml (msg, "Body", "", "item", "BodyType", "Text");

	/*update alarm items*/
	has_alarms = e_cal_component_has_alarms (convert_data->comp);
	if (has_alarms) {
		alarm = ews_get_alarm (convert_data->comp);
		has_alarms_old = e_cal_component_has_alarms (convert_data->old_comp);
		if (has_alarms_old)
			alarm_old = ews_get_alarm (convert_data->old_comp);
		if (!(alarm == alarm_old)) {
			gchar buf[20];
			snprintf (buf, 20, "%d", alarm);
			convert_vevent_property_to_updatexml (msg, "ReminderIsSet", "true", "item", NULL, NULL);
			convert_vevent_property_to_updatexml (msg, "ReminderMinutesBeforeStart", buf, "item", NULL, NULL);
		}
	}
	else convert_vevent_property_to_updatexml (msg, "ReminderIsSet", "false", "item", NULL, NULL);

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "CalendarItem");

	/*location*/
	value = icalcomponent_get_location (icalcomp);
	old_value = icalcomponent_get_location (icalcomp_old);
	if ((value && old_value && g_ascii_strcasecmp (value, old_value)) ||
	 (value && old_value == NULL)) {
		convert_vevent_property_to_updatexml (msg, "Location", value, "calendar", NULL, NULL);
	} else if (!value && old_value)
		convert_vevent_property_to_updatexml (msg, "Location", "", "calendar", NULL, NULL);

	/*freebusy*/
	transp = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
	value = icalproperty_get_value_as_string (transp);
	transp = icalcomponent_get_first_property (icalcomp_old, ICAL_TRANSP_PROPERTY);
	old_value = icalproperty_get_value_as_string (transp);
	if (g_strcmp0 (value, old_value)) {
		if (!g_strcmp0 (value, "TRANSPARENT"))
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Free" , "calendar", NULL, NULL);
		else
			convert_vevent_property_to_updatexml (msg, "LegacyFreeBusyStatus","Busy" , "calendar", NULL, NULL);
	}

	org_email_address = e_ews_collect_organizer (icalcomp);
	if (org_email_address && convert_data->user_email && g_ascii_strcasecmp (org_email_address, convert_data->user_email)) {
		e_ews_message_end_item_change (msg);
		return;
	}
	/* Update other properties allowed only for meeting organizers*/
	/*meeting dates*/
	dtstart = icalcomponent_get_dtstart (icalcomp);
	dtstart_old = icalcomponent_get_dtstart (icalcomp_old);
	dt_start_changed = icaltime_compare (dtstart, dtstart_old) != 0;
	if (dtstart.zone != NULL) {
		tzid_start = (icaltimezone *) dtstart.zone;
		ical_location_start = icaltimezone_get_location (tzid_start);

		old_ical_location_start = icaltimezone_get_location ((icaltimezone *)dtstart_old.zone);
		if (g_strcmp0 (ical_location_start, old_ical_location_start) != 0)
			dt_start_changed_timezone_name = TRUE;
	}

	dtend = icalcomponent_get_dtend (icalcomp);
	dtend_old = icalcomponent_get_dtend (icalcomp_old);
	dt_end_changed = icaltime_compare (dtend, dtend_old) != 0;
	if (dtend.zone != NULL) {
		tzid_end = (icaltimezone *) dtend.zone;
		ical_location_end = icaltimezone_get_location (tzid_end);

		old_ical_location_end = icaltimezone_get_location ((icaltimezone *)dtend_old.zone);
		if (g_strcmp0 (ical_location_end, old_ical_location_end) != 0)
			dt_end_changed_timezone_name = TRUE;
	}

	satisfies = e_ews_connection_satisfies_server_version (convert_data->connection, E_EWS_EXCHANGE_2010);

	if (satisfies) {
		if (old_ical_location_start != NULL) {
			old_msdn_location_start = e_cal_backend_ews_tz_util_get_msdn_equivalent (old_ical_location_start);
			msdn_location_start = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_start);

			if (g_strcmp0 (old_msdn_location_start, msdn_location_start) != 0)
				dt_start_changed = TRUE;
		}

		if (old_ical_location_end != NULL) {
			old_msdn_location_end = e_cal_backend_ews_tz_util_get_msdn_equivalent (old_ical_location_end);
			msdn_location_end = e_cal_backend_ews_tz_util_get_msdn_equivalent (ical_location_end);

			if (g_strcmp0 (old_msdn_location_end, msdn_location_end) != 0)
				dt_end_changed = TRUE;
		}

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

	if (dt_start_changed) {
		e_ews_message_start_set_item_field (msg, "Start", "calendar","CalendarItem");
		ewscal_set_time (msg, "Start", &dtstart, FALSE);
		e_ews_message_end_set_item_field (msg);
	}

	if (dt_end_changed) {
		e_ews_message_start_set_item_field (msg, "End", "calendar", "CalendarItem");
		ewscal_set_time (msg, "End", &dtend, FALSE);
		e_ews_message_end_set_item_field (msg);
	}

	dt_changed = dt_start_changed || dt_end_changed;

	/*Check for All Day Event*/
	if (dt_changed) {
		if (check_is_all_day_event (dtstart, tzid_start, dtend, tzid_end))
			convert_vevent_property_to_updatexml (msg, "IsAllDayEvent", "true", "calendar", NULL, NULL);
		else
			convert_vevent_property_to_updatexml (msg, "IsAllDayEvent", "false", "calendar", NULL, NULL);
	}

	e_ews_collect_attendees (icalcomp, &required, &optional, &resource);
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
	value = NULL; old_value = NULL;
	prop = icalcomponent_get_first_property (icalcomp_old, ICAL_RRULE_PROPERTY);
	if (prop != NULL)
		old_value = icalproperty_get_value_as_string (prop);
	prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
	if (prop != NULL)
		value = icalproperty_get_value_as_string (prop);

	if (prop != NULL && g_strcmp0 (value, old_value)) {
		e_ews_message_start_set_item_field (msg, "Recurrence", "calendar", "CalendarItem");
		ewscal_set_reccurence (msg, prop, &dtstart);
		e_ews_message_end_set_item_field (msg);
	}

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

static void
convert_vtodo_component_to_updatexml (ESoapMessage *msg,
                                      gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);
	icalproperty *prop;
	icaltimetype dt;
	gint value;
	gchar buffer[16];

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	convert_vtodo_property_to_updatexml (msg, "Subject", icalcomponent_get_summary (icalcomp), "item", NULL, NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_PRIVATE) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			convert_vtodo_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
	}

	convert_vtodo_property_to_updatexml (msg, "Body", icalcomponent_get_description (icalcomp), "item", "BodyType", "Text");

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DUE_PROPERTY);
	if (prop) {
		dt = icalproperty_get_due (prop);
		e_ews_message_start_set_item_field (msg, "DueDate", "task", "Task");
		ewscal_set_time (msg, "DueDate", &dt, TRUE);
		e_ews_message_end_set_item_field (msg);
	} else {
		e_ews_message_add_delete_item_field (msg, "DueDate", "task");
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_PERCENTCOMPLETE_PROPERTY);
	if (prop) {
		value = icalproperty_get_percentcomplete (prop);
		snprintf (buffer, 16, "%d", value);
		e_ews_message_start_set_item_field (msg, "PercentComplete", "task", "Task");
		e_ews_message_write_string_parameter (msg, "PercentComplete", NULL, buffer);
		e_ews_message_end_set_item_field (msg);
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_DTSTART_PROPERTY);
	if (prop) {
		dt = icalproperty_get_dtstart (prop);
		e_ews_message_start_set_item_field (msg, "StartDate", "task", "Task");
		ewscal_set_time (msg, "StartDate", &dt, TRUE);
		e_ews_message_end_set_item_field (msg);
	} else {
		e_ews_message_add_delete_item_field (msg, "StartDate", "task");
	}

	prop = icalcomponent_get_first_property (icalcomp, ICAL_STATUS_PROPERTY);
	if (prop) {
		switch (icalproperty_get_status (prop)) {
		case ICAL_STATUS_INPROCESS:
			convert_vtodo_property_to_updatexml (msg, "Status", "InProgress", "task", NULL, NULL);
			break;
		case ICAL_STATUS_COMPLETED:
			convert_vtodo_property_to_updatexml (msg, "Status", "Completed", "task", NULL, NULL);
			break;
		case ICAL_STATUS_NONE:
		case ICAL_STATUS_NEEDSACTION:
			convert_vtodo_property_to_updatexml (msg, "Status", "NotStarted", "task", NULL, NULL);
			break;
		default:
			break;
		}
	}

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "Task");

	e_ews_message_end_item_change (msg);
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

static void
convert_vjournal_component_to_updatexml (ESoapMessage *msg,
					 gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);
	icalproperty *prop;
	const gchar *text;

	e_ews_message_start_item_change (
		msg, E_EWS_ITEMCHANGE_TYPE_ITEM,
		convert_data->item_id, convert_data->change_key, 0);

	convert_vjournal_property_to_updatexml (msg, "ItemClass", "IPM.StickyNote", "item", NULL, NULL);
	convert_vjournal_property_to_updatexml (msg, "Subject", icalcomponent_get_summary (icalcomp), "item", NULL, NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (prop) {
		icalproperty_class classify = icalproperty_get_class (prop);
		if (classify == ICAL_CLASS_PUBLIC) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Normal", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_PRIVATE) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Private", "item", NULL, NULL);
		} else if (classify == ICAL_CLASS_CONFIDENTIAL) {
			convert_vjournal_property_to_updatexml (msg, "Sensitivity", "Personal", "item", NULL, NULL);
		}
	}

	text = icalcomponent_get_description (icalcomp);
	if (!text || !*text)
		text = icalcomponent_get_summary (icalcomp);

	convert_vjournal_property_to_updatexml (msg, "Body", text, "item", "BodyType", "Text");

	/* Categories */
	convert_component_categories_to_updatexml (convert_data->comp, msg, "Message");

	e_ews_message_end_item_change (msg);
}

void
e_cal_backend_ews_convert_component_to_updatexml (ESoapMessage *msg,
						  gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	icalcomponent *icalcomp = e_cal_component_get_icalcomponent (convert_data->comp);

	switch (icalcomponent_isa (icalcomp)) {
	case ICAL_VEVENT_COMPONENT:
		convert_vevent_component_to_updatexml (msg, user_data);
		break;
	case ICAL_VTODO_COMPONENT:
		convert_vtodo_component_to_updatexml (msg, user_data);
		break;
	case ICAL_VJOURNAL_COMPONENT:
		convert_vjournal_component_to_updatexml (msg, user_data);
		break;
	default:
		break;
	}
}

guint
e_cal_backend_ews_rid_to_index (icaltimezone *timezone,
				const gchar *rid,
				icalcomponent *comp,
				GError **error)
{
	guint index = 1;
	icalproperty *prop = icalcomponent_get_first_property (comp, ICAL_RRULE_PROPERTY);
	struct icalrecurrencetype rule = icalproperty_get_rrule (prop);
	struct icaltimetype dtstart = icalcomponent_get_dtstart (comp);
	icalrecur_iterator * ritr;
	icaltimetype next, o_time;

	/* icalcomponent_get_datetime needs a fix to initialize ret.zone to NULL. If a timezone is not
	 * found in libical, it remains uninitialized in that function causing invalid read or crash. so
	 * we set the timezone as we cannot identify if it has a valid timezone or not */
	dtstart.zone = timezone;
	ritr = icalrecur_iterator_new (rule, dtstart);
	next = icalrecur_iterator_next (ritr);
	o_time = icaltime_from_string (rid);
	o_time.zone = dtstart.zone;

	for (; !icaltime_is_null_time (next); next = icalrecur_iterator_next (ritr), index++) {
		if (icaltime_compare_date_only (o_time, next) == 0)
			break;
	}

	icalrecur_iterator_free (ritr);

	if (icaltime_is_null_time (next)) {
		g_propagate_error (
			error, EDC_ERROR_EX (OtherError,
			"Invalid occurrence ID"));
	}

	return index;
}

void
e_cal_backend_ews_clear_reminder_is_set (ESoapMessage *msg,
					 gpointer user_data)
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
}

void
e_cal_backend_ews_prepare_free_busy_request (ESoapMessage *msg,
					     gpointer user_data)
{
	EwsCalendarConvertData *convert_data = user_data;
	GSList *addr;
	icaltimetype t_start, t_end;
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();

	ewscal_set_availability_timezone (msg, utc_zone);

	e_soap_message_start_element (msg, "MailboxDataArray", "messages", NULL);

	for (addr = convert_data->users; addr; addr = addr->next) {
		e_soap_message_start_element (msg, "MailboxData", NULL, NULL);

		e_soap_message_start_element (msg, "Email", NULL, NULL);
		e_ews_message_write_string_parameter (msg, "Address", NULL, addr->data);
		e_soap_message_end_element (msg); /* "Email" */

		e_ews_message_write_string_parameter (msg, "AttendeeType", NULL, "Required");
		e_ews_message_write_string_parameter (msg, "ExcludeConflicts", NULL, "false");

		e_soap_message_end_element (msg); /* "MailboxData" */
	}

	e_soap_message_end_element (msg); /* "MailboxDataArray" */

	e_soap_message_start_element (msg, "FreeBusyViewOptions", NULL, NULL);

	e_soap_message_start_element (msg, "TimeWindow", NULL, NULL);
	t_start = icaltime_from_timet_with_zone (convert_data->start, 0, utc_zone);
	t_end = icaltime_from_timet_with_zone (convert_data->end, 0, utc_zone);
	ewscal_set_time (msg, "StartTime", &t_start, FALSE);
	ewscal_set_time (msg, "EndTime", &t_end, FALSE);
	e_soap_message_end_element (msg); /* "TimeWindow" */

	e_ews_message_write_string_parameter (msg, "MergedFreeBusyIntervalInMinutes", NULL, "60");
	e_ews_message_write_string_parameter (msg, "RequestedView", NULL, "DetailedMerged");

	e_soap_message_end_element (msg); /* "FreeBusyViewOptions" */
}

void
e_cal_backend_ews_prepare_set_free_busy_status (ESoapMessage *msg,
						gpointer user_data)
{
	EwsCalendarConvertData *data = user_data;

	e_ews_message_start_item_change (msg, E_EWS_ITEMCHANGE_TYPE_ITEM, data->item_id, data->change_key, 0);

	e_ews_message_start_set_item_field (msg, "LegacyFreeBusyStatus", "calendar", "CalendarItem");

	e_ews_message_write_string_parameter (msg, "LegacyFreeBusyStatus", NULL, "Free");

	e_ews_message_end_set_item_field (msg);

	e_ews_message_end_item_change (msg);
}

void
e_cal_backend_ews_prepare_accept_item_request (ESoapMessage *msg,
					       gpointer user_data)
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
}
