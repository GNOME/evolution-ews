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

void e_ews_set_start_time_as_utc(ESoapMessage *msg, icalcomponent *icalcomp) {
	time_t t = icaltime_as_timet_with_zone(icalcomponent_get_dtstart(icalcomp), icaltimezone_get_utc_timezone());
	struct tm * timeinfo;
	char buff[30];

	timeinfo = gmtime(&t);
	strftime(buff, 30, "%Y-%m-%dT%H:%M:%SZ", timeinfo);

	/* The start time soap element */
	e_ews_message_write_string_parameter(msg, "Start", NULL, buff);
}

void e_ews_set_end_time_as_utc(ESoapMessage *msg, icalcomponent *icalcomp) {
	time_t t = icaltime_as_timet_with_zone(icalcomponent_get_dtend(icalcomp), icaltimezone_get_utc_timezone());
	struct tm * timeinfo;
	char buff[30];

	timeinfo = gmtime(&t);
	strftime(buff, 30, "%Y-%m-%dT%H:%M:%SZ", timeinfo);

	/* The start time soap element */
	e_ews_message_write_string_parameter(msg, "Start", NULL, buff);
}
