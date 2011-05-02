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

#ifndef E_EWS_OPERATION_H
#define E_EWS_OPERATION_H

#include <glib-object.h>
#include <libedataserver/e-flag.h>
#include "e-ews-connection.h"
#include "e-ews-message.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_OPERATION            (e_ews_operation_get_type ())
#define E_EWS_OPERATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_OPERATION, EEwsOperation))
#define E_EWS_OPERATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_OPERATION, EEwsOperationClass))
#define E_IS_EWS_OPERATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_OPERATION))
#define E_IS_EWS_OPERATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_OPERATION))
#define E_EWS_OPERATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_EWS_OPERATION, EEwsOperationClass))

typedef struct _EEwsOperation        EEwsOperation;
typedef struct _EEwsOperationClass   EEwsOperationClass;
typedef struct _EEwsOperationPrivate EEwsOperationPrivate;

struct _EEwsOperation {
	GObject parent;
	
	EEwsOperationPrivate *priv;
};

struct _EEwsOperationClass {
	GObjectClass parent_class;

	void (*parse) (EEwsOperation *op, ESoapParameter *response, GError *error);
};

GType e_ews_operation_get_type (void);

void e_ews_operation_submit (EEwsOperation *op, GCancellable *cancellable, EwsOperationPriority priority, EFlag *eflag);

void e_ews_operation_set_message(EEwsOperation *op, ESoapMessage *msg);

void e_ews_operation_set_connection(EEwsOperation *op, EEwsConnection *cnc);
EEwsConnection *e_ews_operation_get_connection(EEwsOperation *op);

G_END_DECLS

#endif
