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

/* https://docs.microsoft.com/en-us/graph/delta-query-overview */

gboolean
e_o365_delta_is_removed_object (JsonObject *object)
{
	return json_object_has_member (object, "@removed");
}

/* https://docs.microsoft.com/en-us/graph/api/resources/mailfolder?view=graph-rest-1.0 */

const gchar *
e_o365_mail_folder_get_display_name (JsonObject *object)
{
	return e_o365_json_get_string_member (object, "displayName", NULL);
}

const gchar *
e_o365_mail_folder_get_id (JsonObject *object)
{
	return e_o365_json_get_string_member (object, "id", NULL);
}

const gchar *
e_o365_mail_folder_get_parent_folder_id (JsonObject *object)
{
	return e_o365_json_get_string_member (object, "parentFolderId", NULL);
}

gint32
e_o365_mail_folder_get_child_folder_count (JsonObject *object)
{
	return (gint32) e_o365_json_get_int_member (object, "childFolderCount", 0);
}

gint32
e_o365_mail_folder_get_total_item_count (JsonObject *object)
{
	return (gint32) e_o365_json_get_int_member (object, "totalItemCount", 0);
}

gint32
e_o365_mail_folder_get_unread_item_count (JsonObject *object)
{
	return (gint32) e_o365_json_get_int_member (object, "unreadItemCount", 0);
}
