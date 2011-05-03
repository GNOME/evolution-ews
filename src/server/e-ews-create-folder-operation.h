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

#ifndef E_EWS_CREATE_FOLDER_OPERATION_H
#define E_EWS_CREATE_FOLDER_OPERATION_H

#include <glib-object.h>
#include <glib.h>
#include "e-ews-operation.h"

G_BEGIN_DECLS

#define E_TYPE_EWS_CREATE_FOLDER_OPERATION            (e_ews_create_folder_operation_get_type ())
#define E_EWS_CREATE_FOLDER_OPERATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_EWS_CREATE_FOLDER_OPERATION, EEwsCreateFolderOperation))
#define E_EWS_CREATE_FOLDER_OPERATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_EWS_CREATE_FOLDER_OPERATION, EEwsCreateFolderOperationClass))
#define E_IS_EWS_CREATE_FOLDER_OPERATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_EWS_CREATE_FOLDER_OPERATION))
#define E_IS_EWS_CREATE_FOLDER_OPERATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_EWS_CREATE_FOLDER_OPERATION))

typedef struct _EEwsCreateFolderOperation        EEwsCreateFolderOperation;
typedef struct _EEwsCreateFolderOperationClass   EEwsCreateFolderOperationClass;
typedef struct _EEwsCreateFolderOperationPrivate EEwsCreateFolderOperationPrivate;

struct _EEwsCreateFolderOperation {
	EEwsOperation parent;
	
	EEwsCreateFolderOperationPrivate *priv;
};

struct _EEwsCreateFolderOperationClass {
	EEwsOperationClass parent_class;
};

GType e_ews_create_folder_operation_get_type (void);

EEwsCreateFolderOperation *e_ews_create_folder_operation_new(EEwsConnection *cnc);

void e_ews_create_folder_operation_prepare (EEwsCreateFolderOperation *op, const gchar *parent_folder_id, gboolean is_distinguished_id, const gchar *folder_name);

typedef void (*e_ews_create_folder_operation_cb) (const EwsFolderId *folder_id, gpointer user_data, const GError *error);
void e_ews_create_folder_operation_submit_async (EEwsCreateFolderOperation *op,
		e_ews_create_folder_operation_cb cb,
		GCancellable *cancellable,
		gpointer user_data);

gboolean
e_ews_create_folder_operation_sync (EEwsConnection *cnc,
				 const gchar *parent_folder_id,
				 gboolean is_distinguished_id,
				 const gchar *folder_name,
				 EwsFolderId **folder_id,
				 GError **error);

G_END_DECLS

#endif
