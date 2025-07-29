/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <stdio.h>
#include <json-glib/json-glib.h>

#include "e-m365-json-utils.h"

typedef struct _MapData {
	const gchar *json_value;
	gint enum_value;
} MapData;

static gint
m365_json_utils_json_value_as_enum (const gchar *json_value,
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
m365_json_utils_get_json_as_enum (JsonObject *object,
				  const gchar *string_member_name,
				  const MapData *items,
				  guint n_items,
				  gint not_set_value,
				  gint unknown_value)
{
	return m365_json_utils_json_value_as_enum (e_m365_json_get_string_member (object, string_member_name, NULL),
		items, n_items, not_set_value, unknown_value);
}

static void
m365_json_utils_add_enum_as_json (JsonBuilder *builder,
				  const gchar *string_member_name,
				  gint enum_value,
				  const MapData *items,
				  guint n_items,
				  gint not_set_value,
				  gint default_value)
{
	const gchar *json_value = NULL, *default_value_str = NULL;
	guint ii;

	if (enum_value == not_set_value) {
		if (string_member_name)
			e_m365_json_add_null_member (builder, string_member_name);
		return;
	}

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
			e_m365_json_add_string_member (builder, string_member_name, json_value);
		else
			json_builder_add_string_value (builder, json_value ? json_value : "");
	}
}

static MapData attachment_data_type_map[] = {
	{ "#microsoft.graph.fileAttachment",		E_M365_ATTACHMENT_DATA_TYPE_FILE },
	{ "#microsoft.graph.itemAttachment",		E_M365_ATTACHMENT_DATA_TYPE_ITEM },
	{ "#microsoft.graph.referenceAttachment",	E_M365_ATTACHMENT_DATA_TYPE_REFERENCE }
};

static MapData attendee_map[] = {
	{ "required", E_M365_ATTENDEE_REQUIRED },
	{ "optional", E_M365_ATTENDEE_OPTIONAL },
	{ "resource", E_M365_ATTENDEE_RESOURCE }
};

static MapData automatic_replies_status_map[] = {
	{ "disabled", E_M365_AUTOMATIC_REPLIES_STATUS_DISABLED },
	{ "alwaysEnabled", E_M365_AUTOMATIC_REPLIES_STATUS_ALWAYS_ENABLED },
	{ "scheduled", E_M365_AUTOMATIC_REPLIES_STATUS_SCHEDULED }
};

static struct _color_map {
	const gchar *name;
	const gchar *rgb;
	EM365CalendarColorType value;
} color_map[] = {
	{ "auto",	NULL,		E_M365_CALENDAR_COLOR_AUTO },
	{ "lightBlue",	"#0099bc",	E_M365_CALENDAR_COLOR_LIGHT_BLUE },
	{ "lightGreen",	"#00cc6a",	E_M365_CALENDAR_COLOR_LIGHT_GREEN },
	{ "lightOrange","#f7630c",	E_M365_CALENDAR_COLOR_LIGHT_ORANGE },
	{ "lightGray",	"#69797e",	E_M365_CALENDAR_COLOR_LIGHT_GRAY },
	{ "lightYellow","#fde300",	E_M365_CALENDAR_COLOR_LIGHT_YELLOW },
	{ "lightTeal",	"#038387",	E_M365_CALENDAR_COLOR_LIGHT_TEAL },
	{ "lightPink",	"#bf0077",	E_M365_CALENDAR_COLOR_LIGHT_PINK },
	{ "lightBrown",	"#8e562e",	E_M365_CALENDAR_COLOR_LIGHT_BROWN },
	{ "lightRed",	"#a4262c",	E_M365_CALENDAR_COLOR_LIGHT_RED },
	{ "maxColor",	NULL,		E_M365_CALENDAR_COLOR_MAX_COLOR }
};

static MapData calendar_role_map[] = {
	{ "none",				E_M365_CALENDAR_PERMISSION_NONE },
	{ "freeBusyRead",			E_M365_CALENDAR_PERMISSION_FREE_BUSY_READ },
	{ "limitedRead",			E_M365_CALENDAR_PERMISSION_LIMITED_READ },
	{ "read",				E_M365_CALENDAR_PERMISSION_READ },
	{ "write",				E_M365_CALENDAR_PERMISSION_WRITE },
	{ "delegateWithoutPrivateEventAccess",	E_M365_CALENDAR_PERMISSION_DELEGATE_WITHOUT_PRIVATE_EVENT_ACCESS },
	{ "delegateWithPrivateEventAccess",	E_M365_CALENDAR_PERMISSION_DELEGATE_WITH_PRIVATE_EVENT_ACCESS },
	{ "custom",				E_M365_CALENDAR_PERMISSION_CUSTOM }
};

static MapData content_type_map[] = {
	{ "text", E_M365_ITEM_BODY_CONTENT_TYPE_TEXT },
	{ "html", E_M365_ITEM_BODY_CONTENT_TYPE_HTML }
};

static MapData day_of_week_map[] = {
	{ "sunday",	E_M365_DAY_OF_WEEK_SUNDAY },
	{ "monday",	E_M365_DAY_OF_WEEK_MONDAY },
	{ "tuesday",	E_M365_DAY_OF_WEEK_TUESDAY },
	{ "wednesday",	E_M365_DAY_OF_WEEK_WEDNESDAY },
	{ "thursday",	E_M365_DAY_OF_WEEK_THURSDAY },
	{ "friday",	E_M365_DAY_OF_WEEK_FRIDAY },
	{ "saturday",	E_M365_DAY_OF_WEEK_SATURDAY }
};

static MapData event_type_map[] = {
	{ "singleInstance",	E_M365_EVENT_TYPE_SINGLE_INSTANCE },
	{ "occurrence",		E_M365_EVENT_TYPE_OCCURRENCE },
	{ "exception",		E_M365_EVENT_TYPE_EXCEPTION },
	{ "seriesMaster",	E_M365_EVENT_TYPE_SERIES_MASTER }
};

static MapData external_audience_scope_map[] = {
	{ "none", E_M365_EXTERNAL_AUDIENCE_SCOPE_NONE },
	{ "contactsOnly", E_M365_EXTERNAL_AUDIENCE_SCOPE_CONTACTS_ONLY },
	{ "all", E_M365_EXTERNAL_AUDIENCE_SCOPE_ALL }
};

static MapData flag_status_map[] = {
	{ "notFlagged",	E_M365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED },
	{ "complete",	E_M365_FOLLOWUP_FLAG_STATUS_COMPLETE },
	{ "flagged",	E_M365_FOLLOWUP_FLAG_STATUS_FLAGGED }
};

static MapData free_busy_status_map[] = {
	{ "unknown",		E_M365_FREE_BUSY_STATUS_UNKNOWN },
	{ "free",		E_M365_FREE_BUSY_STATUS_FREE },
	{ "tentative",		E_M365_FREE_BUSY_STATUS_TENTATIVE },
	{ "busy",		E_M365_FREE_BUSY_STATUS_BUSY },
	{ "oof",		E_M365_FREE_BUSY_STATUS_OOF },
	{ "workingElsewhere",	E_M365_FREE_BUSY_STATUS_WORKING_ELSEWHERE }
};

static MapData importance_map[] = {
	{ "low",	E_M365_IMPORTANCE_LOW },
	{ "normal",	E_M365_IMPORTANCE_NORMAL },
	{ "high",	E_M365_IMPORTANCE_HIGH }
};

static MapData inference_classification_map[] = {
	{ "focused",	E_M365_INFERENCE_CLASSIFICATION_FOCUSED },
	{ "other",	E_M365_INFERENCE_CLASSIFICATION_OTHER }
};

static MapData location_type_map[] = {
	{ "default",		E_M365_LOCATION_DEFAULT },
	{ "conferenceRoom",	E_M365_LOCATION_CONFERENCE_ROOM },
	{ "homeAddress",	E_M365_LOCATION_HOME_ADDRESS },
	{ "businessAddress",	E_M365_LOCATION_BUSINESS_ADDRESS },
	{ "geoCoordinates",	E_M365_LOCATION_GEO_COORDINATES },
	{ "streetAddress",	E_M365_LOCATION_STREET_ADDRESS },
	{ "hotel",		E_M365_LOCATION_HOTEL },
	{ "restaurant",		E_M365_LOCATION_RESTAURANT },
	{ "localBusiness",	E_M365_LOCATION_LOCAL_BUSINESS },
	{ "postalAddress",	E_M365_LOCATION_POSTAL_ADDRESS }
};

static MapData meeting_provider_map[] = {
	{ "unknown",		E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN },
	{ "skypeForBusiness",	E_M365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_BUSINESS },
	{ "skypeForConsumer",	E_M365_ONLINE_MEETING_PROVIDER_SKYPE_FOR_CONSUMER },
	{ "teamsForBusiness",	E_M365_ONLINE_MEETING_PROVIDER_TEAMS_FOR_BUSINESS }
};

static MapData phone_map[] = {
	{ "home",	E_M365_PHONE_HOME },
	{ "business",	E_M365_PHONE_BUSINESS },
	{ "mobile",	E_M365_PHONE_MOBILE },
	{ "other",	E_M365_PHONE_OTHER },
	{ "assistant",	E_M365_PHONE_ASSISTANT },
	{ "homeFax",	E_M365_PHONE_HOMEFAX },
	{ "businessFax",E_M365_PHONE_BUSINESSFAX },
	{ "otherFax",	E_M365_PHONE_OTHERFAX },
	{ "pager",	E_M365_PHONE_PAGER },
	{ "radio",	E_M365_PHONE_RADIO }
};

static MapData recurrence_pattern_map[] = {
	{ "daily",		E_M365_RECURRENCE_PATTERN_DAILY },
	{ "weekly",		E_M365_RECURRENCE_PATTERN_WEEKLY },
	{ "absoluteMonthly",	E_M365_RECURRENCE_PATTERN_ABSOLUTE_MONTHLY },
	{ "relativeMonthly",	E_M365_RECURRENCE_PATTERN_RELATIVE_MONTHLY },
	{ "absoluteYearly",	E_M365_RECURRENCE_PATTERN_ABSOLUTE_YEARLY },
	{ "relativeYearly",	E_M365_RECURRENCE_PATTERN_RELATIVE_YEARLY }
};

static MapData recurrence_range_map[] = {
	{ "endDate",	E_M365_RECURRENCE_RANGE_ENDDATE },
	{ "noEnd",	E_M365_RECURRENCE_RANGE_NOEND },
	{ "numbered",	E_M365_RECURRENCE_RANGE_NUMBERED }
};

static MapData response_map[] = {
	{ "None",		E_M365_RESPONSE_NONE },
	{ "Organizer",		E_M365_RESPONSE_ORGANIZER },
	{ "TentativelyAccepted",E_M365_RESPONSE_TENTATIVELY_ACCEPTED },
	{ "Accepted",		E_M365_RESPONSE_ACCEPTED },
	{ "Declined",		E_M365_RESPONSE_DECLINED },
	{ "NotResponded",	E_M365_RESPONSE_NOT_RESPONDED }
};

static MapData sensitivity_map[] = {
	{ "normal",		E_M365_SENSITIVITY_NORMAL },
	{ "personal",		E_M365_SENSITIVITY_PERSONAL },
	{ "private",		E_M365_SENSITIVITY_PRIVATE },
	{ "confidential",	E_M365_SENSITIVITY_CONFIDENTIAL }
};

static MapData status_map[] = {
	{ "notStarted",		E_M365_STATUS_NOT_STARTED },
	{ "inProgress",		E_M365_STATUS_IN_PROGRESS },
	{ "completed",		E_M365_STATUS_COMPLETED },
	{ "waitingOnOthers",	E_M365_STATUS_WAITING_ON_OTHERS },
	{ "deferred",		E_M365_STATUS_DEFERRED }
};

static MapData week_index_map[] = {
	{ "first",	E_M365_WEEK_INDEX_FIRST },
	{ "second",	E_M365_WEEK_INDEX_SECOND },
	{ "third",	E_M365_WEEK_INDEX_THIRD },
	{ "fourth",	E_M365_WEEK_INDEX_FOURTH },
	{ "last",	E_M365_WEEK_INDEX_LAST }
};

static MapData task_list_kind_map[] = {
	{ "none",		E_M365_TASK_LIST_KIND_NONE },
	{ "defaultList",	E_M365_TASK_LIST_KIND_DEFAULT_LIST },
	{ "flaggedEmails",	E_M365_TASK_LIST_KIND_FLAGGED_EMAILS },
	{ "unknownFutureValue",	E_M365_TASK_LIST_KIND_UNKNOWN_FUTURE_VALUE }
};

static MapData website_type_map[] = {
	{ "other",	E_M365_WEBSITE_TYPE_OTHER },
	{ "home",	E_M365_WEBSITE_TYPE_HOME },
	{ "work",	E_M365_WEBSITE_TYPE_WORK },
	{ "blog",	E_M365_WEBSITE_TYPE_BLOG },
	{ "profile",	E_M365_WEBSITE_TYPE_PROFILE }
};

const gchar *
e_m365_calendar_color_to_rgb (EM365CalendarColorType color)
{
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (color == color_map[ii].value)
			return color_map[ii].rgb;
	}

	return NULL;
}

