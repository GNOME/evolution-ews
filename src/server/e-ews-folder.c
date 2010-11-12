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
	gchar *id;
	gchar *parent;
	gchar *folder_class;
	guint32 unread;
	guint32 total;
	guint32 child_count;
	gboolean is_root;
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
	if (priv) {
		if (priv->name) {
			g_free (priv->name);
			priv->name = NULL;
		}

		if (priv->id) {
			g_free (priv->id);
			priv->id = NULL;
		}

		if (priv->parent) {
			g_free (priv->parent);
			priv->parent = NULL;
		}

		g_free (priv);
		folder->priv = NULL;
	}

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
}

gboolean
e_ews_folder_set_from_soap_parameter (EEwsFolder *folder, ESoapParameter *param)
{
	const gchar *folder_name;
	gchar *value;
	ESoapParameter *subparam, *node;

	g_return_val_if_fail (param != NULL, FALSE);

	node = e_soap_parameter_get_first_child_by_name (param, "Folder");

	if (!node)
		node = e_soap_parameter_get_first_child_by_name (param, "CalendarFolder");

	if (!node)
		node = e_soap_parameter_get_first_child_by_name (param, "ContactsFolder");

	if (!node)
		node = e_soap_parameter_get_first_child_by_name (param, "TasksFolder");

	subparam = e_soap_parameter_get_first_child_by_name (param, "FolderId");
	if (!subparam)
		e_ews_folder_set_id (folder, "");
	else {
		value = e_soap_parameter_get_property (subparam, "Id");
		e_ews_folder_set_id (folder, (const gchar *) value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "ParentFolderId");
	if (!subparam) {
		e_ews_folder_set_parent_id (folder, "");
		folder->priv->is_root = TRUE;
	} else {
		value = e_soap_parameter_get_property (subparam, "Id");
		e_ews_folder_set_parent_id (folder, (const gchar *) value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "DisplayName");
	if (!subparam)
		e_ews_folder_set_name (folder, "");
	else {
		value = e_soap_parameter_get_string_value (subparam);
		e_ews_folder_set_name (folder, (const gchar *) value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (node, "FolderClass");
	if (!subparam)
		e_ews_folder_set_folder_class (folder, "");
	else {
		value = e_soap_parameter_get_string_value (subparam);
		e_ews_folder_set_folder_class (folder, (const gchar *) value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "UnreadCount");
	if (!subparam) {
		folder->priv->unread = 0;
	} else {
		value = e_soap_parameter_get_string_value (subparam);
		if (value)
			folder->priv->unread = atoi (value);
		else
			folder->priv->unread = 0;
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "TotalCount");
	if (subparam) {
		value = e_soap_parameter_get_string_value (subparam);
		if (value)
			folder->priv->total = atoi (value);
		g_free (value);
	}

	subparam = e_soap_parameter_get_first_child_by_name (param, "ChildFolderCount");
	if (subparam) {
		value = e_soap_parameter_get_string_value (subparam);
		if (value)
			folder->priv->child_count = atoi (value);
		g_free (value);
	}

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

const gchar *
e_ews_folder_get_name (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const gchar *) folder->priv->name;
}

void
e_ews_folder_set_name (EEwsFolder *folder, const gchar *new_name)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (new_name != NULL);

	priv = folder->priv;

	if (priv->name)
		g_free (priv->name);
	priv->name = g_strdup (new_name);
}

const gchar *
e_ews_folder_get_folder_class (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const gchar *) folder->priv->folder_class;
}

void
e_ews_folder_set_folder_class (EEwsFolder *folder, const gchar *folder_class)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (folder_class != NULL);

	priv = folder->priv;

	if (priv->folder_class)
		g_free (priv->folder_class);
	priv->folder_class = g_strdup (folder_class);
}


const gchar *
e_ews_folder_get_id (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const gchar *) folder->priv->id;
}

void
e_ews_folder_set_id (EEwsFolder *folder, const gchar *new_id)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (new_id != NULL);

	priv = folder->priv;

	if (priv->id)
		g_free (priv->id);
	priv->id = g_strdup (new_id);
}

const gchar *
e_ews_folder_get_parent_id (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), NULL);

	return (const gchar *) folder->priv->parent;
}

void
e_ews_folder_set_parent_id (EEwsFolder *folder, const gchar *parent_id)
{
	EEwsFolderPrivate *priv;

	g_return_if_fail (E_IS_EWS_FOLDER (folder));
	g_return_if_fail (parent_id != NULL);

	priv = folder->priv;

	if (priv->parent)
		g_free (priv->parent);

	priv->parent = g_strdup (parent_id);
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

gboolean
e_ews_folder_is_root (EEwsFolder *folder)
{
	g_return_val_if_fail (E_IS_EWS_FOLDER (folder), FALSE);

	return folder->priv->is_root;
}
