/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "camel-ews-settings.h"

#include <libedataserver/libedataserver.h>

struct _CamelEwsSettingsPrivate {
	GMutex property_lock;
	gboolean check_all;
	gboolean listen_notifications;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	gboolean oab_offline;
	gboolean use_impersonation;
	gboolean show_public_folders;
	gboolean force_http1;
	gchar *email;
	gchar *gal_uid;
	gchar *hosturl;
	gchar *oaburl;
	gchar *oal_selected;
	guint timeout;
	guint concurrent_connections;
	guint sync_tag_stamp;
	gchar *impersonate_user;
	gboolean override_user_agent;
	gchar *user_agent;
	gboolean override_oauth2;
	gboolean use_oauth2_v2;
	gchar *oauth2_tenant;
	gchar *oauth2_client_id;
	gchar *oauth2_redirect_uri;
	gchar *oauth2_resource_uri;
	gchar *oauth2_endpoint_host;
};

enum {
	PROP_0,
	PROP_AUTH_MECHANISM,
	PROP_CHECK_ALL,
	PROP_LISTEN_NOTIFICATIONS,
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
	PROP_TIMEOUT,
	PROP_USER,
	PROP_USE_IMPERSONATION,
	PROP_IMPERSONATE_USER,
	PROP_OVERRIDE_USER_AGENT,
	PROP_USER_AGENT,
	PROP_OVERRIDE_OAUTH2,
	PROP_OAUTH2_TENANT,
	PROP_OAUTH2_CLIENT_ID,
	PROP_OAUTH2_REDIRECT_URI,
	PROP_OAUTH2_RESOURCE_URI,
	PROP_OAUTH2_ENDPOINT_HOST,
	PROP_SHOW_PUBLIC_FOLDERS,
	PROP_CONCURRENT_CONNECTIONS,
	PROP_SYNC_TAG_STAMP,
	PROP_FORCE_HTTP1,
	PROP_USE_OAUTH2_V2
};

