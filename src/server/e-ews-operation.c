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
#include <libxml2/libxml/tree.h>
#endif
#include "e-ews-operation.h"

G_DEFINE_TYPE (EEwsOperation, e_ews_operation, G_TYPE_OBJECT)

struct _EEwsOperationPrivate {
	EEwsConnection *cnc;
	
	ESoapMessage *msg;

	gchar *name;
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
	ESoapParameter *param, *subparam;
	EFlag *eflag = (EFlag *)user_data;

	if (!g_simple_async_result_propagate_error ((GSimpleAsyncResult *) res, &error))
	{
		response = E_SOAP_RESPONSE(g_simple_async_result_get_op_res_gpointer ((GSimpleAsyncResult *) res));
	}

	if (!error)
	{
		param = e_soap_response_get_first_parameter_by_name (response, "ResponseMessages");
		subparam = e_soap_parameter_get_first_child (param);

		if (g_getenv ("EWS_DEBUG_OPERATION") && (g_strrstr((const gchar *) subparam->name, g_getenv ("EWS_DEBUG_OPERATION")) != NULL))
			e_soap_response_dump_response (response, stdout);

		class->parse (operation, param, error);
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

	g_free (priv->name);

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

	if (self->priv->name && g_getenv ("EWS_DEBUG_OPERATION") && (g_strcmp0(self->priv->name, g_getenv ("EWS_DEBUG_OPERATION")) == 0))
	{
		soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (self->priv->msg)->request_body));
		/* print request's body */
		printf ("\n The request headers");
		fputc ('\n', stdout);
		fputs (SOUP_MESSAGE (self->priv->msg)->request_body->data, stdout);
		fputc ('\n', stdout);
	}

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

gboolean
e_ews_operation_is_response_error (ESoapParameter *param, GError **error)
{
	ESoapParameter *subparam;
	gchar *value, *desc, *res;

	value = e_soap_parameter_get_property (param, "ResponseClass");
	 
	if (!g_ascii_strcasecmp (value, "Error")) {
		subparam = e_soap_parameter_get_first_child_by_name (param, "MessageText");
		desc = e_soap_parameter_get_string_value (subparam);
		subparam = e_soap_parameter_get_first_child_by_name (param, "ResponseCode");
		res = e_soap_parameter_get_string_value (subparam);

		g_set_error (error, EWS_CONNECTION_ERROR, ews_get_error_code ((const gchar *) res), "%s", desc);
		g_free (desc);
		g_free (res);
	}

	g_free (value);

	return (*error != NULL);
}

void
e_ews_operation_set_name (EEwsOperation *op, const gchar *name)
{
	g_return_if_fail (E_IS_EWS_OPERATION (op));

	op->priv->name = g_strdup (name);
}