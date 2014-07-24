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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "camel-ews-settings.h"

#include <libedataserver/libedataserver.h>

#define CAMEL_EWS_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EWS_SETTINGS, CamelEwsSettingsPrivate))

struct _CamelEwsSettingsPrivate {
	GMutex property_lock;
	gboolean check_all;
	gboolean listen_notifications;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	gboolean oab_offline;
	gboolean use_impersonation;
	gboolean folders_initialized;
	gchar *email;
	gchar *gal_uid;
	gchar *hosturl;
	gchar *oaburl;
	gchar *oal_selected;
	guint timeout;
	gchar *impersonate_user;
};

enum {
	PROP_0,
	PROP_AUTH_MECHANISM,
	PROP_CHECK_ALL,
	PROP_LISTEN_NOTIFICATIONS,
	PROP_EMAIL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX,
	PROP_FOLDERS_INITIALIZED,
	PROP_GAL_UID,
	PROP_HOST,
	PROP_HOSTURL,
	PROP_OABURL,
	PROP_OAB_OFFLINE,
	PROP_OAL_SELECTED,
	PROP_PORT,
	PROP_SECURITY_METHOD,
	PROP_TIMEOUT,
	PROP_USER,
	PROP_USE_IMPERSONATION,
	PROP_IMPERSONATE_USER
};

