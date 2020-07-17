/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <stdio.h>
#include <json-glib/json-glib.h>

#include "e-o365-json-utils.h"

typedef struct _MapData {
	const gchar *json_value;
	gint enum_value;
} MapData;

static gint
o365_json_utils_json_value_as_enum (const gchar *json_value,
				    const MapData *items,
				    guint n_items,
				    gint not_set_value,
				    gint unknown_value)
{
	guint ii;

	if (!json_value)
		return not_set_value;

	for (ii = 0; ii < n_items; ii++) {
		if (items[ii].json_value && g_ascii_strcasecmp (items[ii].json_value, json_value) == 0)
			return items[ii].enum_value;
	}

	return unknown_value;
}

static gint
o365_json_utils_get_json_as_enum (JsonObject *object,
				  const gchar *string_member_name,
				  const MapData *items,
				  guint n_items,
				  gint not_set_value,
				  gint unknown_value)
{
	return o365_json_utils_json_value_as_enum (e_o365_json_get_string_member (object, string_member_name, NULL),
		items, n_items, not_set_value, unknown_value);
}

static void
o365_json_utils_add_enum_as_json (JsonBuilder *builder,
				  const gchar *string_member_name,
				  gint enum_value,
				  const MapData *items,
				  guint n_items,
				  gint not_set_value,
				  gint default_value)
{
	const gchar *json_value = NULL, *default_value_str = NULL;
	guint ii;

	if (enum_value == not_set_value)
		return;

	for (ii = 0; ii < n_items; ii++) {
		if (items[ii].enum_value == default_value) {
			default_value_str = items[ii].json_value;

			if (json_value)
				break;
		}

		if (items[ii].enum_value == enum_value) {
			json_value = items[ii].json_value;

			if (default_value_str)
				break;
		}
	}

	if (!json_value) {
		g_warning ("%s: Failed to find enum value %d for member '%s'", G_STRFUNC, enum_value, string_member_name);
		json_value = default_value_str;
	}

	if (json_value) {
		if (string_member_name)
			e_o365_json_add_string_member (builder, string_member_name, json_value);
		else
			json_builder_add_string_value (builder, json_value ? json_value : "");
	}
}

static MapData attachment_data_type_map[] = {
	{ "#microsoft.graph.fileAttachment",		E_O365_ATTACHMENT_DATA_TYPE_FILE },
	{ "#microsoft.graph.itemAttachment",		E_O365_ATTACHMENT_DATA_TYPE_ITEM },
	{ "#microsoft.graph.referenceAttachment",	E_O365_ATTACHMENT_DATA_TYPE_REFERENCE }
};

static MapData attendee_map[] = {
	{ "required", E_O365_ATTENDEE_REQUIRED },
	{ "optional", E_O365_ATTENDEE_OPTIONAL },
	{ "resource", E_O365_ATTENDEE_RESOURCE }
};

static struct _color_map {
	const gchar *name;
	const gchar *rgb;
	EO365CalendarColorType value;
} color_map[] = {
	{ "auto",	NULL,		E_O365_CALENDAR_COLOR_AUTO },
	{ "lightBlue",	"#0078d4",	E_O365_CALENDAR_COLOR_LIGHT_BLUE },
	{ "lightGreen",	"#b67dfa",	E_O365_CALENDAR_COLOR_LIGHT_GREEN },
	{ "lightOrange","#25c4fe",	E_O365_CALENDAR_COLOR_LIGHT_ORANGE },
	{ "lightGray",	"#968681",	E_O365_CALENDAR_COLOR_LIGHT_GRAY },
	{ "lightYellow","#ffc699",	E_O365_CALENDAR_COLOR_LIGHT_YELLOW }, /* Navy in web UI */
	{ "lightTeal",	"#fc7c78",	E_O365_CALENDAR_COLOR_LIGHT_TEAL },
	{ "lightPink",	"#1cff73",	E_O365_CALENDAR_COLOR_LIGHT_PINK },
	{ "lightBrown",	"#8bb256",	E_O365_CALENDAR_COLOR_LIGHT_BROWN }, /* Purple in web UI */
	{ "lightRed",	"#3af0e0",	E_O365_CALENDAR_COLOR_LIGHT_RED },
	{ "maxColor",	NULL,		E_O365_CALENDAR_COLOR_MAX_COLOR }
};

static MapData content_type_map[] = {
	{ "text", E_O365_ITEM_BODY_CONTENT_TYPE_TEXT },
	{ "html", E_O365_ITEM_BODY_CONTENT_TYPE_HTML }
};

static MapData day_of_week_map[] = {
	{ "sunday",	E_O365_DAY_OF_WEEK_SUNDAY },
	{ "monday",	E_O365_DAY_OF_WEEK_MONDAY },
	{ "tuesday",	E_O365_DAY_OF_WEEK_TUESDAY },
	{ "wednesday",	E_O365_DAY_OF_WEEK_WEDNESDAY },
	{ "thursday",	E_O365_DAY_OF_WEEK_THURSDAY },
	{ "friday",	E_O365_DAY_OF_WEEK_FRIDAY },
	{ "saturday",	E_O365_DAY_OF_WEEK_SATURDAY }
};

static MapData event_type_map[] = {
	{ "singleInstance",	E_O365_EVENT_TYPE_SINGLE_INSTANCE },
	{ "occurrence",		E_O365_EVENT_TYPE_OCCURRENCE },
	{ "exception",		E_O365_EVENT_TYPE_EXCEPTION },
	{ "seriesMaster",	E_O365_EVENT_TYPE_SERIES_MASTER }
};

static MapData flag_status_map[] = {
	{ "notFlagged",	E_O365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED },
	{ "complete",	E_O365_FOLLOWUP_FLAG_STATUS_COMPLETE },
	{ "flagged",	E_O365_FOLLOWUP_FLAG_STATUS_FLAGGED }
};

static MapData free_busy_status_map[] = {
	{ "unknown",		E_O365_FREE_BUSY_STATUS_UNKNOWN },
	{ "free",		E_O365_FREE_BUSY_STATUS_FREE },
	{ "tentative",		E_O365_FREE_BUSY_STATUS_TENTATIVE },
	{ "busy",		E_O365_FREE_BUSY_STATUS_BUSY },
	{ "oof",		E_O365_FREE_BUSY_STATUS_OOF },
	{ "workingElsewhere",	E_O365_FREE_BUSY_STATUS_WORKING_ELSEWHERE }
};

static MapData importance_map[] = {
	{ "low",	E_O365_IMPORTANCE_LOW },
	{ "normal",	E_O365_IMPORTANCE_NORMAL },
	{ "high",	E_O365_IMPORTANCE_HIGH }
};

static MapData inference_classification_map[] = {
	{ "focused",	E_O365_INFERENCE_CLASSIFICATION_FOCUSED },
	{ "other",	E_O365_INFERENCE_CLASSIFICATION_OTHER }
};

static MapData location_type_map[] = {
	{ "default",		E_O365_LOCATION_DEFAULT },
	{ "conferenceRoom",	E_O365_LOCATION_CONFERENCE_ROOM },
	{ "homeAddress",	E_O365_LOCATION_HOME_ADDRESS },
	{ "businessAddress",	E_O365_LOCATION_BUSINESS_ADDRESS },
	{ "geoCoordinates",	E_O365_LOCATION_GEO_COORDINATES },
	{ "streetAddress",	E_O365_LOCATION_STREET_ADDRESS },
	{ "hotel",		E_O365_LOCATION_HOTEL },
	{ "restaurant",		E_O365_LOCATION_RESTAURANT },
	{ "localBusiness",	E_O365_LOCATION_LOCAL_BUSINESS },
	{ "postalAddress",	E_O365_LOCATION_POSTAL_ADDRESS }
};

static MapData meeting_provider_map[] = {
	{ "unknown",		E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN },
	{ "skypeForBusiness",	E_O365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_BUSINESS },
	{ "skypeForConsumer",	E_O365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_CONSUMER },
	{ "teamsForBusiness",	E_O365_ONLINE_MEETING_PROVIDER_TEAMS_FOR_BUSINESS }
};

static MapData phone_map[] = {
	{ "home",	E_O365_PHONE_HOME },
	{ "business",	E_O365_PHONE_BUSINESS },
	{ "mobile",	E_O365_PHONE_MOBILE },
	{ "other",	E_O365_PHONE_OTHER },
	{ "assistant",	E_O365_PHONE_ASSISTANT },
	{ "homeFax",	E_O365_PHONE_HOMEFAX },
	{ "businessFax",E_O365_PHONE_BUSINESSFAX },
	{ "otherFax",	E_O365_PHONE_OTHERFAX },
	{ "pager",	E_O365_PHONE_PAGER },
	{ "radio",	E_O365_PHONE_RADIO }
};

static MapData recurrence_pattern_map[] = {
	{ "daily",		E_O365_RECURRENCE_PATTERN_DAILY },
	{ "weekly",		E_O365_RECURRENCE_PATTERN_WEEKLY },
	{ "absoluteMonthly",	E_O365_RECURRENCE_PATTERN_ABSOLUTE_MONTHLY },
	{ "relativeMonthly",	E_O365_RECURRENCE_PATTERN_RELATIVE_MONTHLY },
	{ "absoluteYearly",	E_O365_RECURRENCE_PATTERN_ABSOLUTE_YEARLY },
	{ "relativeYearly",	E_O365_RECURRENCE_PATTERN_RELATIVE_YEARLY }
};

static MapData recurrence_range_map[] = {
	{ "endDate",	E_O365_RECURRENCE_RANGE_ENDDATE },
	{ "noEnd",	E_O365_RECURRENCE_RANGE_NOEND },
	{ "numbered",	E_O365_RECURRENCE_RANGE_NUMBERED }
};

static MapData response_map[] = {
	{ "None",		E_O365_RESPONSE_NONE },
	{ "Organizer",		E_O365_RESPONSE_ORGANIZER },
	{ "TentativelyAccepted",E_O365_RESPONSE_TENTATIVELY_ACCEPTED },
	{ "Accepted",		E_O365_RESPONSE_ACCEPTED },
	{ "Declined",		E_O365_RESPONSE_DECLINED },
	{ "NotResponded",	E_O365_RESPONSE_NOT_RESPONDED }
};

static MapData sensitivity_map[] = {
	{ "normal",		E_O365_SENSITIVITY_NORMAL },
	{ "personal",		E_O365_SENSITIVITY_PERSONAL },
	{ "private",		E_O365_SENSITIVITY_PRIVATE },
	{ "confidential",	E_O365_SENSITIVITY_CONFIDENTIAL }

};

