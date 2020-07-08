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

#include <json-glib/json-glib.h>

#include "e-o365-json-utils.h"

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

	if ((time_t) value <= 0) {
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

/* https://docs.microsoft.com/en-us/graph/api/resources/mailfolder?view=graph-rest-1.0 */

const gchar *
e_o365_mail_folder_get_display_name (EO365MailFolder *folder)
{
	return e_o365_json_get_string_member (folder, "displayName", NULL);
}

const gchar *
e_o365_mail_folder_get_id (EO365MailFolder *folder)
{
	return e_o365_json_get_string_member (folder, "id", NULL);
}

const gchar *
e_o365_mail_folder_get_parent_folder_id (EO365MailFolder *folder)
{
	return e_o365_json_get_string_member (folder, "parentFolderId", NULL);
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

	if (name && *name)
		e_o365_json_add_string_member (builder, "name", name);

	if (address && *address)
		e_o365_json_add_string_member (builder, "address", address);

	e_o365_json_end_object_member (builder); /* emailAddress */
	e_o365_json_end_object_member (builder); /* member_name */
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

	if (zone && *zone)
		e_o365_json_add_string_member (builder, "timeZone", zone);

	e_o365_json_end_object_member (builder);
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
	const gchar *status;

	status = e_o365_json_get_string_member (flag, "flagStatus", NULL);

	if (!status)
		return E_O365_FOLLOWUP_FLAG_STATUS_NOT_SET;

	if (g_ascii_strcasecmp (status, "notFlagged") == 0)
		return E_O365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED;

	if (g_ascii_strcasecmp (status, "complete") == 0)
		return E_O365_FOLLOWUP_FLAG_STATUS_COMPLETE;

	if (g_ascii_strcasecmp (status, "flagged") == 0)
		return E_O365_FOLLOWUP_FLAG_STATUS_FLAGGED;

	return E_O365_FOLLOWUP_FLAG_STATUS_UNKNOWN;
}

void
e_o365_followup_flag_add_flag_status (JsonBuilder *builder,
				      EO365FollowupFlagStatusType status)
{
	const gchar *value;

	if (status == E_O365_FOLLOWUP_FLAG_STATUS_COMPLETE)
		value = "complete";
	else if (status == E_O365_FOLLOWUP_FLAG_STATUS_FLAGGED)
		value = "flagged";
	else
		value = "notFlagged";

	e_o365_json_add_string_member (builder, "flagStatus", value);
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
	const gchar *content_type;

	content_type = e_o365_json_get_string_member (item_body, "contentType", NULL);

	if (!content_type)
		return E_O365_ITEM_BODY_CONTENT_TYPE_NOT_SET;

	if (g_ascii_strcasecmp (content_type, "text") == 0)
		return E_O365_ITEM_BODY_CONTENT_TYPE_TEXT;

	if (g_ascii_strcasecmp (content_type, "html") == 0)
		return E_O365_ITEM_BODY_CONTENT_TYPE_HTML;

	return E_O365_ITEM_BODY_CONTENT_TYPE_UNKNOWN;
}

void
e_o365_add_item_body (JsonBuilder *builder,
		      const gchar *member_name,
		      EO365ItemBodyContentTypeType content_type,
		      const gchar *content)
{
	const gchar *content_type_str;

	g_return_if_fail (member_name != NULL);
	g_return_if_fail (content != NULL);

	switch (content_type) {
	case E_O365_ITEM_BODY_CONTENT_TYPE_TEXT:
		content_type_str = "text";
		break;
	case E_O365_ITEM_BODY_CONTENT_TYPE_HTML:
		content_type_str = "html";
		break;
	default:
		g_warn_if_reached ();

		content_type_str = "text";
		break;
	}

	e_o365_json_begin_object_member (builder, member_name);

	e_o365_json_add_string_member (builder, "contentType", content_type_str);
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
	const gchar *value = e_o365_json_get_string_member (mail, "importance", NULL);

	if (!value)
		return E_O365_IMPORTANCE_NOT_SET;

	if (g_ascii_strcasecmp (value, "low") == 0)
		return E_O365_IMPORTANCE_LOW;

	if (g_ascii_strcasecmp (value, "normal") == 0)
		return E_O365_IMPORTANCE_NORMAL;

	if (g_ascii_strcasecmp (value, "high") == 0)
		return E_O365_IMPORTANCE_HIGH;

	return E_O365_IMPORTANCE_UNKNOWN;
}

void
e_o365_mail_message_add_importance (JsonBuilder *builder,
				    EO365ImportanceType importance)
{
	const gchar *value = NULL;

	switch (importance) {
	case E_O365_IMPORTANCE_LOW:
		value = "low";
		break;
	case E_O365_IMPORTANCE_NORMAL:
		value = "normal";
		break;
	case E_O365_IMPORTANCE_HIGH:
		value = "high";
		break;
	default:
		return;
	}

	e_o365_json_add_string_member (builder, "importance", value);
}

EO365InferenceClassificationType
e_o365_mail_message_get_inference_classification (EO365MailMessage *mail)
{
	const gchar *value = e_o365_json_get_string_member (mail, "inferenceClassification", NULL);

	if (!value)
		return E_O365_INFERENCE_CLASSIFICATION_NOT_SET;

	if (g_ascii_strcasecmp (value, "focused") == 0)
		return E_O365_INFERENCE_CLASSIFICATION_FOCUSED;

	if (g_ascii_strcasecmp (value, "other") == 0)
		return E_O365_INFERENCE_CLASSIFICATION_OTHER;

	return E_O365_INFERENCE_CLASSIFICATION_UNKNOWN;
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
	if (message_id && *message_id)
		e_o365_json_add_string_member (builder, "internetMessageId", message_id);
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
	if (subject)
		e_o365_json_add_string_member (builder, "subject", subject);
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
	const gchar *data_type;

	data_type = e_o365_json_get_string_member (attachment, "@odata.type", NULL);

	if (!data_type)
		return E_O365_ATTACHMENT_DATA_TYPE_NOT_SET;

	if (g_ascii_strcasecmp (data_type, "#microsoft.graph.fileAttachment") == 0)
		return E_O365_ATTACHMENT_DATA_TYPE_FILE;

	if (g_ascii_strcasecmp (data_type, "#microsoft.graph.itemAttachment") == 0)
		return E_O365_ATTACHMENT_DATA_TYPE_ITEM;

	if (g_ascii_strcasecmp (data_type, "#microsoft.graph.referenceAttachment") == 0)
		return E_O365_ATTACHMENT_DATA_TYPE_REFERENCE;

	return E_O365_ATTACHMENT_DATA_TYPE_UNKNOWN;
}

void
e_o365_attachment_begin_attachment (JsonBuilder *builder,
				    EO365AttachmentDataType data_type)
{
	e_o365_json_begin_object_member (builder, NULL);

	if (data_type == E_O365_ATTACHMENT_DATA_TYPE_FILE)
		e_o365_json_add_string_member (builder, "@odata.type", "#microsoft.graph.fileAttachment");
	else if (data_type == E_O365_ATTACHMENT_DATA_TYPE_ITEM)
		e_o365_json_add_string_member (builder, "@odata.type", "#microsoft.graph.itemAttachment");
	else if (data_type == E_O365_ATTACHMENT_DATA_TYPE_REFERENCE)
		e_o365_json_add_string_member (builder, "@odata.type", "#microsoft.graph.referenceAttachment");
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