G_DEFINE_TYPE_WITH_CODE (
	CamelEwsSettings,
	camel_ews_settings,
	CAMEL_TYPE_OFFLINE_SETTINGS,
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static gboolean
ews_settings_transform_host_url_to_host_cb (GBinding *binding,
					    const GValue *host_url_value,
					    GValue *host_value,
					    gpointer user_data)
{
	const gchar *host_url;

	host_url = g_value_get_string (host_url_value);
	if (host_url && *host_url) {
		SoupURI *uri;

		uri = soup_uri_new (host_url);
		if (uri) {
			const gchar *host;

			host = soup_uri_get_host (uri);
			if (!host || !*host)
				host = "";

			g_value_set_string (host_value, host);

			soup_uri_free (uri);
		}
	}

	return TRUE;
}

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

		case PROP_LISTEN_NOTIFICATIONS:
			camel_ews_settings_set_listen_notifications (
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

		case PROP_FOLDERS_INITIALIZED:
			camel_ews_settings_set_folders_initialized (
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

		case PROP_TIMEOUT:
			camel_ews_settings_set_timeout (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_USE_IMPERSONATION:
			camel_ews_settings_set_use_impersonation (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_IMPERSONATE_USER:
			camel_ews_settings_set_impersonate_user (
				CAMEL_EWS_SETTINGS (object),
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

		case PROP_LISTEN_NOTIFICATIONS:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_listen_notifications (
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

		case PROP_FOLDERS_INITIALIZED:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_folders_initialized (
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

		case PROP_TIMEOUT:
			g_value_set_uint (
				value,
				camel_ews_settings_get_timeout (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_USER:
			g_value_take_string (
				value,
				camel_network_settings_dup_user (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_USE_IMPERSONATION:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_use_impersonation (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_IMPERSONATE_USER:
			g_value_take_string (
				value,
				camel_ews_settings_dup_impersonate_user (
				CAMEL_EWS_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_settings_finalize (GObject *object)
{
	CamelEwsSettingsPrivate *priv;

	priv = CAMEL_EWS_SETTINGS_GET_PRIVATE (object);

	g_mutex_clear (&priv->property_lock);

	g_free (priv->email);
	g_free (priv->gal_uid);
	g_free (priv->hosturl);
	g_free (priv->oaburl);
	g_free (priv->oal_selected);
	g_free (priv->impersonate_user);

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
		PROP_LISTEN_NOTIFICATIONS,
		g_param_spec_boolean (
			"listen-notifications",
			"Listen Notifications",
			"Whether to listen for server notification",
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
		PROP_FOLDERS_INITIALIZED,
		g_param_spec_boolean (
			"folders-initialized",
			"Folders Initialized",
			"Whether Sent Items and Drafts folders are initialized",
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

	g_object_class_install_property (
		object_class,
		PROP_TIMEOUT,
		g_param_spec_uint (
			"timeout",
			"timeout",
			"Connection timeout in seconds",
			0, G_MAXUINT, 120,
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

	g_object_class_install_property (
		object_class,
		PROP_USE_IMPERSONATION,
		g_param_spec_boolean (
			"use-impersonation",
			"Use Impersonation",
			"Use Impersonation",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_IMPERSONATE_USER,
		g_param_spec_string (
			"impersonate-user",
			"Impersonate User",
			"Impersonate User",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_ews_settings_init (CamelEwsSettings *settings)
{
	settings->priv = CAMEL_EWS_SETTINGS_GET_PRIVATE (settings);
	g_mutex_init (&settings->priv->property_lock);

	g_object_bind_property_full (settings, "hosturl",
				     settings, "host",
				     G_BINDING_DEFAULT,
				     ews_settings_transform_host_url_to_host_cb,
				     NULL,
				     NULL,
				     NULL);
}

/**
 * camel_ews_settings_get_auth_mechanism:
 * @settings: a #CamelEwsSettings
 *
 * Returns a #EwsAuthType enum value indicating which authentication
 * method to use.
 *
 * Returns: authentication method to use for this account
 *
 * Since: 3.14
 **/
EwsAuthType
camel_ews_settings_get_auth_mechanism (CamelEwsSettings *settings)
{
	EwsAuthType result;
	gchar *auth_mech = NULL;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	g_object_get (G_OBJECT (settings), "auth-mechanism", &auth_mech, NULL);

	/* The value for "NTLM" is a special case. Sometimes it's NULL,
	 * and sometimes it's "". But never "NTLM". No, that would be too
	 * simple. (I think it's for backward-compatibility with old
	 * profiles, so they default to NTLM). */
	if (auth_mech && g_ascii_strcasecmp (auth_mech, "PLAIN") == 0)
		result = EWS_AUTH_TYPE_BASIC;
	else if (auth_mech && g_ascii_strcasecmp (auth_mech, "GSSAPI") == 0)
		result = EWS_AUTH_TYPE_GSSAPI;
	else
		result = EWS_AUTH_TYPE_NTLM;

	g_free (auth_mech);

	return result;
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

	if ((settings->priv->check_all ? 1 : 0) == (check_all ? 1 : 0))
		return;

	settings->priv->check_all = check_all;

	g_object_notify (G_OBJECT (settings), "check-all");
}

/**
 * camel_ews_settings_get_listen_notifications:
 * @settings: a #CamelEwsSettings
 *
 * Returns whether to listen for server notifications.
 *
 * Returns: whether to listen for server notifications
 *
 * Since: 3.12
 **/
gboolean
camel_ews_settings_get_listen_notifications (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->listen_notifications;
}

/**
 * camel_ews_settings_set_listen_notifications:
 * @settings: a #CamelEwsSettings
 * @listen_notifications: whether to listen for server notifications
 *
 * Sets whether to listen for server notifications.
 *
 * Since: 3.12
 **/
void
camel_ews_settings_set_listen_notifications (CamelEwsSettings *settings,
					     gboolean listen_notifications)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->listen_notifications ? 1 : 0) == (listen_notifications ? 1 : 0))
		return;

	settings->priv->listen_notifications = listen_notifications;

	g_object_notify (G_OBJECT (settings), "listen-notifications");
}

/**
 * camel_ews_settings_get_folders_initialized:
 * @settings: a #CamelEwsSettings
 *
 * Returns whether folders had been initialized.
 * This is used to know whether it's required to setup Drafts
 * and Sent Items folders as folders for that purpose in Evolution.
 *
 * Returns: whether folders had been initialized
 *
 * Since: 3.6
 **/
gboolean
camel_ews_settings_get_folders_initialized (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->folders_initialized;
}

/**
 * camel_ews_settings_set_folders_initialized:
 * @settings: a #CamelEwsSettings
 * @folders_initialized: whether folders had been initialized
 *
 * Sets whether to folders had been initialized. This is for Drafts
 * and Sent Items folders only.
 *
 * Since: 3.6
 **/
void
camel_ews_settings_set_folders_initialized (CamelEwsSettings *settings,
					    gboolean folders_initialized)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->folders_initialized ? 1 : 0) == (folders_initialized ? 1 : 0))
		return;

	settings->priv->folders_initialized = folders_initialized;

	g_object_notify (G_OBJECT (settings), "folders-initialized");
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

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_ews_settings_get_email (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_email (CamelEwsSettings *settings,
                              const gchar *email)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->email, email) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->email);
	settings->priv->email = e_util_strdup_strip (email);

	g_mutex_unlock (&settings->priv->property_lock);

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

	if ((settings->priv->filter_junk ? 1 : 0) == (filter_junk ? 1 : 0))
		return;

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

	if ((settings->priv->filter_junk_inbox ? 1 : 0) == (filter_junk_inbox ? 1 : 0))
		return;

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

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_ews_settings_get_gal_uid (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_gal_uid (CamelEwsSettings *settings,
                                const gchar *gal_uid)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->gal_uid, gal_uid) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->gal_uid);
	settings->priv->gal_uid = e_util_strdup_strip (gal_uid);

	g_mutex_unlock (&settings->priv->property_lock);

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

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_ews_settings_get_hosturl (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_hosturl (CamelEwsSettings *settings,
                                const gchar *hosturl)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->hosturl, hosturl) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->hosturl);
	settings->priv->hosturl = e_util_strdup_strip (hosturl);

	g_mutex_unlock (&settings->priv->property_lock);

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

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_ews_settings_get_oaburl (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_oaburl (CamelEwsSettings *settings,
                               const gchar *oaburl)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->oaburl, oaburl) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->oaburl);
	settings->priv->oaburl = e_util_strdup_strip (oaburl);

	g_mutex_unlock (&settings->priv->property_lock);

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

	if ((settings->priv->oab_offline ? 1 : 0) == (oab_offline ? 1 : 0))
		return;

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

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_ews_settings_get_oal_selected (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_oal_selected (CamelEwsSettings *settings,
                                     const gchar *oal_selected)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->oal_selected, oal_selected) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->oal_selected);
	settings->priv->oal_selected = e_util_strdup_strip (oal_selected);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "oal-selected");
}

guint
camel_ews_settings_get_timeout (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), 0);

	return settings->priv->timeout;
}

void
camel_ews_settings_set_timeout (CamelEwsSettings *settings,
                                guint timeout)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if (settings->priv->timeout == timeout)
		return;

	settings->priv->timeout = timeout;

	g_object_notify (G_OBJECT (settings), "timeout");
}

gboolean
camel_ews_settings_get_use_impersonation (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->use_impersonation;
}

void
camel_ews_settings_set_use_impersonation (CamelEwsSettings *settings,
					  gboolean use_impersonation)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->use_impersonation ? 1 : 0) == (use_impersonation ? 1 : 0))
		return;

	settings->priv->use_impersonation = use_impersonation;

	g_object_notify (G_OBJECT (settings), "use-impersonation");
}

const gchar *
camel_ews_settings_get_impersonate_user (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->impersonate_user;
}

gchar *
camel_ews_settings_dup_impersonate_user (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	g_mutex_lock (&settings->priv->property_lock);

	protected = camel_ews_settings_get_impersonate_user (settings);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&settings->priv->property_lock);

	return duplicate;
}

void
camel_ews_settings_set_impersonate_user (CamelEwsSettings *settings,
					 const gchar *impersonate_user)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (g_strcmp0 (settings->priv->impersonate_user, impersonate_user) == 0) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	g_free (settings->priv->impersonate_user);
	settings->priv->impersonate_user = e_util_strdup_strip (impersonate_user);

	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "impersonate-user");
}
