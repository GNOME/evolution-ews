/*
 * SPDX-FileCopyrightText: (C) 2016 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "camel/camel.h"
#include "camel-ews-summary.h"

#include "camel-ews-message-info.h"

struct _CamelEwsMessageInfoPrivate {
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

G_DEFINE_TYPE_WITH_PRIVATE (CamelEwsMessageInfo, camel_ews_message_info, CAMEL_TYPE_MESSAGE_INFO_BASE)

static CamelMessageInfo *
ews_message_info_clone (const CamelMessageInfo *mi,
			CamelFolderSummary *assign_summary)
{
	CamelMessageInfo *result;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (mi), NULL);

	result = CAMEL_MESSAGE_INFO_CLASS (camel_ews_message_info_parent_class)->clone (mi, assign_summary);
	if (!result)
		return NULL;

	if (CAMEL_IS_EWS_MESSAGE_INFO (result)) {
		CamelEwsMessageInfo *emi, *emi_result;

		emi = CAMEL_EWS_MESSAGE_INFO (mi);
		emi_result = CAMEL_EWS_MESSAGE_INFO (result);

		camel_ews_message_info_set_server_flags (emi_result, camel_ews_message_info_get_server_flags (emi));
		camel_ews_message_info_set_item_type (emi_result, camel_ews_message_info_get_item_type (emi));
		camel_ews_message_info_take_change_key (emi_result, camel_ews_message_info_dup_change_key (emi));
	}

	return result;
}

static gboolean
ews_message_info_load (CamelMessageInfo *mi,
		       const CamelStoreDBMessageRecord *record,
		       /* const */ gchar **bdata_ptr)
{
	CamelEwsMessageInfo *emi;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (mi), FALSE);
	g_return_val_if_fail (record != NULL, FALSE);
	g_return_val_if_fail (bdata_ptr != NULL, FALSE);

	if (!CAMEL_MESSAGE_INFO_CLASS (camel_ews_message_info_parent_class)->load ||
	    !CAMEL_MESSAGE_INFO_CLASS (camel_ews_message_info_parent_class)->load (mi, record, bdata_ptr))
		return FALSE;

	emi = CAMEL_EWS_MESSAGE_INFO (mi);

	if (*bdata_ptr) {
		gchar **values;

		values = g_strsplit (*bdata_ptr, " ", -1);

		if (values && values[0] && values[1] && values[2]) {
			camel_ews_message_info_set_server_flags (emi, g_ascii_strtoll (values[0], NULL, 10));
			camel_ews_message_info_set_item_type (emi, g_ascii_strtoll (values[1], NULL, 10));
			camel_ews_message_info_set_change_key (emi, values[2]);
		}

		g_strfreev (values);
	}

	return TRUE;
}

static gboolean
ews_message_info_save (const CamelMessageInfo *mi,
		       CamelStoreDBMessageRecord *record,
		       GString *bdata_str)
{
	CamelEwsMessageInfo *emi;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (mi), FALSE);
	g_return_val_if_fail (record != NULL, FALSE);
	g_return_val_if_fail (bdata_str != NULL, FALSE);

	if (!CAMEL_MESSAGE_INFO_CLASS (camel_ews_message_info_parent_class)->save ||
	    !CAMEL_MESSAGE_INFO_CLASS (camel_ews_message_info_parent_class)->save (mi, record, bdata_str))
		return FALSE;

	emi = CAMEL_EWS_MESSAGE_INFO (mi);

	g_string_append_printf (bdata_str, "%u %d %s",
		camel_ews_message_info_get_server_flags (emi),
		camel_ews_message_info_get_item_type (emi),
		camel_ews_message_info_get_change_key (emi));

	return TRUE;
}

