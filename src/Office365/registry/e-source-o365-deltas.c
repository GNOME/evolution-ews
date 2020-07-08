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

#include "e-source-o365-deltas.h"

struct _ESourceO365DeltasPrivate {
	gchar *contacts_link;
};

enum {
	PROP_0,
	PROP_CONTACTS_LINK
};

G_DEFINE_TYPE_WITH_PRIVATE (ESourceO365Deltas, e_source_o365_deltas, E_TYPE_SOURCE_EXTENSION)

static void
source_o365_deltas_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTACTS_LINK:
			e_source_o365_deltas_set_contacts_link (
				E_SOURCE_O365_DELTAS (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_o365_deltas_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTACTS_LINK:
			g_value_take_string (
				value,
				e_source_o365_deltas_dup_contacts_link (
				E_SOURCE_O365_DELTAS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_o365_deltas_finalize (GObject *object)
{
	ESourceO365Deltas *o365_deltas = E_SOURCE_O365_DELTAS (object);

	g_free (o365_deltas->priv->contacts_link);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_source_o365_deltas_parent_class)->finalize (object);
}

static void
e_source_o365_deltas_class_init (ESourceO365DeltasClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_o365_deltas_set_property;
	object_class->get_property = source_o365_deltas_get_property;
	object_class->finalize = source_o365_deltas_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_O365_DELTAS;

	g_object_class_install_property (
		object_class,
		PROP_CONTACTS_LINK,
		g_param_spec_string (
			"contacts-link",
			"Contacts Link",
			"The delta link for contacts",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_o365_deltas_init (ESourceO365Deltas *extension)
{
	extension->priv = e_source_o365_deltas_get_instance_private (extension);
}

void
e_source_o365_deltas_type_register (GTypeModule *type_module)
{
	/* We need to ensure this is registered, because it's looked up
	 * by name in e_source_get_extension(). */
	g_type_ensure (E_TYPE_SOURCE_O365_DELTAS);
}

const gchar *
e_source_o365_deltas_get_contacts_link (ESourceO365Deltas *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_O365_DELTAS (extension), NULL);

	return extension->priv->contacts_link;
}

gchar *
e_source_o365_deltas_dup_contacts_link (ESourceO365Deltas *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_O365_DELTAS (extension), NULL);

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	protected = e_source_o365_deltas_get_contacts_link (extension);
	duplicate = g_strdup (protected);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	return duplicate;
}

void
e_source_o365_deltas_set_contacts_link (ESourceO365Deltas *extension,
					const gchar *delta_link)
{
	g_return_if_fail (E_IS_SOURCE_O365_DELTAS (extension));

	e_source_extension_property_lock (E_SOURCE_EXTENSION (extension));

	if (g_strcmp0 (extension->priv->contacts_link, delta_link) == 0) {
		e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));
		return;
	}

	g_free (extension->priv->contacts_link);
	extension->priv->contacts_link = g_strdup (delta_link);

	e_source_extension_property_unlock (E_SOURCE_EXTENSION (extension));

	g_object_notify (G_OBJECT (extension), "contacts-link");
}
