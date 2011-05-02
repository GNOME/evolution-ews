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
#include "e-ews-operation.h"

G_DEFINE_TYPE (EEwsOperation, e_ews_operation, G_TYPE_OBJECT)

struct _EEwsOperationPrivate {
	EEwsConnection *cnc;
	
	ESoapMessage *msg;
};

#define E_EWS_OPERATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), E_TYPE_EWS_OPERATION, EEwsOperationPrivate))

/*
 * General callback from the transport layer
 */
static void
e_ews_operation_callback (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	EEwsOperation *operation = E_EWS_OPERATION (source);
	EEwsOperationClass *class = E_EWS_OPERATION_GET_CLASS (source);
	ESoapResponse *response = NULL;
	EFlag *eflag = (EFlag *)user_data;

	if (!g_simple_async_result_propagate_error ((GSimpleAsyncResult *) res, &error))
	{
		response = E_SOAP_RESPONSE(g_simple_async_result_get_op_res_gpointer ((GSimpleAsyncResult *) res));
	}

	if (!error)
	{
		class->parse (operation,
			e_soap_response_get_first_parameter_by_name (response, "ResponseMessages"), error);

		g_object_unref (response);

	} else
	{
		class->parse (operation, NULL, error);
	}

	// cleanup
	g_clear_error (&error);

	if (eflag != NULL)
		e_flag_set (eflag);
}

/* Class and instances handling
 */
static void
e_ews_operation_dispose (GObject *self)
{
	EEwsOperation *operation = E_EWS_OPERATION(self);
	EEwsOperationPrivate *priv;

	g_return_if_fail (E_IS_EWS_OPERATION (operation));
	
	priv = operation->priv;
	
	if (priv->msg) {
		g_object_unref(priv->msg);
		priv->msg = NULL;
	}
	
	if (priv->cnc) {
		g_object_unref(priv->cnc);
		priv->msg = NULL;
	}
	
	/* Chain up to the parent class */
	G_OBJECT_CLASS (e_ews_operation_parent_class)->dispose (self);
}

static void
e_ews_operation_finalize (GObject *self)
{
	EEwsOperation *operation = E_EWS_OPERATION(self);
	EEwsOperationPrivate *priv;

	g_return_if_fail (E_IS_EWS_OPERATION (operation));

	priv = operation->priv;

	/* Chain up to the parent class */
	G_OBJECT_CLASS (e_ews_operation_parent_class)->finalize (self);
}

static void
e_ews_operation_class_init (EEwsOperationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = e_ews_operation_dispose;
	object_class->finalize = e_ews_operation_finalize;
	
	g_type_class_add_private (klass, sizeof (EEwsOperationPrivate));

	klass->parse = NULL;
}

static void
e_ews_operation_init (EEwsOperation *operation)
{
	operation->priv = E_EWS_OPERATION_GET_PRIVATE(operation);
	
	operation->priv->msg = NULL;
}

/* Class interface
 */
void e_ews_operation_submit (EEwsOperation *self, GCancellable *cancellable, EwsOperationPriority priority, EFlag *eflag)
{	
	g_return_if_fail (E_IS_EWS_OPERATION (self));
	g_return_if_fail (self->priv->msg != NULL);

	e_ews_connection_queue_operation (self->priv->cnc, self->priv->msg, cancellable, priority, e_ews_operation_callback, (GObject *)(self), eflag);

	if (eflag != NULL)
	{
		e_flag_wait (eflag);
		e_flag_free (eflag);
	}
}

void e_ews_operation_set_message(EEwsOperation *op, ESoapMessage *msg)
{
	g_return_if_fail (E_IS_EWS_OPERATION (op));
	
	op->priv->msg = g_object_ref(msg);
}

void e_ews_operation_set_connection(EEwsOperation *op, EEwsConnection *cnc)
{
	g_return_if_fail (E_IS_EWS_OPERATION (op));

	op->priv->cnc = cnc;
}

EEwsConnection *e_ews_operation_get_connection(EEwsOperation *op)
{
	g_return_val_if_fail (E_IS_EWS_OPERATION (op), NULL);
	
	return op->priv->cnc;
}
