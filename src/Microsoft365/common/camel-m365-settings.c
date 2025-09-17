/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <libebackend/libebackend.h>
#include <libedataserver/libedataserver.h>

#include "camel-m365-settings.h"

struct _CamelM365SettingsPrivate {
	GMutex property_lock;
	gboolean use_impersonation;
	gboolean check_all;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	gboolean override_oauth2;
	guint timeout;
	guint concurrent_connections;
	gchar *impersonate_user;
	gchar *email;
	gchar *oauth2_tenant;
	gchar *oauth2_client_id;
	gchar *oauth2_redirect_uri;
	gchar *oauth2_endpoint_host;
};

enum {
	PROP_0,
	PROP_AUTH_MECHANISM,
	PROP_CHECK_ALL,
	PROP_EMAIL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX,
	PROP_HOST,
	PROP_PORT,
	PROP_SECURITY_METHOD,
	PROP_TIMEOUT,
	PROP_USER,
	PROP_USE_IMPERSONATION,
	PROP_IMPERSONATE_USER,
	PROP_OVERRIDE_OAUTH2,
	PROP_OAUTH2_TENANT,
	PROP_OAUTH2_CLIENT_ID,
	PROP_OAUTH2_REDIRECT_URI,
	PROP_OAUTH2_ENDPOINT_HOST,
	PROP_CONCURRENT_CONNECTIONS
};

G_DEFINE_TYPE_WITH_CODE (CamelM365Settings, camel_m365_settings, CAMEL_TYPE_OFFLINE_SETTINGS,
	G_IMPLEMENT_INTERFACE (CAMEL_TYPE_NETWORK_SETTINGS, NULL)
	G_ADD_PRIVATE (CamelM365Settings))

