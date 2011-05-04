/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Or Goshen, Pavel Ocheretny
 *
 * Copyright (C) 1999-2011 Intel, Inc. (www.intel.com)
 *
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
#include "e-ews-move-items-operation.h"
#include <stdarg.h>

G_DEFINE_TYPE (EEwsMoveItemsOperation, e_ews_move_items_operation, E_TYPE_EWS_OPERATION)

struct _EEwsMoveItemsOperationPrivate {
	e_ews_move_items_operation_cb cb;
	gpointer user_data;
};

#define E_EWS_MOVE_ITEMS_OPERATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), E_TYPE_EWS_MOVE_ITEMS_OPERATION, EEwsMoveItemsOperationPrivate))

static EEwsOperationClass *parent_class = NULL;

/*
 * Callback for synchroneous invocation
 */
struct e_ews_move_items_operation_data {
	GError *error;
	GSList *items;
};

static void
e_ews_move_items_operation_callback (GSList *items, gpointer user_data, const GError *error)
{
	struct e_ews_move_items_operation_data *data = user_data;

	if (error == NULL) {
		data->error = NULL;
	}
	else {
		data->error = g_error_copy (error);
	}
	
	data->items = items;
}

/* Class private implementation
 */
static void
move_items_operation_parse (EEwsOperation *op, ESoapParameter *response, GError *conn_error)
{
	ESoapParameter *subparam, *node;
	EwsId *item;
	GSList *items = NULL;
	GError *error = NULL;

	// check for connection or response error
	if (conn_error)
	{
		g_propagate_error (&error, conn_error);

		goto finish;
	}

	for (subparam = e_soap_parameter_get_first_child (response);
		subparam; subparam = e_soap_parameter_get_next_child (response)) {

		// make sure we got a "MoveItemResponseMessage"
		if (g_strcmp0((const char *)subparam->name, "MoveItemResponseMessage") != 0)
		{
			g_set_error	(&error,
						 EWS_CONNECTION_ERROR,
						 EWS_CONNECTION_ERROR_CORRUPTDATA,
						 "Expected 'MoveItemsResponseMessage' in response, but got '%s'", subparam->name);

			goto finish;
		}

		// make sure the exchange didnt return an error
		if (e_ews_operation_is_response_error (subparam, &error)) {
			g_warning ("Got an error while parsing ItemID: %s\n", error->message);
			g_clear_error (&error);
		}

		// iterate over "Items"
		for (node = e_soap_parameter_get_first_child_by_name (subparam, "Items");
		     node; node = e_soap_parameter_get_next_child_by_name (subparam, "Items")) {
			item = e_ews_parse_item_id (node, &error);
			items = g_slist_append (items, item);
		}
	}

finish:

	if (error && items) {
		g_slist_foreach (items,(GFunc) e_ews_free_id, NULL);
		g_slist_free (items);
		items = NULL;
	}

	// we assume the user will release the items list, otherwise (in case of no callback) we free it ourselves
	if (E_EWS_MOVE_ITEMS_OPERATION (op)->priv->cb != NULL)
			E_EWS_MOVE_ITEMS_OPERATION (op)->priv->cb (items, E_EWS_MOVE_ITEMS_OPERATION (op)->priv->user_data, error);
	else
	{
		g_slist_foreach (items,(GFunc) e_ews_free_id, NULL);
		g_slist_free (items);
		items = NULL;
	}

	// cleanup
	g_clear_error (&error);
}

/* Class and instances handling
 */
static void
e_ews_move_items_operation_dispose (GObject *self)
{
	EEwsMoveItemsOperation *move_items_operation = E_EWS_MOVE_ITEMS_OPERATION(self);
	EEwsMoveItemsOperationPrivate *priv;

	g_return_if_fail (E_IS_EWS_MOVE_ITEMS_OPERATION (move_items_operation));
	
	priv = move_items_operation->priv;
	
	/* Chain up to the parent class */
	G_OBJECT_CLASS (e_ews_move_items_operation_parent_class)->dispose (self);
}

static void
e_ews_move_items_operation_finalize (GObject *self)
{
	EEwsMoveItemsOperation *move_items_operation = E_EWS_MOVE_ITEMS_OPERATION(self);
	EEwsMoveItemsOperationPrivate *priv;

	g_return_if_fail (E_IS_EWS_MOVE_ITEMS_OPERATION (move_items_operation));

	priv = move_items_operation->priv;

	/* Chain up to the parent class */
	G_OBJECT_CLASS (e_ews_move_items_operation_parent_class)->finalize (self);
}

