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

#include "camel/camel.h"

#include "camel-o365-message-info.h"

struct _CamelO365MessageInfoPrivate {
	guint32 server_flags;
	gint32 item_type;
	gchar *change_key;
};

enum {
	PROP_0,
	PROP_SERVER_FLAGS,
	PROP_ITEM_TYPE,
	PROP_CHANGE_KEY,
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelO365MessageInfo, camel_o365_message_info, CAMEL_TYPE_MESSAGE_INFO_BASE)

static CamelMessageInfo *
o365_message_info_clone (const CamelMessageInfo *mi,
			 CamelFolderSummary *assign_summary)
{
	CamelMessageInfo *result;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (mi), NULL);

	result = CAMEL_MESSAGE_INFO_CLASS (camel_o365_message_info_parent_class)->clone (mi, assign_summary);
	if (!result)
		return NULL;

	if (CAMEL_IS_O365_MESSAGE_INFO (result)) {
		CamelO365MessageInfo *omi, *omi_result;

		omi = CAMEL_O365_MESSAGE_INFO (mi);
		omi_result = CAMEL_O365_MESSAGE_INFO (result);

		camel_o365_message_info_set_server_flags (omi_result, camel_o365_message_info_get_server_flags (omi));
		camel_o365_message_info_set_item_type (omi_result, camel_o365_message_info_get_item_type (omi));
		camel_o365_message_info_take_change_key (omi_result, camel_o365_message_info_dup_change_key (omi));
	}

	return result;
}

static gboolean
o365_message_info_load (CamelMessageInfo *mi,
			const CamelMIRecord *record,
			/* const */ gchar **bdata_ptr)
{
	CamelO365MessageInfo *omi;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (mi), FALSE);
	g_return_val_if_fail (record != NULL, FALSE);
	g_return_val_if_fail (bdata_ptr != NULL, FALSE);

	if (!CAMEL_MESSAGE_INFO_CLASS (camel_o365_message_info_parent_class)->load ||
	    !CAMEL_MESSAGE_INFO_CLASS (camel_o365_message_info_parent_class)->load (mi, record, bdata_ptr))
		return FALSE;

	omi = CAMEL_O365_MESSAGE_INFO (mi);

	if (*bdata_ptr) {
		gchar **values;

		values = g_strsplit (*bdata_ptr, " ", -1);

		if (values && values[0] && values[1] && values[2]) {
			camel_o365_message_info_set_server_flags (omi, g_ascii_strtoll (values[0], NULL, 10));
			camel_o365_message_info_set_item_type (omi, g_ascii_strtoll (values[1], NULL, 10));
			camel_o365_message_info_set_change_key (omi, values[2]);
		}

		g_strfreev (values);
	}

	return TRUE;
}

static gboolean
o365_message_info_save (const CamelMessageInfo *mi,
			CamelMIRecord *record,
			GString *bdata_str)
{
	CamelO365MessageInfo *omi;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (mi), FALSE);
	g_return_val_if_fail (record != NULL, FALSE);
	g_return_val_if_fail (bdata_str != NULL, FALSE);

	if (!CAMEL_MESSAGE_INFO_CLASS (camel_o365_message_info_parent_class)->save ||
	    !CAMEL_MESSAGE_INFO_CLASS (camel_o365_message_info_parent_class)->save (mi, record, bdata_str))
		return FALSE;

	omi = CAMEL_O365_MESSAGE_INFO (mi);

	g_string_append_printf (bdata_str, "%u %d %s",
		camel_o365_message_info_get_server_flags (omi),
		camel_o365_message_info_get_item_type (omi),
		camel_o365_message_info_get_change_key (omi));

	return TRUE;
}