G_DEFINE_TYPE_WITH_CODE (CamelEwsSettings, camel_ews_settings, CAMEL_TYPE_OFFLINE_SETTINGS,
	G_ADD_PRIVATE (CamelEwsSettings)
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static gboolean
ews_settings_transform_host_url_to_host_cb (GBinding *binding,
					    const GValue *host_url_value,
					    GValue *host_value,
					    gpointer user_data)
{
	const gchar *host_url;

	host_url = g_value_get_string (host_url_value);
	if (host_url && *host_url) {
		GUri *uri;

		uri = g_uri_parse (host_url, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);
		if (uri) {
			const gchar *host;

			host = g_uri_get_host (uri);
			if (!host || !*host)
				host = "";

			g_value_set_string (host_value, host);

			g_uri_unref (uri);
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

		case PROP_OVERRIDE_USER_AGENT:
			camel_ews_settings_set_override_user_agent (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USER_AGENT:
			camel_ews_settings_set_user_agent (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OVERRIDE_OAUTH2:
			camel_ews_settings_set_override_oauth2 (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_OAUTH2_TENANT:
			camel_ews_settings_set_oauth2_tenant (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAUTH2_CLIENT_ID:
			camel_ews_settings_set_oauth2_client_id (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAUTH2_REDIRECT_URI:
			camel_ews_settings_set_oauth2_redirect_uri (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAUTH2_RESOURCE_URI:
			camel_ews_settings_set_oauth2_resource_uri (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAUTH2_ENDPOINT_HOST:
			camel_ews_settings_set_oauth2_endpoint_host (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_SHOW_PUBLIC_FOLDERS:
			camel_ews_settings_set_show_public_folders (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			camel_ews_settings_set_concurrent_connections (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_SYNC_TAG_STAMP:
			camel_ews_settings_set_sync_tag_stamp (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_FORCE_HTTP1:
			camel_ews_settings_set_force_http1 (
				CAMEL_EWS_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_OAUTH2_V2:
			camel_ews_settings_set_use_oauth2_v2 (
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

		case PROP_OVERRIDE_USER_AGENT:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_override_user_agent (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_USER_AGENT:
			g_value_take_string (
				value,
				camel_ews_settings_dup_user_agent (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OVERRIDE_OAUTH2:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_override_oauth2 (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OAUTH2_TENANT:
			g_value_take_string (
				value,
				camel_ews_settings_dup_oauth2_tenant (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OAUTH2_CLIENT_ID:
			g_value_take_string (
				value,
				camel_ews_settings_dup_oauth2_client_id (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OAUTH2_REDIRECT_URI:
			g_value_take_string (
				value,
				camel_ews_settings_dup_oauth2_redirect_uri (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OAUTH2_RESOURCE_URI:
			g_value_take_string (
				value,
				camel_ews_settings_dup_oauth2_resource_uri (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_OAUTH2_ENDPOINT_HOST:
			g_value_take_string (
				value,
				camel_ews_settings_dup_oauth2_endpoint_host (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_SHOW_PUBLIC_FOLDERS:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_show_public_folders (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			g_value_set_uint (
				value,
				camel_ews_settings_get_concurrent_connections (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_SYNC_TAG_STAMP:
			g_value_set_uint (
				value,
				camel_ews_settings_get_sync_tag_stamp (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_FORCE_HTTP1:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_force_http1 (
				CAMEL_EWS_SETTINGS (object)));
			return;

		case PROP_USE_OAUTH2_V2:
			g_value_set_boolean (
				value,
				camel_ews_settings_get_use_oauth2_v2 (
				CAMEL_EWS_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_settings_finalize (GObject *object)
{
	CamelEwsSettings *ews_settings = CAMEL_EWS_SETTINGS (object);

	g_mutex_clear (&ews_settings->priv->property_lock);

	g_free (ews_settings->priv->email);
	g_free (ews_settings->priv->gal_uid);
	g_free (ews_settings->priv->hosturl);
	g_free (ews_settings->priv->oaburl);
	g_free (ews_settings->priv->oal_selected);
	g_free (ews_settings->priv->impersonate_user);
	g_free (ews_settings->priv->user_agent);
	g_free (ews_settings->priv->oauth2_tenant);
	g_free (ews_settings->priv->oauth2_client_id);
	g_free (ews_settings->priv->oauth2_redirect_uri);
	g_free (ews_settings->priv->oauth2_resource_uri);
	g_free (ews_settings->priv->oauth2_endpoint_host);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_settings_parent_class)->finalize (object);
}

static void
camel_ews_settings_class_init (CamelEwsSettingsClass *class)
{
	GObjectClass *object_class;

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
			TRUE,
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

	g_object_class_install_property (
		object_class,
		PROP_TIMEOUT,
		g_param_spec_uint (
			"timeout",
			"timeout",
			"Connection timeout in seconds",
			0, G_MAXUINT, 15,
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

	g_object_class_install_property (
		object_class,
		PROP_OVERRIDE_USER_AGENT,
		g_param_spec_boolean (
			"override-user-agent",
			"Override User Agent",
			"Whether to override User-Agent header",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USER_AGENT,
		g_param_spec_string (
			"user-agent",
			"User Agent",
			"User-Agent header value to use, if override-user-agent is set to TRUE",
			"Microsoft Office/14.0 (Windows NT ,5.1; Microsoft Outlook 14.0.4734; Pro)",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OVERRIDE_OAUTH2,
		g_param_spec_boolean (
			"override-oauth2",
			"Override OAuth2",
			"Whether to override OAuth2 values",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OAUTH2_TENANT,
		g_param_spec_string (
			"oauth2-tenant",
			"OAuth2 Tenant",
			"OAuth2 Tenant to use, only if override-oauth2 is TRUE, otherwise the compile-time value is used",
			OFFICE365_TENANT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OAUTH2_CLIENT_ID,
		g_param_spec_string (
			"oauth2-client-id",
			"OAuth2 Client ID",
			"OAuth2 Client-ID to use, only if override-oauth2 is TRUE, otherwise the compile-time value is used",
			OFFICE365_CLIENT_ID,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OAUTH2_REDIRECT_URI,
		g_param_spec_string (
			"oauth2-redirect-uri",
			"OAuth2 Redirect URI",
			"OAuth2 Redirect URI to use, only if override-oauth2 is TRUE, otherwise the compile-time value is used",
			OFFICE365_REDIRECT_URI,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OAUTH2_RESOURCE_URI,
		g_param_spec_string (
			"oauth2-resource-uri",
			"OAuth2 Resource URI",
			"OAuth2 Resource URI to use, only if override-oauth2 is TRUE, otherwise the compile-time value is used",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OAUTH2_ENDPOINT_HOST,
		g_param_spec_string (
			"oauth2-endpoint-host",
			"OAuth2 Endpoint Host",
			"OAuth2 endpoint host to use, only if override-oauth2 is TRUE, otherwise the compile-time value is used",
			OFFICE365_ENDPOINT_HOST,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SHOW_PUBLIC_FOLDERS,
		g_param_spec_boolean (
			"show-public-folders",
			"Show Public Folders",
			NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_CONCURRENT_CONNECTIONS,
		g_param_spec_uint (
			"concurrent-connections",
			"Concurrent Connections",
			"Number of concurrent connections to use",
			MIN_CONCURRENT_CONNECTIONS,
			MAX_CONCURRENT_CONNECTIONS,
			1,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SYNC_TAG_STAMP,
		g_param_spec_uint (
			"sync-tag-stamp",
			"Sync Tag Stamp",
			"Stamp for synchronization tag",
			0,
			G_MAXUINT,
			0,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FORCE_HTTP1,
		g_param_spec_boolean (
			"force-http1",
			"Force HTTP1",
			NULL,
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_OAUTH2_V2,
		g_param_spec_boolean (
			"use-oauth2-v2",
			"Use OAuth2 v2",
			NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_ews_settings_init (CamelEwsSettings *settings)
{
	settings->priv = camel_ews_settings_get_instance_private (settings);
	g_mutex_init (&settings->priv->property_lock);
	settings->priv->use_oauth2_v2 = TRUE;

	e_binding_bind_property_full (settings, "hosturl",
				     settings, "host",
				     G_BINDING_DEFAULT,
				     ews_settings_transform_host_url_to_host_cb,
				     NULL,
				     NULL,
				     NULL);
}

void
camel_ews_settings_lock (CamelEwsSettings *settings)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);
}

void
camel_ews_settings_unlock (CamelEwsSettings *settings)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_unlock (&settings->priv->property_lock);
}

/**
 * camel_ews_settings_get_auth_mechanism:
 * @settings: a #CamelEwsSettings
 *
 * Returns an #EwsAuthType enum value indicating which authentication
 * method to use.
 *
 * Returns: authentication method to use for this account
 *
 * Since: 3.16
 **/
EwsAuthType
camel_ews_settings_get_auth_mechanism (CamelEwsSettings *settings)
{
	EwsAuthType result;
	gchar *auth_mech = NULL;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), EWS_AUTH_TYPE_NTLM);

	g_object_get (G_OBJECT (settings), "auth-mechanism", &auth_mech, NULL);

	/* The value for "NTLM" is a special case. Sometimes it's NULL,
	 * and sometimes it's "". But never "NTLM". No, that would be too
	 * simple. (I think it's for backward-compatibility with old
	 * profiles, so they default to NTLM). */
	if (auth_mech && g_ascii_strcasecmp (auth_mech, "PLAIN") == 0)
		result = EWS_AUTH_TYPE_BASIC;
	else if (auth_mech && g_ascii_strcasecmp (auth_mech, "GSSAPI") == 0)
		result = EWS_AUTH_TYPE_GSSAPI;
	else if (auth_mech && g_ascii_strcasecmp (auth_mech, "Office365") == 0)
		result = EWS_AUTH_TYPE_OAUTH2;
	else
		result = EWS_AUTH_TYPE_NTLM;

	g_free (auth_mech);

	return result;
}

/**
 * camel_ews_settings_get_auth_mechanism_string:
 * @settings: a #CamelEwsSettings
 *
 * Returns an authentication method to use. It's similar to
 * the camel_ews_settings_get_auth_mechanism(), only returning
 * the method as a string.
 *
 * Returns: authentication method to use for this account
 *
 * Since: 3.48
 **/
const gchar *
camel_ews_settings_get_auth_mechanism_string (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	switch (camel_ews_settings_get_auth_mechanism (settings)) {
	case EWS_AUTH_TYPE_NTLM:
		return "NTLM";
	case EWS_AUTH_TYPE_BASIC:
		return "PLAIN";
	case EWS_AUTH_TYPE_GSSAPI:
		return "GSSAPI";
	case EWS_AUTH_TYPE_OAUTH2:
		return "Office365";
	}

	return "NTLM";
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

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_email (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_email (CamelEwsSettings *settings,
                              const gchar *email)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->email, email) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->email);
	settings->priv->email = e_util_strdup_strip (email);

	camel_ews_settings_unlock (settings);

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

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_gal_uid (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_gal_uid (CamelEwsSettings *settings,
                                const gchar *gal_uid)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->gal_uid, gal_uid) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->gal_uid);
	settings->priv->gal_uid = e_util_strdup_strip (gal_uid);

	camel_ews_settings_unlock (settings);

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

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_hosturl (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_hosturl (CamelEwsSettings *settings,
                                const gchar *hosturl)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->hosturl, hosturl) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->hosturl);
	settings->priv->hosturl = e_util_strdup_strip (hosturl);

	camel_ews_settings_unlock (settings);

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

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_oaburl (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_oaburl (CamelEwsSettings *settings,
                               const gchar *oaburl)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oaburl, oaburl) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oaburl);
	settings->priv->oaburl = e_util_strdup_strip (oaburl);

	camel_ews_settings_unlock (settings);

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

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_oal_selected (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_oal_selected (CamelEwsSettings *settings,
                                     const gchar *oal_selected)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oal_selected, oal_selected) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oal_selected);
	settings->priv->oal_selected = e_util_strdup_strip (oal_selected);

	camel_ews_settings_unlock (settings);

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

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_impersonate_user (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_impersonate_user (CamelEwsSettings *settings,
					 const gchar *impersonate_user)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->impersonate_user, impersonate_user) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->impersonate_user);
	settings->priv->impersonate_user = e_util_strdup_strip (impersonate_user);

	camel_ews_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "impersonate-user");
}

gboolean
camel_ews_settings_get_override_user_agent (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->override_user_agent;
}

void
camel_ews_settings_set_override_user_agent (CamelEwsSettings *settings,
					    gboolean override_user_agent)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->override_user_agent ? 1 : 0) == (override_user_agent ? 1 : 0))
		return;

	settings->priv->override_user_agent = override_user_agent;

	g_object_notify (G_OBJECT (settings), "override-user-agent");
}

const gchar *
camel_ews_settings_get_user_agent (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->user_agent;
}

gchar *
camel_ews_settings_dup_user_agent (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_user_agent (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_user_agent (CamelEwsSettings *settings,
				   const gchar *user_agent)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->user_agent, user_agent) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->user_agent);
	settings->priv->user_agent = e_util_strdup_strip (user_agent);

	camel_ews_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "user-agent");
}

gboolean
camel_ews_settings_get_override_oauth2 (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->override_oauth2;
}

void
camel_ews_settings_set_override_oauth2 (CamelEwsSettings *settings,
					gboolean override_oauth2)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->override_oauth2 ? 1 : 0) == (override_oauth2 ? 1 : 0))
		return;

	settings->priv->override_oauth2 = override_oauth2;

	g_object_notify (G_OBJECT (settings), "override-oauth2");
}

const gchar *
camel_ews_settings_get_oauth2_tenant (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->oauth2_tenant;
}

gchar *
camel_ews_settings_dup_oauth2_tenant (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_oauth2_tenant (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_oauth2_tenant (CamelEwsSettings *settings,
				      const gchar *tenant)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_tenant, tenant) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_tenant);
	settings->priv->oauth2_tenant = e_util_strdup_strip (tenant);

	camel_ews_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-tenant");
}

const gchar *
camel_ews_settings_get_oauth2_client_id (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->oauth2_client_id;
}

gchar *
camel_ews_settings_dup_oauth2_client_id (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_oauth2_client_id (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_oauth2_client_id (CamelEwsSettings *settings,
					 const gchar *client_id)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_client_id, client_id) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_client_id);
	settings->priv->oauth2_client_id = e_util_strdup_strip (client_id);

	camel_ews_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-client-id");
}

const gchar *
camel_ews_settings_get_oauth2_redirect_uri (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->oauth2_redirect_uri;
}

gchar *
camel_ews_settings_dup_oauth2_redirect_uri (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_oauth2_redirect_uri (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_oauth2_redirect_uri (CamelEwsSettings *settings,
					    const gchar *redirect_uri)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_redirect_uri, redirect_uri) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_redirect_uri);
	settings->priv->oauth2_redirect_uri = e_util_strdup_strip (redirect_uri);

	camel_ews_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-redirect-uri");
}

const gchar *
camel_ews_settings_get_oauth2_resource_uri (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->oauth2_resource_uri;
}

gchar *
camel_ews_settings_dup_oauth2_resource_uri (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_oauth2_resource_uri (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_oauth2_resource_uri (CamelEwsSettings *settings,
					    const gchar *resource_uri)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_resource_uri, resource_uri) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_resource_uri);
	settings->priv->oauth2_resource_uri = e_util_strdup_strip (resource_uri);

	camel_ews_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-resource-uri");
}

const gchar *
camel_ews_settings_get_oauth2_endpoint_host (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	return settings->priv->oauth2_endpoint_host;
}

gchar *
camel_ews_settings_dup_oauth2_endpoint_host (CamelEwsSettings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), NULL);

	camel_ews_settings_lock (settings);

	protected = camel_ews_settings_get_oauth2_endpoint_host (settings);
	duplicate = g_strdup (protected);

	camel_ews_settings_unlock (settings);

	return duplicate;
}

void
camel_ews_settings_set_oauth2_endpoint_host (CamelEwsSettings *settings,
					     const gchar *endpoint_host)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	camel_ews_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_endpoint_host, endpoint_host) == 0) {
		camel_ews_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_endpoint_host);
	settings->priv->oauth2_endpoint_host = e_util_strdup_strip (endpoint_host);

	camel_ews_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-endpoint-host");
}

gboolean
camel_ews_settings_get_show_public_folders (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->show_public_folders;
}

void
camel_ews_settings_set_show_public_folders (CamelEwsSettings *settings,
					    gboolean show_public_folders)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->show_public_folders ? 1 : 0) == (show_public_folders ? 1 : 0))
		return;

	settings->priv->show_public_folders = show_public_folders;

	g_object_notify (G_OBJECT (settings), "show-public-folders");
}

guint
camel_ews_settings_get_concurrent_connections (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), 1);

	return settings->priv->concurrent_connections;
}

void
camel_ews_settings_set_concurrent_connections (CamelEwsSettings *settings,
					       guint concurrent_connections)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	concurrent_connections = CLAMP (
		concurrent_connections,
		MIN_CONCURRENT_CONNECTIONS,
		MAX_CONCURRENT_CONNECTIONS);

	if (settings->priv->concurrent_connections == concurrent_connections)
		return;

	settings->priv->concurrent_connections = concurrent_connections;

	g_object_notify (G_OBJECT (settings), "concurrent-connections");
}

guint
camel_ews_settings_get_sync_tag_stamp (CamelEwsSettings *settings)
{
	guint res;

	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), ~0u);

	g_mutex_lock (&settings->priv->property_lock);
	res = settings->priv->sync_tag_stamp;
	g_mutex_unlock (&settings->priv->property_lock);

	return res;
}

void
camel_ews_settings_set_sync_tag_stamp (CamelEwsSettings *settings,
				       guint value)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);

	if (settings->priv->sync_tag_stamp == value) {
		g_mutex_unlock (&settings->priv->property_lock);
		return;
	}

	settings->priv->sync_tag_stamp = value;
	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "sync-tag-stamp");
}

void
camel_ews_settings_inc_sync_tag_stamp (CamelEwsSettings *settings)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);
	settings->priv->sync_tag_stamp++;
	g_mutex_unlock (&settings->priv->property_lock);

	g_object_notify (G_OBJECT (settings), "sync-tag-stamp");
}

gboolean
camel_ews_settings_get_force_http1 (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->force_http1;
}

void
camel_ews_settings_set_force_http1 (CamelEwsSettings *settings,
				    gboolean force_http1)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->force_http1 ? 1 : 0) == (force_http1 ? 1 : 0))
		return;

	settings->priv->force_http1 = force_http1;

	g_object_notify (G_OBJECT (settings), "force-http1");
}

gboolean
camel_ews_settings_get_use_oauth2_v2 (CamelEwsSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EWS_SETTINGS (settings), FALSE);

	return settings->priv->use_oauth2_v2;
}

void
camel_ews_settings_set_use_oauth2_v2 (CamelEwsSettings *settings,
				      gboolean use_oauth2_v2)
{
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if ((settings->priv->use_oauth2_v2 ? 1 : 0) == (use_oauth2_v2 ? 1 : 0))
		return;

	settings->priv->use_oauth2_v2 = use_oauth2_v2;

	g_object_notify (G_OBJECT (settings), "use-oauth2-v2");
}