static MapData week_index_map[] = {
	{ "first",	E_O365_WEEK_INDEX_FIRST },
	{ "second",	E_O365_WEEK_INDEX_SECOND },
	{ "third",	E_O365_WEEK_INDEX_THIRD },
	{ "fourth",	E_O365_WEEK_INDEX_FOURTH },
	{ "last",	E_O365_WEEK_INDEX_LAST }
};

const gchar *
e_o365_calendar_color_to_rgb (EO365CalendarColorType color)
{
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (color == color_map[ii].value)
			return color_map[ii].rgb;
	}

	return NULL;
}

EO365CalendarColorType
e_o365_rgb_to_calendar_color (const gchar *rgb)
{
	EO365CalendarColorType res;
	gint ii, rr, gg, bb;
	gdouble distance, res_distance = -1.0;

	if (!rgb || !*rgb)
		return E_O365_CALENDAR_COLOR_NOT_SET;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (color_map[ii].rgb && g_ascii_strcasecmp (color_map[ii].rgb, rgb) == 0)
			return color_map[ii].value;
	}

	/* When exact match did not work, approximate to the closest */

	if (sscanf (rgb, "#%02x%02x%02x", &rr, &gg, &bb) != 3)
		return E_O365_CALENDAR_COLOR_UNKNOWN;

	distance = (rr * rr) + (gg * gg) + (bb * bb);
	res = E_O365_CALENDAR_COLOR_UNKNOWN;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (color_map[ii].rgb && sscanf (color_map[ii].rgb, "#%02x%02x%02x", &rr, &gg, &bb) == 3) {
			gdouble candidate_distance;

			candidate_distance = (rr * rr) + (gg * gg) + (bb * bb) - distance;

			if (candidate_distance < 0.0)
				candidate_distance *= -1.0;

			if (!ii || candidate_distance < res_distance) {
				res_distance = candidate_distance;
				res = color_map[ii].value;
			}
		}
	}

	return res;
}

JsonArray *
e_o365_json_get_array_member (JsonObject *object,
			      const gchar *member_name)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (member_name != NULL, NULL);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return NULL;

	g_return_val_if_fail (JSON_NODE_HOLDS_ARRAY (node), NULL);

	return json_node_get_array (node);
}

void
e_o365_json_begin_array_member (JsonBuilder *builder,
				const gchar *member_name)
{
	if (member_name && *member_name)
		json_builder_set_member_name (builder, member_name);

	json_builder_begin_array (builder);
}

void
e_o365_json_end_array_member (JsonBuilder *builder)
{
	json_builder_end_array (builder);
}

gboolean
e_o365_json_get_boolean_member (JsonObject *object,
				const gchar *member_name,
				gboolean default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_boolean (node);
}

void
e_o365_json_add_boolean_member (JsonBuilder *builder,
				const gchar *member_name,
				gboolean value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_boolean_value (builder, value);
}

gdouble
e_o365_json_get_double_member (JsonObject *object,
			       const gchar *member_name,
			       gdouble default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_double (node);
}

void
e_o365_json_add_double_member (JsonBuilder *builder,
			       const gchar *member_name,
			       gdouble value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_double_value (builder, value);
}

gint64
e_o365_json_get_int_member (JsonObject *object,
			    const gchar *member_name,
			    gint64 default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_int (node);
}

void
e_o365_json_add_int_member (JsonBuilder *builder,
			    const gchar *member_name,
			    gint64 value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_int_value (builder, value);
}

gboolean
e_o365_json_get_null_member (JsonObject *object,
			     const gchar *member_name,
			     gboolean default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node)
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_NULL (node), default_value);

	return json_node_is_null (node);
}

void
e_o365_json_add_null_member (JsonBuilder *builder,
			     const gchar *member_name)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_null_value (builder);
}

JsonObject *
e_o365_json_get_object_member (JsonObject *object,
			       const gchar *member_name)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (member_name != NULL, NULL);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return NULL;

	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), NULL);

	return json_node_get_object (node);
}

void
e_o365_json_begin_object_member (JsonBuilder *builder,
				 const gchar *member_name)
{
	if (member_name && *member_name)
		json_builder_set_member_name (builder, member_name);

	json_builder_begin_object (builder);
}

void
e_o365_json_end_object_member (JsonBuilder *builder)
{
	json_builder_end_object (builder);
}

const gchar *
e_o365_json_get_string_member (JsonObject *object,
			       const gchar *member_name,
			       const gchar *default_value)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, default_value);
	g_return_val_if_fail (member_name != NULL, default_value);

	node = json_object_get_member (object, member_name);

	if (!node || JSON_NODE_HOLDS_NULL (node))
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_string (node);
}

void
e_o365_json_add_string_member (JsonBuilder *builder,
			       const gchar *member_name,
			       const gchar *value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_string_value (builder, value ? value : "");
}

void
e_o365_json_add_nonempty_string_member (JsonBuilder *builder,
					const gchar *member_name,
					const gchar *value)
{
	g_return_if_fail (member_name && *member_name);

	if (value && *value)
		e_o365_json_add_string_member (builder, member_name, value);
}

void
e_o365_json_add_nonempty_or_null_string_member (JsonBuilder *builder,
						const gchar *member_name,
						const gchar *value)
{
	g_return_if_fail (member_name && *member_name);

	if (value && *value)
		e_o365_json_add_string_member (builder, member_name, value);
	else
		e_o365_json_add_null_member (builder, member_name);
}

EO365Date
e_o365_date_get (JsonObject *object,
		 const gchar *member_name)
{
	const gchar *value;
	guint year = 0, month = 0, day = 0;

	value = e_o365_json_get_string_member (object, member_name, NULL);

	if (!value || !*value)
		return -1;

	if (sscanf (value, "%04u-%02u-%02u", &year, &month, &day) != 3) {
		g_warning ("%s: Failed to decode date '%s' of member '%s'", G_STRFUNC, value, member_name);
		return -1;
	}

	return e_o365_date_encode (year, month, day);
}

void
e_o365_add_date (JsonBuilder *builder,
		 const gchar *member_name,
		 EO365Date value)
{
	guint year, month, day;

	if (e_o365_date_decode (value, &year, &month, &day)) {
		gchar buff[128];

		g_snprintf (buff, sizeof (buff), "%04u-%02u-%02u", year, month, day);
		e_o365_json_add_string_member (builder, member_name, buff);
	}
}

gboolean
e_o365_date_decode (EO365Date dt,
		    guint *out_year,
		    guint *out_month,
		    guint *out_day)
{
	g_return_val_if_fail (out_year != NULL, FALSE);
	g_return_val_if_fail (out_month != NULL, FALSE);
	g_return_val_if_fail (out_day != NULL, FALSE);

	if (dt <= 0)
		return FALSE;

	*out_year = dt % 10000;
	*out_month = (dt / 10000) % 100;
	*out_day = (dt / 1000000) % 100;

	return *out_year > 1000 &&
		*out_month >= 1 && *out_month <= 12 &&
		*out_day >= 1 && *out_day <= 31;
}

EO365Date
e_o365_date_encode (guint year,
		    guint month,
		    guint day)
{
	g_return_val_if_fail (year > 0 && year < 10000, -1);
	g_return_val_if_fail (month >= 1 && month <= 12, -1);
	g_return_val_if_fail (day >= 1 && day <= 31, -1);

	return year + (10000 * month) + (1000000 * day);
}

time_t
e_o365_get_date_time_offset_member (JsonObject *object,
				    const gchar *member_name)
{
	const gchar *value;
	time_t res = (time_t) 0;

	value = e_o365_json_get_string_member (object, member_name, NULL);

	if (value) {
		GDateTime *dt;

		dt = g_date_time_new_from_iso8601 (value, NULL);

		if (dt) {
			res = (time_t) g_date_time_to_unix (dt);
			g_date_time_unref (dt);
		}
	}

	return res;
}

