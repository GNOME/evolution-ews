/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-source-ews-folder.h"

struct _ESourceEwsFolderPrivate {
	gchar *change_key;
	gchar *id;
	gchar *name;
	gboolean foreign;
	gboolean foreign_subfolders;
	gchar *foreign_mail;
	gboolean is_public;
	guint freebusy_weeks_before;
	guint freebusy_weeks_after;
	gboolean use_primary_address;
	gboolean fetch_gal_photos;
};

enum {
	PROP_0,
	PROP_CHANGE_KEY,
	PROP_ID,
	PROP_FOREIGN,
	PROP_FOREIGN_SUBFOLDERS,
	PROP_FOREIGN_MAIL,
	PROP_FREEBUSY_WEEKS_BEFORE,
	PROP_FREEBUSY_WEEKS_AFTER,
	PROP_NAME,
	PROP_PUBLIC,
	PROP_USE_PRIMARY_ADDRESS,
	PROP_FETCH_GAL_PHOTOS
};

G_DEFINE_TYPE_WITH_PRIVATE (ESourceEwsFolder, e_source_ews_folder, E_TYPE_SOURCE_EXTENSION)

static void
source_ews_folder_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHANGE_KEY:
			e_source_ews_folder_set_change_key (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_ID:
			e_source_ews_folder_set_id (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_FOREIGN:
			e_source_ews_folder_set_foreign (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_FOREIGN_SUBFOLDERS:
			e_source_ews_folder_set_foreign_subfolders (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_FOREIGN_MAIL:
			e_source_ews_folder_set_foreign_mail (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_FREEBUSY_WEEKS_BEFORE:
			e_source_ews_folder_set_freebusy_weeks_before (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_uint (value));
			return;

		case PROP_FREEBUSY_WEEKS_AFTER:
			e_source_ews_folder_set_freebusy_weeks_after (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_uint (value));
			return;

		case PROP_NAME:
			e_source_ews_folder_set_name (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_PUBLIC:
			e_source_ews_folder_set_public (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_PRIMARY_ADDRESS:
			e_source_ews_folder_set_use_primary_address (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_FETCH_GAL_PHOTOS:
			e_source_ews_folder_set_fetch_gal_photos (
				E_SOURCE_EWS_FOLDER (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_ews_folder_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHANGE_KEY:
			g_value_take_string (
				value,
				e_source_ews_folder_dup_change_key (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_ID:
			g_value_take_string (
				value,
				e_source_ews_folder_dup_id (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_FOREIGN:
			g_value_set_boolean (
				value,
				e_source_ews_folder_get_foreign (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_FOREIGN_SUBFOLDERS:
			g_value_set_boolean (
				value,
				e_source_ews_folder_get_foreign_subfolders (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_FOREIGN_MAIL:
			g_value_take_string (
				value,
				e_source_ews_folder_dup_foreign_mail (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_FREEBUSY_WEEKS_BEFORE:
			g_value_set_uint (
				value,
				e_source_ews_folder_get_freebusy_weeks_before (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_FREEBUSY_WEEKS_AFTER:
			g_value_set_uint (
				value,
				e_source_ews_folder_get_freebusy_weeks_after (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_NAME:
			g_value_take_string (
				value,
				e_source_ews_folder_dup_name (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_PUBLIC:
			g_value_set_boolean (
				value,
				e_source_ews_folder_get_public (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_USE_PRIMARY_ADDRESS:
			g_value_set_boolean (
				value,
				e_source_ews_folder_get_use_primary_address (
				E_SOURCE_EWS_FOLDER (object)));
			return;

		case PROP_FETCH_GAL_PHOTOS:
			g_value_set_boolean (
				value,
				e_source_ews_folder_get_fetch_gal_photos (
				E_SOURCE_EWS_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_ews_folder_finalize (GObject *object)
{
	ESourceEwsFolder *extension = E_SOURCE_EWS_FOLDER (object);

	g_free (extension->priv->change_key);
	g_free (extension->priv->id);
	g_free (extension->priv->name);
	g_free (extension->priv->foreign_mail);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_ews_folder_parent_class)->finalize (object);
}

static void
e_source_ews_folder_class_init (ESourceEwsFolderClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_ews_folder_set_property;
	object_class->get_property = source_ews_folder_get_property;
	object_class->finalize = source_ews_folder_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_EWS_FOLDER;

	g_object_class_install_property (
		object_class,
		PROP_CHANGE_KEY,
		g_param_spec_string (
			"change-key",
			"Change Key",
			"Essentially an entity tag, "
			"used when submitting changes",
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

	g_object_class_install_property (
		object_class,
		PROP_FOREIGN,
		g_param_spec_boolean (
			"foreign",
			"Foreign",
			"The folder is a foreign folder, aka belongs to other user",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_FOREIGN_SUBFOLDERS,
		g_param_spec_boolean (
			"foreign-subfolders",
			"ForeignSubfolders",
			"Whether to search for subfolders of (this) foreign folder",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_FOREIGN_MAIL,
		g_param_spec_string (
			"foreign-mail",
			"ForeignMail",
			"Other user's mail address",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_FREEBUSY_WEEKS_BEFORE,
		g_param_spec_uint (
			"freebusy-weeks-before",
			"FreeBusyWeeksBefore",
			"How many weeks to read Free/Busy before today",
			0, 5, 1,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_FREEBUSY_WEEKS_AFTER,
		g_param_spec_uint (
			"freebusy-weeks-after",
			"FreeBusyWeeksAfter",
			"How many weeks to read Free/Busy after today",
			1, 54, 5,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_NAME,
		g_param_spec_string (
			"name",
			"Name",
			"The server-side folder name",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_PUBLIC,
		g_param_spec_boolean (
			"public",
			"Public",
			"The folder is a public folder, part of Public Folders",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_USE_PRIMARY_ADDRESS,
		g_param_spec_boolean (
			"use-primary-address",
			"Use Primary Address",
			"Whether online GAL should use only the primary contact address",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_FETCH_GAL_PHOTOS,
		g_param_spec_boolean (
			"fetch-gal-photos",
			"Fetch GAL Photos",
			"Whether fetch photos for GAL contacts",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_ews_folder_init (ESourceEwsFolder *extension)
{
	extension->priv = e_source_ews_folder_get_instance_private (extension);
}

void
e_source_ews_folder_type_register (GTypeModule *type_module)
{
	/* We need to ensure this is registered, because it's looked up
	 * by name in e_source_get_extension(). */
	g_type_ensure (E_TYPE_SOURCE_EWS_FOLDER);
}

const gchar *
e_source_ews_folder_get_change_key (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	return extension->priv->change_key;
}

gchar *
e_source_ews_folder_dup_change_key (ESourceEwsFolder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_ews_folder_get_change_key (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_ews_folder_set_change_key (ESourceEwsFolder *extension,
                                    const gchar *change_key)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->change_key, change_key) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->change_key);
	extension->priv->change_key = e_util_strdup_strip (change_key);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "change-key");
}

const gchar *
e_source_ews_folder_get_id (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	return extension->priv->id;
}

gchar *
e_source_ews_folder_dup_id (ESourceEwsFolder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_ews_folder_get_id (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_ews_folder_set_id (ESourceEwsFolder *extension,
                            const gchar *id)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->id, id) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->id);
	extension->priv->id = e_util_strdup_strip (id);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "id");
}

/* free returned pointer with e_ews_folder_id_free() */
EwsFolderId *
e_source_ews_folder_dup_folder_id (ESourceEwsFolder *extension)
{
	EwsFolderId *folder_id;

	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	folder_id = e_ews_folder_id_new (
		extension->priv->id, extension->priv->change_key, FALSE);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return folder_id;
}

const gchar *
e_source_ews_folder_get_name (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	return extension->priv->name;
}

gchar *
e_source_ews_folder_dup_name (ESourceEwsFolder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_ews_folder_get_name (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_ews_folder_set_name (ESourceEwsFolder *extension,
			      const gchar *name)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->name, name) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->name);
	extension->priv->name = e_util_strdup_strip (name);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "name");
}

gboolean
e_source_ews_folder_get_foreign (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), FALSE);

	return extension->priv->foreign;
}

void
e_source_ews_folder_set_foreign (ESourceEwsFolder *extension,
                                 gboolean is_foreign)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	if ((extension->priv->foreign ? 1 : 0) == (is_foreign ? 1 : 0))
		return;

	extension->priv->foreign = is_foreign;

	g_object_notify (G_OBJECT (extension), "foreign");
}

gboolean
e_source_ews_folder_get_foreign_subfolders (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), FALSE);

	return extension->priv->foreign_subfolders;
}

void
e_source_ews_folder_set_foreign_subfolders (ESourceEwsFolder *extension,
					    gboolean foreign_subfolders)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	if ((extension->priv->foreign_subfolders ? 1 : 0) == (foreign_subfolders ? 1 : 0))
		return;

	extension->priv->foreign_subfolders = foreign_subfolders;

	g_object_notify (G_OBJECT (extension), "foreign-subfolders");
}

const gchar *
e_source_ews_folder_get_foreign_mail (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	return extension->priv->foreign_mail;
}

gchar *
e_source_ews_folder_dup_foreign_mail (ESourceEwsFolder *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_ews_folder_get_foreign_mail (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_ews_folder_set_foreign_mail (ESourceEwsFolder *extension,
				      const gchar *foreign_mail)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (e_util_strcmp0 (extension->priv->foreign_mail, foreign_mail) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->foreign_mail);
	extension->priv->foreign_mail = e_util_strdup_strip (foreign_mail);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "foreign-mail");
}

guint
e_source_ews_folder_get_freebusy_weeks_before (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), 0);

	return extension->priv->freebusy_weeks_before;
}

void
e_source_ews_folder_set_freebusy_weeks_before (ESourceEwsFolder *extension,
					       guint freebusy_weeks_before)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	if (extension->priv->freebusy_weeks_before == freebusy_weeks_before)
		return;

	extension->priv->freebusy_weeks_before = freebusy_weeks_before;

	g_object_notify (G_OBJECT (extension), "freebusy-weeks-before");
}

guint
e_source_ews_folder_get_freebusy_weeks_after (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), 0);

	return extension->priv->freebusy_weeks_after;
}

void
e_source_ews_folder_set_freebusy_weeks_after (ESourceEwsFolder *extension,
					      guint freebusy_weeks_after)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	if (extension->priv->freebusy_weeks_after == freebusy_weeks_after)
		return;

	extension->priv->freebusy_weeks_after = freebusy_weeks_after;

	g_object_notify (G_OBJECT (extension), "freebusy-weeks-after");
}

gboolean
e_source_ews_folder_get_public (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), FALSE);

	return extension->priv->is_public;
}

void
e_source_ews_folder_set_public (ESourceEwsFolder *extension,
                                 gboolean is_public)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	if ((extension->priv->is_public ? 1 : 0) == (is_public ? 1 : 0))
		return;

	extension->priv->is_public = is_public;

	g_object_notify (G_OBJECT (extension), "public");
}

gboolean
e_source_ews_folder_get_use_primary_address (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), FALSE);

	return extension->priv->use_primary_address;
}

void
e_source_ews_folder_set_use_primary_address (ESourceEwsFolder *extension,
					     gboolean use_primary_address)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	if ((extension->priv->use_primary_address ? 1 : 0) == (use_primary_address ? 1 : 0))
		return;

	extension->priv->use_primary_address = use_primary_address;

	g_object_notify (G_OBJECT (extension), "use-primary-address");
}

gboolean
e_source_ews_folder_get_fetch_gal_photos (ESourceEwsFolder *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_EWS_FOLDER (extension), FALSE);

	return extension->priv->fetch_gal_photos;
}

void
e_source_ews_folder_set_fetch_gal_photos (ESourceEwsFolder *extension,
					  gboolean fetch_gal_photos)
{
	g_return_if_fail (E_IS_SOURCE_EWS_FOLDER (extension));

	if ((extension->priv->fetch_gal_photos ? 1 : 0) == (fetch_gal_photos ? 1 : 0))
		return;

	extension->priv->fetch_gal_photos = fetch_gal_photos;

	g_object_notify (G_OBJECT (extension), "fetch-gal-photos");
}
