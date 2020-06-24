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

	if (!node)
		return NULL;

	g_return_val_if_fail (JSON_NODE_HOLDS_ARRAY (node), NULL);

	return json_node_get_array (node);
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

	if (!node)
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_boolean (node);
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

	if (!node)
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_double (node);
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

	if (!node)
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_int (node);
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

JsonObject *
e_o365_json_get_object_member (JsonObject *object,
			       const gchar *member_name)
{
	JsonNode *node;

	g_return_val_if_fail (object != NULL, NULL);
	g_return_val_if_fail (member_name != NULL, NULL);

	node = json_object_get_member (object, member_name);

	if (!node)
		return NULL;

	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), NULL);

	return json_node_get_object (node);
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

	if (!node)
		return default_value;

	g_return_val_if_fail (JSON_NODE_HOLDS_VALUE (node), default_value);

	return json_node_get_string (node);
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

/* https://docs.microsoft.com/en-us/graph/delta-query-overview */

gboolean
e_o365_delta_is_removed_object (JsonObject *object)
{
	return json_object_has_member (object, "@removed");
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
e_o365_recipient_get_address (EO365Recipient *recipient)
{
	JsonObject *email_address;

	email_address = e_o365_json_get_object_member (recipient, "emailAddress");

	if (!email_address)
		return NULL;

	return e_o365_json_get_string_member (email_address, "address", NULL);
}

const gchar *
e_o365_recipient_get_name (EO365Recipient *recipient)
{
	JsonObject *email_address;

	email_address = e_o365_json_get_object_member (recipient, "emailAddress");

	if (!email_address)
		return NULL;

	return e_o365_json_get_string_member (email_address, "name", NULL);
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

/* https://docs.microsoft.com/en-us/graph/api/resources/followupflag?view=graph-rest-1.0 */

EO365DateTimeWithZone *
e_o365_followup_flag_get_completed_date_time (EO365FollowupFlag *flag)
{
	return e_o365_json_get_object_member (flag, "completedDateTime");
}

EO365DateTimeWithZone *
e_o365_followup_flag_get_due_date_time (EO365FollowupFlag *flag)
{
	return e_o365_json_get_object_member (flag, "dueDateTime");
}

EO365FollowupFlagStatusType
e_o365_followup_flag_get_flag_status (EO365FollowupFlag *flag)
{
	const gchar *status;

	status = e_o365_json_get_string_member (flag, "flagStatus", NULL);

	if (!status)
		return E_O365_FOLLOWUP_FLAG_STATUS_NOT_SET;

	if (g_strcmp0 (status, "notFlagged") == 0)
		return E_O365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED;

	if (g_strcmp0 (status, "complete") == 0)
		return E_O365_FOLLOWUP_FLAG_STATUS_COMPLETE;

	if (g_strcmp0 (status, "flagged") == 0)
		return E_O365_FOLLOWUP_FLAG_STATUS_FLAGGED;

	return E_O365_FOLLOWUP_FLAG_STATUS_UNKNOWN;
}

EO365DateTimeWithZone *
e_o365_followup_flag_get_start_date_time (EO365FollowupFlag *flag)
{
	return e_o365_json_get_object_member (flag, "startDateTime");
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

	if (g_strcmp0 (content_type, "text") == 0)
		return E_O365_ITEM_BODY_CONTENT_TYPE_TEXT;

	if (g_strcmp0 (content_type, "html") == 0)
		return E_O365_ITEM_BODY_CONTENT_TYPE_HTML;

	return E_O365_ITEM_BODY_CONTENT_TYPE_UNKNOWN;
}

/* https://docs.microsoft.com/en-us/graph/api/resources/message?view=graph-rest-1.0 */

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_bcc_recipients (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "bccRecipients");
}

EO365ItemBody *
e_o365_mail_message_get_body (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "body");
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

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_cc_recipients (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "ccRecipients");
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

EO365Recipient *
e_o365_mail_message_get_from (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "from");
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

	if (g_strcmp0 (value, "Low") == 0)
		return E_O365_IMPORTANCE_LOW;

	if (g_strcmp0 (value, "Normal") == 0)
		return E_O365_IMPORTANCE_NORMAL;

	if (g_strcmp0 (value, "High") == 0)
		return E_O365_IMPORTANCE_HIGH;

	return E_O365_IMPORTANCE_UNKNOWN;
}

EO365InferenceClassificationType
e_o365_mail_message_get_inference_classification (EO365MailMessage *mail)
{
	const gchar *value = e_o365_json_get_string_member (mail, "inferenceClassification", NULL);

	if (!value)
		return E_O365_INFERENCE_CLASSIFICATION_NOT_SET;

	if (g_strcmp0 (value, "focused") == 0)
		return E_O365_INFERENCE_CLASSIFICATION_FOCUSED;

	if (g_strcmp0 (value, "other") == 0)
		return E_O365_INFERENCE_CLASSIFICATION_OTHER;

	return E_O365_INFERENCE_CLASSIFICATION_UNKNOWN;
}

JsonArray * /* EO365InternetMessageHeader * */
e_o365_mail_message_get_internet_message_headers (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "internetMessageHeaders");
}

const gchar *
e_o365_mail_message_get_internet_message_id (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "internetMessageId", NULL);
}

gboolean
e_o365_mail_message_get_is_delivery_receipt_requested (EO365MailMessage *mail)
{
	return e_o365_json_get_boolean_member (mail, "isDeliveryReceiptRequested", FALSE);
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

gboolean
e_o365_mail_message_get_is_read_receipt_requested (EO365MailMessage *mail)
{
	return e_o365_json_get_boolean_member (mail, "isReadReceiptRequested", FALSE);
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

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_reply_to (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "replyTo");
}

EO365Recipient *
e_o365_mail_message_get_sender (EO365MailMessage *mail)
{
	return e_o365_json_get_object_member (mail, "sender");
}

time_t
e_o365_mail_message_get_sent_date_time (EO365MailMessage *mail)
{
	return e_o365_get_date_time_offset_member (mail, "sentDateTime");
}

const gchar *
e_o365_mail_message_get_subject (EO365MailMessage *mail)
{
	return e_o365_json_get_string_member (mail, "subject", NULL);
}

JsonArray * /* EO365Recipient * */
e_o365_mail_message_get_to_recipients (EO365MailMessage *mail)
{
	return e_o365_json_get_array_member (mail, "toRecipients");
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