void
e_o365_add_date_time_offset_member (JsonBuilder *builder,
				    const gchar *member_name,
				    time_t value)
{
	GDateTime *dt;
	gchar *value_str;

	if (value <= (time_t) 0) {
		e_o365_json_add_null_member (builder, member_name);
		return;
	}

	dt = g_date_time_new_from_unix_utc (value);
	g_return_if_fail (dt != NULL);

	value_str = g_date_time_format_iso8601 (dt);

	e_o365_json_add_string_member (builder, member_name, value_str);

	g_date_time_unref (dt);
	g_free (value_str);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/datetimetimezone?view=graph-rest-1.0 */

time_t
e_o365_date_time_get_date_time (EO365DateTimeWithZone *datetime)
{
	return e_o365_get_date_time_offset_member (datetime, "dateTime");
}

const gchar *
e_o365_date_time_get_time_zone (EO365DateTimeWithZone *datetime)
{
	return e_o365_json_get_string_member (datetime, "timeZone", NULL);
}

void
e_o365_add_date_time (JsonBuilder *builder,
		      const gchar *member_name,
		      time_t date_time,
		      const gchar *zone)
{
	g_return_if_fail (member_name != NULL);

	if (date_time <= (time_t) 0) {
		e_o365_json_add_null_member (builder, member_name);
		return;
	}

	e_o365_json_begin_object_member (builder, member_name);

	e_o365_add_date_time_offset_member (builder, "dateTime", date_time);
	e_o365_json_add_nonempty_string_member (builder, "timeZone", zone);

	e_o365_json_end_object_member (builder);
}

/* https://docs.microsoft.com/en-us/graph/delta-query-overview */

gboolean
e_o365_delta_is_removed_object (JsonObject *object)
{
	return json_object_has_member (object, "@removed");
}

/* https://docs.microsoft.com/en-us/graph/api/resources/outlookcategory?view=graph-rest-1.0 */

const gchar *
e_o365_category_get_display_name (EO365Category *category)
{
	return e_o365_json_get_string_member (category, "displayName", NULL);
}

const gchar *
e_o365_category_get_id (EO365Category *category)
{
	return e_o365_json_get_string_member (category, "id", NULL);
}

const gchar *
e_o365_category_get_color (EO365Category *category)
{
	const gchar *colors_array[] = {
		"#ff1a36", /* Red */
		"#ff8c00", /* Orange */
		"#f4b10b", /* Peach */
		"#fff100", /* Yellow */
		"#009e48", /* Green */
		"#00b294", /* Teal */
		"#89933f", /* Olive */
		"#00bcf2", /* Blue */
		"#8e69df", /* Purple */
		"#f30092", /* Maroon */
		"#6c7e9a", /* Steel */
		"#425066", /* DarkSteel */
		"#969696", /* Gray */
		"#525552", /* DarkGray */
		"#282828", /* Black */
		"#a00023", /* DarkRed */
		"#c45502", /* DarkOrange */
		"#af7000", /* DarkPeach */
		"#b59b02", /* DarkYellow */
		"#176002", /* DarkGreen */
		"#00725c", /* DarkTeal */
		"#5c6022", /* DarkOlive */
		"#036393", /* DarkBlue */
		"#422f8e", /* DarkPurple */
		"#960269"  /* DarkMaroon */
	};
	const gchar *color_str;
	gchar *enptr = NULL;
	gint color_index;

	color_str = e_o365_json_get_string_member (category, "color", NULL);

	if (!color_str ||
	    g_ascii_strcasecmp (color_str, "None") == 0 ||
	    g_ascii_strncasecmp (color_str, "preset", 6) != 0)
		return NULL;

	color_index = (gint) g_ascii_strtoll (color_str + 6, &enptr, 10);

	if (enptr != color_str && color_index >= 0 && color_index < G_N_ELEMENTS (colors_array))
		return colors_array[color_index];

	return NULL;
}

/* https://docs.microsoft.com/en-us/graph/api/resources/mailfolder?view=graph-rest-1.0
   https://docs.microsoft.com/en-us/graph/api/resources/contactfolder?view=graph-rest-1.0
 */

const gchar *
e_o365_folder_get_id (EO365Folder *folder)
{
	return e_o365_json_get_string_member (folder, "id", NULL);
}

const gchar *
e_o365_folder_get_parent_folder_id (EO365Folder *folder)
{
	return e_o365_json_get_string_member (folder, "parentFolderId", NULL);
}

const gchar *
e_o365_folder_get_display_name (EO365Folder *folder)
{
	return e_o365_json_get_string_member (folder, "displayName", NULL);
}

gint32
e_o365_mail_folder_get_child_folder_count (EO365MailFolder *folder)
{
	return (gint32) e_o365_json_get_int_member (folder, "childFolderCount", 0);
}

gint32
e_o365_mail_folder_get_total_item_count (EO365MailFolder *folder)
{
	return (gint32) e_o365_json_get_int_member (folder, "totalItemCount", 0);
}

gint32
e_o365_mail_folder_get_unread_item_count (EO365MailFolder *folder)
{
	return (gint32) e_o365_json_get_int_member (folder, "unreadItemCount", 0);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/recipient?view=graph-rest-1.0
   https://docs.microsoft.com/en-us/graph/api/resources/emailaddress?view=graph-rest-1.0
 */
const gchar *
e_o365_recipient_get_name (EO365Recipient *recipient)
{
	JsonObject *email_address;

	email_address = e_o365_json_get_object_member (recipient, "emailAddress");

	if (!email_address)
		return NULL;

	return e_o365_json_get_string_member (email_address, "name", NULL);
}

const gchar *
e_o365_recipient_get_address (EO365Recipient *recipient)
{
	JsonObject *email_address;

	email_address = e_o365_json_get_object_member (recipient, "emailAddress");

	if (!email_address)
		return NULL;

	return e_o365_json_get_string_member (email_address, "address", NULL);
}

void
e_o365_add_recipient (JsonBuilder *builder,
		      const gchar *member_name,
		      const gchar *name,
		      const gchar *address)
{
	g_return_if_fail ((name && *name) || (address && *address));

	e_o365_json_begin_object_member (builder, member_name);
	e_o365_json_begin_object_member (builder, "emailAddress");

	e_o365_json_add_nonempty_string_member (builder, "name", name);
	e_o365_json_add_nonempty_string_member (builder, "address", address);

	e_o365_json_end_object_member (builder); /* emailAddress */
	e_o365_json_end_object_member (builder); /* member_name */
}

/* https://docs.microsoft.com/en-us/graph/api/resources/internetmessageheader?view=graph-rest-1.0 */

const gchar *
e_o365_internet_message_header_get_name (EO365InternetMessageHeader *header)
{
	return e_o365_json_get_string_member (header, "name", NULL);
}

const gchar *
e_o365_internet_message_header_get_value (EO365InternetMessageHeader *header)
{
	return e_o365_json_get_string_member (header, "value", NULL);
}

void
e_o365_add_internet_message_header (JsonBuilder *builder,
				    const gchar *name,
				    const gchar *value)
{
	g_return_if_fail (name && *name);
	g_return_if_fail (value);

	json_builder_begin_object (builder);

	if (value && (*value == ' ' || *value == '\t'))
		value++;

	e_o365_json_add_string_member (builder, "name", name);
	e_o365_json_add_string_member (builder, "value", value);

	json_builder_end_object (builder);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/followupflag?view=graph-rest-1.0 */

EO365DateTimeWithZone *
e_o365_followup_flag_get_completed_date_time (EO365FollowupFlag *flag)
{
	return e_o365_json_get_object_member (flag, "completedDateTime");
}

void
e_o365_followup_flag_add_completed_date_time (JsonBuilder *builder,
					      time_t date_time,
					      const gchar *zone)
{
	e_o365_add_date_time (builder, "completedDateTime", date_time, zone);
}

EO365DateTimeWithZone *
e_o365_followup_flag_get_due_date_time (EO365FollowupFlag *flag)
{
	return e_o365_json_get_object_member (flag, "dueDateTime");
}

void
e_o365_followup_flag_add_due_date_time (JsonBuilder *builder,
					time_t date_time,
					const gchar *zone)
{
	e_o365_add_date_time (builder, "dueDateTime", date_time, zone);
}

EO365FollowupFlagStatusType
e_o365_followup_flag_get_flag_status (EO365FollowupFlag *flag)
{
	return o365_json_utils_get_json_as_enum (flag, "flagStatus",
		flag_status_map, G_N_ELEMENTS (flag_status_map),
		E_O365_FOLLOWUP_FLAG_STATUS_NOT_SET,
		E_O365_FOLLOWUP_FLAG_STATUS_UNKNOWN);
}

void
e_o365_followup_flag_add_flag_status (JsonBuilder *builder,
				      EO365FollowupFlagStatusType status)
{
	o365_json_utils_add_enum_as_json (builder, "flagStatus", status,
		flag_status_map, G_N_ELEMENTS (flag_status_map),
		E_O365_FOLLOWUP_FLAG_STATUS_NOT_SET,
		E_O365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED);
}

EO365DateTimeWithZone *
e_o365_followup_flag_get_start_date_time (EO365FollowupFlag *flag)
{
	return e_o365_json_get_object_member (flag, "startDateTime");
}

void
e_o365_followup_flag_add_start_date_time (JsonBuilder *builder,
					  time_t date_time,
					  const gchar *zone)
{
	e_o365_add_date_time (builder, "startDateTime", date_time, zone);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/itembody?view=graph-rest-1.0 */

const gchar *
e_o365_item_body_get_content (EO365ItemBody *item_body)
{
	return e_o365_json_get_string_member (item_body, "content", NULL);
}

EO365ItemBodyContentTypeType
e_o365_item_body_get_content_type (EO365ItemBody *item_body)
{
	return o365_json_utils_get_json_as_enum (item_body, "contentType",
		content_type_map, G_N_ELEMENTS (content_type_map),
		E_O365_ITEM_BODY_CONTENT_TYPE_NOT_SET,
		E_O365_ITEM_BODY_CONTENT_TYPE_UNKNOWN);
}

void
e_o365_add_item_body (JsonBuilder *builder,
		      const gchar *member_name,
		      EO365ItemBodyContentTypeType content_type,
		      const gchar *content)
{
	g_return_if_fail (member_name != NULL);
	g_return_if_fail (content != NULL);

	e_o365_json_begin_object_member (builder, member_name);

	o365_json_utils_add_enum_as_json (builder, "contentType", content_type,
		content_type_map, G_N_ELEMENTS (content_type_map),
		E_O365_ITEM_BODY_CONTENT_TYPE_NOT_SET,
		E_O365_ITEM_BODY_CONTENT_TYPE_TEXT);

	e_o365_json_add_string_member (builder, "content", content);

	e_o365_json_end_object_member (builder);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/message?view=graph-rest-1.0 */

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_bcc_recipients (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "bccRecipients");
}

void
e_o365_mail_message_begin_bcc_recipients (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "bccRecipients");
}

void
e_o365_mail_message_end_bcc_recipients (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

EO365ItemBody *
e_o365_mail_message_get_body (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "body");
}

void
e_o365_mail_message_add_body (JsonBuilder *builder,
			      EO365ItemBodyContentTypeType content_type,
			      const gchar *content)
{
	e_o365_add_item_body (builder, "body", content_type, content);
}

const gchar *
e_o365_mail_message_get_body_preview (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "bodyPreview", NULL);
}

JsonArray * /* const gchar * */
e_o365_mail_message_get_categories (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "categories");
}

void
e_o365_mail_message_begin_categories (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "categories");
}

void
e_o365_mail_message_end_categories (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_mail_message_add_category (JsonBuilder *builder,
				  const gchar *category)
{
	g_return_if_fail (category && *category);

	json_builder_add_string_value (builder, category);
}

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_cc_recipients (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "ccRecipients");
}

void
e_o365_mail_message_begin_cc_recipients (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "ccRecipients");
}

void
e_o365_mail_message_end_cc_recipients (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

const gchar *
e_o365_mail_message_get_change_key (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "changeKey", NULL);
}

const gchar *
e_o365_mail_message_get_conversation_id (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "conversationId", NULL);
}

JsonObject * /* Edm.Binary */
e_o365_mail_message_get_conversation_index (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "conversationIndex");
}

time_t
e_o365_mail_message_get_created_date_time (EO365MailMessage *mail)
{
	return e_o365_get_date_time_offset_member (mail, "createdDateTime");
}

EO365FollowupFlag *
e_o365_mail_message_get_flag (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "flag");
}

void
e_o365_mail_message_begin_flag (JsonBuilder *builder)
{
	e_o365_json_begin_object_member (builder, "flag");
}

void
e_o365_mail_message_end_flag (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

EO365Recipient *
e_o365_mail_message_get_from (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "from");
}

void
e_o365_mail_message_add_from (JsonBuilder *builder,
			      const gchar *name,
			      const gchar *address)
{
	e_o365_add_recipient (builder, "from", name, address);
}

gboolean
e_o365_mail_message_get_has_attachments	(EO365MailMessage *mail)
{
	return e_o365_json_get_boolean_member (mail, "hasAttachments", FALSE);
}

const gchar *
e_o365_mail_message_get_id (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "id", NULL);
}

