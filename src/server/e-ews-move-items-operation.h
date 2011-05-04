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

#ifndef E_EWS_MOVE_ITEMS_OPERATION_H
#define E_EWS_MOVE_ITEMS_OPERATION_H

#include <glib-object.h>
#include <glib.h>
#include "e-ews-operation.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_MOVE_ITEMS_OPERATION            (e_ews_move_items_operation_get_type ())
#define E_EWS_MOVE_ITEMS_OPERATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_MOVE_ITEMS_OPERATION, EEwsMoveItemsOperation))
#define E_EWS_MOVE_ITEMS_OPERATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_MOVE_ITEMS_OPERATION, EEwsMoveItemsOperationClass))
#define E_IS_EWS_MOVE_ITEMS_OPERATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_MOVE_ITEMS_OPERATION))
#define E_IS_EWS_MOVE_ITEMS_OPERATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_MOVE_ITEMS_OPERATION))

typedef struct _EEwsMoveItemsOperation        EEwsMoveItemsOperation;
typedef struct _EEwsMoveItemsOperationClass   EEwsMoveItemsOperationClass;
typedef struct _EEwsMoveItemsOperationPrivate EEwsMoveItemsOperationPrivate;

struct _EEwsMoveItemsOperation {
	EEwsOperation parent;
	
	EEwsMoveItemsOperationPrivate *priv;
};

struct _EEwsMoveItemsOperationClass {
	EEwsOperationClass parent_class;
};

GType e_ews_move_items_operation_get_type (void);

EEwsMoveItemsOperation *e_ews_move_items_operation_new(EEwsConnection *cnc);

void
e_ews_move_items_operation_prepare (EEwsMoveItemsOperation *op, const gchar *folder_id, gboolean docopy, const GSList *ids);

typedef void (*e_ews_move_items_operation_cb) (GSList *items, gpointer user_data, const GError *error);
void e_ews_move_items_operation_submit_async (EEwsMoveItemsOperation *op,
		e_ews_move_items_operation_cb cb,
		GCancellable *cancellable,
		gpointer user_data);

GSList *
e_ews_move_items_operation_sync (EEwsConnection *cnc,
				 const gchar *folder_id, gboolean docopy, const GSList *ids,
				 GError **error);

G_END_DECLS

#endif
