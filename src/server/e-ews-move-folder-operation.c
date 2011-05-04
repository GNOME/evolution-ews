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
#include "e-ews-move-folder-operation.h"
#include <stdarg.h>

G_DEFINE_TYPE (EEwsMoveFolderOperation, e_ews_move_folder_operation, E_TYPE_EWS_OPERATION)

struct _EEwsMoveFolderOperationPrivate {
	e_ews_move_folder_operation_cb cb;
	gpointer user_data;
};

#define E_EWS_MOVE_FOLDER_OPERATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), E_TYPE_EWS_MOVE_FOLDER_OPERATION, EEwsMoveFolderOperationPrivate))

static EEwsOperationClass *parent_class = NULL;

/*
 * Callback for synchroneous invocation
 */
struct e_ews_move_folder_operation_data {
	GError *error;
	gboolean success;
};

static void
e_ews_move_folder_operation_callback (gboolean success, gpointer user_data, const GError *error)
{
	struct e_ews_move_folder_operation_data *data = user_data;

	if (error == NULL) {
		data->error = NULL;
	}
	else {
		data->error = g_error_copy (error);
	}
	data->success = success;
}

/* Class private implementation
 */
static void
move_folder_operation_parse (EEwsOperation *op, ESoapParameter *response, GError *conn_error)
{
	ESoapParameter *subparam;
	gboolean success = FALSE;
	GError *error = NULL;

	subparam = e_soap_parameter_get_first_child (response);

	// check for connection or response error
	if (conn_error)
	{
		g_propagate_error (&error, conn_error);

		goto finish;
	}
	// make sure we got a "MoveFolderResponseMessage"
	if (g_strcmp0((const char *)subparam->name, "MoveFolderResponseMessage") != 0)
	{
		g_set_error	(&error,
					 EWS_CONNECTION_ERROR,
					 EWS_CONNECTION_ERROR_CORRUPTDATA,
					 "Expected 'MoveFolderResponseMessage' in response, but got '%s'", subparam->name);

		goto finish;
	}

	// make sure the exchange didnt return an error
	if (e_ews_operation_is_response_error (subparam, &error))
		goto finish;

	success = TRUE;

finish:
	if (E_EWS_MOVE_FOLDER_OPERATION (op)->priv->cb != NULL)
			E_EWS_MOVE_FOLDER_OPERATION (op)->priv->cb (success, E_EWS_MOVE_FOLDER_OPERATION (op)->priv->user_data, error);

	// cleanup
	g_clear_error (&error);
}

/* Class and instances handling
 */
static void
e_ews_move_folder_operation_dispose (GObject *self)
{
	EEwsMoveFolderOperation *move_folder_operation = E_EWS_MOVE_FOLDER_OPERATION(self);
	EEwsMoveFolderOperationPrivate *priv;

	g_return_if_fail (E_IS_EWS_MOVE_FOLDER_OPERATION (move_folder_operation));
	
	priv = move_folder_operation->priv;
	
	/* Chain up to the parent class */
	G_OBJECT_CLASS (e_ews_move_folder_operation_parent_class)->dispose (self);
}

static void
e_ews_move_folder_operation_finalize (GObject *self)
{
	EEwsMoveFolderOperation *move_folder_operation = E_EWS_MOVE_FOLDER_OPERATION(self);
	EEwsMoveFolderOperationPrivate *priv;

	g_return_if_fail (E_IS_EWS_MOVE_FOLDER_OPERATION (move_folder_operation));

	priv = move_folder_operation->priv;

	/* Chain up to the parent class */
	G_OBJECT_CLASS (e_ews_move_folder_operation_parent_class)->finalize (self);
}

static void
e_ews_move_folder_operation_class_init (EEwsMoveFolderOperationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_ews_move_folder_operation_dispose;
	object_class->finalize = e_ews_move_folder_operation_finalize;
	
	g_type_class_add_private (klass, sizeof (EEwsMoveFolderOperationPrivate));

	E_EWS_OPERATION_CLASS(klass)->parse = move_folder_operation_parse;
}

static void
e_ews_move_folder_operation_init (EEwsMoveFolderOperation *move_folder_operation)
{
	move_folder_operation->priv = E_EWS_MOVE_FOLDER_OPERATION_GET_PRIVATE(move_folder_operation);
}

/*
 * Class interface
 */
EEwsMoveFolderOperation *
e_ews_move_folder_operation_new(EEwsConnection *cnc)
{
	EEwsMoveFolderOperation *op = g_object_new (E_TYPE_EWS_MOVE_FOLDER_OPERATION, NULL);
	e_ews_operation_set_connection (E_EWS_OPERATION (op), cnc);
	e_ews_operation_set_name (E_EWS_OPERATION (op), "MoveFolder");

	return op;
}

void
e_ews_move_folder_operation_prepare (EEwsMoveFolderOperation *op,
				const gchar *to_folder,
				const gchar *folder)
{
	EEwsConnection *cnc;
	ESoapMessage *msg;
	const gchar *uri, *email;

	g_return_if_fail (E_IS_EWS_MOVE_FOLDER_OPERATION (op));

	cnc = e_ews_operation_get_connection (E_EWS_OPERATION (op));
	uri = e_ews_connection_get_uri (cnc);
	email = e_ews_connection_get_email (cnc);
	/* Prepare soap message
	 */
	msg = e_ews_message_new_with_header (uri, "MoveFolder",
					     NULL, NULL, EWS_EXCHANGE_2007);

	e_soap_message_start_element (msg, "ToFolderId", "messages", NULL);
	if (to_folder)
		e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", NULL,
								     NULL, "Id", to_folder);
	else
		e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", NULL,
								     NULL, "Id", "msgfolderroot");

	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "FolderIds", "messages", NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "FolderId", NULL,
							     NULL, "Id", folder);
	e_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);

	/* Set operation message
	 */
	e_ews_operation_set_message(E_EWS_OPERATION (op), msg);

	// cleanup
	g_object_unref(msg);
}

void
e_ews_move_folder_operation_submit_async (EEwsMoveFolderOperation *op,
			e_ews_move_folder_operation_cb cb,
			GCancellable *cancellable,
			gpointer user_data)
{
	g_return_if_fail (E_IS_EWS_MOVE_FOLDER_OPERATION (op));

	op->priv->cb = cb;
	op->priv->user_data = user_data;

	e_ews_operation_submit (E_EWS_OPERATION (op), cancellable, EWS_PRIORITY_MEDIUM, NULL);
}

/*
 * e_ews_move_folder_operation_sync()
 * Convenience function.
 * Does everything for you synchroneously.
 * 
 * returns TRUE/FALSE based on success.
 */
gboolean
e_ews_move_folder_operation_sync (EEwsConnection *cnc,
				const gchar *to_folder,
				const gchar *folder,
				GError **error)
{
	EEwsMoveFolderOperation *op;
	struct e_ews_move_folder_operation_data data;

	op = e_ews_move_folder_operation_new(cnc);

	e_ews_move_folder_operation_prepare (op, to_folder, folder);

	op->priv->cb = e_ews_move_folder_operation_callback;
	op->priv->user_data = &data;

	e_ews_operation_submit (E_EWS_OPERATION (op), NULL, EWS_PRIORITY_MEDIUM, e_flag_new ());

	if (data.error)
	{
		g_propagate_error (error, data.error);
		g_clear_error (&data.error);
	}

	g_object_unref (op);

	return (*error == NULL);
}