EO365ImportanceType
e_o365_mail_message_get_importance (EO365MailMessage *mail)
{
	return o365_json_utils_get_json_as_enum (mail, "importance",
		importance_map, G_N_ELEMENTS (importance_map),
		E_O365_IMPORTANCE_NOT_SET,
		E_O365_IMPORTANCE_UNKNOWN);
}

void
e_o365_mail_message_add_importance (JsonBuilder *builder,
				    EO365ImportanceType importance)
{
	o365_json_utils_add_enum_as_json (builder, "importance", importance,
		importance_map, G_N_ELEMENTS (importance_map),
		E_O365_IMPORTANCE_NOT_SET,
		E_O365_IMPORTANCE_NOT_SET);
}

EO365InferenceClassificationType
e_o365_mail_message_get_inference_classification (EO365MailMessage *mail)
{
	return o365_json_utils_get_json_as_enum (mail, "inferenceClassification",
		inference_classification_map, G_N_ELEMENTS (inference_classification_map),
		E_O365_INFERENCE_CLASSIFICATION_NOT_SET,
		E_O365_INFERENCE_CLASSIFICATION_UNKNOWN);
}

JsonArray * /* EO365InternetMessageHeader * */
e_o365_mail_message_get_internet_message_headers (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "internetMessageHeaders");
}

void
e_o365_mail_message_begin_internet_message_headers (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "internetMessageHeaders");
}

void
e_o365_mail_message_end_internet_message_headers (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

const gchar *
e_o365_mail_message_get_internet_message_id (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "internetMessageId", NULL);
}

void
e_o365_mail_message_add_internet_message_id (JsonBuilder *builder,
					     const gchar *message_id)
{
	e_o365_json_add_nonempty_string_member (builder, "internetMessageId", message_id);
}

gboolean
e_o365_mail_message_get_is_delivery_receipt_requested (EO365MailMessage *mail)
{
	return e_o365_json_get_boolean_member (mail, "isDeliveryReceiptRequested", FALSE);
}

void
e_o365_mail_message_add_is_delivery_receipt_requested (JsonBuilder *builder,
						       gboolean value)
{
	e_o365_json_add_boolean_member (builder, "isDeliveryReceiptRequested", value);
}

gboolean
e_o365_mail_message_get_is_draft (EO365MailMessage *mail)
{
	return e_o365_json_get_boolean_member (mail, "isDraft", FALSE);
}

gboolean
e_o365_mail_message_get_is_read (EO365MailMessage *mail)
{
	return e_o365_json_get_boolean_member (mail, "isRead", FALSE);
}

void
e_o365_mail_message_add_is_read (JsonBuilder *builder,
				 gboolean value)
{
	e_o365_json_add_boolean_member (builder, "isRead", value);
}

gboolean
e_o365_mail_message_get_is_read_receipt_requested (EO365MailMessage *mail)
{
	return e_o365_json_get_boolean_member (mail, "isReadReceiptRequested", FALSE);
}

void
e_o365_mail_message_add_is_read_receipt_requested (JsonBuilder *builder,
						   gboolean value)
{
	e_o365_json_add_boolean_member (builder, "isReadReceiptRequested", value);
}

time_t
e_o365_mail_message_get_last_modified_date_time (EO365MailMessage *mail)
{
	return e_o365_get_date_time_offset_member (mail, "lastModifiedDateTime");
}

const gchar *
e_o365_mail_message_get_parent_folder_id (EO365MailMessage *mail)
{
		return e_o365_json_get_string_member (mail, "parentFolderId", NULL);
}

time_t
e_o365_mail_message_get_received_date_time (EO365MailMessage *mail)
{
	return e_o365_get_date_time_offset_member (mail, "receivedDateTime");
}

void
e_o365_mail_message_add_received_date_time (JsonBuilder *builder,
					    time_t value)
{
	e_o365_add_date_time_offset_member (builder, "receivedDateTime", value);
}

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_reply_to (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "replyTo");
}

void
e_o365_mail_message_begin_reply_to (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "replyTo");
}

void
e_o365_mail_message_end_reply_to (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

EO365Recipient *
e_o365_mail_message_get_sender (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "sender");
}

void
e_o365_mail_message_add_sender (JsonBuilder *builder,
				const gchar *name,
				const gchar *address)
{
	g_return_if_fail ((name && *name) || (address && *address));

	e_o365_add_recipient (builder, "sender", name, address);
}

time_t
e_o365_mail_message_get_sent_date_time (EO365MailMessage *mail)
{
	return e_o365_get_date_time_offset_member (mail, "sentDateTime");
}

void
e_o365_mail_message_add_sent_date_time (JsonBuilder *builder,
					time_t value)
{
	e_o365_add_date_time_offset_member (builder, "sentDateTime", value);
}

const gchar *
e_o365_mail_message_get_subject (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "subject", NULL);
}

void
e_o365_mail_message_add_subject (JsonBuilder *builder,
				 const gchar *subject)
{
	e_o365_json_add_nonempty_string_member (builder, "subject", subject);
}

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_to_recipients (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "toRecipients");
}

void
e_o365_mail_message_begin_to_recipients (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "toRecipients");
}

void
e_o365_mail_message_end_to_recipients (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

EO365ItemBody *
e_o365_mail_message_get_unique_body (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "uniqueBody");
}

const gchar *
e_o365_mail_message_get_web_link (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "webLink", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/attachment?view=graph-rest-1.0 */

EO365AttachmentDataType
e_o365_attachment_get_data_type (EO365Attachment *attachment)
{
	return o365_json_utils_get_json_as_enum (attachment, "@odata.type",
		attachment_data_type_map, G_N_ELEMENTS (attachment_data_type_map),
		E_O365_ATTACHMENT_DATA_TYPE_NOT_SET,
		E_O365_ATTACHMENT_DATA_TYPE_UNKNOWN);
}

void
e_o365_attachment_begin_attachment (JsonBuilder *builder,
				    EO365AttachmentDataType data_type)
{
	e_o365_json_begin_object_member (builder, NULL);

	o365_json_utils_add_enum_as_json (builder, "@odata.type", data_type,
		attachment_data_type_map, G_N_ELEMENTS (attachment_data_type_map),
		E_O365_ATTACHMENT_DATA_TYPE_NOT_SET,
		E_O365_ATTACHMENT_DATA_TYPE_FILE);
}

void
e_o365_attachment_end_attachment (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

const gchar *
e_o365_attachment_get_content_type (EO365Attachment *attachment)
{
	return e_o365_json_get_string_member (attachment, "contentType", NULL);
}

void
e_o365_attachment_add_content_type (JsonBuilder *builder,
				    const gchar *value)
{
	e_o365_json_add_string_member (builder, "contentType", value);
}

const gchar *
e_o365_attachment_get_id (EO365Attachment *attachment)
{
	return e_o365_json_get_string_member (attachment, "id", NULL);
}

gboolean
e_o365_attachment_get_is_inline (EO365Attachment *attachment)
{
	return e_o365_json_get_boolean_member (attachment, "isInline", FALSE);
}

void
e_o365_attachment_add_is_inline (JsonBuilder *builder,
				 gboolean value)
{
	e_o365_json_add_boolean_member (builder, "isInline", value);
}

time_t
e_o365_attachment_get_last_modified_date_time (EO365Attachment *attachment)
{
	return e_o365_get_date_time_offset_member (attachment, "lastModifiedDateTime");
}

void
e_o365_attachment_add_last_modified_date_time (JsonBuilder *builder,
					       time_t value)
{
	e_o365_add_date_time_offset_member (builder, "lastModifiedDateTime", value);
}

const gchar *
e_o365_attachment_get_name (EO365Attachment *attachment)
{
	return e_o365_json_get_string_member (attachment, "name", NULL);
}

void
e_o365_attachment_add_name (JsonBuilder *builder,
			    const gchar *value)
{
	e_o365_json_add_string_member (builder, "name", value);
}

gint32
e_o365_attachment_get_size (EO365Attachment *attachment)
{
	return (gint32) e_o365_json_get_int_member (attachment, "size", -1);
}

void
e_o365_attachment_add_size (JsonBuilder *builder,
			    gint32 value)
{
	e_o365_json_add_int_member (builder, "size", value);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/fileattachment?view=graph-rest-1.0 */

const gchar * /* base64-encoded */
e_o365_file_attachment_get_content_bytes (EO365Attachment *attachment)
{
	return e_o365_json_get_string_member (attachment, "contentBytes", NULL);
}

void
e_o365_file_attachment_add_content_bytes (JsonBuilder *builder,
					  const gchar *base64_value)
{
	e_o365_json_add_string_member (builder, "contentBytes", base64_value);
}

const gchar *
e_o365_file_attachment_get_content_id (EO365Attachment *attachment)
{
	return e_o365_json_get_string_member (attachment, "contentId", NULL);
}

void
e_o365_file_attachment_add_content_id (JsonBuilder *builder,
				       const gchar *value)
{
	e_o365_json_add_string_member (builder, "contentId", value);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/emailaddress?view=graph-rest-1.0 */

const gchar *
e_o365_email_address_get_name (EO365EmailAddress *email)
{
	return e_o365_json_get_string_member (email, "name", NULL);
}

const gchar *
e_o365_email_address_get_address (EO365EmailAddress *email)
{
	return e_o365_json_get_string_member (email, "address", NULL);
}

void
e_o365_add_email_address (JsonBuilder *builder,
			  const gchar *member_name,
			  const gchar *name,
			  const gchar *address)
{
	g_return_if_fail ((name && *name) || (address && *address));

	e_o365_json_begin_object_member (builder, member_name);

	e_o365_json_add_nonempty_string_member (builder, "name", name);
	e_o365_json_add_nonempty_string_member (builder, "address", address);

	e_o365_json_end_object_member (builder); /* member_name */
}

/* https://docs.microsoft.com/en-us/graph/api/resources/physicaladdress?view=graph-rest-1.0 */

const gchar *
e_o365_physical_address_get_city (EO365PhysicalAddress *address)
{
	return e_o365_json_get_string_member (address, "city", NULL);
}

const gchar *
e_o365_physical_address_get_country_or_region (EO365PhysicalAddress *address)
{
	return e_o365_json_get_string_member (address, "countryOrRegion", NULL);
}

const gchar *
e_o365_physical_address_get_postal_code (EO365PhysicalAddress *address)
{
	return e_o365_json_get_string_member (address, "postalCode", NULL);
}

const gchar *
e_o365_physical_address_get_state (EO365PhysicalAddress *address)
{
	return e_o365_json_get_string_member (address, "state", NULL);
}

const gchar *
e_o365_physical_address_get_street (EO365PhysicalAddress *address)
{
	return e_o365_json_get_string_member (address, "street", NULL);
}

void
e_o365_add_physical_address (JsonBuilder *builder,
			     const gchar *member_name,
			     const gchar *city,
			     const gchar *country_or_region,
			     const gchar *postal_code,
			     const gchar *state,
			     const gchar *street)
{
	if ((city && *city) ||
	    (country_or_region && *country_or_region) ||
	    (postal_code && *postal_code) ||
	    (state && *state) ||
	    (street && *street)) {
		e_o365_json_begin_object_member (builder, member_name);
		e_o365_json_add_nonempty_string_member (builder, "city", city);
		e_o365_json_add_nonempty_string_member (builder, "countryOrRegion", country_or_region);
		e_o365_json_add_nonempty_string_member (builder, "postalCode", postal_code);
		e_o365_json_add_nonempty_string_member (builder, "state", state);
		e_o365_json_add_nonempty_string_member (builder, "street", street);
		e_o365_json_end_object_member (builder);
	} else {
		e_o365_json_begin_object_member (builder, member_name);
		e_o365_json_end_object_member (builder);
	}
}

/* https://docs.microsoft.com/en-us/graph/api/resources/contact?view=graph-rest-1.0 */

const gchar *
e_o365_contact_get_id (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "id", NULL);
}

const gchar *
e_o365_contact_get_parent_folder_id (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "parentFolderId", NULL);
}

const gchar *
e_o365_contact_get_change_key (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "changeKey", NULL);
}

time_t
e_o365_contact_get_created_date_time (EO365Contact *contact)
{
	return e_o365_get_date_time_offset_member (contact, "createdDateTime");
}

time_t
e_o365_contact_get_last_modified_date_time (EO365Contact *contact)
{
	return e_o365_get_date_time_offset_member (contact, "lastModifiedDateTime");
}

const gchar *
e_o365_contact_get_assistant_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "assistantName", NULL);
}