EM365CalendarColorType
e_m365_rgb_to_calendar_color (const gchar *rgb)
{
	EM365CalendarColorType res;
	gint ii, rr, gg, bb;
	gdouble distance, res_distance = -1.0;

	if (!rgb || !*rgb)
		return E_M365_CALENDAR_COLOR_NOT_SET;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (color_map[ii].rgb && g_ascii_strcasecmp (color_map[ii].rgb, rgb) == 0)
			return color_map[ii].value;
	}

	/* When exact match did not work, approximate to the closest */

	if (sscanf (rgb, "#%02x%02x%02x", &rr, &gg, &bb) != 3)
		return E_M365_CALENDAR_COLOR_UNKNOWN;

	distance = (rr * rr) + (gg * gg) + (bb * bb);
	res = E_M365_CALENDAR_COLOR_UNKNOWN;

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
e_m365_json_get_array_member (JsonObject *object,
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
e_m365_json_begin_array_member (JsonBuilder *builder,
				const gchar *member_name)
{
	if (member_name && *member_name)
		json_builder_set_member_name (builder, member_name);

	json_builder_begin_array (builder);
}

void
e_m365_json_end_array_member (JsonBuilder *builder)
{
	json_builder_end_array (builder);
}

gboolean
e_m365_json_get_boolean_member (JsonObject *object,
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
e_m365_json_add_boolean_member (JsonBuilder *builder,
				const gchar *member_name,
				gboolean value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_boolean_value (builder, value);
}

gdouble
e_m365_json_get_double_member (JsonObject *object,
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
e_m365_json_add_double_member (JsonBuilder *builder,
			       const gchar *member_name,
			       gdouble value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_double_value (builder, value);
}

gint64
e_m365_json_get_int_member (JsonObject *object,
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
e_m365_json_add_int_member (JsonBuilder *builder,
			    const gchar *member_name,
			    gint64 value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_int_value (builder, value);
}

gboolean
e_m365_json_get_null_member (JsonObject *object,
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
e_m365_json_add_null_member (JsonBuilder *builder,
			     const gchar *member_name)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_null_value (builder);
}

JsonObject *
e_m365_json_get_object_member (JsonObject *object,
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
e_m365_json_begin_object_member (JsonBuilder *builder,
				 const gchar *member_name)
{
	if (member_name && *member_name)
		json_builder_set_member_name (builder, member_name);

	json_builder_begin_object (builder);
}

void
e_m365_json_end_object_member (JsonBuilder *builder)
{
	json_builder_end_object (builder);
}

const gchar *
e_m365_json_get_string_member (JsonObject *object,
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
e_m365_json_add_string_member (JsonBuilder *builder,
			       const gchar *member_name,
			       const gchar *value)
{
	g_return_if_fail (member_name && *member_name);

	json_builder_set_member_name (builder, member_name);
	json_builder_add_string_value (builder, value ? value : "");
}

void
e_m365_json_add_nonempty_string_member (JsonBuilder *builder,
					const gchar *member_name,
					const gchar *value)
{
	g_return_if_fail (member_name && *member_name);

	if (value && *value)
		e_m365_json_add_string_member (builder, member_name, value);
}

void
e_m365_json_add_nonempty_or_null_string_member (JsonBuilder *builder,
						const gchar *member_name,
						const gchar *value)
{
	g_return_if_fail (member_name && *member_name);

	if (value && *value)
		e_m365_json_add_string_member (builder, member_name, value);
	else
		e_m365_json_add_null_member (builder, member_name);
}

static JsonObject * /* (transfer none) (nullable) */
e_m365_json_get_single_value_extended_property (JsonObject *object,
						const gchar *property_name)
{
	JsonArray *array;
	guint ii, len;

	if (!object || !property_name)
		return NULL;

	array = e_m365_json_get_array_member (object, "singleValueExtendedProperties");
	if (!array)
		return NULL;

	len = json_array_get_length (array);

	for (ii = 0; ii < len; ii++) {
		JsonObject *item = json_array_get_object_element (array, ii);
		const gchar *id;

		if (!item)
			break;

		id = e_m365_json_get_string_member (item, "id", NULL);
		if (!id)
			continue;

		if (g_ascii_strcasecmp (id, property_name) == 0)
			return item;
	}

	return NULL;
}

const gchar *
e_m365_json_get_string_single_value_extended_property (JsonObject *object,
						       const gchar *property_name)
{
	JsonObject *item;

	item = e_m365_json_get_single_value_extended_property (object, property_name);
	if (item)
		return e_m365_json_get_string_member (item, "value", NULL);

	return NULL;
}

gint64
e_m365_json_get_integer_single_value_extended_property (JsonObject *object,
							const gchar *property_name,
							gint64 default_value)
{
	JsonObject *item;

	item = e_m365_json_get_single_value_extended_property (object, property_name);
	if (item) {
		gint64 value;

		value = e_m365_json_get_int_member (item, "value", default_value);

		if (!value || value == default_value) {
			const gchar *str_value = e_m365_json_get_string_member (item, "value", NULL);

			if (str_value) {
				gchar *endptr = NULL;

				value = g_ascii_strtoll (str_value, &endptr, 10);
				if (!value && endptr == str_value)
					value = default_value;
			}
		}

		return value;
	}

	return default_value;
}

EM365Date
e_m365_date_get (JsonObject *object,
		 const gchar *member_name)
{
	const gchar *value;
	gint year = 0, month = 0, day = 0;

	value = e_m365_json_get_string_member (object, member_name, NULL);

	if (!value || !*value)
		return -1;

	if (sscanf (value, "%04d-%02d-%02d", &year, &month, &day) != 3) {
		g_warning ("%s: Failed to decode date '%s' of member '%s'", G_STRFUNC, value, member_name);
		return -1;
	}

	return e_m365_date_encode (year, month, day);
}

void
e_m365_add_date (JsonBuilder *builder,
		 const gchar *member_name,
		 EM365Date value)
{
	gint year, month, day;

	if (e_m365_date_decode (value, &year, &month, &day)) {
		gchar buff[128];

		g_snprintf (buff, sizeof (buff), "%04d-%02d-%02d", year, month, day);
		e_m365_json_add_string_member (builder, member_name, buff);
	}
}

gboolean
e_m365_date_decode (EM365Date dt,
		    gint *out_year,
		    gint *out_month,
		    gint *out_day)
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

EM365Date
e_m365_date_encode (gint year,
		    gint month,
		    gint day)
{
	g_return_val_if_fail (year > 0 && year < 10000, -1);
	g_return_val_if_fail (month >= 1 && month <= 12, -1);
	g_return_val_if_fail (day >= 1 && day <= 31, -1);

	return year + (10000 * month) + (1000000 * day);
}

EM365TimeOfDay
e_m365_time_of_day_get (JsonObject *object,
			const gchar *member_name)
{
	const gchar *value;
	gint hour = 0, minute = 0, second = 0, fraction = 0;

	value = e_m365_json_get_string_member (object, member_name, NULL);

	if (!value || !*value)
		return -1;

	if (sscanf (value, "%02d:%02d:%02d.%07d", &hour, &minute, &second, &fraction) != 4) {
		g_warning ("%s: Failed to decode timeOfDay '%s' of member '%s'", G_STRFUNC, value, member_name);
		return -1;
	}

	return e_m365_time_of_day_encode (hour, minute, second, fraction);
}

void
e_m365_add_time_of_day (JsonBuilder *builder,
			const gchar *member_name,
			EM365TimeOfDay value)
{
	gint hour, minute, second, fraction;

	if (e_m365_time_of_day_decode (value, &hour, &minute, &second, &fraction)) {
		gchar buff[128];

		g_snprintf (buff, sizeof (buff), "%02d:%02d:%02d.%07d", hour, minute, second, fraction);
		e_m365_json_add_string_member (builder, member_name, buff);
	}
}

gboolean
e_m365_time_of_day_decode (EM365TimeOfDay tod,
			   gint *out_hour,
			   gint *out_minute,
			   gint *out_second,
			   gint *out_fraction)
{
	g_return_val_if_fail (out_hour != NULL, FALSE);
	g_return_val_if_fail (out_minute != NULL, FALSE);
	g_return_val_if_fail (out_second != NULL, FALSE);
	g_return_val_if_fail (out_fraction != NULL, FALSE);

	if (tod <= 0)
		return FALSE;

	*out_hour = tod % 100;
	*out_minute = (tod / 100) % 100;
	*out_second = (tod / 10000) % 100;
	*out_fraction = tod / 1000000;

	return *out_hour >= 0 && *out_hour < 24 &&
		*out_minute >= 0 && *out_minute < 60 &&
		*out_second >= 0 && *out_second < 60;
}

EM365TimeOfDay
e_m365_time_of_day_encode (gint hour,
			   gint minute,
			   gint second,
			   gint fraction)
{
	g_return_val_if_fail (hour >= 0 && hour < 24, -1);
	g_return_val_if_fail (minute >= 0 && minute < 60, -1);
	g_return_val_if_fail (second >= 0 && second < 60, -1);
	g_return_val_if_fail (fraction >= 0 && fraction < 10000000, -1);

	return ((EM365TimeOfDay) hour) + (100L * minute) + (10000L * second) + (1000000L * fraction);
}

time_t
e_m365_get_date_time_offset_member (JsonObject *object,
				    const gchar *member_name,
				    gboolean *out_exists)
{
	const gchar *value;
	gboolean exists = FALSE;
	time_t res = (time_t) 0;

	value = e_m365_json_get_string_member (object, member_name, NULL);

	if (value) {
		GDateTime *dt;

		dt = g_date_time_new_from_iso8601 (value, NULL);

		if (!dt) {
			gint len = strlen (value);

			/* 2020-07-14T00:00:00.0000000 , possibly with 'Z' at the end */
			if (len == 27 && value[4] == '-' && value[7] == '-' && value[10] == 'T' && value[13] == ':' && value[16] == ':' && value[19] == '.') {
				gchar tmp[32];

				strncpy (tmp, value, 27);
				tmp[27] = 'Z';
				tmp[28] = '\0';

				dt = g_date_time_new_from_iso8601 (tmp, NULL);
			}
		}

		if (dt) {
			/* Ignore default/unset birthday "0001-01-01T08:00:00Z" */
			if (g_date_time_get_year (dt) > 1000) {
				res = (time_t) g_date_time_to_unix (dt);
				exists = TRUE;
			}
			g_date_time_unref (dt);
		}
	}

	if (out_exists)
		*out_exists = exists;

	return res;
}

static void
e_m365_add_date_time_offset_member_ex (JsonBuilder *builder,
				       const gchar *member_name,
				       time_t value,
				       gboolean with_utc_zone_char)
{
	GDateTime *dt;
	gchar *value_str;

	if (value <= (time_t) 0) {
		e_m365_json_add_null_member (builder, member_name);
		return;
	}

	dt = g_date_time_new_from_unix_utc (value);
	g_return_if_fail (dt != NULL);

	value_str = g_date_time_format_iso8601 (dt);

	if (value_str && !with_utc_zone_char) {
		gchar *z_pos;

		z_pos = strrchr (value_str, 'Z');

		if (z_pos)
			*z_pos = '\0';
	}

	e_m365_json_add_string_member (builder, member_name, value_str);

	g_date_time_unref (dt);
	g_free (value_str);
}

void
e_m365_add_date_time_offset_member (JsonBuilder *builder,
				    const gchar *member_name,
				    time_t value)
{
	e_m365_add_date_time_offset_member_ex (builder, member_name, value, TRUE);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/datetimetimezone?view=graph-rest-1.0 */

time_t
e_m365_date_time_get_date_time (EM365DateTimeWithZone *datetime)
{
	return e_m365_get_date_time_offset_member (datetime, "dateTime", NULL);
}

const gchar *
e_m365_date_time_get_time_zone (EM365DateTimeWithZone *datetime)
{
	return e_m365_json_get_string_member (datetime, "timeZone", NULL);
}

void
e_m365_add_date_time (JsonBuilder *builder,
		      const gchar *member_name,
		      time_t date_time,
		      const gchar *zone)
{
	g_return_if_fail (member_name != NULL);

	if (date_time <= (time_t) 0) {
		e_m365_json_add_null_member (builder, member_name);
		return;
	}

	e_m365_json_begin_object_member (builder, member_name);

	e_m365_add_date_time_offset_member_ex (builder, "dateTime", date_time, FALSE);
	e_m365_json_add_string_member (builder, "timeZone", (zone && *zone) ? zone : "UTC");

	e_m365_json_end_object_member (builder);
}

/* https://docs.microsoft.com/en-us/graph/delta-query-overview */

gboolean
e_m365_delta_is_removed_object (JsonObject *object)
{
	return json_object_has_member (object, "@removed");
}

/* https://docs.microsoft.com/en-us/graph/api/resources/outlookcategory?view=graph-rest-1.0 */

const gchar *
e_m365_category_get_display_name (EM365Category *category)
{
	return e_m365_json_get_string_member (category, "displayName", NULL);
}

const gchar *
e_m365_category_get_id (EM365Category *category)
{
	return e_m365_json_get_string_member (category, "id", NULL);
}

const gchar *
e_m365_category_get_color (EM365Category *category)
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

	color_str = e_m365_json_get_string_member (category, "color", NULL);

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
e_m365_folder_get_id (EM365Folder *folder)
{
	return e_m365_json_get_string_member (folder, "id", NULL);
}

const gchar *
e_m365_folder_get_parent_folder_id (EM365Folder *folder)
{
	return e_m365_json_get_string_member (folder, "parentFolderId", NULL);
}

const gchar *
e_m365_folder_get_display_name (EM365Folder *folder)
{
	return e_m365_json_get_string_member (folder, "displayName", NULL);
}

gint32
e_m365_mail_folder_get_child_folder_count (EM365MailFolder *folder)
{
	return (gint32) e_m365_json_get_int_member (folder, "childFolderCount", 0);
}

gint32
e_m365_mail_folder_get_total_item_count (EM365MailFolder *folder)
{
	return (gint32) e_m365_json_get_int_member (folder, "totalItemCount", 0);
}

gint32
e_m365_mail_folder_get_unread_item_count (EM365MailFolder *folder)
{
	return (gint32) e_m365_json_get_int_member (folder, "unreadItemCount", 0);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/recipient?view=graph-rest-1.0
   https://docs.microsoft.com/en-us/graph/api/resources/emailaddress?view=graph-rest-1.0
 */
const gchar *
e_m365_recipient_get_name (EM365Recipient *recipient)
{
	JsonObject *email_address;

	email_address = e_m365_json_get_object_member (recipient, "emailAddress");

	if (!email_address)
		return NULL;

	return e_m365_json_get_string_member (email_address, "name", NULL);
}

const gchar *
e_m365_recipient_get_address (EM365Recipient *recipient)
{
	JsonObject *email_address;

	email_address = e_m365_json_get_object_member (recipient, "emailAddress");

	if (!email_address)
		return NULL;

	return e_m365_json_get_string_member (email_address, "address", NULL);
}

void
e_m365_add_recipient (JsonBuilder *builder,
		      const gchar *member_name,
		      const gchar *name,
		      const gchar *address)
{
	g_return_if_fail ((name && *name) || (address && *address));

	e_m365_json_begin_object_member (builder, member_name);
	e_m365_json_begin_object_member (builder, "emailAddress");

	e_m365_json_add_nonempty_string_member (builder, "name", name);
	e_m365_json_add_nonempty_string_member (builder, "address", address);

	e_m365_json_end_object_member (builder); /* emailAddress */
	e_m365_json_end_object_member (builder); /* member_name */
}

/* https://docs.microsoft.com/en-us/graph/api/resources/internetmessageheader?view=graph-rest-1.0 */

const gchar *
e_m365_internet_message_header_get_name (EM365InternetMessageHeader *header)
{
	return e_m365_json_get_string_member (header, "name", NULL);
}

const gchar *
e_m365_internet_message_header_get_value (EM365InternetMessageHeader *header)
{
	return e_m365_json_get_string_member (header, "value", NULL);
}

void
e_m365_add_internet_message_header (JsonBuilder *builder,
				    const gchar *name,
				    const gchar *value)
{
	g_return_if_fail (name && *name);
	g_return_if_fail (value);

	json_builder_begin_object (builder);

	if (value && (*value == ' ' || *value == '\t'))
		value++;

	e_m365_json_add_string_member (builder, "name", name);
	e_m365_json_add_string_member (builder, "value", value);

	json_builder_end_object (builder);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/followupflag?view=graph-rest-1.0 */

EM365DateTimeWithZone *
e_m365_followup_flag_get_completed_date_time (EM365FollowupFlag *flag)
{
	return e_m365_json_get_object_member (flag, "completedDateTime");
}

void
e_m365_followup_flag_add_completed_date_time (JsonBuilder *builder,
					      time_t date_time,
					      const gchar *zone)
{
	e_m365_add_date_time (builder, "completedDateTime", date_time, zone);
}

EM365DateTimeWithZone *
e_m365_followup_flag_get_due_date_time (EM365FollowupFlag *flag)
{
	return e_m365_json_get_object_member (flag, "dueDateTime");
}

void
e_m365_followup_flag_add_due_date_time (JsonBuilder *builder,
					time_t date_time,
					const gchar *zone)
{
	e_m365_add_date_time (builder, "dueDateTime", date_time, zone);
}

EM365FollowupFlagStatusType
e_m365_followup_flag_get_flag_status (EM365FollowupFlag *flag)
{
	return m365_json_utils_get_json_as_enum (flag, "flagStatus",
		flag_status_map, G_N_ELEMENTS (flag_status_map),
		E_M365_FOLLOWUP_FLAG_STATUS_NOT_SET,
		E_M365_FOLLOWUP_FLAG_STATUS_UNKNOWN);
}

void
e_m365_followup_flag_add_flag_status (JsonBuilder *builder,
				      EM365FollowupFlagStatusType status)
{
	m365_json_utils_add_enum_as_json (builder, "flagStatus", status,
		flag_status_map, G_N_ELEMENTS (flag_status_map),
		E_M365_FOLLOWUP_FLAG_STATUS_NOT_SET,
		E_M365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED);
}

EM365DateTimeWithZone *
e_m365_followup_flag_get_start_date_time (EM365FollowupFlag *flag)
{
	return e_m365_json_get_object_member (flag, "startDateTime");
}

void
e_m365_followup_flag_add_start_date_time (JsonBuilder *builder,
					  time_t date_time,
					  const gchar *zone)
{
	e_m365_add_date_time (builder, "startDateTime", date_time, zone);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/itembody?view=graph-rest-1.0 */

const gchar *
e_m365_item_body_get_content (EM365ItemBody *item_body)
{
	return e_m365_json_get_string_member (item_body, "content", NULL);
}

EM365ItemBodyContentTypeType
e_m365_item_body_get_content_type (EM365ItemBody *item_body)
{
	return m365_json_utils_get_json_as_enum (item_body, "contentType",
		content_type_map, G_N_ELEMENTS (content_type_map),
		E_M365_ITEM_BODY_CONTENT_TYPE_NOT_SET,
		E_M365_ITEM_BODY_CONTENT_TYPE_UNKNOWN);
}

void
e_m365_add_item_body (JsonBuilder *builder,
		      const gchar *member_name,
		      EM365ItemBodyContentTypeType content_type,
		      const gchar *content)
{
	g_return_if_fail (member_name != NULL);

	if (content_type == E_M365_ITEM_BODY_CONTENT_TYPE_NOT_SET || !content) {
		e_m365_json_add_null_member (builder, member_name);
		return;
	}

	e_m365_json_begin_object_member (builder, member_name);

	m365_json_utils_add_enum_as_json (builder, "contentType", content_type,
		content_type_map, G_N_ELEMENTS (content_type_map),
		E_M365_ITEM_BODY_CONTENT_TYPE_NOT_SET,
		E_M365_ITEM_BODY_CONTENT_TYPE_TEXT);

	e_m365_json_add_string_member (builder, "content", content);

	e_m365_json_end_object_member (builder);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/message?view=graph-rest-1.0 */

JsonArray * /* EM365Recipient * */
e_m365_mail_message_get_bcc_recipients (EM365MailMessage *mail)
{
	return e_m365_json_get_array_member (mail, "bccRecipients");
}

void
e_m365_mail_message_begin_bcc_recipients (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "bccRecipients");
}

void
e_m365_mail_message_end_bcc_recipients (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

EM365ItemBody *
e_m365_mail_message_get_body (EM365MailMessage *mail)
{
	return e_m365_json_get_object_member (mail, "body");
}

void
e_m365_mail_message_add_body (JsonBuilder *builder,
			      EM365ItemBodyContentTypeType content_type,
			      const gchar *content)
{
	e_m365_add_item_body (builder, "body", content_type, content);
}

const gchar *
e_m365_mail_message_get_body_preview (EM365MailMessage *mail)
{
	return e_m365_json_get_string_member (mail, "bodyPreview", NULL);
}

JsonArray * /* const gchar * */
e_m365_mail_message_get_categories (EM365MailMessage *mail)
{
	return e_m365_json_get_array_member (mail, "categories");
}

void
e_m365_mail_message_begin_categories (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "categories");
}

void
e_m365_mail_message_end_categories (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_mail_message_add_category (JsonBuilder *builder,
				  const gchar *category)
{
	g_return_if_fail (category && *category);

	json_builder_add_string_value (builder, category);
}

JsonArray * /* EM365Recipient * */
e_m365_mail_message_get_cc_recipients (EM365MailMessage *mail)
{
	return e_m365_json_get_array_member (mail, "ccRecipients");
}

void
e_m365_mail_message_begin_cc_recipients (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "ccRecipients");
}

void
e_m365_mail_message_end_cc_recipients (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

const gchar *
e_m365_mail_message_get_change_key (EM365MailMessage *mail)
{
	return e_m365_json_get_string_member (mail, "changeKey", NULL);
}

const gchar *
e_m365_mail_message_get_conversation_id (EM365MailMessage *mail)
{
	return e_m365_json_get_string_member (mail, "conversationId", NULL);
}

JsonObject * /* Edm.Binary */
e_m365_mail_message_get_conversation_index (EM365MailMessage *mail)
{
	return e_m365_json_get_object_member (mail, "conversationIndex");
}

time_t
e_m365_mail_message_get_created_date_time (EM365MailMessage *mail)
{
	return e_m365_get_date_time_offset_member (mail, "createdDateTime", NULL);
}

EM365FollowupFlag *
e_m365_mail_message_get_flag (EM365MailMessage *mail)
{
	return e_m365_json_get_object_member (mail, "flag");
}

void
e_m365_mail_message_begin_flag (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "flag");
}

void
e_m365_mail_message_end_flag (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

EM365Recipient *
e_m365_mail_message_get_from (EM365MailMessage *mail)
{
	return e_m365_json_get_object_member (mail, "from");
}

void
e_m365_mail_message_add_from (JsonBuilder *builder,
			      const gchar *name,
			      const gchar *address)
{
	e_m365_add_recipient (builder, "from", name, address);
}

gboolean
e_m365_mail_message_get_has_attachments	(EM365MailMessage *mail)
{
	return e_m365_json_get_boolean_member (mail, "hasAttachments", FALSE);
}

const gchar *
e_m365_mail_message_get_id (EM365MailMessage *mail)
{
	return e_m365_json_get_string_member (mail, "id", NULL);
}

EM365ImportanceType
e_m365_mail_message_get_importance (EM365MailMessage *mail)
{
	return m365_json_utils_get_json_as_enum (mail, "importance",
		importance_map, G_N_ELEMENTS (importance_map),
		E_M365_IMPORTANCE_NOT_SET,
		E_M365_IMPORTANCE_UNKNOWN);
}

void
e_m365_mail_message_add_importance (JsonBuilder *builder,
				    EM365ImportanceType importance)
{
	m365_json_utils_add_enum_as_json (builder, "importance", importance,
		importance_map, G_N_ELEMENTS (importance_map),
		E_M365_IMPORTANCE_NOT_SET,
		E_M365_IMPORTANCE_NOT_SET);
}

EM365InferenceClassificationType
e_m365_mail_message_get_inference_classification (EM365MailMessage *mail)
{
	return m365_json_utils_get_json_as_enum (mail, "inferenceClassification",
		inference_classification_map, G_N_ELEMENTS (inference_classification_map),
		E_M365_INFERENCE_CLASSIFICATION_NOT_SET,
		E_M365_INFERENCE_CLASSIFICATION_UNKNOWN);
}

JsonArray * /* EM365InternetMessageHeader * */
e_m365_mail_message_get_internet_message_headers (EM365MailMessage *mail)
{
	return e_m365_json_get_array_member (mail, "internetMessageHeaders");
}

void
e_m365_mail_message_begin_internet_message_headers (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "internetMessageHeaders");
}

void
e_m365_mail_message_end_internet_message_headers (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

const gchar *
e_m365_mail_message_get_internet_message_id (EM365MailMessage *mail)
{
	return e_m365_json_get_string_member (mail, "internetMessageId", NULL);
}

void
e_m365_mail_message_add_internet_message_id (JsonBuilder *builder,
					     const gchar *message_id)
{
	e_m365_json_add_nonempty_string_member (builder, "internetMessageId", message_id);
}

gboolean
e_m365_mail_message_get_is_delivery_receipt_requested (EM365MailMessage *mail)
{
	return e_m365_json_get_boolean_member (mail, "isDeliveryReceiptRequested", FALSE);
}

void
e_m365_mail_message_add_is_delivery_receipt_requested (JsonBuilder *builder,
						       gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isDeliveryReceiptRequested", value);
}

gboolean
e_m365_mail_message_get_is_draft (EM365MailMessage *mail)
{
	return e_m365_json_get_boolean_member (mail, "isDraft", FALSE);
}

gboolean
e_m365_mail_message_get_is_read (EM365MailMessage *mail)
{
	return e_m365_json_get_boolean_member (mail, "isRead", FALSE);
}

void
e_m365_mail_message_add_is_read (JsonBuilder *builder,
				 gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isRead", value);
}

gboolean
e_m365_mail_message_get_is_read_receipt_requested (EM365MailMessage *mail)
{
	return e_m365_json_get_boolean_member (mail, "isReadReceiptRequested", FALSE);
}

void
e_m365_mail_message_add_is_read_receipt_requested (JsonBuilder *builder,
						   gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isReadReceiptRequested", value);
}

time_t
e_m365_mail_message_get_last_modified_date_time (EM365MailMessage *mail)
{
	return e_m365_get_date_time_offset_member (mail, "lastModifiedDateTime", NULL);
}

const gchar *
e_m365_mail_message_get_parent_folder_id (EM365MailMessage *mail)
{
		return e_m365_json_get_string_member (mail, "parentFolderId", NULL);
}

time_t
e_m365_mail_message_get_received_date_time (EM365MailMessage *mail)
{
	return e_m365_get_date_time_offset_member (mail, "receivedDateTime", NULL);
}

void
e_m365_mail_message_add_received_date_time (JsonBuilder *builder,
					    time_t value)
{
	e_m365_add_date_time_offset_member (builder, "receivedDateTime", value);
}

JsonArray * /* EM365Recipient * */
e_m365_mail_message_get_reply_to (EM365MailMessage *mail)
{
	return e_m365_json_get_array_member (mail, "replyTo");
}

void
e_m365_mail_message_begin_reply_to (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "replyTo");
}

void
e_m365_mail_message_end_reply_to (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

EM365Recipient *
e_m365_mail_message_get_sender (EM365MailMessage *mail)
{
	return e_m365_json_get_object_member (mail, "sender");
}

void
e_m365_mail_message_add_sender (JsonBuilder *builder,
				const gchar *name,
				const gchar *address)
{
	g_return_if_fail ((name && *name) || (address && *address));

	e_m365_add_recipient (builder, "sender", name, address);
}

time_t
e_m365_mail_message_get_sent_date_time (EM365MailMessage *mail)
{
	return e_m365_get_date_time_offset_member (mail, "sentDateTime", NULL);
}

void
e_m365_mail_message_add_sent_date_time (JsonBuilder *builder,
					time_t value)
{
	e_m365_add_date_time_offset_member (builder, "sentDateTime", value);
}

const gchar *
e_m365_mail_message_get_subject (EM365MailMessage *mail)
{
	return e_m365_json_get_string_member (mail, "subject", NULL);
}

void
e_m365_mail_message_add_subject (JsonBuilder *builder,
				 const gchar *subject)
{
	e_m365_json_add_nonempty_string_member (builder, "subject", subject);
}

JsonArray * /* EM365Recipient * */
e_m365_mail_message_get_to_recipients (EM365MailMessage *mail)
{
	return e_m365_json_get_array_member (mail, "toRecipients");
}

void
e_m365_mail_message_begin_to_recipients (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "toRecipients");
}

void
e_m365_mail_message_end_to_recipients (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

EM365ItemBody *
e_m365_mail_message_get_unique_body (EM365MailMessage *mail)
{
	return e_m365_json_get_object_member (mail, "uniqueBody");
}

const gchar *
e_m365_mail_message_get_web_link (EM365MailMessage *mail)
{
	return e_m365_json_get_string_member (mail, "webLink", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/attachment?view=graph-rest-1.0 */

EM365AttachmentDataType
e_m365_attachment_get_data_type (EM365Attachment *attachment)
{
	return m365_json_utils_get_json_as_enum (attachment, "@odata.type",
		attachment_data_type_map, G_N_ELEMENTS (attachment_data_type_map),
		E_M365_ATTACHMENT_DATA_TYPE_NOT_SET,
		E_M365_ATTACHMENT_DATA_TYPE_UNKNOWN);
}

void
e_m365_attachment_begin_attachment (JsonBuilder *builder,
				    EM365AttachmentDataType data_type)
{
	e_m365_json_begin_object_member (builder, NULL);

	m365_json_utils_add_enum_as_json (builder, "@odata.type", data_type,
		attachment_data_type_map, G_N_ELEMENTS (attachment_data_type_map),
		E_M365_ATTACHMENT_DATA_TYPE_NOT_SET,
		E_M365_ATTACHMENT_DATA_TYPE_FILE);
}

void
e_m365_attachment_end_attachment (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

const gchar *
e_m365_attachment_get_content_type (EM365Attachment *attachment)
{
	return e_m365_json_get_string_member (attachment, "contentType", NULL);
}

void
e_m365_attachment_add_content_type (JsonBuilder *builder,
				    const gchar *value)
{
	e_m365_json_add_string_member (builder, "contentType", value);
}

const gchar *
e_m365_attachment_get_id (EM365Attachment *attachment)
{
	return e_m365_json_get_string_member (attachment, "id", NULL);
}

gboolean
e_m365_attachment_get_is_inline (EM365Attachment *attachment)
{
	return e_m365_json_get_boolean_member (attachment, "isInline", FALSE);
}

void
e_m365_attachment_add_is_inline (JsonBuilder *builder,
				 gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isInline", value);
}

time_t
e_m365_attachment_get_last_modified_date_time (EM365Attachment *attachment)
{
	return e_m365_get_date_time_offset_member (attachment, "lastModifiedDateTime", NULL);
}

void
e_m365_attachment_add_last_modified_date_time (JsonBuilder *builder,
					       time_t value)
{
	e_m365_add_date_time_offset_member (builder, "lastModifiedDateTime", value);
}

const gchar *
e_m365_attachment_get_name (EM365Attachment *attachment)
{
	return e_m365_json_get_string_member (attachment, "name", NULL);
}

void
e_m365_attachment_add_name (JsonBuilder *builder,
			    const gchar *value)
{
	e_m365_json_add_string_member (builder, "name", value);
}

gint32
e_m365_attachment_get_size (EM365Attachment *attachment)
{
	return (gint32) e_m365_json_get_int_member (attachment, "size", -1);
}

void
e_m365_attachment_add_size (JsonBuilder *builder,
			    gint32 value)
{
	e_m365_json_add_int_member (builder, "size", value);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/fileattachment?view=graph-rest-1.0 */

const gchar * /* base64-encoded */
e_m365_file_attachment_get_content_bytes (EM365Attachment *attachment)
{
	return e_m365_json_get_string_member (attachment, "contentBytes", NULL);
}

void
e_m365_file_attachment_add_content_bytes (JsonBuilder *builder,
					  const gchar *base64_value)
{
	e_m365_json_add_string_member (builder, "contentBytes", base64_value);
}

const gchar *
e_m365_file_attachment_get_content_id (EM365Attachment *attachment)
{
	return e_m365_json_get_string_member (attachment, "contentId", NULL);
}

void
e_m365_file_attachment_add_content_id (JsonBuilder *builder,
				       const gchar *value)
{
	e_m365_json_add_string_member (builder, "contentId", value);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/emailaddress?view=graph-rest-1.0 */

const gchar *
e_m365_email_address_get_name (EM365EmailAddress *email)
{
	return e_m365_json_get_string_member (email, "name", NULL);
}

const gchar *
e_m365_email_address_get_address (EM365EmailAddress *email)
{
	return e_m365_json_get_string_member (email, "address", NULL);
}

void
e_m365_add_email_address (JsonBuilder *builder,
			  const gchar *member_name,
			  const gchar *name,
			  const gchar *address)
{
	g_return_if_fail ((name && *name) || (address && *address));

	e_m365_json_begin_object_member (builder, member_name);

	e_m365_json_add_nonempty_string_member (builder, "name", name);
	e_m365_json_add_nonempty_string_member (builder, "address", address);

	e_m365_json_end_object_member (builder); /* member_name */
}

/* https://docs.microsoft.com/en-us/graph/api/resources/physicaladdress?view=graph-rest-1.0 */

const gchar *
e_m365_physical_address_get_city (EM365PhysicalAddress *address)
{
	return e_m365_json_get_string_member (address, "city", NULL);
}

const gchar *
e_m365_physical_address_get_country_or_region (EM365PhysicalAddress *address)
{
	return e_m365_json_get_string_member (address, "countryOrRegion", NULL);
}

const gchar *
e_m365_physical_address_get_postal_code (EM365PhysicalAddress *address)
{
	return e_m365_json_get_string_member (address, "postalCode", NULL);
}

const gchar *
e_m365_physical_address_get_state (EM365PhysicalAddress *address)
{
	return e_m365_json_get_string_member (address, "state", NULL);
}

const gchar *
e_m365_physical_address_get_street (EM365PhysicalAddress *address)
{
	return e_m365_json_get_string_member (address, "street", NULL);
}

void
e_m365_add_physical_address (JsonBuilder *builder,
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
		e_m365_json_begin_object_member (builder, member_name);
		e_m365_json_add_nonempty_string_member (builder, "city", city);
		e_m365_json_add_nonempty_string_member (builder, "countryOrRegion", country_or_region);
		e_m365_json_add_nonempty_string_member (builder, "postalCode", postal_code);
		e_m365_json_add_nonempty_string_member (builder, "state", state);
		e_m365_json_add_nonempty_string_member (builder, "street", street);
		e_m365_json_end_object_member (builder);
	} else {
		e_m365_json_begin_object_member (builder, member_name);
		e_m365_json_end_object_member (builder);
	}
}

/* https://docs.microsoft.com/en-us/graph/api/resources/contact?view=graph-rest-1.0 */

const gchar *
e_m365_contact_get_id (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "id", NULL);
}

const gchar *
e_m365_contact_get_parent_folder_id (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "parentFolderId", NULL);
}

const gchar *
e_m365_contact_get_change_key (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "changeKey", NULL);
}

time_t
e_m365_contact_get_created_date_time (EM365Contact *contact)
{
	return e_m365_get_date_time_offset_member (contact, "createdDateTime", NULL);
}

time_t
e_m365_contact_get_last_modified_date_time (EM365Contact *contact)
{
	return e_m365_get_date_time_offset_member (contact, "lastModifiedDateTime", NULL);
}

const gchar *
e_m365_contact_get_assistant_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "assistantName", NULL);
}

void
e_m365_contact_add_assistant_name (JsonBuilder *builder,
				   const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "assistantName", value);
}

time_t
e_m365_contact_get_birthday (EM365Contact *contact,
			     gboolean *out_exists)
{
	return e_m365_get_date_time_offset_member (contact, "birthday", out_exists);
}

void
e_m365_contact_add_birthday (JsonBuilder *builder,
			     time_t value)
{
	e_m365_add_date_time_offset_member (builder, "birthday", value);
}

EM365PhysicalAddress *
e_m365_contact_get_business_address (EM365Contact *contact)
{
	return e_m365_json_get_object_member (contact, "businessAddress");
}

void
e_m365_contact_add_business_address (JsonBuilder *builder,
				     const gchar *city,
				     const gchar *country_or_region,
				     const gchar *postal_code,
				     const gchar *state,
				     const gchar *street)
{
	e_m365_add_physical_address (builder, "businessAddress", city, country_or_region, postal_code, state, street);
}

const gchar *
e_m365_contact_get_business_home_page (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "businessHomePage", NULL);
}

void
e_m365_contact_add_business_home_page (JsonBuilder *builder,
				       const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "businessHomePage", value);
}

JsonArray * /* const gchar * */
e_m365_contact_get_business_phones (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "businessPhones");
}

void
e_m365_contact_begin_business_phones (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "businessPhones");
}

void
e_m365_contact_end_business_phones (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_contact_add_business_phone (JsonBuilder *builder,
				   const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

JsonArray * /* const gchar * */
e_m365_contact_get_categories (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "categories");
}

void
e_m365_contact_begin_categories (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "categories");
}

void
e_m365_contact_end_categories (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_contact_add_category (JsonBuilder *builder,
			     const gchar *category)
{
	g_return_if_fail (category && *category);

	json_builder_add_string_value (builder, category);
}

JsonArray * /* const gchar * */
e_m365_contact_get_children (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "children");
}

void
e_m365_contact_begin_children (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "children");
}

void
e_m365_contact_end_children (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_contact_add_child (JsonBuilder *builder,
			  const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

const gchar *
e_m365_contact_get_company_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "companyName", NULL);
}

void
e_m365_contact_add_company_name (JsonBuilder *builder,
				 const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "companyName", value);
}

const gchar *
e_m365_contact_get_department (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "department", NULL);
}

void
e_m365_contact_add_department (JsonBuilder *builder,
			       const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "department", value);
}

const gchar *
e_m365_contact_get_display_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "displayName", NULL);
}

