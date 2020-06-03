/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include "e-source-o365-folder.h"

struct _ESourceO365FolderPrivate {
	gchar *change_key;
	gchar *id;
};

enum {
	PROP_0,
	PROP_CHANGE_KEY,
	PROP_ID
};

G_DEFINE_TYPE_WITH_PRIVATE (ESourceO365Folder, e_source_o365_folder, E_TYPE_SOURCE_EXTENSION)

static void
source_o365_folder_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHANGE_KEY:
			e_source_o365_folder_set_change_key (
				E_SOURCE_O365_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_ID:
			e_source_o365_folder_set_id (
				E_SOURCE_O365_FOLDER (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_o365_folder_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHANGE_KEY:
			g_value_take_string (
				value,
				e_source_o365_folder_dup_change_key (
				E_SOURCE_O365_FOLDER (object)));
			return;

		case PROP_ID:
			g_value_take_string (
				value,
				e_source_o365_folder_dup_id (
				E_SOURCE_O365_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_o365_folder_finalize (GObject *object)
{
	ESourceO365Folder *o365_folder = E_SOURCE_O365_FOLDER (object);

	g_free (o365_folder->priv->change_key);
	g_free (o365_folder->priv->id);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_source_o365_folder_parent_class)->finalize (object);
}

static void
e_source_o365_folder_class_init (ESourceO365FolderClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_o365_folder_set_property;
	object_class->get_property = source_o365_folder_get_property;
	object_class->finalize = source_o365_folder_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_O365_FOLDER;

	g_object_class_install_property (
		object_class,
		PROP_CHANGE_KEY,
		g_param_spec_string (
			"change-key",
			"Change Key",
			"Essentially an entity tag, used when submitting changes",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

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
}

static void
e_source_o365_folder_init (ESourceO365Folder *extension)
{
	extension->priv = e_source_o365_folder_get_instance_private (extension);
}

void
e_source_o365_folder_type_register (GTypeModule *type_module)
{
	/* We need to ensure this is registered, because it's looked up
	 * by name in e_source_get_extension(). */
	g_type_ensure (E_TYPE_SOURCE_O365_FOLDER);
}

const gchar *
e_source_o365_folder_get_change_key (ESourceO365Folder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_O365_FOLDER (extension), NULL);

	return extension->priv->change_key;
}

gchar *
e_source_o365_folder_dup_change_key (ESourceO365Folder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_O365_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_o365_folder_get_change_key (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_o365_folder_set_change_key (ESourceO365Folder *extension,
				     const gchar *change_key)
{
	g_return_if_fail (E_IS_SOURCE_O365_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (g_strcmp0 (extension->priv->change_key, change_key) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->change_key);
	extension->priv->change_key = g_strdup (change_key);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "change-key");
}

const gchar *
e_source_o365_folder_get_id (ESourceO365Folder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_O365_FOLDER (extension), NULL);

	return extension->priv->id;
}

gchar *
e_source_o365_folder_dup_id (ESourceO365Folder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_O365_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_o365_folder_get_id (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_o365_folder_set_id (ESourceO365Folder *extension,
			     const gchar *id)
{
	g_return_if_fail (E_IS_SOURCE_O365_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (g_strcmp0 (extension->priv->id, id) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->id);
	extension->priv->id = g_strdup (id);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "id");
}