static void
o365_message_info_set_property (GObject *object,
				guint property_id,
				const GValue *value,
				GParamSpec *pspec)
{
	CamelO365MessageInfo *omi = CAMEL_O365_MESSAGE_INFO (object);

	switch (property_id) {
	case PROP_SERVER_FLAGS:
		camel_o365_message_info_set_server_flags (omi, g_value_get_uint (value));
		return;

	case PROP_ITEM_TYPE:
		camel_o365_message_info_set_item_type (omi, g_value_get_int (value));
		return;

	case PROP_CHANGE_KEY:
		camel_o365_message_info_set_change_key (omi, g_value_get_string (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
o365_message_info_get_property (GObject *object,
				guint property_id,
				GValue *value,
				GParamSpec *pspec)
{
	CamelO365MessageInfo *omi = CAMEL_O365_MESSAGE_INFO (object);

	switch (property_id) {

	case PROP_SERVER_FLAGS:
		g_value_set_uint (value, camel_o365_message_info_get_server_flags (omi));
		return;

	case PROP_ITEM_TYPE:
		g_value_set_int (value, camel_o365_message_info_get_item_type (omi));
		return;

	case PROP_CHANGE_KEY:
		g_value_take_string (value, camel_o365_message_info_dup_change_key (omi));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
o365_message_info_dispose (GObject *object)
{
	CamelO365MessageInfo *omi = CAMEL_O365_MESSAGE_INFO (object);

	g_free (omi->priv->change_key);
	omi->priv->change_key = NULL;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_o365_message_info_parent_class)->dispose (object);
}

static void
camel_o365_message_info_class_init (CamelO365MessageInfoClass *class)
{
	CamelMessageInfoClass *mi_class;
	GObjectClass *object_class;

	mi_class = CAMEL_MESSAGE_INFO_CLASS (class);
	mi_class->clone = o365_message_info_clone;
	mi_class->load = o365_message_info_load;
	mi_class->save = o365_message_info_save;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = o365_message_info_set_property;
	object_class->get_property = o365_message_info_get_property;
	object_class->dispose = o365_message_info_dispose;

	/*
	 * CamelO365MessageInfo:server-flags
	 *
	 * Last known server flags of the message.
	 */
	g_object_class_install_property (
		object_class,
		PROP_SERVER_FLAGS,
		g_param_spec_uint (
			"server-flags",
			"Server Flags",
			NULL,
			0, G_MAXUINT32, 0,
			G_PARAM_READWRITE));

	/*
	 * CamelO365MessageInfo:item-type
	 *
	 * Item type of the message.
	 */
	g_object_class_install_property (
		object_class,
		PROP_ITEM_TYPE,
		g_param_spec_int (
			"item-type",
			"Item Type",
			NULL,
			0, G_MAXINT32, 0,
			G_PARAM_READWRITE));

	/*
	 * CamelO365MessageInfo:change-key
	 *
	 * Change key of the message on the server.
	 */
	g_object_class_install_property (
		object_class,
		PROP_CHANGE_KEY,
		g_param_spec_string (
			"change-key",
			"Change Key",
			NULL,
			NULL,
			G_PARAM_READWRITE));
}

static void
camel_o365_message_info_init (CamelO365MessageInfo *omi)
{
	omi->priv = camel_o365_message_info_get_instance_private (omi);
}

guint32
camel_o365_message_info_get_server_flags (const CamelO365MessageInfo *omi)
{
	CamelMessageInfo *mi;
	guint32 result;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), 0);

	mi = CAMEL_MESSAGE_INFO (omi);

	camel_message_info_property_lock (mi);
	result = omi->priv->server_flags;
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_o365_message_info_set_server_flags (CamelO365MessageInfo *omi,
					  guint32 server_flags)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), FALSE);

	mi = CAMEL_MESSAGE_INFO (omi);

	camel_message_info_property_lock (mi);

	changed = omi->priv->server_flags != server_flags;

	if (changed)
		omi->priv->server_flags = server_flags;

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (omi), "server-flags");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}

gint32
camel_o365_message_info_get_item_type (const CamelO365MessageInfo *omi)
{
	CamelMessageInfo *mi;
	gint32 result;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), 0);

	mi = CAMEL_MESSAGE_INFO (omi);

	camel_message_info_property_lock (mi);
	result = omi->priv->item_type;
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_o365_message_info_set_item_type (CamelO365MessageInfo *omi,
				       gint32 item_type)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), FALSE);

	mi = CAMEL_MESSAGE_INFO (omi);

	camel_message_info_property_lock (mi);

	changed = omi->priv->item_type != item_type;

	if (changed)
		omi->priv->item_type = item_type;

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (omi), "item-type");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}

const gchar *
camel_o365_message_info_get_change_key (const CamelO365MessageInfo *omi)
{
	CamelMessageInfo *mi;
	const gchar *result;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), NULL);

	mi = CAMEL_MESSAGE_INFO (omi);

	camel_message_info_property_lock (mi);
	result = omi->priv->change_key;
	camel_message_info_property_unlock (mi);

	return result;
}

gchar *
camel_o365_message_info_dup_change_key (const CamelO365MessageInfo *omi)
{
	CamelMessageInfo *mi;
	gchar *result;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), NULL);

	mi = CAMEL_MESSAGE_INFO (omi);

	camel_message_info_property_lock (mi);
	result = g_strdup (omi->priv->change_key);
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_o365_message_info_set_change_key (CamelO365MessageInfo *omi,
					const gchar *change_key)
{
	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), FALSE);

	return camel_o365_message_info_take_change_key (omi, g_strdup (change_key));
}

gboolean
camel_o365_message_info_take_change_key (CamelO365MessageInfo *omi,
					 gchar *change_key)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_O365_MESSAGE_INFO (omi), FALSE);

	mi = CAMEL_MESSAGE_INFO (omi);

	camel_message_info_property_lock (mi);

	changed = g_strcmp0 (omi->priv->change_key, change_key) != 0;

	if (changed) {
		g_free (omi->priv->change_key);
		omi->priv->change_key = change_key;
	} else if (change_key != omi->priv->change_key) {
		g_free (change_key);
	}

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (omi), "change-key");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}