void
e_m365_contact_add_display_name (JsonBuilder *builder,
				 const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "displayName", value);
}

JsonArray * /* EM365EmailAddress * */
e_m365_contact_get_email_addresses (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "emailAddresses");
}

void
e_m365_contact_begin_email_addresses (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "emailAddresses");
}

void
e_m365_contact_end_email_addresses (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

const gchar *
e_m365_contact_get_file_as (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "fileAs", NULL);
}

void
e_m365_contact_add_file_as (JsonBuilder *builder,
			    const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "fileAs", value);
}

const gchar *
e_m365_contact_get_generation (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "generation", NULL);
}

void
e_m365_contact_add_generation (JsonBuilder *builder,
			       const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "generation", value);
}

const gchar *
e_m365_contact_get_given_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "givenName", NULL);
}

void
e_m365_contact_add_given_name (JsonBuilder *builder,
			       const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "givenName", value);
}

EM365PhysicalAddress *
e_m365_contact_get_home_address (EM365Contact *contact)
{
	return e_m365_json_get_object_member (contact, "homeAddress");
}

void
e_m365_contact_add_home_address (JsonBuilder *builder,
				 const gchar *city,
				 const gchar *country_or_region,
				 const gchar *postal_code,
				 const gchar *state,
				 const gchar *street)
{
	e_m365_add_physical_address (builder, "homeAddress", city, country_or_region, postal_code, state, street);
}

