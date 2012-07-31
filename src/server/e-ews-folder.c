/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
#include <string.h>
#include "e-ews-folder.h"
#include "e-ews-message.h"

G_DEFINE_TYPE (EEwsFolder, e_ews_folder, G_TYPE_OBJECT)

struct _EEwsFolderPrivate {
	gchar *name;
	EwsFolderId *fid;
	EwsFolderId *parent_fid;
	gchar *folder_class;
	EwsFolderType folder_type;
	guint32 unread;
	guint32 total;
	guint32 child_count;
};

static GObjectClass *parent_class = NULL;

static void
e_ews_folder_dispose (GObject *object)
{
	EEwsFolder *folder = (EEwsFolder *) object;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	if (parent_class->dispose)
		(* parent_class->dispose) (object);
}

static void
e_ews_folder_finalize (GObject *object)
{
	EEwsFolder *folder = (EEwsFolder *) object;
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	priv = folder->priv;

	if (priv->name) {
		g_free (priv->name);
		priv->name = NULL;
	}

	if (priv->fid) {
		g_free (priv->fid->id);
		g_free (priv->fid->change_key);
		g_free (priv->fid);
		priv->fid = NULL;
	}

	if (priv->parent_fid) {
		g_free (priv->parent_fid->id);
		g_free (priv->parent_fid->change_key);
		g_free (priv->parent_fid);
		priv->parent_fid = NULL;
	}

	g_free (priv);
	folder->priv = NULL;

	if (parent_class->finalize)
		(* parent_class->finalize) (object);
}

static void
e_ews_folder_class_init (EEwsFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_ews_folder_dispose;
	object_class->finalize = e_ews_folder_finalize;
}

static void
e_ews_folder_init (EEwsFolder *folder)
{
	EEwsFolderPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EEwsFolderPrivate, 1);
	folder->priv = priv;

	priv->folder_type = EWS_FOLDER_TYPE_MAILBOX;
}

/* FIXME pick it from folder_type and make it set folder_type */
static void
e_ews_folder_set_folder_class (EEwsFolder *folder,
                               const gchar *folder_class)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (folder_class != NULL);

	priv = folder->priv;

	if (priv->folder_class)
		g_free (priv->folder_class);
	priv->folder_class = g_strdup (folder_class);
}

static gboolean
e_ews_folder_set_from_soap_parameter (EEwsFolder *folder,
                                      ESoapParameter *param)
{
	EEwsFolderPrivate *priv = folder->priv;
	gchar *value;
	ESoapParameter *subparam, *node;

	g_return_val_if_fail (param != NULL, FALSE);

	if ((node = e_soap_parameter_get_first_child_by_name (param, "Folder")))
		priv->folder_type = EWS_FOLDER_TYPE_MAILBOX;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "CalendarFolder")))
		priv->folder_type = EWS_FOLDER_TYPE_CALENDAR;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "ContactsFolder")))
		priv->folder_type = EWS_FOLDER_TYPE_CONTACTS;
	else if ((node = e_soap_parameter_get_first_child_by_name (param, "TasksFolder")))
		priv->folder_type = EWS_FOLDER_TYPE_TASKS;
	else {
		g_warning ("Unable to find the Folder node \n");
		return FALSE;
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "FolderId");
	if (subparam) {
		priv->fid = g_new0 (EwsFolderId, 1);
		priv->fid->id = e_soap_parameter_get_property (subparam, "Id");
		priv->fid->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "ParentFolderId");
	if (subparam) {
		priv->parent_fid = g_new0 (EwsFolderId, 1);
		priv->parent_fid->id = e_soap_parameter_get_property (subparam, "Id");
		priv->parent_fid->change_key = e_soap_parameter_get_property (subparam, "ChangeKey");
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "FolderClass");
	if (subparam) {
		value = e_soap_parameter_get_string_value (subparam);
		e_ews_folder_set_folder_class (folder, (const gchar *) value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "DisplayName");
	if (subparam)
		priv->name = e_soap_parameter_get_string_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "UnreadCount");
	if (subparam)
		priv->unread = e_soap_parameter_get_int_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "TotalCount");
	if (subparam)
		priv->total = e_soap_parameter_get_int_value (subparam);

	subparam = e_soap_parameter_get_first_child_by_name (node, "ChildFolderCount");
	if (subparam)
		priv->child_count = e_soap_parameter_get_int_value (subparam);

	return TRUE;
}

EEwsFolder *
e_ews_folder_new_from_soap_parameter (ESoapParameter *param)
{
	EEwsFolder *folder;

	g_return_val_if_fail (param != NULL, NULL);

	folder = g_object_new (E_TYPE_EWS_FOLDER, NULL);
	if (!e_ews_folder_set_from_soap_parameter (folder, param)) {
		g_object_unref (folder);
		return NULL;
	}

	return folder;
}

EwsFolderId *
e_ews_folder_id_new (const gchar *id,
		     const gchar *change_key,
		     gboolean is_distinguished_id)
{
	EwsFolderId *fid;

	fid = g_new0 (EwsFolderId, 1);
	fid->id = g_strdup (id);
	fid->change_key = g_strdup (change_key);
	fid->is_distinguished_id = is_distinguished_id;

	return fid;
}

void
e_ews_folder_id_free (EwsFolderId *fid)
{
	if (fid) {
		g_free (fid->id);
		g_free (fid->change_key);
		g_free (fid);
	}
}

const gchar *
e_ews_folder_get_name (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const gchar *) folder->priv->name;
}

void
e_ews_folder_set_name (EEwsFolder *folder,
                       const gchar *new_name)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (new_name != NULL);

	priv = folder->priv;

	if (priv->name)
		g_free (priv->name);
	priv->name = g_strdup (new_name);
}

const EwsFolderId *
e_ews_folder_get_id (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const EwsFolderId *) folder->priv->fid;
}

const EwsFolderId *
e_ews_folder_get_parent_id (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const EwsFolderId *) folder->priv->parent_fid;
}

void
e_ews_folder_set_parent_id (EEwsFolder *folder,
                            EwsFolderId *parent_fid)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (parent_fid != NULL);

	priv = folder->priv;

	if (priv->parent_fid) {
		g_free (priv->parent_fid->id);
		g_free (priv->parent_fid->change_key);
		g_free (priv->parent_fid);
	}

	priv->parent_fid = parent_fid;
}

EwsFolderType
e_ews_folder_get_folder_type (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->folder_type;
}

void
e_ews_folder_set_folder_type (EEwsFolder *folder,
                              EwsFolderType folder_type)
{
	g_return_if_fail (E_IS_EWS_FOLDER (folder));

	folder->priv->folder_type = folder_type;
}

guint32
e_ews_folder_get_total_count (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->total;
}

guint32
e_ews_folder_get_unread_count (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->unread;

}

guint32
e_ews_folder_get_child_count (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), -1);

	return folder->priv->child_count;

}
