/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-source-m365-folder.h"

struct _ESourceM365FolderPrivate {
	gchar *id;
	gchar *group_id;
	gboolean is_default;
};

enum {
	PROP_0,
	PROP_ID,
	PROP_IS_DEFAULT,
	PROP_GROUP_ID
};

G_DEFINE_TYPE_WITH_PRIVATE (ESourceM365Folder, e_source_m365_folder, E_TYPE_SOURCE_EXTENSION)

static void
source_m365_folder_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_IS_DEFAULT:
			e_source_m365_folder_set_is_default (
				E_SOURCE_M365_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_ID:
			e_source_m365_folder_set_id (
				E_SOURCE_M365_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_GROUP_ID:
			e_source_m365_folder_set_group_id (
				E_SOURCE_M365_FOLDER (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_m365_folder_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_IS_DEFAULT:
			g_value_set_boolean (
				value,
				e_source_m365_folder_get_is_default (
				E_SOURCE_M365_FOLDER (object)));
			return;

		case PROP_ID:
			g_value_take_string (
				value,
				e_source_m365_folder_dup_id (
				E_SOURCE_M365_FOLDER (object)));
			return;

		case PROP_GROUP_ID:
			g_value_take_string (
				value,
				e_source_m365_folder_dup_group_id (
				E_SOURCE_M365_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_m365_folder_finalize (GObject *object)
{
	ESourceM365Folder *m365_folder = E_SOURCE_M365_FOLDER (object);

	g_free (m365_folder->priv->id);
	g_free (m365_folder->priv->group_id);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_source_m365_folder_parent_class)->finalize (object);
}

static void
e_source_m365_folder_class_init (ESourceM365FolderClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_m365_folder_set_property;
	object_class->get_property = source_m365_folder_get_property;
	object_class->finalize = source_m365_folder_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_M365_FOLDER;

	g_object_class_install_property (
		object_class,
		PROP_ID,
		g_param_spec_string (
			"id",
			"ID",
			"The server-assigned folder ID",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_IS_DEFAULT,
		g_param_spec_boolean (
			"is-default",
			"Is Default",
			"Whether it's user's default folder (like 'contacts', which are not part of the contactFolders)",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_GROUP_ID,
		g_param_spec_string (
			"group-id",
			"Group ID",
			"Optional group ID, into which the folder ID belongs",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_m365_folder_init (ESourceM365Folder *extension)
{
	extension->priv = e_source_m365_folder_get_instance_private (extension);
}

void
e_source_m365_folder_type_register (GTypeModule *type_module)
{
	/* We need to ensure this is registered, because it's looked up
	 * by name in e_source_get_extension(). */
	g_type_ensure (E_TYPE_SOURCE_M365_FOLDER);
}

const gchar *
e_source_m365_folder_get_id (ESourceM365Folder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_M365_FOLDER (extension), NULL);

	return extension->priv->id;
}

gchar *
e_source_m365_folder_dup_id (ESourceM365Folder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_M365_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_m365_folder_get_id (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_m365_folder_set_id (ESourceM365Folder *extension,
			     const gchar *id)
{
	g_return_if_fail (E_IS_SOURCE_M365_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (g_strcmp0 (extension->priv->id, id) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->id);
	extension->priv->id = e_util_strdup_strip (id);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "id");
}

gboolean
e_source_m365_folder_get_is_default (ESourceM365Folder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_M365_FOLDER (extension), FALSE);

	return extension->priv->is_default;
}

void
e_source_m365_folder_set_is_default (ESourceM365Folder *extension,
				     gboolean value)
{
	g_return_if_fail (E_IS_SOURCE_M365_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if ((extension->priv->is_default ? 1 : 0) == (value ? 1 : 0)) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	extension->priv->is_default = value;

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "is-default");
}

const gchar *
e_source_m365_folder_get_group_id (ESourceM365Folder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_M365_FOLDER (extension), NULL);

	return extension->priv->group_id;
}

gchar *
e_source_m365_folder_dup_group_id (ESourceM365Folder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_M365_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_m365_folder_get_group_id (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_m365_folder_set_group_id (ESourceM365Folder *extension,
				   const gchar *group_id)
{
	g_return_if_fail (E_IS_SOURCE_M365_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (g_strcmp0 (extension->priv->group_id, group_id) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->group_id);
	extension->priv->group_id = e_util_strdup_strip (group_id);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "group-id");
}