JsonArray * /* const gchar * */
e_m365_contact_get_home_phones (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "homePhones");
}

void
e_m365_contact_begin_home_phones (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "homePhones");
}

void
e_m365_contact_end_home_phones (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_contact_add_home_phone (JsonBuilder *builder,
			       const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

JsonArray * /* const gchar * */
e_m365_contact_get_im_addresses (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "imAddresses");
}

void
e_m365_contact_begin_im_addresses (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "imAddresses");
}

void
e_m365_contact_end_im_addresses (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_contact_add_im_address (JsonBuilder *builder,
			       const gchar *value)
{
	g_return_if_fail (value && *value);

	json_builder_add_string_value (builder, value);
}

const gchar *
e_m365_contact_get_initials (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "initials", NULL);
}

void
e_m365_contact_add_initials (JsonBuilder *builder,
			     const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "initials", value);
}

const gchar *
e_m365_contact_get_job_title (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "jobTitle", NULL);
}

void
e_m365_contact_add_job_title (JsonBuilder *builder,
			      const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "jobTitle", value);
}

const gchar *
e_m365_contact_get_manager (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "manager", NULL);
}

void
e_m365_contact_add_manager (JsonBuilder *builder,
			    const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "manager", value);
}

const gchar *
e_m365_contact_get_middle_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "middleName", NULL);
}

void
e_m365_contact_add_middle_name (JsonBuilder *builder,
				const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "middleName", value);
}

const gchar *
e_m365_contact_get_mobile_phone (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "mobilePhone", NULL);
}

void
e_m365_contact_add_mobile_phone (JsonBuilder *builder,
				 const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "mobilePhone", value);
}

const gchar *
e_m365_contact_get_nick_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "nickName", NULL);
}

void
e_m365_contact_add_nick_name (JsonBuilder *builder,
			      const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "nickName", value);
}

const gchar *
e_m365_contact_get_office_location (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "officeLocation", NULL);
}

void
e_m365_contact_add_office_location (JsonBuilder *builder,
				    const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "officeLocation", value);
}

EM365PhysicalAddress *
e_m365_contact_get_other_address (EM365Contact *contact)
{
	return e_m365_json_get_object_member (contact, "otherAddress");
}

void
e_m365_contact_add_other_address (JsonBuilder *builder,
				  const gchar *city,
				  const gchar *country_or_region,
				  const gchar *postal_code,
				  const gchar *state,
				  const gchar *street)
{
	e_m365_add_physical_address (builder, "otherAddress", city, country_or_region, postal_code, state, street);
}

const gchar *
e_m365_contact_get_personal_notes (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "personalNotes", NULL);
}

void
e_m365_contact_add_personal_notes (JsonBuilder *builder,
				   const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "personalNotes", value);
}

const gchar *
e_m365_contact_get_profession (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "profession", NULL);
}

void
e_m365_contact_add_profession (JsonBuilder *builder,
			       const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "profession", value);
}

const gchar *
e_m365_contact_get_spouse_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "spouseName", NULL);
}

void
e_m365_contact_add_spouse_name (JsonBuilder *builder,
				const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "spouseName", value);
}

const gchar *
e_m365_contact_get_surname (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "surname", NULL);
}

void
e_m365_contact_add_surname (JsonBuilder *builder,
			    const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "surname", value);
}

const gchar *
e_m365_contact_get_title (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "title", NULL);
}

void
e_m365_contact_add_title (JsonBuilder *builder,
			  const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "title", value);
}

const gchar *
e_m365_contact_get_yomi_company_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "yomiCompanyName", NULL);
}

void
e_m365_contact_add_yomi_company_name (JsonBuilder *builder,
				      const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "yomiCompanyName", value);
}

const gchar *
e_m365_contact_get_yomi_given_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "yomiGivenName", NULL);
}

void
e_m365_contact_add_yomi_given_name (JsonBuilder *builder,
				    const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "yomiGivenName", value);
}

const gchar *
e_m365_contact_get_yomi_surname (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "yomiSurname", NULL);
}

void
e_m365_contact_add_yomi_surname (JsonBuilder *builder,
				 const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "yomiSurname", value);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/orgcontact?view=graph-rest-1.0
   only properties added on top of the EM365Contact */

JsonArray * /* EM365PhysicalAddress * */
e_m365_contact_org_get_addresses (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "addresses");
}

const gchar *
e_m365_contact_org_get_mail (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "mail", NULL);
}

const gchar *
e_m365_contact_org_get_mail_nickname (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "mailNickname", NULL);
}

JsonArray * /* EM365Phone * */
e_m365_contact_org_get_phones (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "phones");
}

JsonArray * /* const gchar * */
e_m365_contact_org_get_proxy_addresses (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "proxyAddresses");
}

/* https://learn.microsoft.com/en-us/graph/api/resources/user?view=graph-rest-1.0
   only properties added on top of the EM365Contact */

const gchar *
e_m365_contact_user_get_about_me (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "aboutMe", NULL);
}

const gchar *
e_m365_contact_user_get_city (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "city", NULL);
}

const gchar *
e_m365_contact_user_get_country (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "country", NULL);
}

const gchar *
e_m365_contact_user_get_fax_number (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "faxNumber", NULL);
}

