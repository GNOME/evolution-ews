/*
 * camel-ews-settings.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include "camel-ews-settings.h"

struct _CamelEwsSettingsPrivate {
	gboolean check_all;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
};

enum {
	PROP_0,
	PROP_SECURITY_METHOD,
	PROP_CHECK_ALL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX
};

G_DEFINE_TYPE_WITH_CODE (
	CamelEwsSettings,
	camel_ews_settings,
	CAMEL_TYPE_OFFLINE_SETTINGS,
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static void
ews_settings_set_property (GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SECURITY_METHOD:
			camel_network_settings_set_security_method (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_CHECK_ALL:
			camel_ews_settings_set_check_all (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK:
			camel_ews_settings_set_filter_junk (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK_INBOX:
			camel_ews_settings_set_filter_junk_inbox (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_settings_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SECURITY_METHOD:
			g_value_set_enum (
				value,
				camel_network_settings_get_security_method (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_CHECK_ALL:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_check_all (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_filter_junk (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK_INBOX:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_filter_junk_inbox (
				CAMEL_EWS_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_settings_finalize (GObject *object)
{
	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_settings_parent_class)->finalize (object);
}

static void
camel_ews_settings_class_init (CamelEwsSettingsClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelEwsSettingsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_settings_set_property;
	object_class->get_property = ews_settings_get_property;
	object_class->finalize = ews_settings_finalize;

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_SECURITY_METHOD,
		"security-method");

	/* Newly added properties */
	g_object_class_install_property (
		object_class,
		PROP_CHECK_ALL,
		g_param_spec_boolean (
			"check-all",
			"Check All",
			"Check all folders for new messages",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK,
		g_param_spec_boolean (
			"filter-junk",
			"Filter Junk",
			"Whether to filter junk from all folders",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK_INBOX,
		g_param_spec_boolean (
			"filter-junk-inbox",
			"Filter Junk Inbox",
			"Whether to filter junk from Inbox only",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_ews_settings_init (CamelEwsSettings *settings)
{
	settings->priv = G_TYPE_INSTANCE_GET_PRIVATE (settings, CAMEL_TYPE_EWS_SETTINGS, CamelEwsSettingsPrivate);

}

/**
 * camel_ews_settings_get_check_all:
 * @settings: a #CamelEwsSettings
 *
 * Returns whether to check all folders for new messages.
 *
 * Returns: whether to check all folders for new messages
 *
 * Since: 3.4
 **/
gboolean
camel_ews_settings_get_check_all (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->check_all;
}

/**
 * camel_ews_settings_set_check_all:
 * @settings: a #CamelEwsSettings
 * @check_all: whether to check all folders for new messages
 *
 * Sets whether to check all folders for new messages.
 *
 * Since: 3.4
 **/
void
camel_ews_settings_set_check_all (CamelEwsSettings *settings,
                                  gboolean check_all)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	settings->priv->check_all = check_all;

	g_object_notify (G_OBJECT (settings), "check-all");
}

/**
 * camel_ews_settings_get_filter_junk:
 * @settings: a #CamelEwsSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Returns: whether to filter junk in all folders
 *
 * Since: 3.4
 **/
gboolean
camel_ews_settings_get_filter_junk (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk;
}

/**
 * camel_ews_settings_set_filter_junk:
 * @settings: a #CamelEwsSettings
 * @filter_junk: whether to filter junk in all filers
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in all folders.
 *
 * Since: 3.4
 **/
void
camel_ews_settings_set_filter_junk (CamelEwsSettings *settings,
                                    gboolean filter_junk)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	settings->priv->filter_junk = filter_junk;

	g_object_notify (G_OBJECT (settings), "filter-junk");
}

/**
 * camel_ews_settings_get_filter_junk_inbox:
 * @settings: a #CamelEwsSettings
 *
 * Returns whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Returns: whether to filter junk in Inbox only
 *
 * Since: 3.4
 **/
gboolean
camel_ews_settings_get_filter_junk_inbox (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk_inbox;
}

/**
 * camel_ews_settings_set_filter_junk_inbox:
 * @settings: a #CamelEwsSettings
 * @filter_junk_inbox: whether to filter junk in Inbox only
 *
 * Sets whether to automatically find and tag junk messages amongst new
 * messages in the Inbox folder only.
 *
 * Since: 3.4
 **/
void
camel_ews_settings_set_filter_junk_inbox (CamelEwsSettings *settings,
                                          gboolean filter_junk_inbox)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	settings->priv->filter_junk_inbox = filter_junk_inbox;

	g_object_notify (G_OBJECT (settings), "filter-junk-inbox");
}
