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

#ifndef E_EWS_MOVE_FOLDER_OPERATION_H
#define E_EWS_MOVE_FOLDER_OPERATION_H

#include <glib-object.h>
#include <glib.h>
#include "e-ews-operation.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_MOVE_FOLDER_OPERATION            (e_ews_move_folder_operation_get_type ())
#define E_EWS_MOVE_FOLDER_OPERATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_MOVE_FOLDER_OPERATION, EEwsMoveFolderOperation))
#define E_EWS_MOVE_FOLDER_OPERATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_MOVE_FOLDER_OPERATION, EEwsMoveFolderOperationClass))
#define E_IS_EWS_MOVE_FOLDER_OPERATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_MOVE_FOLDER_OPERATION))
#define E_IS_EWS_MOVE_FOLDER_OPERATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_MOVE_FOLDER_OPERATION))

typedef struct _EEwsMoveFolderOperation        EEwsMoveFolderOperation;
typedef struct _EEwsMoveFolderOperationClass   EEwsMoveFolderOperationClass;
typedef struct _EEwsMoveFolderOperationPrivate EEwsMoveFolderOperationPrivate;

struct _EEwsMoveFolderOperation {
	EEwsOperation parent;
	
	EEwsMoveFolderOperationPrivate *priv;
};

struct _EEwsMoveFolderOperationClass {
	EEwsOperationClass parent_class;
};

GType e_ews_move_folder_operation_get_type (void);

EEwsMoveFolderOperation *e_ews_move_folder_operation_new(EEwsConnection *cnc);

void e_ews_move_folder_operation_prepare (EEwsMoveFolderOperation *op, const gchar *to_folder, const gchar *folder);

typedef void (*e_ews_move_folder_operation_cb) (gboolean success, gpointer user_data, const GError *error);
void e_ews_move_folder_operation_submit_async (EEwsMoveFolderOperation *op,
		e_ews_move_folder_operation_cb cb,
		GCancellable *cancellable,
		gpointer user_data);

gboolean
e_ews_move_folder_operation_sync (EEwsConnection *cnc,
				const gchar *to_folder,
				const gchar *folder,
				GError **error);

G_END_DECLS

#endif