const gchar *
e_m365_contact_user_get_mail (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "mail", NULL);
}

const gchar *
e_m365_contact_user_get_mail_nickname (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "mailNickname", NULL);
}

const gchar *
e_m365_contact_user_get_mobile_phone (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "mobilePhone", NULL);
}

const gchar *
e_m365_contact_user_get_my_site (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "mySite", NULL);
}

JsonArray * /* const gchar * */
e_m365_contact_user_get_other_mails (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "otherMails");
}

const gchar *
e_m365_contact_user_get_postal_code (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "postalCode", NULL);
}

JsonArray * /* const gchar * */
e_m365_contact_user_get_proxy_addresses (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "proxyAddresses");
}

const gchar *
e_m365_contact_user_get_state (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "state", NULL);
}

const gchar *
e_m365_contact_user_get_street_address (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "streetAddress", NULL);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/person?view=graph-rest-1.0 */
const gchar *
e_m365_contact_person_get_im_address (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "imAddress", NULL);
}

gboolean
e_m365_contact_person_get_is_favorite (EM365Contact *contact)
{
	return e_m365_json_get_boolean_member (contact, "isFavorite", FALSE);
}

const gchar *
e_m365_contact_person_get_type_class (EM365Contact *contact)
{
	JsonObject *obj;

	obj = e_m365_json_get_object_member (contact, "personType");
	if (!obj)
		return NULL;

	/* https://learn.microsoft.com/en-us/graph/api/resources/persontype?view=graph-rest-1.0 */
	return e_m365_json_get_string_member (obj, "class", NULL);
}

const gchar *
e_m365_contact_person_get_type_subclass (EM365Contact *contact)
{
	JsonObject *obj;

	obj = e_m365_json_get_object_member (contact, "personType");
	if (!obj)
		return NULL;

	/* https://learn.microsoft.com/en-us/graph/api/resources/persontype?view=graph-rest-1.0 */
	return e_m365_json_get_string_member (obj, "subclass", NULL);
}

JsonArray * /* EM365Phone * */
e_m365_contact_person_get_phones (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "phones");
}

JsonArray * /* EM365Location * */
e_m365_contact_person_get_postal_addresses (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "postalAddresses");
}

JsonArray * /* EM365ScoredEmailAddress * */
e_m365_contact_person_get_scored_email_addresses (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "scoredEmailAddresses");
}

const gchar *
e_m365_contact_person_get_user_principal_name (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "userPrincipalName", NULL);
}

JsonArray * /* EM365Website * */
e_m365_contact_person_get_websites (EM365Contact *contact)
{
	return e_m365_json_get_array_member (contact, "websites");
}