static void
m365_settings_set_property (GObject *object,
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
			camel_m365_settings_set_check_all (
				CAMEL_M365_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_EMAIL:
			camel_m365_settings_set_email (
				CAMEL_M365_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_FILTER_JUNK:
			camel_m365_settings_set_filter_junk (
				CAMEL_M365_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK_INBOX:
			camel_m365_settings_set_filter_junk_inbox (
				CAMEL_M365_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_HOST:
			camel_network_settings_set_host (
				CAMEL_NETWORK_SETTINGS (object),
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
			camel_m365_settings_set_timeout (
				CAMEL_M365_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_USE_IMPERSONATION:
			camel_m365_settings_set_use_impersonation (
				CAMEL_M365_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_IMPERSONATE_USER:
			camel_m365_settings_set_impersonate_user (
				CAMEL_M365_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OVERRIDE_OAUTH2:
			camel_m365_settings_set_override_oauth2 (
				CAMEL_M365_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_OAUTH2_TENANT:
			camel_m365_settings_set_oauth2_tenant (
				CAMEL_M365_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAUTH2_CLIENT_ID:
			camel_m365_settings_set_oauth2_client_id (
				CAMEL_M365_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAUTH2_REDIRECT_URI:
			camel_m365_settings_set_oauth2_redirect_uri (
				CAMEL_M365_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OAUTH2_ENDPOINT_HOST:
			camel_m365_settings_set_oauth2_endpoint_host (
				CAMEL_M365_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			camel_m365_settings_set_concurrent_connections (
				CAMEL_M365_SETTINGS (object),
				g_value_get_uint (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_settings_get_property (GObject *object,
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
				camel_m365_settings_get_check_all (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_EMAIL:
			g_value_take_string (
				value,
				camel_m365_settings_dup_email (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK:
			g_value_set_boolean (
				value,
				camel_m365_settings_get_filter_junk (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK_INBOX:
			g_value_set_boolean (
				value,
				camel_m365_settings_get_filter_junk_inbox (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_HOST:
			g_value_take_string (
				value,
				camel_network_settings_dup_host (
				CAMEL_NETWORK_SETTINGS (object)));
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
				camel_m365_settings_get_timeout (
				CAMEL_M365_SETTINGS (object)));
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
				camel_m365_settings_get_use_impersonation (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_IMPERSONATE_USER:
			g_value_take_string (
				value,
				camel_m365_settings_dup_impersonate_user (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_OVERRIDE_OAUTH2:
			g_value_set_boolean (
				value,
				camel_m365_settings_get_override_oauth2 (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_OAUTH2_TENANT:
			g_value_take_string (
				value,
				camel_m365_settings_dup_oauth2_tenant (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_OAUTH2_CLIENT_ID:
			g_value_take_string (
				value,
				camel_m365_settings_dup_oauth2_client_id (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_OAUTH2_REDIRECT_URI:
			g_value_take_string (
				value,
				camel_m365_settings_dup_oauth2_redirect_uri (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_OAUTH2_ENDPOINT_HOST:
			g_value_take_string (
				value,
				camel_m365_settings_dup_oauth2_endpoint_host (
				CAMEL_M365_SETTINGS (object)));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			g_value_set_uint (
				value,
				camel_m365_settings_get_concurrent_connections (
				CAMEL_M365_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_settings_finalize (GObject *object)
{
	CamelM365Settings *m365_settings = CAMEL_M365_SETTINGS (object);

	g_mutex_clear (&m365_settings->priv->property_lock);

	g_free (m365_settings->priv->email);
	g_free (m365_settings->priv->oauth2_tenant);
	g_free (m365_settings->priv->oauth2_client_id);
	g_free (m365_settings->priv->oauth2_redirect_uri);
	g_free (m365_settings->priv->oauth2_endpoint_host);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_m365_settings_parent_class)->finalize (object);
}

static void
camel_m365_settings_class_init (CamelM365SettingsClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = m365_settings_set_property;
	object_class->get_property = m365_settings_get_property;
	object_class->finalize = m365_settings_finalize;

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

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_HOST,
		"host");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_PORT,
		"port");

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
			MICROSOFT365_TENANT,
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
			MICROSOFT365_CLIENT_ID,
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
			MICROSOFT365_REDIRECT_URI,
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
			MICROSOFT365_ENDPOINT_HOST,
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
}

static void
camel_m365_settings_init (CamelM365Settings *settings)
{
	settings->priv = camel_m365_settings_get_instance_private (settings);

	g_mutex_init (&settings->priv->property_lock);
}

/* transfer none. Checks the settings from the ESource extension related to backend. */
CamelM365Settings *
camel_m365_settings_get_from_backend (struct _EBackend *backend,
				      struct _ESourceRegistry *registry)
{
	ESource *source;
	ESource *collection;
	ESourceCamel *extension;
	CamelSettings *settings;
	const gchar *extension_name;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);
	if (registry)
		g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	source = e_backend_get_source (backend);
	extension_name = e_source_camel_get_extension_name ("microsoft365");
	e_source_camel_generate_subtype ("microsoft365", CAMEL_TYPE_M365_SETTINGS);

	if (registry) {
		/* It's either in the 'source' or in the collection parent. */
		collection = e_source_registry_find_extension (registry, source, extension_name);

		g_return_val_if_fail (collection != NULL, NULL);
	} else {
		collection = g_object_ref (source);
	}

	extension = e_source_get_extension (collection, extension_name);
	settings = e_source_camel_get_settings (extension);

	g_object_unref (collection);

	return CAMEL_M365_SETTINGS (settings);
}

void
camel_m365_settings_lock (CamelM365Settings *settings)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	g_mutex_lock (&settings->priv->property_lock);
}

void
camel_m365_settings_unlock (CamelM365Settings *settings)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	g_mutex_unlock (&settings->priv->property_lock);
}

gboolean
camel_m365_settings_get_use_impersonation (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), FALSE);

	return settings->priv->use_impersonation;
}

void
camel_m365_settings_set_use_impersonation (CamelM365Settings *settings,
					   gboolean use_impersonation)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	if ((settings->priv->use_impersonation ? 1 : 0) == (use_impersonation ? 1 : 0))
		return;

	settings->priv->use_impersonation = use_impersonation;

	g_object_notify (G_OBJECT (settings), "use-impersonation");
}

const gchar *
camel_m365_settings_get_impersonate_user (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return settings->priv->impersonate_user;
}

gchar *
camel_m365_settings_dup_impersonate_user (CamelM365Settings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	camel_m365_settings_lock (settings);

	protected = camel_m365_settings_get_impersonate_user (settings);
	duplicate = g_strdup (protected);

	camel_m365_settings_unlock (settings);

	return duplicate;
}

void
camel_m365_settings_set_impersonate_user (CamelM365Settings *settings,
					  const gchar *impersonate_user)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	camel_m365_settings_lock (settings);

	if (g_strcmp0 (settings->priv->impersonate_user, impersonate_user) == 0) {
		camel_m365_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->impersonate_user);
	settings->priv->impersonate_user = e_util_strdup_strip (impersonate_user);

	camel_m365_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "impersonate-user");
}

gboolean
camel_m365_settings_get_check_all (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), FALSE);

	return settings->priv->check_all;
}

void
camel_m365_settings_set_check_all (CamelM365Settings *settings,
				   gboolean check_all)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	if ((settings->priv->check_all ? 1 : 0) == (check_all ? 1 : 0))
		return;

	settings->priv->check_all = check_all;

	g_object_notify (G_OBJECT (settings), "check-all");
}

const gchar *
camel_m365_settings_get_email (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return settings->priv->email;
}

gchar *
camel_m365_settings_dup_email (CamelM365Settings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	camel_m365_settings_lock (settings);

	protected = camel_m365_settings_get_email (settings);
	duplicate = g_strdup (protected);

	camel_m365_settings_unlock (settings);

	return duplicate;
}

void
camel_m365_settings_set_email (CamelM365Settings *settings,
			       const gchar *email)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	camel_m365_settings_lock (settings);

	if (g_strcmp0 (settings->priv->email, email) == 0) {
		camel_m365_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->email);
	settings->priv->email = e_util_strdup_strip (email);

	camel_m365_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "email");
}

gboolean
camel_m365_settings_get_filter_junk (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk;
}

void
camel_m365_settings_set_filter_junk (CamelM365Settings *settings,
				     gboolean filter_junk)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	if ((settings->priv->filter_junk ? 1 : 0) == (filter_junk ? 1 : 0))
		return;

	settings->priv->filter_junk = filter_junk;

	g_object_notify (G_OBJECT (settings), "filter-junk");
}

gboolean
camel_m365_settings_get_filter_junk_inbox (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk_inbox;
}

void
camel_m365_settings_set_filter_junk_inbox (CamelM365Settings *settings,
					   gboolean filter_junk_inbox)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	if ((settings->priv->filter_junk_inbox ? 1 : 0) == (filter_junk_inbox ? 1 : 0))
		return;

	settings->priv->filter_junk_inbox = filter_junk_inbox;

	g_object_notify (G_OBJECT (settings), "filter-junk-inbox");
}

guint
camel_m365_settings_get_timeout (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), 0);

	return settings->priv->timeout;
}

void
camel_m365_settings_set_timeout (CamelM365Settings *settings,
				 guint timeout)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	if (settings->priv->timeout == timeout)
		return;

	settings->priv->timeout = timeout;

	g_object_notify (G_OBJECT (settings), "timeout");
}

gboolean
camel_m365_settings_get_override_oauth2 (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), FALSE);

	return settings->priv->override_oauth2;
}

void
camel_m365_settings_set_override_oauth2 (CamelM365Settings *settings,
					 gboolean override_oauth2)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	if ((settings->priv->override_oauth2 ? 1 : 0) == (override_oauth2 ? 1 : 0))
		return;

	settings->priv->override_oauth2 = override_oauth2;

	g_object_notify (G_OBJECT (settings), "override-oauth2");
}

const gchar *
camel_m365_settings_get_oauth2_tenant (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return settings->priv->oauth2_tenant;
}

gchar *
camel_m365_settings_dup_oauth2_tenant (CamelM365Settings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	camel_m365_settings_lock (settings);

	protected = camel_m365_settings_get_oauth2_tenant (settings);
	duplicate = g_strdup (protected);

	camel_m365_settings_unlock (settings);

	return duplicate;
}

void
camel_m365_settings_set_oauth2_tenant (CamelM365Settings *settings,
				       const gchar *tenant)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	camel_m365_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_tenant, tenant) == 0) {
		camel_m365_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_tenant);
	settings->priv->oauth2_tenant = e_util_strdup_strip (tenant);

	camel_m365_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-tenant");
}

const gchar *
camel_m365_settings_get_oauth2_client_id (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return settings->priv->oauth2_client_id;
}

gchar *
camel_m365_settings_dup_oauth2_client_id (CamelM365Settings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	camel_m365_settings_lock (settings);

	protected = camel_m365_settings_get_oauth2_client_id (settings);
	duplicate = g_strdup (protected);

	camel_m365_settings_unlock (settings);

	return duplicate;
}

void
camel_m365_settings_set_oauth2_client_id (CamelM365Settings *settings,
					  const gchar *client_id)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	camel_m365_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_client_id, client_id) == 0) {
		camel_m365_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_client_id);
	settings->priv->oauth2_client_id = e_util_strdup_strip (client_id);

	camel_m365_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-client-id");
}

const gchar *
camel_m365_settings_get_oauth2_redirect_uri (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return settings->priv->oauth2_redirect_uri;
}

gchar *
camel_m365_settings_dup_oauth2_redirect_uri (CamelM365Settings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	camel_m365_settings_lock (settings);

	protected = camel_m365_settings_get_oauth2_redirect_uri (settings);
	duplicate = g_strdup (protected);

	camel_m365_settings_unlock (settings);

	return duplicate;
}

void
camel_m365_settings_set_oauth2_redirect_uri (CamelM365Settings *settings,
					     const gchar *redirect_uri)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	camel_m365_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_redirect_uri, redirect_uri) == 0) {
		camel_m365_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_redirect_uri);
	settings->priv->oauth2_redirect_uri = e_util_strdup_strip (redirect_uri);

	camel_m365_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-redirect-uri");
}

const gchar *
camel_m365_settings_get_oauth2_endpoint_host (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return settings->priv->oauth2_endpoint_host;
}

gchar *
camel_m365_settings_dup_oauth2_endpoint_host (CamelM365Settings *settings)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	camel_m365_settings_lock (settings);

	protected = camel_m365_settings_get_oauth2_endpoint_host (settings);
	duplicate = g_strdup (protected);

	camel_m365_settings_unlock (settings);

	return duplicate;
}

void
camel_m365_settings_set_oauth2_endpoint_host (CamelM365Settings *settings,
					      const gchar *endpoint_host)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	camel_m365_settings_lock (settings);

	if (g_strcmp0 (settings->priv->oauth2_endpoint_host, endpoint_host) == 0) {
		camel_m365_settings_unlock (settings);
		return;
	}

	g_free (settings->priv->oauth2_endpoint_host);
	settings->priv->oauth2_endpoint_host = e_util_strdup_strip (endpoint_host);

	camel_m365_settings_unlock (settings);

	g_object_notify (G_OBJECT (settings), "oauth2-endpoint-host");
}

guint
camel_m365_settings_get_concurrent_connections (CamelM365Settings *settings)
{
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), 1);

	return settings->priv->concurrent_connections;
}

void
camel_m365_settings_set_concurrent_connections (CamelM365Settings *settings,
						guint concurrent_connections)
{
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));

	concurrent_connections = CLAMP (
		concurrent_connections,
		MIN_CONCURRENT_CONNECTIONS,
		MAX_CONCURRENT_CONNECTIONS);

	if (settings->priv->concurrent_connections == concurrent_connections)
		return;

	settings->priv->concurrent_connections = concurrent_connections;

	g_object_notify (G_OBJECT (settings), "concurrent-connections");
}