void
e_o365_contact_add_assistant_name (JsonBuilder *builder,
				   const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "assistantName", value);
}

time_t
e_o365_contact_get_birthday (EO365Contact *contact)
{
	return e_o365_get_date_time_offset_member (contact, "birthday");
}

void
e_o365_contact_add_birthday (JsonBuilder *builder,
			     time_t value)
{
	e_o365_add_date_time_offset_member (builder, "birthday", value);
}

EO365PhysicalAddress *
e_o365_contact_get_business_address (EO365Contact *contact)
{
	return e_o365_json_get_object_member (contact, "businessAddress");
}

void
e_o365_contact_add_business_address (JsonBuilder *builder,
				     const gchar *city,
				     const gchar *country_or_region,
				     const gchar *postal_code,
				     const gchar *state,
				     const gchar *street)
{
	e_o365_add_physical_address (builder, "businessAddress", city, country_or_region, postal_code, state, street);
}

const gchar *
e_o365_contact_get_business_home_page (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "businessHomePage", NULL);
}

void
e_o365_contact_add_business_home_page (JsonBuilder *builder,
				       const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "businessHomePage", value);
}

JsonArray * /* const gchar * */
e_o365_contact_get_business_phones (EO365Contact *contact)
{
	return e_o365_json_get_array_member (contact, "businessPhones");
}

void
e_o365_contact_begin_business_phones (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "businessPhones");
}

void
e_o365_contact_end_business_phones (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_contact_add_business_phone (JsonBuilder *builder,
				   const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

JsonArray * /* const gchar * */
e_o365_contact_get_categories (EO365Contact *contact)
{
	return e_o365_json_get_array_member (contact, "categories");
}

void
e_o365_contact_begin_categories (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "categories");
}

void
e_o365_contact_end_categories (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_contact_add_category (JsonBuilder *builder,
			     const gchar *category)
{
	g_return_if_fail (category && *category);

	json_builder_add_string_value (builder, category);
}

JsonArray * /* const gchar * */
e_o365_contact_get_children (EO365Contact *contact)
{
	return e_o365_json_get_array_member (contact, "children");
}

void
e_o365_contact_begin_children (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "children");
}

void
e_o365_contact_end_children (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_contact_add_child (JsonBuilder *builder,
			  const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

const gchar *
e_o365_contact_get_company_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "companyName", NULL);
}

void
e_o365_contact_add_company_name (JsonBuilder *builder,
				 const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "companyName", value);
}

const gchar *
e_o365_contact_get_department (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "department", NULL);
}

void
e_o365_contact_add_department (JsonBuilder *builder,
			       const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "department", value);
}

const gchar *
e_o365_contact_get_display_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "displayName", NULL);
}

void
e_o365_contact_add_display_name (JsonBuilder *builder,
				 const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "displayName", value);
}

JsonArray * /* EO365EmailAddress * */
e_o365_contact_get_email_addresses (EO365Contact *contact)
{
	return e_o365_json_get_array_member (contact, "emailAddresses");
}

void
e_o365_contact_begin_email_addresses (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "emailAddresses");
}

void
e_o365_contact_end_email_addresses (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

const gchar *
e_o365_contact_get_file_as (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "fileAs", NULL);
}

void
e_o365_contact_add_file_as (JsonBuilder *builder,
			    const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "fileAs", value);
}

const gchar *
e_o365_contact_get_generation (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "generation", NULL);
}

void
e_o365_contact_add_generation (JsonBuilder *builder,
			       const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "generation", value);
}

const gchar *
e_o365_contact_get_given_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "givenName", NULL);
}

void
e_o365_contact_add_given_name (JsonBuilder *builder,
			       const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "givenName", value);
}

EO365PhysicalAddress *
e_o365_contact_get_home_address (EO365Contact *contact)
{
	return e_o365_json_get_object_member (contact, "homeAddress");
}

void
e_o365_contact_add_home_address (JsonBuilder *builder,
				 const gchar *city,
				 const gchar *country_or_region,
				 const gchar *postal_code,
				 const gchar *state,
				 const gchar *street)
{
	e_o365_add_physical_address (builder, "homeAddress", city, country_or_region, postal_code, state, street);
}

JsonArray * /* const gchar * */
e_o365_contact_get_home_phones (EO365Contact *contact)
{
	return e_o365_json_get_array_member (contact, "homePhones");
}

void
e_o365_contact_begin_home_phones (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "homePhones");
}

void
e_o365_contact_end_home_phones (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_contact_add_home_phone (JsonBuilder *builder,
			       const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

JsonArray * /* const gchar * */
e_o365_contact_get_im_addresses (EO365Contact *contact)
{
	return e_o365_json_get_array_member (contact, "imAddresses");
}

void
e_o365_contact_begin_im_addresses (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "imAddresses");
}

void
e_o365_contact_end_im_addresses (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_contact_add_im_address (JsonBuilder *builder,
			       const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

const gchar *
e_o365_contact_get_initials (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "initials", NULL);
}

void
e_o365_contact_add_initials (JsonBuilder *builder,
			     const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "initials", value);
}

const gchar *
e_o365_contact_get_job_title (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "jobTitle", NULL);
}

void
e_o365_contact_add_job_title (JsonBuilder *builder,
			      const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "jobTitle", value);
}

const gchar *
e_o365_contact_get_manager (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "manager", NULL);
}

void
e_o365_contact_add_manager (JsonBuilder *builder,
			    const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "manager", value);
}

const gchar *
e_o365_contact_get_middle_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "middleName", NULL);
}

void
e_o365_contact_add_middle_name (JsonBuilder *builder,
				const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "middleName", value);
}

const gchar *
e_o365_contact_get_mobile_phone (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "mobilePhone", NULL);
}

void
e_o365_contact_add_mobile_phone (JsonBuilder *builder,
				 const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "mobilePhone", value);
}

const gchar *
e_o365_contact_get_nick_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "nickName", NULL);
}

void
e_o365_contact_add_nick_name (JsonBuilder *builder,
			      const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "nickName", value);
}

const gchar *
e_o365_contact_get_office_location (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "officeLocation", NULL);
}

void
e_o365_contact_add_office_location (JsonBuilder *builder,
				    const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "officeLocation", value);
}

EO365PhysicalAddress *
e_o365_contact_get_other_address (EO365Contact *contact)
{
	return e_o365_json_get_object_member (contact, "otherAddress");
}

void
e_o365_contact_add_other_address (JsonBuilder *builder,
				  const gchar *city,
				  const gchar *country_or_region,
				  const gchar *postal_code,
				  const gchar *state,
				  const gchar *street)
{
	e_o365_add_physical_address (builder, "otherAddress", city, country_or_region, postal_code, state, street);
}

const gchar *
e_o365_contact_get_personal_notes (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "personalNotes", NULL);
}

void
e_o365_contact_add_personal_notes (JsonBuilder *builder,
				   const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "personalNotes", value);
}

const gchar *
e_o365_contact_get_profession (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "profession", NULL);
}

void
e_o365_contact_add_profession (JsonBuilder *builder,
			       const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "profession", value);
}

const gchar *
e_o365_contact_get_spouse_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "spouseName", NULL);
}

void
e_o365_contact_add_spouse_name (JsonBuilder *builder,
				const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "spouseName", value);
}

const gchar *
e_o365_contact_get_surname (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "surname", NULL);
}

void
e_o365_contact_add_surname (JsonBuilder *builder,
			    const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "surname", value);
}

const gchar *
e_o365_contact_get_title (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "title", NULL);
}

void
e_o365_contact_add_title (JsonBuilder *builder,
			  const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "title", value);
}

const gchar *
e_o365_contact_get_yomi_company_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "yomiCompanyName", NULL);
}

void
e_o365_contact_add_yomi_company_name (JsonBuilder *builder,
				      const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "yomiCompanyName", value);
}

const gchar *
e_o365_contact_get_yomi_given_name (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "yomiGivenName", NULL);
}

void
e_o365_contact_add_yomi_given_name (JsonBuilder *builder,
				    const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "yomiGivenName", value);
}

const gchar *
e_o365_contact_get_yomi_surname (EO365Contact *contact)
{
	return e_o365_json_get_string_member (contact, "yomiSurname", NULL);
}

void
e_o365_contact_add_yomi_surname (JsonBuilder *builder,
				 const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "yomiSurname", value);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/calendargroup?view=graph-rest-1.0 */

const gchar *
e_o365_calendar_group_get_id (EO365CalendarGroup *group)
{
	return e_o365_json_get_string_member (group, "id", NULL);
}

const gchar *
e_o365_calendar_group_get_change_key (EO365CalendarGroup *group)
{
	return e_o365_json_get_string_member (group, "changeKey", NULL);
}

const gchar *
e_o365_calendar_group_get_class_id (EO365CalendarGroup *group)
{
	return e_o365_json_get_string_member (group, "classId", NULL);
}