const gchar *
e_m365_contact_person_get_yomi_company (EM365Contact *contact)
{
	return e_m365_json_get_string_member (contact, "yomiCompany", NULL);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/scoredemailaddress?view=graph-rest-1.0 */

const gchar *
e_m365_scored_email_address_get_address (EM365ScoredEmailAddress *scored_email_address)
{
	return e_m365_json_get_string_member (scored_email_address, "address", NULL);
}

gdouble
e_m365_scored_email_address_get_relevance_score (EM365ScoredEmailAddress *scored_email_address)
{
	return e_m365_json_get_double_member (scored_email_address, "relevanceScore", -1.0);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/website?view=graph-rest-1.0 */

const gchar *
e_m365_website_get_address (EM365Website *website)
{
	return e_m365_json_get_string_member (website, "address", NULL);
}

const gchar *
e_m365_website_get_display_name (EM365Website *website)
{
	return e_m365_json_get_string_member (website, "displayName", NULL);
}

EM365WebsiteType
e_m365_website_get_type (EM365Website *website)
{
	return m365_json_utils_get_json_as_enum (website, "type",
		website_type_map, G_N_ELEMENTS (website_type_map),
		E_M365_WEBSITE_TYPE_NOT_SET,
		E_M365_WEBSITE_TYPE_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/calendargroup?view=graph-rest-1.0 */

const gchar *
e_m365_calendar_group_get_id (EM365CalendarGroup *group)
{
	return e_m365_json_get_string_member (group, "id", NULL);
}

const gchar *
e_m365_calendar_group_get_change_key (EM365CalendarGroup *group)
{
	return e_m365_json_get_string_member (group, "changeKey", NULL);
}

const gchar *
e_m365_calendar_group_get_class_id (EM365CalendarGroup *group)
{
	return e_m365_json_get_string_member (group, "classId", NULL);
}

const gchar *
e_m365_calendar_group_get_name (EM365CalendarGroup *group)
{
	return e_m365_json_get_string_member (group, "name", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/calendar?view=graph-rest-1.0 */

const gchar *
e_m365_calendar_get_id (EM365Calendar *calendar)
{
	return e_m365_json_get_string_member (calendar, "id", NULL);
}

const gchar *
e_m365_calendar_get_change_key (EM365Calendar *calendar)
{
	return e_m365_json_get_string_member (calendar, "changeKey", NULL);
}

gboolean
e_m365_calendar_get_can_edit (EM365Calendar *calendar)
{
	return e_m365_json_get_boolean_member (calendar, "canEdit", FALSE);
}

gboolean
e_m365_calendar_get_can_share (EM365Calendar *calendar)
{
	return e_m365_json_get_boolean_member (calendar, "canShare", FALSE);
}

gboolean
e_m365_calendar_get_can_view_private_items (EM365Calendar *calendar)
{
	return e_m365_json_get_boolean_member (calendar, "canViewPrivateItems", FALSE);
}

gboolean
e_m365_calendar_get_is_removable (EM365Calendar *calendar)
{
	return e_m365_json_get_boolean_member (calendar, "isRemovable", FALSE);
}

gboolean
e_m365_calendar_get_is_tallying_responses (EM365Calendar *calendar)
{
	return e_m365_json_get_boolean_member (calendar, "isTallyingResponses", FALSE);
}

EM365EmailAddress *
e_m365_calendar_get_owner (EM365Calendar *calendar)
{
	return e_m365_json_get_object_member (calendar, "owner");
}

const gchar *
e_m365_calendar_get_name (EM365Calendar *calendar)
{
	return e_m365_json_get_string_member (calendar, "name", NULL);
}

void
e_m365_calendar_add_name (JsonBuilder *builder,
			  const gchar *name)
{
	e_m365_json_add_nonempty_string_member (builder, "name", name);
}

guint32 /* bit-or of EM365OnlineMeetingProviderType */
e_m365_calendar_get_allowed_online_meeting_providers (EM365Calendar *calendar)
{
	guint32 providers = E_M365_ONLINE_MEETING_PROVIDER_NOT_SET;
	JsonArray *array;

	array = e_m365_json_get_array_member (calendar, "allowedOnlineMeetingProviders");

	if (array) {
		guint ii, len;

		providers = E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN;

		len = json_array_get_length (array);

		for (ii = 0; ii < len; ii++) {
			const gchar *str = json_array_get_string_element (array, ii);
			gint enum_value;

			if (!str)
				continue;

			enum_value = m365_json_utils_json_value_as_enum (str,
				meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
				E_M365_ONLINE_MEETING_PROVIDER_NOT_SET,
				E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN);

			if (enum_value != E_M365_ONLINE_MEETING_PROVIDER_NOT_SET)
				providers |= enum_value;
		}
	}

	return providers;
}

void
e_m365_calendar_add_allowed_online_meeting_providers (JsonBuilder *builder,
						      guint providers) /* bit-or of EM365OnlineMeetingProviderType */
{
	gint ii;

	if (providers == E_M365_ONLINE_MEETING_PROVIDER_NOT_SET)
		return;

	e_m365_json_begin_array_member (builder, "allowedOnlineMeetingProviders");

	if (providers == E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN)
		json_builder_add_string_value (builder, "unknown");

	for (ii = 0; ii < G_N_ELEMENTS (meeting_provider_map); ii++) {
		if ((providers & meeting_provider_map[ii].enum_value) != 0)
			json_builder_add_string_value (builder, meeting_provider_map[ii].json_value);
	}

	e_m365_json_end_array_member (builder);
}

EM365CalendarColorType
e_m365_calendar_get_color (EM365Calendar *calendar)
{
	const gchar *color;
	gint ii;

	color = e_m365_json_get_string_member (calendar, "color", NULL);

	if (!color)
		return E_M365_CALENDAR_COLOR_NOT_SET;

	for (ii = 0; ii < G_N_ELEMENTS (color_map); ii++) {
		if (g_ascii_strcasecmp (color_map[ii].name, color) == 0)
			return color_map[ii].value;
	}

	return E_M365_CALENDAR_COLOR_UNKNOWN;
}

void
e_m365_calendar_add_color (JsonBuilder *builder,
			   EM365CalendarColorType color)
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
		e_m365_json_add_string_member (builder, "color", name);
}

const gchar *
e_m365_calendar_get_hex_color (EM365Calendar *calendar)
{
	return e_m365_json_get_string_member (calendar, "hexColor", NULL);
}

EM365OnlineMeetingProviderType
e_m365_calendar_get_default_online_meeting_provider (EM365Calendar *calendar)
{
	return m365_json_utils_get_json_as_enum (calendar, "defaultOnlineMeetingProvider",
		meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
		E_M365_ONLINE_MEETING_PROVIDER_NOT_SET,
		E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN);
}

void
e_m365_calendar_add_default_online_meeting_provider (JsonBuilder *builder,
						     EM365OnlineMeetingProviderType provider)
{
	m365_json_utils_add_enum_as_json (builder, "defaultOnlineMeetingProvider", provider,
		meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
		E_M365_ONLINE_MEETING_PROVIDER_NOT_SET,
		E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/calendarpermission?view=graph-rest-1.0 */

guint32
e_m365_calendar_permission_get_allowed_roles (EM365CalendarPermission *permission)
{
	guint32 allowed_roles = E_M365_CALENDAR_PERMISSION_NOT_SET;
	JsonArray *array;

	array = e_m365_json_get_array_member (permission, "allowedRoles");

	if (array) {
		guint ii, len;

		allowed_roles = E_M365_CALENDAR_PERMISSION_UNKNOWN;

		len = json_array_get_length (array);

		for (ii = 0; ii < len; ii++) {
			const gchar *str = json_array_get_string_element (array, ii);
			gint enum_value;

			if (!str)
				continue;

			enum_value = m365_json_utils_json_value_as_enum (str,
				calendar_role_map, G_N_ELEMENTS (calendar_role_map),
				E_M365_CALENDAR_PERMISSION_NOT_SET,
				E_M365_CALENDAR_PERMISSION_UNKNOWN);

			if (enum_value != E_M365_CALENDAR_PERMISSION_NOT_SET)
				allowed_roles |= enum_value;
		}
	}

	return allowed_roles;
}

EM365EmailAddress *
e_m365_calendar_permission_get_email_address (EM365CalendarPermission *permission)
{
	return e_m365_json_get_object_member (permission, "emailAddress");
}

void
e_m365_calendar_permission_add_email_address (JsonBuilder *builder,
					      const gchar *name,
					      const gchar *address)
{
	e_m365_add_email_address (builder, "emailAddress", name, address);
}

const gchar *
e_m365_calendar_permission_get_id (EM365CalendarPermission *permission)
{
	return e_m365_json_get_string_member (permission, "id", NULL);
}

gboolean
e_m365_calendar_permission_get_is_inside_organization (EM365CalendarPermission *permission)
{
	return e_m365_json_get_boolean_member (permission, "isInsideOrganization", FALSE);
}

void
e_m365_calendar_permission_add_is_inside_organization (JsonBuilder *builder,
						       gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isInsideOrganization", value);
}

gboolean
e_m365_calendar_permission_get_is_removable (EM365CalendarPermission *permission)
{
	return e_m365_json_get_boolean_member (permission, "isRemovable", FALSE);
}

void
e_m365_calendar_permission_add_is_removable (JsonBuilder *builder,
					     gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isRemovable", value);
}

EM365CalendarPermissionType
e_m365_calendar_permission_get_role (EM365CalendarPermission *permission)
{
	return m365_json_utils_get_json_as_enum (permission, "role",
		calendar_role_map, G_N_ELEMENTS (calendar_role_map),
		E_M365_CALENDAR_PERMISSION_NOT_SET,
		E_M365_CALENDAR_PERMISSION_UNKNOWN);
}

void
e_m365_calendar_permission_add_role (JsonBuilder *builder,
				     EM365CalendarPermissionType value)
{
	m365_json_utils_add_enum_as_json (builder, "role", value,
		calendar_role_map, G_N_ELEMENTS (calendar_role_map),
		E_M365_CALENDAR_PERMISSION_NOT_SET,
		E_M365_CALENDAR_PERMISSION_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/responsestatus?view=graph-rest-1.0 */

EM365ResponseType
e_m365_response_status_get_response (EM365ResponseStatus *response_status)
{
	return m365_json_utils_get_json_as_enum (response_status, "response",
		response_map, G_N_ELEMENTS (response_map),
		E_M365_RESPONSE_NOT_SET,
		E_M365_RESPONSE_UNKNOWN);
}

time_t
e_m365_response_status_get_time (EM365ResponseStatus *response_status)
{
	return e_m365_get_date_time_offset_member (response_status, "time", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/attendee?view=graph-rest-1.0 */

EM365ResponseStatus *
e_m365_attendee_get_status (EM365Attendee *attendee)
{
	return e_m365_json_get_object_member (attendee, "status");
}

EM365AttendeeType
e_m365_attendee_get_type (EM365Attendee *attendee)
{
	return m365_json_utils_get_json_as_enum (attendee, "type",
		attendee_map, G_N_ELEMENTS (attendee_map),
		E_M365_ATTENDEE_NOT_SET,
		E_M365_ATTENDEE_UNKNOWN);
}

EM365EmailAddress *
e_m365_attendee_get_email_address (EM365Attendee *attendee)
{
	return e_m365_json_get_object_member (attendee, "emailAddress");
}

/* https://docs.microsoft.com/en-us/graph/api/resources/outlookgeocoordinates?view=graph-rest-1.0 */

gdouble
e_m365_outlook_geo_coordinates_get_accuracy (EM365OutlookGeoCoordinates *coords)
{
	return e_m365_json_get_double_member (coords, "accuracy", 0.0);
}

void
e_m365_outlook_geo_coordinates_add_accuracy (JsonBuilder *builder,
					     gdouble value)
{
	e_m365_json_add_double_member (builder, "accuracy", value);
}

gdouble
e_m365_outlook_geo_coordinates_get_altitude (EM365OutlookGeoCoordinates *coords)
{
	return e_m365_json_get_double_member (coords, "altitude", 0.0);
}

void
e_m365_outlook_geo_coordinates_add_altitude (JsonBuilder *builder,
					     gdouble value)
{
	e_m365_json_add_double_member (builder, "altitude", value);
}

gdouble
e_m365_outlook_geo_coordinates_get_altitude_accuracy (EM365OutlookGeoCoordinates *coords)
{
	return e_m365_json_get_double_member (coords, "altitudeAccuracy", 0.0);
}

void
e_m365_outlook_geo_coordinates_add_altitude_accuracy (JsonBuilder *builder,
						      gdouble value)
{
	e_m365_json_add_double_member (builder, "altitudeAccuracy", value);
}

gdouble
e_m365_outlook_geo_coordinates_get_latitude (EM365OutlookGeoCoordinates *coords)
{
	return e_m365_json_get_double_member (coords, "latitude", 0.0);
}

void
e_m365_outlook_geo_coordinates_add_latitude (JsonBuilder *builder,
					     gdouble value)
{
	e_m365_json_add_double_member (builder, "latitude", value);
}

gdouble
e_m365_outlook_geo_coordinates_get_longitude (EM365OutlookGeoCoordinates *coords)
{
	return e_m365_json_get_double_member (coords, "longitude", 0.0);
}

void
e_m365_outlook_geo_coordinates_add_longitude (JsonBuilder *builder,
					      gdouble value)
{
	e_m365_json_add_double_member (builder, "longitude", value);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/location?view=graph-rest-1.0 */

EM365PhysicalAddress *
e_m365_location_get_address (EM365Location *location)
{
	return e_m365_json_get_object_member (location, "address");
}

void
e_m365_location_add_address (JsonBuilder *builder,
			     const gchar *city,
			     const gchar *country_or_region,
			     const gchar *postal_code,
			     const gchar *state,
			     const gchar *street)
{
	e_m365_add_physical_address (builder, "address", city, country_or_region, postal_code, state, street);
}

EM365OutlookGeoCoordinates *
e_m365_location_get_coordinates (EM365Location *location)
{
	return e_m365_json_get_object_member (location, "coordinates");
}

void
e_m365_location_begin_coordinates (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "coordinates");
}

void
e_m365_location_end_coordinates (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

const gchar *
e_m365_location_get_display_name (EM365Location *location)
{
	return e_m365_json_get_string_member (location, "displayName", NULL);
}

void
e_m365_location_add_display_name (JsonBuilder *builder,
				  const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "displayName", value);
}

const gchar *
e_m365_location_get_email_address (EM365Location *location)
{
	return e_m365_json_get_string_member (location, "locationEmailAddress", NULL);
}

void
e_m365_location_add_email_address (JsonBuilder *builder,
				   const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "locationEmailAddress", value);
}

const gchar *
e_m365_location_get_uri (EM365Location *location)
{
	return e_m365_json_get_string_member (location, "locationUri", NULL);
}

void
e_m365_location_add_uri (JsonBuilder *builder,
			 const gchar *value)
{
	e_m365_json_add_nonempty_or_null_string_member (builder, "locationUri", value);
}

EM365LocationType
e_m365_location_get_type (EM365Location *location)
{
	return m365_json_utils_get_json_as_enum (location, "locationType",
		location_type_map, G_N_ELEMENTS (location_type_map),
		E_M365_LOCATION_NOT_SET,
		E_M365_LOCATION_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/phone?view=graph-rest-1.0 */

const gchar *
e_m365_phone_get_number (EM365Phone *phone)
{
	return e_m365_json_get_string_member (phone, "number", NULL);
}

EM365PhoneType
e_m365_phone_get_type (EM365Phone *phone)
{
	return m365_json_utils_get_json_as_enum (phone, "type",
		phone_map, G_N_ELEMENTS (phone_map),
		E_M365_PHONE_NOT_SET,
		E_M365_PHONE_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/onlinemeetinginfo?view=graph-rest-1.0 */

const gchar *
e_m365_online_meeting_info_get_conference_id (EM365OnlineMeetingInfo *meeting_info)
{
	return e_m365_json_get_string_member (meeting_info, "conferenceId", NULL);
}

const gchar *
e_m365_online_meeting_info_get_join_url (EM365OnlineMeetingInfo *meeting_info)
{
	return e_m365_json_get_string_member (meeting_info, "joinUrl", NULL);
}

JsonArray * /* EM365Phone * */
e_m365_online_meeting_info_get_phones (EM365OnlineMeetingInfo *meeting_info)
{
	return e_m365_json_get_array_member (meeting_info, "phones");
}

const gchar *
e_m365_online_meeting_info_get_quick_dial (EM365OnlineMeetingInfo *meeting_info)
{
	return e_m365_json_get_string_member (meeting_info, "quickDial", NULL);
}

JsonArray * /* gchar * */
e_m365_online_meeting_info_get_toll_free_numbers (EM365OnlineMeetingInfo *meeting_info)
{
	return e_m365_json_get_array_member (meeting_info, "tollFreeNumbers");
}

const gchar *
e_m365_online_meeting_info_get_toll_number (EM365OnlineMeetingInfo *meeting_info)
{
	return e_m365_json_get_string_member (meeting_info, "tollNumber", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/patternedrecurrence?view=graph-rest-1.0 */

EM365RecurrencePattern *
e_m365_patterned_recurrence_get_pattern (EM365PatternedRecurrence *patterned_recurrence)
{
	return e_m365_json_get_object_member (patterned_recurrence, "pattern");
}

void
e_m365_patterned_recurrence_begin_pattern (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "pattern");
}

void
e_m365_patterned_recurrence_end_pattern (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

EM365RecurrenceRange *
e_m365_patterned_recurrence_get_range (EM365PatternedRecurrence *patterned_recurrence)
{
	return e_m365_json_get_object_member (patterned_recurrence, "range");
}

void
e_m365_patterned_recurrence_begin_range (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "range");
}

void
e_m365_patterned_recurrence_end_range (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/recurrencepattern?view=graph-rest-1.0 */

EM365DayOfWeekType
e_m365_array_get_day_of_week_element (JsonArray *array, /* const gchar * representing EM365DayOfWeekType */
				      guint index)
{
	return m365_json_utils_json_value_as_enum (json_array_get_string_element (array, index),
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_M365_DAY_OF_WEEK_NOT_SET,
		E_M365_DAY_OF_WEEK_UNKNOWN);
}

gint
e_m365_recurrence_pattern_get_day_of_month (EM365RecurrencePattern *pattern)
{
	return e_m365_json_get_int_member (pattern, "dayOfMonth", 0);
}

void
e_m365_recurrence_pattern_add_day_of_month (JsonBuilder *builder,
					    gint value)
{
	e_m365_json_add_int_member (builder, "dayOfMonth", value);
}

JsonArray * /* const gchar * representing EM365DayOfWeekType, use e_m365_array_get_day_of_week_element() */
e_m365_recurrence_pattern_get_days_of_week (EM365RecurrencePattern *pattern)
{
	return e_m365_json_get_array_member (pattern, "daysOfWeek");
}

void
e_m365_recurrence_pattern_begin_days_of_week (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "daysOfWeek");
}

void
e_m365_recurrence_pattern_end_days_of_week (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_recurrence_pattern_add_day_of_week (JsonBuilder *builder,
					   EM365DayOfWeekType value)
{
	m365_json_utils_add_enum_as_json (builder, NULL, value,
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_M365_DAY_OF_WEEK_NOT_SET,
		E_M365_DAY_OF_WEEK_NOT_SET);
}

EM365DayOfWeekType
e_m365_recurrence_pattern_get_first_day_of_week (EM365RecurrencePattern *pattern)
{
	return m365_json_utils_get_json_as_enum (pattern, "firstDayOfWeek",
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_M365_DAY_OF_WEEK_NOT_SET,
		E_M365_DAY_OF_WEEK_UNKNOWN);
}

void
e_m365_recurrence_pattern_add_first_day_of_week (JsonBuilder *builder,
						 EM365DayOfWeekType value)
{
	m365_json_utils_add_enum_as_json (builder, "firstDayOfWeek", value,
		day_of_week_map, G_N_ELEMENTS (day_of_week_map),
		E_M365_DAY_OF_WEEK_NOT_SET,
		E_M365_DAY_OF_WEEK_NOT_SET);
}

EM365WeekIndexType
e_m365_recurrence_pattern_get_index (EM365RecurrencePattern *pattern)
{
	return m365_json_utils_get_json_as_enum (pattern, "index",
		week_index_map, G_N_ELEMENTS (week_index_map),
		E_M365_WEEK_INDEX_NOT_SET,
		E_M365_WEEK_INDEX_UNKNOWN);
}

void
e_m365_recurrence_pattern_add_index (JsonBuilder *builder,
				     EM365WeekIndexType value)
{
	m365_json_utils_add_enum_as_json (builder, "index", value,
		week_index_map, G_N_ELEMENTS (week_index_map),
		E_M365_WEEK_INDEX_NOT_SET,
		E_M365_WEEK_INDEX_NOT_SET);
}

gint
e_m365_recurrence_pattern_get_interval (EM365RecurrencePattern *pattern)
{
	return e_m365_json_get_int_member (pattern, "interval", -1);
}

void
e_m365_recurrence_pattern_add_interval (JsonBuilder *builder,
					gint value)
{
	e_m365_json_add_int_member (builder, "interval", value);
}

gint
e_m365_recurrence_pattern_get_month (EM365RecurrencePattern *pattern)
{
	return e_m365_json_get_int_member (pattern, "month", -1);
}

void
e_m365_recurrence_pattern_add_month (JsonBuilder *builder,
				     gint value)
{
	e_m365_json_add_int_member (builder, "month", value);
}

EM365RecurrencePatternType
e_m365_recurrence_pattern_get_type (EM365RecurrencePattern *pattern)
{
	return m365_json_utils_get_json_as_enum (pattern, "type",
		recurrence_pattern_map, G_N_ELEMENTS (recurrence_pattern_map),
		E_M365_RECURRENCE_PATTERN_NOT_SET,
		E_M365_RECURRENCE_PATTERN_UNKNOWN);
}

void
e_m365_recurrence_pattern_add_type (JsonBuilder *builder,
				    EM365RecurrencePatternType value)
{
	m365_json_utils_add_enum_as_json (builder, "type", value,
		recurrence_pattern_map, G_N_ELEMENTS (recurrence_pattern_map),
		E_M365_RECURRENCE_PATTERN_NOT_SET,
		E_M365_RECURRENCE_PATTERN_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/recurrencerange?view=graph-rest-1.0 */

EM365Date
e_m365_recurrence_range_get_end_date (EM365RecurrenceRange *range)
{
	return e_m365_date_get (range, "endDate");
}

void
e_m365_recurrence_range_add_end_date (JsonBuilder *builder,
				      EM365Date value)
{
	e_m365_add_date (builder, "endDate", value);
}

gint
e_m365_recurrence_range_get_number_of_occurrences (EM365RecurrenceRange *range)
{
	return e_m365_json_get_int_member (range, "numberOfOccurrences", -1);
}

void
e_m365_recurrence_range_add_number_of_occurrences (JsonBuilder *builder,
						   gint value)
{
	e_m365_json_add_int_member (builder, "numberOfOccurrences", value);
}

const gchar *
e_m365_recurrence_range_get_recurrence_time_zone (EM365RecurrenceRange *range)
{
	return e_m365_json_get_string_member (range, "recurrenceTimeZone", NULL);
}

void
e_m365_recurrence_range_add_recurrence_time_zone (JsonBuilder *builder,
						  const gchar *value)
{
	e_m365_json_add_string_member (builder, "recurrenceTimeZone", value);
}

EM365Date
e_m365_recurrence_range_get_start_date (EM365RecurrenceRange *range)
{
	return e_m365_date_get (range, "startDate");
}

void
e_m365_recurrence_range_add_start_date (JsonBuilder *builder,
					EM365Date value)
{
	e_m365_add_date (builder, "startDate", value);
}

EM365RecurrenceRangeType
e_m365_recurrence_range_get_type (EM365RecurrenceRange *range)
{
	return m365_json_utils_get_json_as_enum (range, "type",
		recurrence_range_map, G_N_ELEMENTS (recurrence_range_map),
		E_M365_RECURRENCE_RANGE_NOT_SET,
		E_M365_RECURRENCE_RANGE_UNKNOWN);
}

void
e_m365_recurrence_range_add_type (JsonBuilder *builder,
				  EM365RecurrenceRangeType value)
{
	m365_json_utils_add_enum_as_json (builder, "type", value,
		recurrence_range_map, G_N_ELEMENTS (recurrence_range_map),
		E_M365_RECURRENCE_RANGE_NOT_SET,
		E_M365_RECURRENCE_RANGE_UNKNOWN);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0 */

const gchar *
e_m365_event_get_id (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "id", NULL);
}

const gchar *
e_m365_event_get_change_key (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "changeKey", NULL);
}

JsonArray * /* EM365Attendee * */
e_m365_event_get_attendees (EM365Event *event)
{
	return e_m365_json_get_array_member (event, "attendees");
}

void
e_m365_event_begin_attendees (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "attendees");
}

void
e_m365_event_end_attendees (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_event_add_attendee (JsonBuilder *builder,
			   EM365AttendeeType type,
			   EM365ResponseType response,
			   time_t response_time,
			   const gchar *name,
			   const gchar *address)
{
	if (response_time <= (time_t) 0)
		response_time = time (NULL);

	e_m365_json_begin_object_member (builder, NULL);

	m365_json_utils_add_enum_as_json (builder, "type", type,
		attendee_map, G_N_ELEMENTS (attendee_map),
		E_M365_ATTENDEE_NOT_SET,
		E_M365_ATTENDEE_NOT_SET);

	e_m365_json_begin_object_member (builder, "status");

	m365_json_utils_add_enum_as_json (builder, "response", response,
		response_map, G_N_ELEMENTS (response_map),
		E_M365_RESPONSE_NOT_SET,
		E_M365_RESPONSE_UNKNOWN);

	e_m365_add_date_time_offset_member (builder, "time", response_time);

	e_m365_json_end_object_member (builder); /* status */

	if ((name && *name) || (address && *address))
		e_m365_add_email_address (builder, "emailAddress", name, address);

	e_m365_json_end_object_member (builder);
}

void
e_m365_event_add_null_attendees (JsonBuilder *builder)
{
	e_m365_json_add_null_member (builder, "attendees");
}

EM365ItemBody *
e_m365_event_get_body (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "body");
}

void
e_m365_event_add_body (JsonBuilder *builder,
		       EM365ItemBodyContentTypeType content_type,
		       const gchar *content)
{
	e_m365_add_item_body (builder, "body", content_type, content);
}

const gchar *
e_m365_event_get_body_preview (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "bodyPreview", NULL);
}

JsonArray * /* const gchar * */
e_m365_event_get_categories (EM365Event *event)
{
	return e_m365_json_get_array_member (event, "categories");
}

void
e_m365_event_begin_categories (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "categories");
}

void
e_m365_event_end_categories (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_event_add_category (JsonBuilder *builder,
			   const gchar *category)
{
	g_return_if_fail (category && *category);

	json_builder_add_string_value (builder, category);
}

time_t
e_m365_event_get_created_date_time (EM365Event *event)
{
	return e_m365_get_date_time_offset_member (event, "createdDateTime", NULL);
}

EM365DateTimeWithZone *
e_m365_event_get_end (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "end");
}

void
e_m365_event_add_end (JsonBuilder *builder,
		      time_t date_time,
		      const gchar *zone)
{
	e_m365_add_date_time (builder, "end", date_time, zone);
}

gboolean
e_m365_event_get_has_attachments (EM365Event *event)
{
	return e_m365_json_get_boolean_member (event, "hasAttachments", FALSE);
}

const gchar *
e_m365_event_get_ical_uid (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "iCalUId", NULL);
}

EM365ImportanceType
e_m365_event_get_importance (EM365Event *event)
{
	return m365_json_utils_get_json_as_enum (event, "importance",
		importance_map, G_N_ELEMENTS (importance_map),
		E_M365_IMPORTANCE_NOT_SET,
		E_M365_IMPORTANCE_UNKNOWN);
}

void
e_m365_event_add_importance (JsonBuilder *builder,
			     EM365ImportanceType value)
{
	m365_json_utils_add_enum_as_json (builder, "importance", value,
		importance_map, G_N_ELEMENTS (importance_map),
		E_M365_IMPORTANCE_NOT_SET,
		E_M365_IMPORTANCE_UNKNOWN);
}

gboolean
e_m365_event_get_is_all_day (EM365Event *event)
{
	return e_m365_json_get_boolean_member (event, "isAllDay", FALSE);
}

void
e_m365_event_add_is_all_day (JsonBuilder *builder,
			     gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isAllDay", value);
}

gboolean
e_m365_event_get_is_cancelled (EM365Event *event)
{
	return e_m365_json_get_boolean_member (event, "isCancelled", FALSE);
}

gboolean
e_m365_event_get_is_online_meeting (EM365Event *event)
{
	return e_m365_json_get_boolean_member (event, "isOnlineMeeting", FALSE);
}

void
e_m365_event_add_is_online_meeting (JsonBuilder *builder,
				    gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isOnlineMeeting", value);
}

gboolean
e_m365_event_get_is_organizer (EM365Event *event)
{
	return e_m365_json_get_boolean_member (event, "isOrganizer", FALSE);
}

gboolean
e_m365_event_get_is_reminder_on (EM365Event *event)
{
	return e_m365_json_get_boolean_member (event, "isReminderOn", FALSE);
}

void
e_m365_event_add_is_reminder_on (JsonBuilder *builder,
				 gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isReminderOn", value);
}

time_t
e_m365_event_get_last_modified_date_time (EM365Event *event)
{
	return e_m365_get_date_time_offset_member (event, "lastModifiedDateTime", NULL);
}

EM365Location *
e_m365_event_get_location (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "location");
}

void
e_m365_event_begin_location (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "location");
}

void
e_m365_event_end_location (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

void
e_m365_event_add_null_location (JsonBuilder *builder)
{
	e_m365_json_add_null_member (builder, "location");
}

JsonArray * /* EM365Location * */
e_m365_event_get_locations (EM365Event *event)
{
	return e_m365_json_get_array_member (event, "locations");
}

void
e_m365_event_begin_locations (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "locations");
}

void
e_m365_event_end_locations (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_event_begin_locations_location (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, NULL);
}

void
e_m365_event_end_locations_location (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

EM365OnlineMeetingInfo *
e_m365_event_get_online_meeting_info (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "onlineMeeting");
}

EM365OnlineMeetingProviderType
e_m365_event_get_online_meeting_provider (EM365Event *event)
{
	return m365_json_utils_get_json_as_enum (event, "onlineMeetingProvider",
		meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
		E_M365_ONLINE_MEETING_PROVIDER_NOT_SET,
		E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN);
}

void
e_m365_event_add_online_meeting_provider (JsonBuilder *builder,
					  EM365OnlineMeetingProviderType value)
{
	if (value == E_M365_ONLINE_MEETING_PROVIDER_NOT_SET) {
		e_m365_json_add_null_member (builder, "onlineMeetingProvider");
	} else {
		m365_json_utils_add_enum_as_json (builder, "onlineMeetingProvider", value,
			meeting_provider_map, G_N_ELEMENTS (meeting_provider_map),
			E_M365_ONLINE_MEETING_PROVIDER_NOT_SET,
			E_M365_ONLINE_MEETING_PROVIDER_UNKNOWN);
	}
}

const gchar *
e_m365_event_get_online_meeting_url (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "onlineMeetingUrl", NULL);
}

EM365Recipient *
e_m365_event_get_organizer (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "organizer");
}

void
e_m365_event_add_organizer (JsonBuilder *builder,
			    const gchar *name,
			    const gchar *address)
{
	e_m365_add_recipient (builder, "organizer", name, address);
}

void
e_m365_event_add_null_organizer (JsonBuilder *builder)
{
	e_m365_json_add_null_member (builder, "organizer");
}

const gchar *
e_m365_event_get_original_end_timezone (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "originalEndTimeZone", NULL);
}

time_t
e_m365_event_get_original_start (EM365Event *event)
{
	return e_m365_get_date_time_offset_member (event, "originalStart", NULL);
}

const gchar *
e_m365_event_get_original_start_timezone (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "originalStartTimeZone", NULL);
}

EM365PatternedRecurrence *
e_m365_event_get_recurrence (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "recurrence");
}

void
e_m365_event_begin_recurrence (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "recurrence");
}

void
e_m365_event_end_recurrence (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

void
e_m365_event_add_null_recurrence (JsonBuilder *builder)
{
	e_m365_json_add_null_member (builder, "recurrence");
}

gint
e_m365_event_get_reminder_minutes_before_start (EM365Event *event)
{
	return e_m365_json_get_int_member (event, "reminderMinutesBeforeStart", -1);
}

void
e_m365_event_add_reminder_minutes_before_start (JsonBuilder *builder,
						gint value)
{
	e_m365_json_add_int_member (builder, "reminderMinutesBeforeStart", value);
}

gboolean
e_m365_event_get_response_requested (EM365Event *event)
{
	return e_m365_json_get_boolean_member (event, "responseRequested", FALSE);
}

void
e_m365_event_add_response_requested (JsonBuilder *builder,
				     gboolean value)
{
	e_m365_json_add_boolean_member (builder, "responseRequested", value);
}


EM365ResponseStatus *
e_m365_event_get_response_status (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "responseStatus");
}

EM365SensitivityType
e_m365_event_get_sensitivity (EM365Event *event)
{
	return m365_json_utils_get_json_as_enum (event, "sensitivity",
		sensitivity_map, G_N_ELEMENTS (sensitivity_map),
		E_M365_SENSITIVITY_NOT_SET,
		E_M365_SENSITIVITY_UNKNOWN);
}

void
e_m365_event_add_sensitivity (JsonBuilder *builder,
			      EM365SensitivityType value)
{
	m365_json_utils_add_enum_as_json (builder, "sensitivity", value,
		sensitivity_map, G_N_ELEMENTS (sensitivity_map),
		E_M365_SENSITIVITY_NOT_SET,
		E_M365_SENSITIVITY_UNKNOWN);
}

const gchar *
e_m365_event_get_series_master_id (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "seriesMasterId", NULL);
}

EM365FreeBusyStatusType
e_m365_event_get_show_as (EM365Event *event)
{
	return m365_json_utils_get_json_as_enum (event, "showAs",
		free_busy_status_map, G_N_ELEMENTS (free_busy_status_map),
		E_M365_FREE_BUSY_STATUS_NOT_SET,
		E_M365_FREE_BUSY_STATUS_UNKNOWN);
}

void
e_m365_event_add_show_as (JsonBuilder *builder,
			  EM365FreeBusyStatusType value)
{
	m365_json_utils_add_enum_as_json (builder, "showAs", value,
		free_busy_status_map, G_N_ELEMENTS (free_busy_status_map),
		E_M365_FREE_BUSY_STATUS_NOT_SET,
		E_M365_FREE_BUSY_STATUS_UNKNOWN);
}

EM365DateTimeWithZone *
e_m365_event_get_start (EM365Event *event)
{
	return e_m365_json_get_object_member (event, "start");
}

void
e_m365_event_add_start (JsonBuilder *builder,
			time_t date_time,
			const gchar *zone)
{
	e_m365_add_date_time (builder, "start", date_time, zone);
}

const gchar *
e_m365_event_get_subject (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "subject", NULL);
}

void
e_m365_event_add_subject (JsonBuilder *builder,
			  const gchar *value)
{
	e_m365_json_add_string_member (builder, "subject", value ? value : "");
}

EM365EventTypeType
e_m365_event_get_type (EM365Event *event)
{
	return m365_json_utils_get_json_as_enum (event, "type",
		event_type_map, G_N_ELEMENTS (event_type_map),
		E_M365_EVENT_TYPE_NOT_SET,
		E_M365_EVENT_TYPE_UNKNOWN);
}

const gchar *
e_m365_event_get_web_link (EM365Event *event)
{
	return e_m365_json_get_string_member (event, "webLink", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/freebusyerror?view=graph-rest-1.0 */

const gchar *
e_m365_free_busy_error_get_message (EM365FreeBusyError *fberror)
{
	return e_m365_json_get_string_member (fberror, "message", NULL);
}

const gchar *
e_m365_free_busy_error_get_response_code (EM365FreeBusyError *fberror)
{
	return e_m365_json_get_string_member (fberror, "responseCode", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/scheduleitem?view=graph-rest-1.0 */

EM365DateTimeWithZone *
e_m365_schedule_item_get_end (EM365ScheduleItem *schitem)
{
	return e_m365_json_get_object_member (schitem, "end");
}

gboolean
e_m365_schedule_item_get_is_private (EM365ScheduleItem *schitem)
{
	return e_m365_json_get_boolean_member (schitem, "isPrivate", FALSE);
}

const gchar *
e_m365_schedule_item_get_location (EM365ScheduleItem *schitem)
{
	return e_m365_json_get_string_member (schitem, "location", NULL);
}

EM365DateTimeWithZone *
e_m365_schedule_item_get_start (EM365ScheduleItem *schitem)
{
	return e_m365_json_get_object_member (schitem, "start");
}

EM365FreeBusyStatusType
e_m365_schedule_item_get_status (EM365ScheduleItem *schitem)
{
	return m365_json_utils_get_json_as_enum (schitem, "status",
		free_busy_status_map, G_N_ELEMENTS (free_busy_status_map),
		E_M365_FREE_BUSY_STATUS_NOT_SET,
		E_M365_FREE_BUSY_STATUS_UNKNOWN);
}

const gchar *
e_m365_schedule_item_get_subject (EM365ScheduleItem *schitem)
{
	return e_m365_json_get_string_member (schitem, "subject", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/workinghours?view=graph-rest-1.0 */

JsonArray * /* Use e_m365_array_get_day_of_week_element() to get the items */
e_m365_working_hours_get_days_of_week (EM365WorkingHours *wrkhrs)
{
	return e_m365_json_get_array_member (wrkhrs, "daysOfWeek");
}

EM365TimeOfDay
e_m365_working_hours_get_start_time (EM365WorkingHours *wrkhrs)
{
	return e_m365_time_of_day_get (wrkhrs, "startTime");
}

EM365TimeOfDay
e_m365_working_hours_get_end_time (EM365WorkingHours *wrkhrs)
{
	return e_m365_time_of_day_get (wrkhrs, "endTime");
}

const gchar *
e_m365_working_hours_get_time_zone_name (EM365WorkingHours *wrkhrs)
{
	JsonObject *value;

	value = e_m365_json_get_object_member (wrkhrs, "timeZone");

	if (!value)
		return NULL;

	/* https://docs.microsoft.com/en-us/graph/api/resources/timezonebase?view=graph-rest-1.0 */

	return e_m365_json_get_string_member (value, "name", NULL);
}

/* https://docs.microsoft.com/en-us/graph/api/resources/scheduleinformation?view=graph-rest-1.0 */

const gchar *
e_m365_schedule_information_get_availability_view (EM365ScheduleInformation *schinfo)
{
	return e_m365_json_get_string_member (schinfo, "availabilityView", NULL);
}

EM365FreeBusyError *
e_m365_schedule_information_get_free_busy_error (EM365ScheduleInformation *schinfo)
{
	return e_m365_json_get_object_member (schinfo, "error");
}

const gchar *
e_m365_schedule_information_get_schedule_id (EM365ScheduleInformation *schinfo)
{
	return e_m365_json_get_string_member (schinfo, "scheduleId", NULL);
}

JsonArray * /* EM365ScheduleItem * */
e_m365_schedule_information_get_schedule_items (EM365ScheduleInformation *schinfo)
{
	return e_m365_json_get_array_member (schinfo, "scheduleItems");
}

EM365WorkingHours *
e_m365_schedule_information_get_working_hours (EM365ScheduleInformation *schinfo)
{
	return e_m365_json_get_object_member (schinfo, "workingHours");
}

/* https://learn.microsoft.com/en-us/graph/api/resources/todotasklist?view=graph-rest-1.0 */

const gchar *
e_m365_task_list_get_id (EM365TaskList *list)
{
	return e_m365_json_get_string_member (list, "id", NULL);
}

const gchar *
e_m365_task_list_get_display_name (EM365TaskList *list)
{
	return e_m365_json_get_string_member (list, "displayName", NULL);
}

void
e_m365_task_list_add_display_name (JsonBuilder *builder,
				   const gchar *display_name)
{
	e_m365_json_add_string_member (builder, "displayName", display_name);
}

gboolean
e_m365_task_list_get_is_owner (EM365TaskList *list)
{
	return e_m365_json_get_boolean_member (list, "isOwner", FALSE);
}

gboolean
e_m365_task_list_get_is_shared (EM365TaskList *list)
{
	return e_m365_json_get_boolean_member (list, "isShared", FALSE);
}

EM365TaskListKind
e_m365_task_list_get_kind (EM365TaskList *list)
{
	return m365_json_utils_get_json_as_enum (list, "wellknownListName",
		task_list_kind_map, G_N_ELEMENTS (task_list_kind_map),
		E_M365_TASK_LIST_KIND_NOT_SET,
		E_M365_TASK_LIST_KIND_UNKNOWN);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/todotask?view=graph-rest-1.0 */

const gchar *
e_m365_task_get_id (EM365Task *task)
{
	return e_m365_json_get_string_member (task, "id", NULL);
}

void
e_m365_task_add_id (JsonBuilder *builder,
		    const gchar *value)
{
	e_m365_json_add_string_member (builder, "id", value);
}

EM365ItemBody *
e_m365_task_get_body (EM365Task *task)
{
	return e_m365_json_get_object_member (task, "body");
}

void
e_m365_task_add_body (JsonBuilder *builder,
		      EM365ItemBodyContentTypeType content_type,
		      const gchar *content)
{
	e_m365_add_item_body (builder, "body", content_type, content);
}

time_t
e_m365_task_get_body_last_modified_date_time (EM365Task *task)
{
	return e_m365_get_date_time_offset_member (task, "bodyLastModifiedDateTime", NULL);
}

void
e_m365_task_add_boady_last_modified_date_time (JsonBuilder *builder,
					       time_t value)
{
	return e_m365_add_date_time_offset_member (builder, "bodyLastModifiedDateTime", value);
}

JsonArray * /* const gchar * */
e_m365_task_get_categories (EM365Task *task)
{
	return e_m365_json_get_array_member (task, "categories");
}

void
e_m365_task_begin_categories (JsonBuilder *builder)
{
	e_m365_json_begin_array_member (builder, "categories");
}

void
e_m365_task_end_categories (JsonBuilder *builder)
{
	e_m365_json_end_array_member (builder);
}

void
e_m365_task_add_category (JsonBuilder *builder,
			  const gchar *category)
{
	g_return_if_fail (category && *category);

	json_builder_add_string_value (builder, category);
}

EM365DateTimeWithZone *
e_m365_task_get_completed_date_time (EM365Task *task)
{
	return e_m365_json_get_object_member (task, "completedDateTime");
}

void
e_m365_task_add_completed_date_time (JsonBuilder *builder,
				     time_t date_time,
				     const gchar *zone)
{
	e_m365_add_date_time (builder, "completedDateTime", date_time, zone);
}

time_t
e_m365_task_get_created_date_time (EM365Task *task)
{
	return e_m365_get_date_time_offset_member (task, "createdDateTime", NULL);
}

void
e_m365_task_add_created_date_time (JsonBuilder *builder,
				   time_t value)
{
	return e_m365_add_date_time_offset_member (builder, "createdDateTime", value);
}

EM365DateTimeWithZone *
e_m365_task_get_due_date_time (EM365Task *task)
{
	return e_m365_json_get_object_member (task, "dueDateTime");
}

void
e_m365_task_add_due_date_time (JsonBuilder *builder,
			       time_t date_time,
			       const gchar *zone)
{
	e_m365_add_date_time (builder, "dueDateTime", date_time, zone);
}

EM365ImportanceType
e_m365_task_get_importance (EM365Task *task)
{
	return m365_json_utils_get_json_as_enum (task, "importance",
		importance_map, G_N_ELEMENTS (importance_map),
		E_M365_IMPORTANCE_NOT_SET,
		E_M365_IMPORTANCE_UNKNOWN);
}

void
e_m365_task_add_importance (JsonBuilder *builder,
			    EM365ImportanceType value)
{
	m365_json_utils_add_enum_as_json (builder, "importance", value,
		importance_map, G_N_ELEMENTS (importance_map),
		E_M365_IMPORTANCE_NOT_SET,
		E_M365_IMPORTANCE_NOT_SET);
}

gboolean
e_m365_task_get_is_reminder_on (EM365Task *task)
{
	return e_m365_json_get_boolean_member (task, "isReminderOn", FALSE);
}

void
e_m365_task_add_is_reminder_on (JsonBuilder *builder,
				gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isReminderOn", value);
}

time_t
e_m365_task_get_last_modified_date_time (EM365Task *task)
{
	return e_m365_get_date_time_offset_member (task, "lastModifiedDateTime", NULL);
}

const gchar *
e_m365_task_get_last_modified_as_string (EM365Task *task)
{
	return e_m365_json_get_string_member (task, "lastModifiedDateTime", NULL);
}

void
e_m365_task_add_last_modified_date_time (JsonBuilder *builder,
					 time_t value)
{
	return e_m365_add_date_time_offset_member (builder, "lastModifiedDateTime", value);
}

EM365PatternedRecurrence *
e_m365_task_get_recurrence (EM365Task *task)
{
	return e_m365_json_get_object_member (task, "recurrence");
}

void
e_m365_task_begin_recurrence (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "recurrence");
}

void
e_m365_task_end_recurrence (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

void
e_m365_task_add_null_recurrence (JsonBuilder *builder)
{
	e_m365_json_add_null_member (builder, "recurrence");
}

EM365DateTimeWithZone *
e_m365_task_get_reminder_date_time (EM365Task *task)
{
	return e_m365_json_get_object_member (task, "reminderDateTime");
}

void
e_m365_task_add_reminder_date_time (JsonBuilder *builder,
				    time_t date_time,
				    const gchar *zone)
{
	e_m365_add_date_time (builder, "reminderDateTime", date_time, zone);
}

EM365StatusType
e_m365_task_get_status (EM365Task *task)
{
	return m365_json_utils_get_json_as_enum (task, "status",
		status_map, G_N_ELEMENTS (status_map),
		E_M365_STATUS_NOT_SET,
		E_M365_STATUS_UNKNOWN);
}

EM365DateTimeWithZone *
e_m365_task_get_start_date_time (EM365Task *task)
{
	return e_m365_json_get_object_member (task, "startDateTime");
}

void
e_m365_task_add_start_date_time (JsonBuilder *builder,
				 time_t date_time,
				 const gchar *zone)
{
	e_m365_add_date_time (builder, "startDateTime", date_time, zone);
}

void
e_m365_task_add_status (JsonBuilder *builder,
			EM365StatusType value)
{
	m365_json_utils_add_enum_as_json (builder, "status", value,
		status_map, G_N_ELEMENTS (status_map),
		E_M365_STATUS_NOT_SET,
		E_M365_STATUS_UNKNOWN);
}

const gchar *
e_m365_task_get_title (EM365Task *task)
{
	return e_m365_json_get_string_member (task, "title", NULL);
}

void
e_m365_task_add_title (JsonBuilder *builder,
		       const gchar *value)
{
	e_m365_json_add_string_member (builder, "title", value);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/checklistitem?view=graph-rest-1.0 */

const gchar *
e_m365_checklist_item_get_id (EM365ChecklistItem *item)
{
	return e_m365_json_get_string_member (item, "id", NULL);
}

time_t
e_m365_checklist_item_get_checked_date_time (EM365ChecklistItem *item)
{
	return e_m365_get_date_time_offset_member (item, "checkedDateTime", NULL);
}

void
e_m365_checklist_item_add_checked_date_time (JsonBuilder *builder,
					     time_t value)
{
	e_m365_add_date_time_offset_member (builder, "checkedDateTime", value);
}

time_t
e_m365_checklist_item_get_created_date_time (EM365ChecklistItem *item)
{
	return e_m365_get_date_time_offset_member (item, "createdDateTime", NULL);
}

void
e_m365_checklist_item_add_created_date_time (JsonBuilder *builder,
					     time_t value)
{
	e_m365_add_date_time_offset_member (builder, "createdDateTime", value);
}

const gchar *
e_m365_checklist_item_get_display_name (EM365ChecklistItem *item)
{
	return e_m365_json_get_string_member (item, "displayName", NULL);
}

void
e_m365_checklist_item_add_display_name (JsonBuilder *builder,
					const gchar *value)
{
	e_m365_json_add_string_member (builder, "displayName", value);
}

gboolean
e_m365_checklist_item_get_is_checked (EM365ChecklistItem *item)
{
	return e_m365_json_get_boolean_member (item, "isChecked", FALSE);
}

void
e_m365_checklist_item_add_is_checked (JsonBuilder *builder,
				      gboolean value)
{
	e_m365_json_add_boolean_member (builder, "isChecked", value);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/linkedresource?view=graph-rest-1.0 */

const gchar *
e_m365_linked_resource_get_id (EM365LinkedResource *resource)
{
	return e_m365_json_get_string_member (resource, "id", NULL);
}

const gchar *
e_m365_linked_resource_get_application_name (EM365LinkedResource *resource)
{
	return e_m365_json_get_string_member (resource, "applicationName", NULL);
}

void
e_m365_linked_resource_add_application_name (JsonBuilder *builder,
					     const gchar *value)
{
	e_m365_json_add_string_member (builder, "applicationName", value);
}

const gchar *
e_m365_linked_resource_get_display_name (EM365LinkedResource *resource)
{
	return e_m365_json_get_string_member (resource, "displayName", NULL);
}

void
e_m365_linked_resource_add_display_name (JsonBuilder *builder,
					 const gchar *value)
{
	e_m365_json_add_string_member (builder, "displayName", value);
}

const gchar *
e_m365_linked_resource_get_external_id (EM365LinkedResource *resource)
{
	return e_m365_json_get_string_member (resource, "externalId", NULL);
}

void
e_m365_linked_resource_add_external_id (JsonBuilder *builder,
					const gchar *value)
{
	e_m365_json_add_string_member (builder, "externalId", value);
}

const gchar *
e_m365_linked_resource_get_web_url (EM365LinkedResource *resource)
{
	return e_m365_json_get_string_member (resource, "webUrl", NULL);
}

void
e_m365_linked_resource_add_web_url (JsonBuilder *builder,
				    const gchar *value)
{
	e_m365_json_add_string_member (builder, "webUrl", value);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/mailboxsettings?view=graph-rest-1.0 */

EM365AutomaticRepliesSetting *
e_m365_mailbox_settings_get_automatic_replies_setting (EM365MailboxSettings *malbox_settings)
{
	return e_m365_json_get_object_member (malbox_settings, "automaticRepliesSetting");
}

void
e_m365_begin_mailbox_settings (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, NULL);
}

void
e_m365_end_mailbox_settings (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

/* https://learn.microsoft.com/en-us/graph/api/resources/automaticrepliessetting?view=graph-rest-1.0 */

void
e_m365_begin_automatic_replies_setting (JsonBuilder *builder)
{
	e_m365_json_begin_object_member (builder, "automaticRepliesSetting");
}

void
e_m365_end_automatic_replies_setting (JsonBuilder *builder)
{
	e_m365_json_end_object_member (builder);
}

EM365ExternalAudienceScopeType
e_m365_automatic_replies_setting_get_external_audience (EM365AutomaticRepliesSetting *automatic_replies_setting)
{
	return m365_json_utils_get_json_as_enum (automatic_replies_setting, "externalAudience",
		external_audience_scope_map, G_N_ELEMENTS (external_audience_scope_map),
		E_M365_EXTERNAL_AUDIENCE_SCOPE_NOT_SET,
		E_M365_EXTERNAL_AUDIENCE_SCOPE_UNKNOWN);
}

void
e_m365_automatic_replies_setting_add_external_audience (JsonBuilder *builder,
							EM365ExternalAudienceScopeType value)
{
	m365_json_utils_add_enum_as_json (builder, "externalAudience", value,
		external_audience_scope_map, G_N_ELEMENTS (external_audience_scope_map),
		E_M365_EXTERNAL_AUDIENCE_SCOPE_NOT_SET,
		E_M365_EXTERNAL_AUDIENCE_SCOPE_UNKNOWN);
}

const gchar *
e_m365_automatic_replies_setting_get_external_reply_message (EM365AutomaticRepliesSetting *automatic_replies_setting)
{
	return e_m365_json_get_string_member (automatic_replies_setting, "externalReplyMessage", NULL);
}

void
e_m365_automatic_replies_setting_add_external_reply_message (JsonBuilder *builder,
							     const gchar *value)
{
	e_m365_json_add_string_member (builder, "externalReplyMessage", value);
}

const gchar *
e_m365_automatic_replies_setting_get_internal_reply_message (EM365AutomaticRepliesSetting *automatic_replies_setting)
{
	return e_m365_json_get_string_member (automatic_replies_setting, "internalReplyMessage", NULL);
}

void
e_m365_automatic_replies_setting_add_internal_reply_message (JsonBuilder *builder,
							     const gchar *value)
{
	e_m365_json_add_string_member (builder, "internalReplyMessage", value);
}

EM365DateTimeWithZone *
e_m365_automatic_replies_setting_get_scheduled_end_date_time (EM365AutomaticRepliesSetting *automatic_replies_setting)
{
	return e_m365_json_get_object_member (automatic_replies_setting, "scheduledEndDateTime");
}

void
e_m365_automatic_replies_setting_add_scheduled_end_date_time (JsonBuilder *builder,
							      time_t date_time,
							      const gchar *zone)
{
	e_m365_add_date_time (builder, "scheduledEndDateTime", date_time, zone);
}

EM365DateTimeWithZone *
e_m365_automatic_replies_setting_get_scheduled_start_date_time (EM365AutomaticRepliesSetting *automatic_replies_setting)
{
	return e_m365_json_get_object_member (automatic_replies_setting, "scheduledStartDateTime");
}

void
e_m365_automatic_replies_setting_add_scheduled_start_date_time (JsonBuilder *builder,
								time_t date_time,
								const gchar *zone)
{
	e_m365_add_date_time (builder, "scheduledStartDateTime", date_time, zone);
}

EM365AutomaticRepliesStatusType
e_m365_automatic_replies_setting_get_status (EM365AutomaticRepliesSetting *automatic_replies_setting)
{
	return m365_json_utils_get_json_as_enum (automatic_replies_setting, "status",
		automatic_replies_status_map, G_N_ELEMENTS (automatic_replies_status_map),
		E_M365_AUTOMATIC_REPLIES_STATUS_NOT_SET,
		E_M365_AUTOMATIC_REPLIES_STATUS_UNKNOWN);
}

void
e_m365_automatic_replies_setting_add_status (JsonBuilder *builder,
					     EM365AutomaticRepliesStatusType value)
{
	m365_json_utils_add_enum_as_json (builder, "status", value,
		automatic_replies_status_map, G_N_ELEMENTS (automatic_replies_status_map),
		E_M365_AUTOMATIC_REPLIES_STATUS_NOT_SET,
		E_M365_AUTOMATIC_REPLIES_STATUS_UNKNOWN);
}
