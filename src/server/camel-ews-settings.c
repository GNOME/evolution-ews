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

#include <libedataserver/libedataserver.h>

#define CAMEL_EWS_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_SETTINGS, CamelEwsSettingsPrivate))

struct _CamelEwsSettingsPrivate {
	GMutex *property_lock;
	gboolean check_all;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	gboolean oab_offline;
	gchar *email;
	gchar *gal_uid;
	gchar *hosturl;
	gchar *oaburl;
	gchar *oal_selected;
};

enum {
	PROP_0,
	PROP_AUTH_MECHANISM,
	PROP_CHECK_ALL,
	PROP_EMAIL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX,
	PROP_GAL_UID,
	PROP_HOST,
	PROP_HOSTURL,
	PROP_OABURL,
	PROP_OAB_OFFLINE,
	PROP_OAL_SELECTED,
	PROP_PORT,
	PROP_SECURITY_METHOD,
	PROP_USER
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
		case PROP_AUTH_MECHANISM:
			camel_network_settings_set_auth_mechanism (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_CHECK_ALL:
			camel_ews_settings_set_check_all (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_EMAIL:
			camel_ews_settings_set_email (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
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

		case PROP_GAL_UID:
			camel_ews_settings_set_gal_uid (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_HOST:
			camel_network_settings_set_host (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_HOSTURL:
			camel_ews_settings_set_hosturl (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OABURL:
			camel_ews_settings_set_oaburl (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAB_OFFLINE:
			camel_ews_settings_set_oab_offline (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_OAL_SELECTED:
			camel_ews_settings_set_oal_selected (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_PORT:
			camel_network_settings_set_port (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_SECURITY_METHOD:
			camel_network_settings_set_security_method (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
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
		case PROP_AUTH_MECHANISM:
			g_value_take_string (
				value,
				camel_network_settings_dup_auth_mechanism (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_CHECK_ALL:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_check_all (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_EMAIL:
			g_value_take_string (
				value,
				camel_ews_settings_dup_email (
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

		case PROP_GAL_UID:
			g_value_take_string (
				value,
				camel_ews_settings_dup_gal_uid (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_HOST:
			g_value_take_string (
				value,
				camel_network_settings_dup_host (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_HOSTURL:
			g_value_take_string (
				value,
				camel_ews_settings_dup_hosturl (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OABURL:
			g_value_take_string (
				value,
				camel_ews_settings_dup_oaburl (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OAB_OFFLINE:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_oab_offline (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OAL_SELECTED:
			g_value_take_string (
				value,
				camel_ews_settings_dup_oal_selected (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_PORT:
			g_value_set_uint (
				value,
				camel_network_settings_get_port (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_SECURITY_METHOD:
			g_value_set_enum (
				value,
				camel_network_settings_get_security_method (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_USER:
			g_value_take_string (
				value,
				camel_network_settings_dup_user (
				CAMEL_NETWORK_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_settings_finalize (GObject *object)
{
	CamelEwsSettingsPrivate *priv;

	priv = CAMEL_EWS_SETTINGS_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->email);
	g_free (priv->gal_uid);
	g_free (priv->hosturl);
	g_free (priv->oaburl);
	g_free (priv->oal_selected);

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
		PROP_AUTH_MECHANISM,
		"auth-mechanism");

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
		PROP_EMAIL,
		g_param_spec_string (
			"email",
			"Email",
			"Email",
			NULL,
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

	g_object_class_install_property (
		object_class,
		PROP_GAL_UID,
		g_param_spec_string (
			"gal-uid",
			"GAL UID",
			"Global Address List data source UID",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_HOST,
		"host");

	g_object_class_install_property (
		object_class,
		PROP_HOSTURL,
		g_param_spec_string (
			"hosturl",
			"Host URL",
			"Host URL",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_PORT,
		"port");

	g_object_class_install_property (
		object_class,
		PROP_OABURL,
		g_param_spec_string (
			"oaburl",
			"OABURL",
			"OABURL",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OAB_OFFLINE,
		g_param_spec_boolean (
			"oab-offline",
			"OAB Offline",
			"OAB Offline",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OAL_SELECTED,
		g_param_spec_string (
			"oal-selected",
			"OAL Selected",
			"OAL Selected",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_SECURITY_METHOD,
		"security-method");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_USER,
		"user");
}

static void
camel_ews_settings_init (CamelEwsSettings *settings)
{
	settings->priv = CAMEL_EWS_SETTINGS_GET_PRIVATE (settings);
	settings->priv->property_lock = g_mutex_new ();
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

const gchar *
camel_ews_settings_get_email (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->email;
}

gchar *
camel_ews_settings_dup_email (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_ews_settings_get_email (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_email (CamelEwsSettings *settings,
                              const gchar *email)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	g_free (settings->priv->email);
	settings->priv->email = e_util_strdup_strip (email);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "email");
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

const gchar *
camel_ews_settings_get_gal_uid (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->gal_uid;
}

gchar *
camel_ews_settings_dup_gal_uid (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_ews_settings_get_gal_uid (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_gal_uid (CamelEwsSettings *settings,
                                const gchar *gal_uid)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	g_free (settings->priv->gal_uid);
	settings->priv->gal_uid = e_util_strdup_strip (gal_uid);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "gal-uid");
}

const gchar *
camel_ews_settings_get_hosturl (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->hosturl;
}

gchar *
camel_ews_settings_dup_hosturl (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_ews_settings_get_hosturl (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_hosturl (CamelEwsSettings *settings,
                                const gchar *hosturl)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	g_free (settings->priv->hosturl);
	settings->priv->hosturl = e_util_strdup_strip (hosturl);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "hosturl");
}

const gchar *
camel_ews_settings_get_oaburl (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->oaburl;
}

gchar *
camel_ews_settings_dup_oaburl (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_ews_settings_get_oaburl (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_oaburl (CamelEwsSettings *settings,
                               const gchar *oaburl)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	g_free (settings->priv->oaburl);
	settings->priv->oaburl = e_util_strdup_strip (oaburl);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "oaburl");
}

gboolean
camel_ews_settings_get_oab_offline (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->oab_offline;
}

void
camel_ews_settings_set_oab_offline (CamelEwsSettings *settings,
                                    gboolean oab_offline)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	settings->priv->oab_offline = oab_offline;

	g_object_notify (G_OBJECT (settings), "oab-offline");
}

const gchar *
camel_ews_settings_get_oal_selected (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->oal_selected;
}

gchar *
camel_ews_settings_dup_oal_selected (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	g_mutex_lock (settings->priv->property_lock);

	protected = camel_ews_settings_get_oal_selected (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_oal_selected (CamelEwsSettings *settings,
                                     const gchar *oal_selected)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (settings->priv->property_lock);

	g_free (settings->priv->oal_selected);
	settings->priv->oal_selected = e_util_strdup_strip (oal_selected);

	g_mutex_unlock (settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "oal-selected");
}