const gchar *
e_o365_calendar_group_get_name (EO365CalendarGroup *group)
{
	return e_o365_json_get_string_member (group, "name", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/calendar?view=graph-rest-1.0 */

const gchar *
e_o365_calendar_get_id (EO365Calendar *calendar)
{
	return e_o365_json_get_string_member (calendar, "id", NULL);
}

const gchar *
e_o365_calendar_get_change_key (EO365Calendar *calendar)
{
	return e_o365_json_get_string_member (calendar, "changeKey", NULL);
}

gboolean
e_o365_calendar_get_can_edit (EO365Calendar *calendar)
{
	return e_o365_json_get_boolean_member (calendar, "canEdit", FALSE);
}

gboolean
e_o365_calendar_get_can_share (EO365Calendar *calendar)
{
	return e_o365_json_get_boolean_member (calendar, "canShare", FALSE);
}

gboolean
e_o365_calendar_get_can_view_private_items (EO365Calendar *calendar)
{
	return e_o365_json_get_boolean_member (calendar, "canViewPrivateItems", FALSE);
}

gboolean
e_o365_calendar_get_is_removable (EO365Calendar *calendar)
{
	return e_o365_json_get_boolean_member (calendar, "isRemovable", FALSE);
}

gboolean
e_o365_calendar_get_is_tallying_responses (EO365Calendar *calendar)
{
	return e_o365_json_get_boolean_member (calendar, "isTallyingResponses", FALSE);
}

EO365EmailAddress *
e_o365_calendar_get_owner (EO365Calendar *calendar)
{
	return e_o365_json_get_object_member (calendar, "owner");
}

const gchar *
e_o365_calendar_get_name (EO365Calendar *calendar)
{
	return e_o365_json_get_string_member (calendar, "name", NULL);
}

void
e_o365_calendar_add_name (JsonBuilder *builder,
			  const gchar *name)
{
	e_o365_json_add_nonempty_string_member (builder, "name", name);
}

guint32 /* bit-or of EO365OnlineMeetingProviderType */
e_o365_calendar_get_allowed_online_meeting_providers (EO365Calendar *calendar)
{
	guint32 providers = E_O365_ONLINE_MEETING_PROVIDER_NOT_SET;
	JsonArray *array;

	array = e_o365_json_get_array_member (calendar, "allowedOnlineMeetingProviders");

	if (array) {
		guint ii, len;

		providers = E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN;

		len = json_array_get_length (array);

		for (ii = 0; ii < len; ii++) {
			const gchar *str = json_array_get_string_element (array, ii);
			gint enum_value;

			if (!str)
				continue;

			enum_value = o365_json_utils_json_value_as_enum (str,
				meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
				E_O365_ONLINE_MEETING_PROVIDER_NOT_SET,
				E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN);

			if (enum_value != E_O365_ONLINE_MEETING_PROVIDER_NOT_SET)
				providers |= enum_value;
		}
	}

	return providers;
}

void
e_o365_calendar_add_allowed_online_meeting_providers (JsonBuilder *builder,
						      guint providers) /* bit-or of EO365OnlineMeetingProviderType */
{
	gint ii;

	if (providers == E_O365_ONLINE_MEETING_PROVIDER_NOT_SET)
		return;

	e_o365_json_begin_array_member (builder, "allowedOnlineMeetingProviders");

	if (providers == E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN)
		json_builder_add_string_value (builder, "unknown");

	for (ii = 0; ii < G_N_ELEMENTS (meeting_provider_map); ii++) {
		if ((providers & meeting_provider_map[ii].enum_value) != 0)
			json_builder_add_string_value (builder, meeting_provider_map[ii].json_value);
	}

	e_o365_json_end_array_member (builder);
}

EO365CalendarColorType
e_o365_calendar_get_color (EO365Calendar *calendar)
{
	const gchar *color;
	gint ii;

	color = e_o365_json_get_string_member (calendar, "color", NULL);

	if (!color)
		return E_O365_CALENDAR_COLOR_NOT_SET;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (g_ascii_strcasecmp (color_map[ii].name, color) == 0)
			return color_map[ii].value;
	}

	return E_O365_CALENDAR_COLOR_UNKNOWN;
}

void
e_o365_calendar_add_color (JsonBuilder *builder,
			   EO365CalendarColorType color)
{
	const gchar *name = NULL;
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (color_map[ii].value == color) {
			name = color_map[ii].name;
			break;
		}
	}

	if (name && g_ascii_strcasecmp (name, "maxColor") != 0)
		e_o365_json_add_string_member (builder, "color", name);
}

EO365OnlineMeetingProviderType
e_o365_calendar_get_default_online_meeting_provider (EO365Calendar *calendar)
{
	return o365_json_utils_get_json_as_enum (calendar, "defaultOnlineMeetingProvider",
		meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
		E_O365_ONLINE_MEETING_PROVIDER_NOT_SET,
		E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN);
}