static void
e_ews_move_items_operation_class_init (EEwsMoveItemsOperationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_ews_move_items_operation_dispose;
	object_class->finalize = e_ews_move_items_operation_finalize;
	
	g_type_class_add_private (klass, sizeof (EEwsMoveItemsOperationPrivate));

	E_EWS_OPERATION_CLASS(klass)->parse = move_items_operation_parse;
}

static void
e_ews_move_items_operation_init (EEwsMoveItemsOperation *move_items_operation)
{
	move_items_operation->priv = E_EWS_MOVE_ITEMS_OPERATION_GET_PRIVATE(move_items_operation);
}

/*
 * Class interface
 */
EEwsMoveItemsOperation *
e_ews_move_items_operation_new(EEwsConnection *cnc)
{
	EEwsMoveItemsOperation *op = g_object_new (E_TYPE_EWS_MOVE_ITEMS_OPERATION, NULL);
	e_ews_operation_set_connection (E_EWS_OPERATION (op), cnc);
	e_ews_operation_set_name (E_EWS_OPERATION (op), "MoveItem");
	
	return op;
}

void
e_ews_move_items_operation_prepare (EEwsMoveItemsOperation *op,
				const gchar *folder_id,
				gboolean docopy,
				const GSList *ids)
{
	EEwsConnection *cnc;
	ESoapMessage *msg;
	const gchar *uri, *email;
	GSList *id_item;

	g_return_if_fail (E_IS_EWS_MOVE_ITEMS_OPERATION (op));

	cnc = e_ews_operation_get_connection (E_EWS_OPERATION (op));
	uri = e_ews_connection_get_uri (cnc);
	email = e_ews_connection_get_email (cnc);
	/* Prepare soap message
	 */
	if (docopy)
		msg = e_ews_message_new_with_header (uri, "CopyItem",
					     NULL, NULL, EWS_EXCHANGE_2007);
	else
		msg = e_ews_message_new_with_header (uri, "MoveItem",
					     NULL, NULL, EWS_EXCHANGE_2007);

	e_soap_message_start_element (msg, "ToFolderId", "messages", NULL);
	e_soap_message_start_element (msg, "FolderId", NULL, NULL);
	e_soap_message_add_attribute (msg, "Id", folder_id, NULL, NULL);
	e_soap_message_end_element (msg); /* FolderId */
	e_soap_message_end_element (msg); /* ToFolderId */

	e_soap_message_start_element (msg, "ItemIds", "messages", NULL);

	for (id_item = (GSList*) ids; id_item != NULL; id_item = g_slist_next (id_item))
		e_ews_message_write_string_parameter_with_attribute (msg, "ItemId", NULL, NULL, "Id", id_item->data);

	e_soap_message_end_element (msg); /* ItemIds */

	e_ews_message_write_footer (msg);

	/* Set operation message
	 */
	e_ews_operation_set_message(E_EWS_OPERATION (op), msg);

	// cleanup
	g_object_unref(msg);
}

void
e_ews_move_items_operation_submit_async (EEwsMoveItemsOperation *op,
			e_ews_move_items_operation_cb cb,
			GCancellable *cancellable,
			gpointer user_data)
{
	g_return_if_fail (E_IS_EWS_MOVE_ITEMS_OPERATION (op));

	op->priv->cb = cb;
	op->priv->user_data = user_data;

	e_ews_operation_submit (E_EWS_OPERATION (op), cancellable, EWS_PRIORITY_MEDIUM, NULL);
}

/*
 * e_ews_move_items_operation_sync()
 * Convenience function.
 * Does everything for you synchroneously.
 * 
 * returns a list of newly created items
 */
GSList *
e_ews_move_items_operation_sync (EEwsConnection *cnc,
				 const gchar *folder_id, gboolean docopy, const GSList *ids,
				 GError **error)
{
	EEwsMoveItemsOperation *op;
	struct e_ews_move_items_operation_data data;

	op = e_ews_move_items_operation_new(cnc);

	e_ews_move_items_operation_prepare (op, folder_id, docopy, ids);

	op->priv->cb = e_ews_move_items_operation_callback;
	op->priv->user_data = &data;

	e_ews_operation_submit (E_EWS_OPERATION (op), NULL, EWS_PRIORITY_MEDIUM, e_flag_new ());

	g_object_unref (op);
	
	if (data.error)
	{
		g_propagate_error (error, data.error);
		g_clear_error (&data.error);
	}

	return data.items;
}