static void
ews_message_info_set_property (GObject *object,
			       guint property_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	CamelEwsMessageInfo *emi = CAMEL_EWS_MESSAGE_INFO (object);

	switch (property_id) {
	case PROP_SERVER_FLAGS:
		camel_ews_message_info_set_server_flags (emi, g_value_get_uint (value));
		return;

	case PROP_ITEM_TYPE:
		camel_ews_message_info_set_item_type (emi, g_value_get_int (value));
		return;

	case PROP_CHANGE_KEY:
		camel_ews_message_info_set_change_key (emi, g_value_get_string (value));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_message_info_get_property (GObject *object,
			       guint property_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	CamelEwsMessageInfo *emi = CAMEL_EWS_MESSAGE_INFO (object);

	switch (property_id) {

	case PROP_SERVER_FLAGS:
		g_value_set_uint (value, camel_ews_message_info_get_server_flags (emi));
		return;

	case PROP_ITEM_TYPE:
		g_value_set_int (value, camel_ews_message_info_get_item_type (emi));
		return;

	case PROP_CHANGE_KEY:
		g_value_take_string (value, camel_ews_message_info_dup_change_key (emi));
		return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_message_info_dispose (GObject *object)
{
	CamelEwsMessageInfo *emi = CAMEL_EWS_MESSAGE_INFO (object);

	g_free (emi->priv->change_key);
	emi->priv->change_key = NULL;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_ews_message_info_parent_class)->dispose (object);
}

static void
camel_ews_message_info_class_init (CamelEwsMessageInfoClass *class)
{
	CamelMessageInfoClass *mi_class;
	GObjectClass *object_class;

	mi_class = CAMEL_MESSAGE_INFO_CLASS (class);
	mi_class->clone = ews_message_info_clone;
	mi_class->load = ews_message_info_load;
	mi_class->save = ews_message_info_save;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_message_info_set_property;
	object_class->get_property = ews_message_info_get_property;
	object_class->dispose = ews_message_info_dispose;

	/**
	 * CamelEwsMessageInfo:server-flags
	 *
	 * Last known server flags of the message.
	 *
	 * Since: 3.24
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SERVER_FLAGS,
		g_param_spec_uint (
			"server-flags",
			"Server Flags",
			NULL,
			0, G_MAXUINT32, 0,
			G_PARAM_READWRITE));

	/**
	 * CamelEwsMessageInfo:item-type
	 *
	 * Item type of the message.
	 *
	 * Since: 3.24
	 **/
	g_object_class_install_property (
		object_class,
		PROP_ITEM_TYPE,
		g_param_spec_int (
			"item-type",
			"Item Type",
			NULL,
			0, G_MAXINT32, 0,
			G_PARAM_READWRITE));

	/**
	 * CamelEwsMessageInfo:change-key
	 *
	 * Change key of the message on the server.
	 *
	 * Since: 3.24
	 **/
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
camel_ews_message_info_init (CamelEwsMessageInfo *emi)
{
	emi->priv = camel_ews_message_info_get_instance_private (emi);
}

guint32
camel_ews_message_info_get_server_flags (const CamelEwsMessageInfo *emi)
{
	CamelMessageInfo *mi;
	guint32 result;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), 0);

	mi = CAMEL_MESSAGE_INFO (emi);

	camel_message_info_property_lock (mi);
	result = emi->priv->server_flags;
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_ews_message_info_set_server_flags (CamelEwsMessageInfo *emi,
					 guint32 server_flags)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), FALSE);

	mi = CAMEL_MESSAGE_INFO (emi);

	camel_message_info_property_lock (mi);

	changed = emi->priv->server_flags != server_flags;

	if (changed)
		emi->priv->server_flags = server_flags;

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (emi), "server-flags");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}

gint32
camel_ews_message_info_get_item_type (const CamelEwsMessageInfo *emi)
{
	CamelMessageInfo *mi;
	gint32 result;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), 0);

	mi = CAMEL_MESSAGE_INFO (emi);

	camel_message_info_property_lock (mi);
	result = emi->priv->item_type;
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_ews_message_info_set_item_type (CamelEwsMessageInfo *emi,
				      gint32 item_type)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), FALSE);

	mi = CAMEL_MESSAGE_INFO (emi);

	camel_message_info_property_lock (mi);

	changed = emi->priv->item_type != item_type;

	if (changed)
		emi->priv->item_type = item_type;

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (emi), "item-type");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}

const gchar *
camel_ews_message_info_get_change_key (const CamelEwsMessageInfo *emi)
{
	CamelMessageInfo *mi;
	const gchar *result;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), NULL);

	mi = CAMEL_MESSAGE_INFO (emi);

	camel_message_info_property_lock (mi);
	result = emi->priv->change_key;
	camel_message_info_property_unlock (mi);

	return result;
}

gchar *
camel_ews_message_info_dup_change_key (const CamelEwsMessageInfo *emi)
{
	CamelMessageInfo *mi;
	gchar *result;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), NULL);

	mi = CAMEL_MESSAGE_INFO (emi);

	camel_message_info_property_lock (mi);
	result = g_strdup (emi->priv->change_key);
	camel_message_info_property_unlock (mi);

	return result;
}

gboolean
camel_ews_message_info_set_change_key (CamelEwsMessageInfo *emi,
				       const gchar *change_key)
{
	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), FALSE);

	return camel_ews_message_info_take_change_key (emi, g_strdup (change_key));
}

gboolean
camel_ews_message_info_take_change_key (CamelEwsMessageInfo *emi,
					gchar *change_key)
{
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_EWS_MESSAGE_INFO (emi), FALSE);

	mi = CAMEL_MESSAGE_INFO (emi);

	camel_message_info_property_lock (mi);

	changed = g_strcmp0 (emi->priv->change_key, change_key) != 0;

	if (changed) {
		g_free (emi->priv->change_key);
		emi->priv->change_key = change_key;
	} else if (change_key != emi->priv->change_key) {
		g_free (change_key);
	}

	camel_message_info_property_unlock (mi);

	if (changed && !camel_message_info_get_abort_notifications (mi)) {
		g_object_notify (G_OBJECT (emi), "change-key");
		camel_message_info_set_dirty (mi, TRUE);
	}

	return changed;
}