void
e_o365_calendar_add_default_online_meeting_provider (JsonBuilder *builder,
						     EO365OnlineMeetingProviderType provider)
{
	o365_json_utils_add_enum_as_json (builder, "defaultOnlineMeetingProvider", provider,
		meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
		E_O365_ONLINE_MEETING_PROVIDER_NOT_SET,
		E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/responsestatus?view=graph-rest-1.0 */

EO365ResponseType
e_o365_response_status_get_response (EO365ResponseStatus *response_status)
{
	return o365_json_utils_get_json_as_enum (response_status, "response",
		response_map, G_N_ELEMENTS (response_map),
		E_O365_RESPONSE_NOT_SET,
		E_O365_RESPONSE_UNKNOWN);
}

time_t
e_o365_response_status_get_time (EO365ResponseStatus *response_status)
{
	return e_o365_get_date_time_offset_member (response_status, "time");
}

/* https://docs.microsoft.com/en-us/graph/api/resources/attendee?view=graph-rest-1.0 */

EO365ResponseStatus *
e_o365_attendee_get_status (EO365Attendee *attendee)
{
	return e_o365_json_get_object_member (attendee, "status");
}

EO365AttendeeType
e_o365_attendee_get_type (EO365Attendee *attendee)
{
	return o365_json_utils_get_json_as_enum (attendee, "type",
		attendee_map, G_N_ELEMENTS (attendee_map),
		E_O365_ATTENDEE_NOT_SET,
		E_O365_ATTENDEE_UNKNOWN);
}

EO365EmailAddress *
e_o365_attendee_get_email_address (EO365Attendee *attendee)
{
	return e_o365_json_get_object_member (attendee, "emailAddress");
}

/* https://docs.microsoft.com/en-us/graph/api/resources/outlookgeocoordinates?view=graph-rest-1.0 */

gdouble
e_o365_outlook_geo_coordinates_get_accuracy (EO365OutlookGeoCoordinates *coords)
{
	return e_o365_json_get_double_member (coords, "accuracy", 0.0);
}

void
e_o365_outlook_geo_coordinates_add_accuracy (JsonBuilder *builder,
					     gdouble value)
{
	e_o365_json_add_double_member (builder, "accuracy", value);
}

gdouble
e_o365_outlook_geo_coordinates_get_altitude (EO365OutlookGeoCoordinates *coords)
{
	return e_o365_json_get_double_member (coords, "altitude", 0.0);
}

void
e_o365_outlook_geo_coordinates_add_altitude (JsonBuilder *builder,
					     gdouble value)
{
	e_o365_json_add_double_member (builder, "altitude", value);
}

gdouble
e_o365_outlook_geo_coordinates_get_altitude_accuracy (EO365OutlookGeoCoordinates *coords)
{
	return e_o365_json_get_double_member (coords, "altitudeAccuracy", 0.0);
}

void
e_o365_outlook_geo_coordinates_add_altitude_accuracy (JsonBuilder *builder,
						      gdouble value)
{
	e_o365_json_add_double_member (builder, "altitudeAccuracy", value);
}

gdouble
e_o365_outlook_geo_coordinates_get_latitude (EO365OutlookGeoCoordinates *coords)
{
	return e_o365_json_get_double_member (coords, "latitude", 0.0);
}

void
e_o365_outlook_geo_coordinates_add_latitude (JsonBuilder *builder,
					     gdouble value)
{
	e_o365_json_add_double_member (builder, "latitude", value);
}

gdouble
e_o365_outlook_geo_coordinates_get_longitude (EO365OutlookGeoCoordinates *coords)
{
	return e_o365_json_get_double_member (coords, "longitude", 0.0);
}

void
e_o365_outlook_geo_coordinates_add_longitude (JsonBuilder *builder,
					      gdouble value)
{
	e_o365_json_add_double_member (builder, "longitude", value);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/location?view=graph-rest-1.0 */

EO365PhysicalAddress *
e_o365_location_get_address (EO365Location *location)
{
	return e_o365_json_get_object_member (location, "address");
}

void
e_o365_location_add_address (JsonBuilder *builder,
			     const gchar *city,
			     const gchar *country_or_region,
			     const gchar *postal_code,
			     const gchar *state,
			     const gchar *street)
{
	e_o365_add_physical_address (builder, "address", city, country_or_region, postal_code, state, street);
}

EO365OutlookGeoCoordinates *
e_o365_location_get_coordinates (EO365Location *location)
{
	return e_o365_json_get_object_member (location, "coordinates");
}

void
e_o365_location_begin_coordinates (JsonBuilder *builder)
{
	e_o365_json_begin_object_member (builder, "coordinates");
}

void
e_o365_location_end_coordinates (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

const gchar *
e_o365_location_get_display_name (EO365Location *location)
{
	return e_o365_json_get_string_member (location, "displayName", NULL);
}

void
e_o365_location_add_display_name (JsonBuilder *builder,
				  const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "displayName", value);
}

const gchar *
e_o365_location_get_email_address (EO365Location *location)
{
	return e_o365_json_get_string_member (location, "locationEmailAddress", NULL);
}

void
e_o365_location_add_email_address (JsonBuilder *builder,
				   const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "locationEmailAddress", value);
}

const gchar *
e_o365_location_get_uri (EO365Location *location)
{
	return e_o365_json_get_string_member (location, "locationUri", NULL);
}

void
e_o365_location_add_uri (JsonBuilder *builder,
			 const gchar *value)
{
	e_o365_json_add_nonempty_or_null_string_member (builder, "locationUri", value);
}

EO365LocationType
e_o365_location_get_type (EO365Location *location)
{
	return o365_json_utils_get_json_as_enum (location, "locationType",
		location_type_map, G_N_ELEMENTS (location_type_map),
		E_O365_LOCATION_NOT_SET,
		E_O365_LOCATION_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/phone?view=graph-rest-1.0 */

const gchar *
e_o365_phone_get_number (EO365Phone *phone)
{
	return e_o365_json_get_string_member (phone, "number", NULL);
}

EO365PhoneType
e_o365_phone_get_type (EO365Phone *phone)
{
	return o365_json_utils_get_json_as_enum (phone, "type",
		phone_map, G_N_ELEMENTS (phone_map),
		E_O365_PHONE_NOT_SET,
		E_O365_PHONE_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/onlinemeetinginfo?view=graph-rest-1.0 */

const gchar *
e_o365_online_meeting_info_get_conference_id (EO365OnlineMeetingInfo *meeting_info)
{
	return e_o365_json_get_string_member (meeting_info, "conferenceId", NULL);
}

const gchar *
e_o365_online_meeting_info_get_join_url (EO365OnlineMeetingInfo *meeting_info)
{
	return e_o365_json_get_string_member (meeting_info, "joinUrl", NULL);
}

JsonArray * /* EO365Phone * */
e_o365_online_meeting_info_get_phones (EO365OnlineMeetingInfo *meeting_info)
{
	return e_o365_json_get_array_member (meeting_info, "phones");
}

const gchar *
e_o365_online_meeting_info_get_quick_dial (EO365OnlineMeetingInfo *meeting_info)
{
	return e_o365_json_get_string_member (meeting_info, "quickDial", NULL);
}

JsonArray * /* gchar * */
e_o365_online_meeting_info_get_toll_free_numbers (EO365OnlineMeetingInfo *meeting_info)
{
	return e_o365_json_get_array_member (meeting_info, "tollFreeNumbers");
}

const gchar *
e_o365_online_meeting_info_get_toll_number (EO365OnlineMeetingInfo *meeting_info)
{
	return e_o365_json_get_string_member (meeting_info, "tollNumber", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/patternedrecurrence?view=graph-rest-1.0 */

EO365RecurrencePattern *
e_o365_patterned_recurrence_get_pattern (EO365PatternedRecurrence *patterned_recurrence)
{
	return e_o365_json_get_object_member (patterned_recurrence, "pattern");
}

void
e_o365_patterned_recurrence_begin_pattern (JsonBuilder *builder)
{
	e_o365_json_begin_object_member (builder, "pattern");
}

void
e_o365_patterned_recurrence_end_pattern (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

EO365RecurrenceRange *
e_o365_patterned_recurrence_get_range (EO365PatternedRecurrence *patterned_recurrence)
{
	return e_o365_json_get_object_member (patterned_recurrence, "range");
}

void
e_o365_patterned_recurrence_begin_range (JsonBuilder *builder)
{
	e_o365_json_begin_object_member (builder, "range");
}

void
e_o365_patterned_recurrence_end_range (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/recurrencepattern?view=graph-rest-1.0 */

EO365DayOfWeekType
e_o365_array_get_day_of_week_element (JsonArray *array, /* const gchar * representing EO365DayOfWeekType */
				      guint index)
{
	return o365_json_utils_json_value_as_enum (json_array_get_string_element (array, index),
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_O365_DAY_OF_WEEK_NOT_SET,
		E_O365_DAY_OF_WEEK_UNKNOWN);
}

gint
e_o365_recurrence_pattern_get_day_of_month (EO365RecurrencePattern *pattern)
{
	return e_o365_json_get_int_member (pattern, "dayOfMonth", 0);
}

void
e_o365_recurrence_pattern_add_day_of_month (JsonBuilder *builder,
					    gint value)
{
	e_o365_json_add_int_member (builder, "dayOfMonth", value);
}

JsonArray * /* const gchar * representing EO365DayOfWeekType, use e_o365_array_get_day_of_week_element() */
e_o365_recurrence_pattern_get_days_of_week (EO365RecurrencePattern *pattern)
{
	return e_o365_json_get_array_member (pattern, "daysOfWeek");
}

void
e_o365_recurrence_pattern_begin_days_of_week (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "daysOfWeek");
}

void
e_o365_recurrence_pattern_end_days_of_week (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_recurrence_pattern_add_day_of_week (JsonBuilder *builder,
					   EO365DayOfWeekType value)
{
	o365_json_utils_add_enum_as_json (builder, NULL, value,
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_O365_DAY_OF_WEEK_NOT_SET,
		E_O365_DAY_OF_WEEK_NOT_SET);
}

EO365DayOfWeekType
e_o365_recurrence_pattern_get_first_day_of_week (EO365RecurrencePattern *pattern)
{
	return o365_json_utils_get_json_as_enum (pattern, "firstDayOfWeek",
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_O365_DAY_OF_WEEK_NOT_SET,
		E_O365_DAY_OF_WEEK_UNKNOWN);
}

void
e_o365_recurrence_pattern_add_first_day_of_week (JsonBuilder *builder,
						 EO365DayOfWeekType value)
{
	o365_json_utils_add_enum_as_json (builder, "firstDayOfWeek", value,
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_O365_DAY_OF_WEEK_NOT_SET,
		E_O365_DAY_OF_WEEK_NOT_SET);
}

EO365WeekIndexType
e_o365_recurrence_pattern_get_index (EO365RecurrencePattern *pattern)
{
	return o365_json_utils_get_json_as_enum (pattern, "index",
		week_index_map, G_N_ELEMENTS (week_index_map),
		E_O365_WEEK_INDEX_NOT_SET,
		E_O365_WEEK_INDEX_UNKNOWN);
}

void
e_o365_recurrence_pattern_add_index (JsonBuilder *builder,
				     EO365WeekIndexType value)
{
	o365_json_utils_add_enum_as_json (builder, "index", value,
		week_index_map, G_N_ELEMENTS (week_index_map),
		E_O365_WEEK_INDEX_NOT_SET,
		E_O365_WEEK_INDEX_NOT_SET);
}

gint
e_o365_recurrence_pattern_get_interval (EO365RecurrencePattern *pattern)
{
	return e_o365_json_get_int_member (pattern, "interval", -1);
}

void
e_o365_recurrence_pattern_add_interval (JsonBuilder *builder,
					gint value)
{
	e_o365_json_add_int_member (builder, "interval", value);
}

gint
e_o365_recurrence_pattern_get_month (EO365RecurrencePattern *pattern)
{
	return e_o365_json_get_int_member (pattern, "month", -1);
}

void
e_o365_recurrence_pattern_add_month (JsonBuilder *builder,
				     gint value)
{
	e_o365_json_add_int_member (builder, "month", value);
}

EO365RecurrencePatternType
e_o365_recurrence_pattern_get_type (EO365RecurrencePattern *pattern)
{
	return o365_json_utils_get_json_as_enum (pattern, "type",
		recurrence_pattern_map, G_N_ELEMENTS (recurrence_pattern_map),
		E_O365_RECURRENCE_PATTERN_NOT_SET,
		E_O365_RECURRENCE_PATTERN_UNKNOWN);
}

void
e_o365_recurrence_pattern_add_type (JsonBuilder *builder,
				    EO365RecurrencePatternType value)
{
	o365_json_utils_add_enum_as_json (builder, "type", value,
		recurrence_pattern_map, G_N_ELEMENTS (recurrence_pattern_map),
		E_O365_RECURRENCE_PATTERN_NOT_SET,
		E_O365_RECURRENCE_PATTERN_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/recurrencerange?view=graph-rest-1.0 */

EO365Date
e_o365_recurrence_range_get_end_date (EO365RecurrenceRange *range)
{
	return e_o365_date_get (range, "endDate");
}

void
e_o365_recurrence_range_add_end_date (JsonBuilder *builder,
				      EO365Date value)
{
	e_o365_add_date (builder, "endDate", value);
}

gint
e_o365_recurrence_range_get_number_of_occurrences (EO365RecurrenceRange *range)
{
	return e_o365_json_get_int_member (range, "numberOfOccurrences", -1);
}

void
e_o365_recurrence_range_add_number_of_occurrences (JsonBuilder *builder,
						   gint value)
{
	e_o365_json_add_int_member (builder, "numberOfOccurrences", value);
}

const gchar *
e_o365_recurrence_range_get_recurrence_time_zone (EO365RecurrenceRange *range)
{
	return e_o365_json_get_string_member (range, "recurrenceTimeZone", NULL);
}

void
e_o365_recurrence_range_add_recurrence_time_zone (JsonBuilder *builder,
						  const gchar *value)
{
	e_o365_json_add_string_member (builder, "recurrenceTimeZone", value);
}

EO365Date
e_o365_recurrence_range_get_start_date (EO365RecurrenceRange *range)
{
	return e_o365_date_get (range, "startDate");
}

void
e_o365_recurrence_range_add_start_date (JsonBuilder *builder,
					EO365Date value)
{
	e_o365_add_date (builder, "startDate", value);
}

EO365RecurrenceRangeType
e_o365_recurrence_range_get_type (EO365RecurrenceRange *range)
{
	return o365_json_utils_get_json_as_enum (range, "type",
		recurrence_range_map, G_N_ELEMENTS (recurrence_range_map),
		E_O365_RECURRENCE_RANGE_NOT_SET,
		E_O365_RECURRENCE_RANGE_UNKNOWN);
}

void
e_o365_recurrence_range_add_type (JsonBuilder *builder,
				  EO365RecurrenceRangeType value)
{
	o365_json_utils_add_enum_as_json (builder, "type", value,
		recurrence_range_map, G_N_ELEMENTS (recurrence_range_map),
		E_O365_RECURRENCE_RANGE_NOT_SET,
		E_O365_RECURRENCE_RANGE_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0 */

const gchar *
e_o365_event_get_id (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "id", NULL);
}

const gchar *
e_o365_event_get_change_key (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "changeKey", NULL);
}

JsonArray * /* EO365Attendee * */
e_o365_event_get_attendees (EO365Event *event)
{
	return e_o365_json_get_array_member (event, "attendees");
}

void
e_o365_event_begin_attendees (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "attendees");
}

void
e_o365_event_end_attendees (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_event_add_attendee (JsonBuilder *builder,
			   EO365AttendeeType type,
			   EO365ResponseType response,
			   const gchar *name,
			   const gchar *address)
{
	e_o365_json_begin_object_member (builder, NULL);

	o365_json_utils_add_enum_as_json (builder, "type", type,
		attendee_map, G_N_ELEMENTS (attendee_map),
		E_O365_ATTENDEE_NOT_SET,
		E_O365_ATTENDEE_NOT_SET);

	e_o365_json_begin_object_member (builder, "status");

	o365_json_utils_add_enum_as_json (builder, "response", response,
		response_map, G_N_ELEMENTS (response_map),
		E_O365_RESPONSE_NOT_SET,
		E_O365_RESPONSE_UNKNOWN);

	e_o365_add_date_time_offset_member (builder, "time", time (NULL));

	e_o365_json_end_object_member (builder); /* status */

	if ((name && *name) || (address && *address))
		e_o365_add_email_address (builder, "emailAddress", name, address);

	e_o365_json_end_object_member (builder);
}

EO365ItemBody *
e_o365_event_get_body (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "body");
}

void
e_o365_event_add_body (JsonBuilder *builder,
		       EO365ItemBodyContentTypeType content_type,
		       const gchar *content)
{
	e_o365_add_item_body (builder, "body", content_type, content);
}

const gchar *
e_o365_event_get_body_preview (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "bodyPreview", NULL);
}

JsonArray * /* const gchar * */
e_o365_event_get_categories (EO365Event *event)
{
	return e_o365_json_get_array_member (event, "categories");
}

void
e_o365_event_begin_categories (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "categories");
}

void
e_o365_event_end_categories (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_event_add_category (JsonBuilder *builder,
			   const gchar *category)
{
	g_return_if_fail (category && *category);

	json_builder_add_string_value (builder, category);
}

time_t
e_o365_event_get_created_date_time (EO365Event *event)
{
	return e_o365_get_date_time_offset_member (event, "createdDateTime");
}

EO365DateTimeWithZone *
e_o365_event_get_end (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "end");
}

void
e_o365_event_add_end (JsonBuilder *builder,
		      time_t date_time,
		      const gchar *zone)
{
	e_o365_add_date_time (builder, "end", date_time, zone);
}

gboolean
e_o365_event_get_has_attachments (EO365Event *event)
{
	return e_o365_json_get_boolean_member (event, "hasAttachments", FALSE);
}

const gchar *
e_o365_event_get_ical_uid (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "iCalUId", NULL);
}

EO365ImportanceType
e_o365_event_get_importance (EO365Event *event)
{
	return o365_json_utils_get_json_as_enum (event, "importance",
		importance_map, G_N_ELEMENTS (importance_map),
		E_O365_IMPORTANCE_NOT_SET,
		E_O365_IMPORTANCE_UNKNOWN);
}

void
e_o365_event_add_importance (JsonBuilder *builder,
			     EO365ImportanceType value)
{
	o365_json_utils_add_enum_as_json (builder, "importance", value,
		importance_map, G_N_ELEMENTS (importance_map),
		E_O365_IMPORTANCE_NOT_SET,
		E_O365_IMPORTANCE_UNKNOWN);
}

gboolean
e_o365_event_get_is_all_day (EO365Event *event)
{
	return e_o365_json_get_boolean_member (event, "isAllDay", FALSE);
}

void
e_o365_event_add_is_all_day (JsonBuilder *builder,
			     gboolean value)
{
	e_o365_json_add_boolean_member (builder, "isAllDay", value);
}

gboolean
e_o365_event_get_is_cancelled (EO365Event *event)
{
	return e_o365_json_get_boolean_member (event, "isCancelled", FALSE);
}

gboolean
e_o365_event_get_is_online_meeting (EO365Event *event)
{
	return e_o365_json_get_boolean_member (event, "isOnlineMeeting", FALSE);
}

void
e_o365_event_add_is_online_meeting (JsonBuilder *builder,
				    gboolean value)
{
	e_o365_json_add_boolean_member (builder, "isOnlineMeeting", value);
}

gboolean
e_o365_event_get_is_organizer (EO365Event *event)
{
	return e_o365_json_get_boolean_member (event, "isOrganizer", FALSE);
}

gboolean
e_o365_event_get_is_reminder_on (EO365Event *event)
{
	return e_o365_json_get_boolean_member (event, "isReminderOn", FALSE);
}

void
e_o365_event_add_is_reminder_on (JsonBuilder *builder,
				 gboolean value)
{
	e_o365_json_add_boolean_member (builder, "isReminderOn", value);
}

time_t
e_o365_event_get_last_modified_date_time (EO365Event *event)
{
	return e_o365_get_date_time_offset_member (event, "lastModifiedDateTime");
}

EO365Location *
e_o365_event_get_location (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "location");
}

void
e_o365_event_begin_location (JsonBuilder *builder)
{
	e_o365_json_begin_object_member (builder, "location");
}

void
e_o365_event_end_location (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

JsonArray * /* EO365Location * */
e_o365_event_get_locations (EO365Event *event)
{
	return e_o365_json_get_array_member (event, "locations");
}

void
e_o365_event_begin_locations (JsonBuilder *builder)
{
	e_o365_json_begin_array_member (builder, "locations");
}

void
e_o365_event_end_locations (JsonBuilder *builder)
{
	e_o365_json_end_array_member (builder);
}

void
e_o365_event_begin_locations_location (JsonBuilder *builder)
{
	e_o365_json_begin_object_member (builder, NULL);
}

void
e_o365_event_end_locations_location (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

EO365OnlineMeetingInfo *
e_o365_event_get_online_meeting_info (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "onlineMeeting");
}

EO365OnlineMeetingProviderType
e_o365_event_get_online_meeting_provider (EO365Event *event)
{
	return o365_json_utils_get_json_as_enum (event, "onlineMeetingProvider",
		meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
		E_O365_ONLINE_MEETING_PROVIDER_NOT_SET,
		E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN);
}

void
e_o365_event_add_online_meeting_provider (JsonBuilder *builder,
					  EO365OnlineMeetingProviderType value)
{
	if (value == E_O365_ONLINE_MEETING_PROVIDER_NOT_SET) {
		e_o365_json_add_null_member (builder, "onlineMeetingProvider");
	} else {
		o365_json_utils_add_enum_as_json (builder, "onlineMeetingProvider", value,
			meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
			E_O365_ONLINE_MEETING_PROVIDER_NOT_SET,
			E_O365_ONLINE_MEETING_PROVIDER_UNKNOWN);
	}
}

const gchar *
e_o365_event_get_online_meeting_url (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "onlineMeetingUrl", NULL);
}

EO365Recipient *
e_o365_event_get_organizer (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "organizer");
}

void
e_o365_event_add_organizer (JsonBuilder *builder,
			    const gchar *name,
			    const gchar *address)
{
	e_o365_add_recipient (builder, "organizer", name, address);
}

const gchar *
e_o365_event_get_original_end_timezone (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "originalEndTimeZone", NULL);
}

time_t
e_o365_event_get_original_start (EO365Event *event)
{
	return e_o365_get_date_time_offset_member (event, "originalStart");
}

const gchar *
e_o365_event_get_original_start_timezone (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "originalStartTimeZone", NULL);
}

EO365PatternedRecurrence *
e_o365_event_get_recurrence (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "recurrence");
}

void
e_o365_event_begin_recurrence (JsonBuilder *builder)
{
	e_o365_json_begin_object_member (builder, "recurrence");
}

void
e_o365_event_end_recurrence (JsonBuilder *builder)
{
	e_o365_json_end_object_member (builder);
}

void
e_o365_event_add_null_recurrence (JsonBuilder *builder)
{
	e_o365_json_add_null_member (builder, "recurrence");
}

gint
e_o365_event_get_reminder_minutes_before_start (EO365Event *event)
{
	return e_o365_json_get_int_member (event, "reminderMinutesBeforeStart", -1);
}

void
e_o365_event_add_reminder_minutes_before_start (JsonBuilder *builder,
						gint value)
{
	e_o365_json_add_int_member (builder, "reminderMinutesBeforeStart", value);
}

gboolean
e_o365_event_get_response_requested (EO365Event *event)
{
	return e_o365_json_get_boolean_member (event, "responseRequested", FALSE);
}

void
e_o365_event_add_response_requested (JsonBuilder *builder,
				     gboolean value)
{
	e_o365_json_add_boolean_member (builder, "responseRequested", value);
}


EO365ResponseStatus *
e_o365_event_get_response_status (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "responseStatus");
}

EO365SensitivityType
e_o365_event_get_sensitivity (EO365Event *event)
{
	return o365_json_utils_get_json_as_enum (event, "sensitivity",
		sensitivity_map, G_N_ELEMENTS (sensitivity_map),
		E_O365_SENSITIVITY_NOT_SET,
		E_O365_SENSITIVITY_UNKNOWN);
}

void
e_o365_event_add_sensitivity (JsonBuilder *builder,
			      EO365SensitivityType value)
{
	o365_json_utils_add_enum_as_json (builder, "sensitivity", value,
		sensitivity_map, G_N_ELEMENTS (sensitivity_map),
		E_O365_SENSITIVITY_NOT_SET,
		E_O365_SENSITIVITY_UNKNOWN);
}

const gchar *
e_o365_event_get_series_master_id (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "seriesMasterId", NULL);
}

EO365FreeBusyStatusType
e_o365_event_get_show_as (EO365Event *event)
{
	return o365_json_utils_get_json_as_enum (event, "showAs",
		free_busy_status_map, G_N_ELEMENTS (free_busy_status_map),
		E_O365_FREE_BUSY_STATUS_NOT_SET,
		E_O365_FREE_BUSY_STATUS_UNKNOWN);
}

void
e_o365_event_add_show_as (JsonBuilder *builder,
			  EO365FreeBusyStatusType value)
{
	o365_json_utils_add_enum_as_json (builder, "showAs", value,
		free_busy_status_map, G_N_ELEMENTS (free_busy_status_map),
		E_O365_FREE_BUSY_STATUS_NOT_SET,
		E_O365_FREE_BUSY_STATUS_UNKNOWN);
}

EO365DateTimeWithZone *
e_o365_event_get_start (EO365Event *event)
{
	return e_o365_json_get_object_member (event, "start");
}

void
e_o365_event_add_start (JsonBuilder *builder,
			time_t date_time,
			const gchar *zone)
{
	e_o365_add_date_time (builder, "start", date_time, zone);
}

const gchar *
e_o365_event_get_subject (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "subject", NULL);
}

void
e_o365_event_add_subject (JsonBuilder *builder,
			  const gchar *value)
{
	e_o365_json_add_string_member (builder, "subject", value ? value : "");
}

EO365EventTypeType
e_o365_event_get_type (EO365Event *event)
{
	return o365_json_utils_get_json_as_enum (event, "type",
		event_type_map, G_N_ELEMENTS (event_type_map),
		E_O365_EVENT_TYPE_NOT_SET,
		E_O365_EVENT_TYPE_UNKNOWN);
}

const gchar *
e_o365_event_get_web_link (EO365Event *event)
{
	return e_o365_json_get_string_member (event, "webLink", NULL);
}
