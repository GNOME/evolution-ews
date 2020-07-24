/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>

#include "camel-m365-settings.h"
#include "e-m365-json-utils.h"

#include "e-m365-connection.h"

#define LOCK(x) g_rec_mutex_lock (&(x->priv->property_lock))
#define UNLOCK(x) g_rec_mutex_unlock (&(x->priv->property_lock))

#define X_EVO_M365_DATA "X-EVO-M365-DATA"

typedef enum _CSMFlags {
	CSM_DEFAULT		= 0,
	CSM_DISABLE_RESPONSE	= 1 << 0
} CSMFlags;

struct _EM365ConnectionPrivate {
	GRecMutex property_lock;

	ESource *source;
	CamelM365Settings *settings;
	SoupSession *soup_session;
	GProxyResolver *proxy_resolver;
	ESoupAuthBearer *bearer_auth;

	gchar *user; /* The default user for the URL */
	gchar *impersonate_user;

	gboolean ssl_info_set;
	gchar *ssl_certificate_pem;
	GTlsCertificateFlags ssl_certificate_errors;

	gchar *hash_key; /* in the opened connections hash */

	/* How many microseconds to wait, until can execute a new request.
	   This is to cover throttling and server unavailable responses.
	   https://docs.microsoft.com/en-us/graph/best-practices-concept#handling-expected-errors */
	gint64 backoff_for_usec;
};

enum {
	PROP_0,
	PROP_PROXY_RESOLVER,
	PROP_SETTINGS,
	PROP_SOURCE,
	PROP_CONCURRENT_CONNECTIONS,
	PROP_USER,			/* This one is hidden, write only */
	PROP_USE_IMPERSONATION,		/* This one is hidden, write only */
	PROP_IMPERSONATE_USER		/* This one is hidden, write only */
};

G_DEFINE_TYPE_WITH_PRIVATE (EM365Connection, e_m365_connection, G_TYPE_OBJECT)

static GHashTable *opened_connections = NULL;
G_LOCK_DEFINE_STATIC (opened_connections);

static gboolean
m365_log_enabled (void)
{
	static gint log_enabled = -1;

	if (log_enabled == -1)
		log_enabled = g_strcmp0 (g_getenv ("M365_DEBUG"), "1") == 0 ? 1 : 0;

	return log_enabled == 1;
}

static SoupSession *
m365_connection_ref_soup_session (EM365Connection *cnc)
{
	SoupSession *soup_session = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	LOCK (cnc);

	if (cnc->priv->soup_session)
		soup_session = g_object_ref (cnc->priv->soup_session);

	UNLOCK (cnc);

	return soup_session;
}

static void
m365_connection_utils_ensure_bearer_auth_usage (SoupSession *session,
						SoupMessage *message,
						ESoupAuthBearer *bearer)
{
	SoupSessionFeature *feature;
	SoupURI *soup_uri;

	g_return_if_fail (SOUP_IS_SESSION (session));

	/* Preload the SoupAuthManager with a valid "Bearer" token
	 * when using OAuth 2.0. This avoids an extra unauthorized
	 * HTTP round-trip, which apparently Google doesn't like. */

	feature = soup_session_get_feature (SOUP_SESSION (session), SOUP_TYPE_AUTH_MANAGER);

	if (!soup_session_feature_has_feature (feature, E_TYPE_SOUP_AUTH_BEARER)) {
		/* Add the "Bearer" auth type to support OAuth 2.0. */
		soup_session_feature_add_feature (feature, E_TYPE_SOUP_AUTH_BEARER);
	}

	soup_uri = message ? soup_message_get_uri (message) : NULL;
	if (soup_uri && soup_uri->host && *soup_uri->host) {
		soup_uri = soup_uri_copy_host (soup_uri);
	} else {
		soup_uri = NULL;
	}

	g_return_if_fail (soup_uri != NULL);

	soup_auth_manager_use_auth (SOUP_AUTH_MANAGER (feature), soup_uri, SOUP_AUTH (bearer));

	soup_uri_free (soup_uri);
}

static gboolean
m365_connection_utils_setup_bearer_auth (EM365Connection *cnc,
					 SoupSession *session,
					 SoupMessage *message,
					 gboolean is_in_authenticate_handler,
					 ESoupAuthBearer *bearer,
					 GCancellable *cancellable,
					 GError **error)
{
	ESource *source;
	gchar *access_token = NULL;
	gint expires_in_seconds = -1;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (E_IS_SOUP_AUTH_BEARER (bearer), FALSE);

	source = e_m365_connection_get_source (cnc);

	success = e_source_get_oauth2_access_token_sync (source, cancellable,
		&access_token, &expires_in_seconds, error);

	if (success) {
		e_soup_auth_bearer_set_access_token (bearer, access_token, expires_in_seconds);

		if (!is_in_authenticate_handler) {
			if (session)
				g_object_ref (session);
			else
				session = m365_connection_ref_soup_session (cnc);

			m365_connection_utils_ensure_bearer_auth_usage (session, message, bearer);

			g_clear_object (&session);
		}
	}

	g_free (access_token);

	return success;
}

static gboolean
m365_connection_utils_prepare_bearer_auth (EM365Connection *cnc,
					   SoupSession *session,
					   SoupMessage *message,
					   GCancellable *cancellable)
{
	ESource *source;
	ESoupAuthBearer *using_bearer_auth;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	source = e_m365_connection_get_source (cnc);
	if (!source)
		return TRUE;

	using_bearer_auth = e_m365_connection_ref_bearer_auth (cnc);
	if (using_bearer_auth) {
		success = m365_connection_utils_setup_bearer_auth (cnc, session, message, FALSE, using_bearer_auth, cancellable, &local_error);
		g_clear_object (&using_bearer_auth);
	} else {
		SoupAuth *soup_auth;
		SoupURI *soup_uri;

		soup_uri = message ? soup_message_get_uri (message) : NULL;
		if (soup_uri && soup_uri->host && *soup_uri->host) {
			soup_uri = soup_uri_copy_host (soup_uri);
		} else {
			soup_uri = NULL;
		}

		g_warn_if_fail (soup_uri != NULL);

		if (!soup_uri) {
			soup_message_set_status_full (message, SOUP_STATUS_MALFORMED, "Cannot get host from message");
			return FALSE;
		}

		soup_auth = g_object_new (E_TYPE_SOUP_AUTH_BEARER, SOUP_AUTH_HOST, soup_uri->host, NULL);

		success = m365_connection_utils_setup_bearer_auth (cnc, session, message, FALSE, E_SOUP_AUTH_BEARER (soup_auth), cancellable, &local_error);
		if (success)
			e_m365_connection_set_bearer_auth (cnc, E_SOUP_AUTH_BEARER (soup_auth));

		g_object_unref (soup_auth);
		soup_uri_free (soup_uri);
	}

	if (!success) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			soup_message_set_status (message, SOUP_STATUS_CANCELLED);
		else if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
			 g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			soup_message_set_status_full (message, SOUP_STATUS_UNAUTHORIZED, local_error->message);
		else
			soup_message_set_status_full (message, SOUP_STATUS_MALFORMED, local_error ? local_error->message : _("Unknown error"));
	}

	g_clear_error (&local_error);

	return success;
}

static void
m365_connection_authenticate (SoupSession *session,
			      SoupMessage *msg,
			      SoupAuth *auth,
			      gboolean retrying,
			      gpointer user_data)
{
	EM365Connection *cnc = user_data;
	ESoupAuthBearer *using_bearer_auth;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	using_bearer_auth = e_m365_connection_ref_bearer_auth (cnc);

	if (E_IS_SOUP_AUTH_BEARER (auth)) {
		g_object_ref (auth);
		g_warn_if_fail ((gpointer) using_bearer_auth == (gpointer) auth);

		g_clear_object (&using_bearer_auth);
		using_bearer_auth = E_SOUP_AUTH_BEARER (auth);

		e_m365_connection_set_bearer_auth (cnc, using_bearer_auth);
	}

	if (!using_bearer_auth) {
		g_warn_if_reached ();
		return;
	}

	m365_connection_utils_setup_bearer_auth (cnc, session, msg, TRUE, E_SOUP_AUTH_BEARER (auth), NULL, &local_error);

	if (local_error)
		soup_message_set_status_full (msg, SOUP_STATUS_IO_ERROR, local_error->message);

	g_object_unref (using_bearer_auth);
	g_clear_error (&local_error);
}

static gboolean
m365_connection_utils_prepare_message (EM365Connection *cnc,
				       SoupSession *session,
				       SoupMessage *message,
				       GCancellable *cancellable)
{
	ESoupAuthBearer *using_bearer_auth;
	ESource *source;
	GError *local_error = NULL;

	source = e_m365_connection_get_source (cnc);
	if (source)
		e_soup_ssl_trust_connect (message, source);

	if (!m365_connection_utils_prepare_bearer_auth (cnc, session, message, cancellable))
		return FALSE;

	using_bearer_auth = e_m365_connection_ref_bearer_auth (cnc);

	if (using_bearer_auth &&
	    e_soup_auth_bearer_is_expired (using_bearer_auth) &&
	    !m365_connection_utils_setup_bearer_auth (cnc, session, message, FALSE, using_bearer_auth, cancellable, &local_error)) {
		if (local_error) {
			soup_message_set_status_full (message, SOUP_STATUS_BAD_REQUEST, local_error->message);
			g_clear_error (&local_error);
		} else {
			soup_message_set_status (message, SOUP_STATUS_BAD_REQUEST);
		}

		g_object_unref (using_bearer_auth);

		return FALSE;
	}

	g_clear_object (&using_bearer_auth);

	return TRUE;
}

static void
m365_connection_set_settings (EM365Connection *cnc,
			      CamelM365Settings *settings)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));
	g_return_if_fail (CAMEL_IS_M365_SETTINGS (settings));
	g_return_if_fail (cnc->priv->settings == NULL);

	cnc->priv->settings = g_object_ref (settings);

	e_binding_bind_property (
		cnc->priv->settings, "user",
		cnc, "user",
		G_BINDING_DEFAULT |
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		cnc->priv->settings, "use-impersonation",
		cnc, "use-impersonation",
		G_BINDING_DEFAULT |
		G_BINDING_SYNC_CREATE);

	/* No need to G_BINDING_SYNC_CREATE, because the 'use-impersonation' already updated the value */
	e_binding_bind_property (
		cnc->priv->settings, "impersonate-user",
		cnc, "impersonate-user",
		G_BINDING_DEFAULT);
}

static void
m365_connection_set_source (EM365Connection *cnc,
			    ESource *source)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (cnc->priv->source == NULL);

	cnc->priv->source = g_object_ref (source);
}

static void
m365_connection_take_user (EM365Connection *cnc,
			   gchar *user)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	if (!user || !*user)
		g_clear_pointer (&user, g_free);

	g_free (cnc->priv->user);
	cnc->priv->user = user;

	UNLOCK (cnc);
}

static void
m365_connection_take_impersonate_user (EM365Connection *cnc,
				       gchar *impersonate_user)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	if (!impersonate_user || !*impersonate_user ||
	    !camel_m365_settings_get_use_impersonation (cnc->priv->settings)) {
		g_clear_pointer (&impersonate_user, g_free);
	}

	if (g_strcmp0 (impersonate_user, cnc->priv->impersonate_user) != 0) {
		g_free (cnc->priv->impersonate_user);
		cnc->priv->impersonate_user = impersonate_user;
	} else {
		g_clear_pointer (&impersonate_user, g_free);
	}

	UNLOCK (cnc);
}

static void
m365_connection_set_use_impersonation (EM365Connection *cnc,
				       gboolean use_impersonation)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	if (!use_impersonation)
		m365_connection_take_impersonate_user (cnc, NULL);
	else
		m365_connection_take_impersonate_user (cnc, camel_m365_settings_dup_impersonate_user (cnc->priv->settings));

	UNLOCK (cnc);
}

static void
m365_connection_set_property (GObject *object,
			      guint property_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PROXY_RESOLVER:
			e_m365_connection_set_proxy_resolver (
				E_M365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SETTINGS:
			m365_connection_set_settings (
				E_M365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SOURCE:
			m365_connection_set_source (
				E_M365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			e_m365_connection_set_concurrent_connections (
				E_M365_CONNECTION (object),
				g_value_get_uint (value));
			return;

		case PROP_USER:
			m365_connection_take_user (
				E_M365_CONNECTION (object),
				g_value_dup_string (value));
			return;

		case PROP_USE_IMPERSONATION:
			m365_connection_set_use_impersonation (
				E_M365_CONNECTION (object),
				g_value_get_boolean (value));
			return;

		case PROP_IMPERSONATE_USER:
			m365_connection_take_impersonate_user (
				E_M365_CONNECTION (object),
				g_value_dup_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_connection_get_property (GObject *object,
			      guint property_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PROXY_RESOLVER:
			g_value_take_object (
				value,
				e_m365_connection_ref_proxy_resolver (
				E_M365_CONNECTION (object)));
			return;

		case PROP_SETTINGS:
			g_value_set_object (
				value,
				e_m365_connection_get_settings (
				E_M365_CONNECTION (object)));
			return;

		case PROP_SOURCE:
			g_value_set_object (
				value,
				e_m365_connection_get_source (
				E_M365_CONNECTION (object)));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			g_value_set_uint (
				value,
				e_m365_connection_get_concurrent_connections (
				E_M365_CONNECTION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
m365_connection_constructed (GObject *object)
{
	EM365Connection *cnc = E_M365_CONNECTION (object);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_connection_parent_class)->constructed (object);

	if (m365_log_enabled ()) {
		SoupLogger *logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);

		soup_session_add_feature (cnc->priv->soup_session, SOUP_SESSION_FEATURE (logger));

		g_object_unref (logger);
	}

	soup_session_add_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_COOKIE_JAR);
	soup_session_add_feature_by_type (cnc->priv->soup_session, E_TYPE_SOUP_AUTH_BEARER);
	soup_session_remove_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_AUTH_BASIC);

	g_signal_connect (
		cnc->priv->soup_session, "authenticate",
		G_CALLBACK (m365_connection_authenticate), cnc);

	cnc->priv->hash_key = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (cnc->priv->settings));

	if (!cnc->priv->hash_key)
		cnc->priv->hash_key = g_strdup ("no-user");

	e_binding_bind_property (
		cnc->priv->settings, "timeout",
		cnc->priv->soup_session, SOUP_SESSION_TIMEOUT,
		G_BINDING_SYNC_CREATE);
}

static void
m365_connection_dispose (GObject *object)
{
	EM365Connection *cnc = E_M365_CONNECTION (object);

	G_LOCK (opened_connections);

	/* Remove the connection from the opened connections */
	if (opened_connections &&
	    g_hash_table_lookup (opened_connections, cnc->priv->hash_key) == (gpointer) object) {
		g_hash_table_remove (opened_connections, cnc->priv->hash_key);
		if (g_hash_table_size (opened_connections) == 0) {
			g_hash_table_destroy (opened_connections);
			opened_connections = NULL;
		}
	}

	G_UNLOCK (opened_connections);

	LOCK (cnc);

	if (cnc->priv->soup_session) {
		g_signal_handlers_disconnect_by_func (
			cnc->priv->soup_session,
			m365_connection_authenticate, object);
	}

	g_clear_object (&cnc->priv->source);
	g_clear_object (&cnc->priv->settings);
	g_clear_object (&cnc->priv->soup_session);
	g_clear_object (&cnc->priv->proxy_resolver);
	g_clear_object (&cnc->priv->bearer_auth);

	UNLOCK (cnc);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_connection_parent_class)->dispose (object);
}

static void
m365_connection_finalize (GObject *object)
{
	EM365Connection *cnc = E_M365_CONNECTION (object);

	g_rec_mutex_clear (&cnc->priv->property_lock);
	g_clear_pointer (&cnc->priv->ssl_certificate_pem, g_free);
	g_clear_pointer (&cnc->priv->user, g_free);
	g_clear_pointer (&cnc->priv->impersonate_user, g_free);
	g_free (cnc->priv->hash_key);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_m365_connection_parent_class)->finalize (object);
}

static void
e_m365_connection_class_init (EM365ConnectionClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = m365_connection_set_property;
	object_class->get_property = m365_connection_get_property;
	object_class->constructed = m365_connection_constructed;
	object_class->dispose = m365_connection_dispose;
	object_class->finalize = m365_connection_finalize;

	g_object_class_install_property (
		object_class,
		PROP_PROXY_RESOLVER,
		g_param_spec_object (
			"proxy-resolver",
			"Proxy Resolver",
			"The proxy resolver for this backend",
			G_TYPE_PROXY_RESOLVER,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SETTINGS,
		g_param_spec_object (
			"settings",
			"Settings",
			"Connection settings",
			CAMEL_TYPE_M365_SETTINGS,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			"Corresponding ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
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
			/* Do not construct it, otherwise it overrides the value derived from CamelM365Settings */
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USER,
		g_param_spec_string (
			"user",
			NULL,
			NULL,
			NULL,
			G_PARAM_WRITABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_IMPERSONATION,
		g_param_spec_boolean (
			"use-impersonation",
			NULL,
			NULL,
			FALSE,
			G_PARAM_WRITABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_IMPERSONATE_USER,
		g_param_spec_string (
			"impersonate-user",
			NULL,
			NULL,
			NULL,
			G_PARAM_WRITABLE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_m365_connection_init (EM365Connection *cnc)
{
	cnc->priv = e_m365_connection_get_instance_private (cnc);

	g_rec_mutex_init (&cnc->priv->property_lock);

	cnc->priv->backoff_for_usec = 0;
	cnc->priv->soup_session = soup_session_new_with_options (
		SOUP_SESSION_TIMEOUT, 90,
		SOUP_SESSION_SSL_STRICT, TRUE,
		SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
		NULL);

	/* Do not use G_BINDING_SYNC_CREATE, because we don't have a GProxyResolver yet anyway. */
	e_binding_bind_property (
		cnc, "proxy-resolver",
		cnc->priv->soup_session, "proxy-resolver",
		G_BINDING_DEFAULT);
}

gboolean
e_m365_connection_util_delta_token_failed (const GError *error)
{
	return g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
	       g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_BAD_REQUEST);
}

EM365Connection *
e_m365_connection_new (ESource *source,
		       CamelM365Settings *settings)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	return e_m365_connection_new_full (source, settings, TRUE);
}

EM365Connection *
e_m365_connection_new_for_backend (EBackend *backend,
				   ESourceRegistry *registry,
				   ESource *source,
				   CamelM365Settings *settings)
{
	ESource *backend_source, *parent_source;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (CAMEL_IS_M365_SETTINGS (settings), NULL);

	backend_source = e_backend_get_source (backend);

	if (!backend_source)
		return e_m365_connection_new (source, settings);

	parent_source = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (parent_source) {
		EM365Connection *cnc;

		cnc = e_m365_connection_new (parent_source, settings);

		g_object_unref (parent_source);

		return cnc;
	}

	return e_m365_connection_new (source, settings);
}

EM365Connection *
e_m365_connection_new_full (ESource *source,
			    CamelM365Settings *settings,
			    gboolean allow_reuse)
{
	EM365Connection *cnc;

	if (allow_reuse) {
		gchar *hash_key = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (settings));

		if (hash_key) {
			G_LOCK (opened_connections);

			if (opened_connections) {
				cnc = g_hash_table_lookup (opened_connections, hash_key);

				if (cnc) {
					g_object_ref (cnc);
					G_UNLOCK (opened_connections);

					g_free (hash_key);

					return cnc;
				}
			}

			G_UNLOCK (opened_connections);
		}

		g_free (hash_key);
	}

	cnc = g_object_new (E_TYPE_M365_CONNECTION,
		"source", source,
		"settings", settings,
		NULL);

	if (allow_reuse && cnc->priv->hash_key) {
		G_LOCK (opened_connections);

		if (!opened_connections)
			opened_connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

		g_hash_table_insert (opened_connections, g_strdup (cnc->priv->hash_key), cnc);

		G_UNLOCK (opened_connections);
	}

	return cnc;
}

ESource *
e_m365_connection_get_source (EM365Connection *cnc)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	return cnc->priv->source;
}

CamelM365Settings *
e_m365_connection_get_settings (EM365Connection *cnc)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	return cnc->priv->settings;
}

guint
e_m365_connection_get_concurrent_connections (EM365Connection *cnc)
{
	guint current_cc = 0;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), 1);

	LOCK (cnc);

	g_object_get (G_OBJECT (cnc->priv->soup_session), SOUP_SESSION_MAX_CONNS, &current_cc, NULL);

	UNLOCK (cnc);

	return current_cc;
}

void
e_m365_connection_set_concurrent_connections (EM365Connection *cnc,
					      guint concurrent_connections)
{
	guint current_cc;

	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	concurrent_connections = CLAMP (concurrent_connections, MIN_CONCURRENT_CONNECTIONS, MAX_CONCURRENT_CONNECTIONS);

	current_cc = e_m365_connection_get_concurrent_connections (cnc);

	if (current_cc == concurrent_connections)
		return;

	LOCK (cnc);

	g_object_set (G_OBJECT (cnc->priv->soup_session),
		SOUP_SESSION_MAX_CONNS, concurrent_connections,
		SOUP_SESSION_MAX_CONNS_PER_HOST, concurrent_connections,
		NULL);

	UNLOCK (cnc);

	g_object_notify (G_OBJECT (cnc), "concurrent-connections");
}

GProxyResolver *
e_m365_connection_ref_proxy_resolver (EM365Connection *cnc)
{
	GProxyResolver *proxy_resolver = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	LOCK (cnc);

	if (cnc->priv->proxy_resolver)
		proxy_resolver = g_object_ref (cnc->priv->proxy_resolver);

	UNLOCK (cnc);

	return proxy_resolver;
}

void
e_m365_connection_set_proxy_resolver (EM365Connection *cnc,
				      GProxyResolver *proxy_resolver)
{
	gboolean notify = FALSE;

	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	/* Emitting a "notify" signal unnecessarily might have
	 * unwanted side effects like cancelling a SoupMessage.
	 * Only emit if we now have a different GProxyResolver. */

	if (proxy_resolver != cnc->priv->proxy_resolver) {
		g_clear_object (&cnc->priv->proxy_resolver);
		cnc->priv->proxy_resolver = proxy_resolver;

		if (proxy_resolver)
			g_object_ref (proxy_resolver);

		notify = TRUE;
	}

	UNLOCK (cnc);

	if (notify)
		g_object_notify (G_OBJECT (cnc), "proxy-resolver");
}

ESoupAuthBearer *
e_m365_connection_ref_bearer_auth (EM365Connection *cnc)
{
	ESoupAuthBearer *res = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	LOCK (cnc);

	if (cnc->priv->bearer_auth)
		res = g_object_ref (cnc->priv->bearer_auth);

	UNLOCK (cnc);

	return res;
}

void
e_m365_connection_set_bearer_auth (EM365Connection *cnc,
				   ESoupAuthBearer *bearer_auth)
{
	g_return_if_fail (E_IS_M365_CONNECTION (cnc));

	LOCK (cnc);

	if (cnc->priv->bearer_auth != bearer_auth) {
		g_clear_object (&cnc->priv->bearer_auth);

		cnc->priv->bearer_auth = bearer_auth;

		if (cnc->priv->bearer_auth)
			g_object_ref (cnc->priv->bearer_auth);
	}

	UNLOCK (cnc);
}

static void
m365_connection_request_cancelled_cb (GCancellable *cancellable,
				      gpointer user_data)
{
	EFlag *flag = user_data;

	g_return_if_fail (flag != NULL);

	e_flag_set (flag);
}

static void
m365_connection_extract_ssl_data (EM365Connection *cnc,
				  SoupMessage *message)
{
	GTlsCertificate *certificate = NULL;

	g_return_if_fail (E_IS_M365_CONNECTION (cnc));
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	LOCK (cnc);

	g_clear_pointer (&cnc->priv->ssl_certificate_pem, g_free);
	cnc->priv->ssl_info_set = FALSE;

	g_object_get (G_OBJECT (message),
		"tls-certificate", &certificate,
		"tls-errors", &cnc->priv->ssl_certificate_errors,
		NULL);

	if (certificate) {
		g_object_get (certificate, "certificate-pem", &cnc->priv->ssl_certificate_pem, NULL);
		cnc->priv->ssl_info_set = TRUE;

		g_object_unref (certificate);
	}

	UNLOCK (cnc);
}

/* An example error response:

  {
    "error": {
      "code": "BadRequest",
      "message": "Parsing Select and Expand failed.",
      "innerError": {
        "request-id": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
        "date": "2020-06-10T13:44:43"
      }
    }
  }

 */
static gboolean
m365_connection_extract_error (JsonNode *node,
			       guint status_code,
			       GError **error)
{
	JsonObject *object;
	const gchar *code, *message;

	if (!node || !JSON_NODE_HOLDS_OBJECT (node))
		return FALSE;

	object = e_m365_json_get_object_member (json_node_get_object (node), "error");

	if (!object)
		return FALSE;

	code = e_m365_json_get_string_member (object, "code", NULL);
	message = e_m365_json_get_string_member (object, "message", NULL);

	if (!code && !message)
		return FALSE;

	if (!status_code || SOUP_STATUS_IS_SUCCESSFUL (status_code))
		status_code = SOUP_STATUS_MALFORMED;
	else if (g_strcmp0 (code, "ErrorInvalidUser") == 0)
		status_code = SOUP_STATUS_UNAUTHORIZED;

	if (code && message)
		g_set_error (error, SOUP_HTTP_ERROR, status_code, "%s: %s", code, message);
	else
		g_set_error_literal (error, SOUP_HTTP_ERROR, status_code, code ? code : message);

	return TRUE;
}

typedef gboolean (* EM365ResponseFunc)	(EM365Connection *cnc,
					 SoupMessage *message,
					 GInputStream *input_stream,
					 JsonNode *node,
					 gpointer user_data,
					 gchar **out_next_link,
					 GCancellable *cancellable,
					 GError **error);

/* (transfer full) (nullable): Free the *out_node with json_node_unref(), if not NULL;
   It can return 'success', even when the *out_node is NULL. */
gboolean
e_m365_connection_json_node_from_message (SoupMessage *message,
					  GInputStream *input_stream,
					  JsonNode **out_node,
					  GCancellable *cancellable,
					  GError **error)
{
	JsonObject *message_json_object;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (out_node != NULL, FALSE);

	*out_node = NULL;

	message_json_object = g_object_get_data (G_OBJECT (message), X_EVO_M365_DATA);

	if (message_json_object) {
		*out_node = json_node_init_object (json_node_new (JSON_NODE_OBJECT), message_json_object);

		success = !m365_connection_extract_error (*out_node, message->status_code, &local_error);
	} else {
		const gchar *content_type;

		content_type = message->response_headers ? soup_message_headers_get_content_type (message->response_headers, NULL) : NULL;

		if (content_type && g_ascii_strcasecmp (content_type, "application/json") == 0) {
			JsonParser *json_parser;

			json_parser = json_parser_new_immutable ();

			if (input_stream) {
				success = json_parser_load_from_stream (json_parser, input_stream, cancellable, error);
			} else {
				SoupBuffer *sbuffer;

				sbuffer = soup_message_body_flatten (message->response_body);

				if (sbuffer) {
					success = json_parser_load_from_data (json_parser, sbuffer->data, sbuffer->length, error);
					soup_buffer_free (sbuffer);
				} else {
					/* This should not happen, it's for safety check only, thus the string is not localized */
					success = FALSE;
					g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED, "No JSON data found");
				}
			}

			if (success) {
				*out_node = json_parser_steal_root (json_parser);

				success = !m365_connection_extract_error (*out_node, message->status_code, &local_error);
			}

			g_object_unref (json_parser);
		}
	}

	if (!success && *out_node) {
		json_node_unref (*out_node);
		*out_node = NULL;
	}

	if (local_error)
		g_propagate_error (error, local_error);

	return success;
}

static gboolean
m365_connection_send_request_sync (EM365Connection *cnc,
				   SoupMessage *message,
				   EM365ResponseFunc response_func,
				   EM365ConnectionRawDataFunc raw_data_func,
				   gpointer func_user_data,
				   GCancellable *cancellable,
				   GError **error)
{
	SoupSession *soup_session;
	gint need_retry_seconds = 5;
	gboolean success = FALSE, need_retry = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (response_func != NULL || raw_data_func != NULL, FALSE);
	g_return_val_if_fail (response_func == NULL || raw_data_func == NULL, FALSE);

	while (need_retry && !g_cancellable_is_cancelled (cancellable) && message->status_code != SOUP_STATUS_CANCELLED) {
		need_retry = FALSE;

		LOCK (cnc);

		if (cnc->priv->backoff_for_usec) {
			EFlag *flag;
			gint64 wait_ms;
			gulong handler_id = 0;

			wait_ms = cnc->priv->backoff_for_usec / G_TIME_SPAN_MILLISECOND;

			UNLOCK (cnc);

			flag = e_flag_new ();

			if (cancellable) {
				handler_id = g_cancellable_connect (cancellable, G_CALLBACK (m365_connection_request_cancelled_cb),
					flag, NULL);
			}

			while (wait_ms > 0 && !g_cancellable_is_cancelled (cancellable) && message->status_code != SOUP_STATUS_CANCELLED) {
				gint64 now = g_get_monotonic_time ();
				gint left_minutes, left_seconds;

				left_minutes = wait_ms / 60000;
				left_seconds = (wait_ms / 1000) % 60;

				if (left_minutes > 0) {
					camel_operation_push_message (cancellable,
						g_dngettext (GETTEXT_PACKAGE,
							"Microsoft 365 server is busy, waiting to retry (%d:%02d minute)",
							"Microsoft 365 server is busy, waiting to retry (%d:%02d minutes)", left_minutes),
						left_minutes, left_seconds);
				} else {
					camel_operation_push_message (cancellable,
						g_dngettext (GETTEXT_PACKAGE,
							"Microsoft 365 server is busy, waiting to retry (%d second)",
							"Microsoft 365 server is busy, waiting to retry (%d seconds)", left_seconds),
						left_seconds);
				}

				e_flag_wait_until (flag, now + (G_TIME_SPAN_MILLISECOND * (wait_ms > 1000 ? 1000 : wait_ms)));
				e_flag_clear (flag);

				now = g_get_monotonic_time () - now;
				now = now / G_TIME_SPAN_MILLISECOND;

				if (now >= wait_ms)
					wait_ms = 0;
				wait_ms -= now;

				camel_operation_pop_message (cancellable);
			}

			if (handler_id)
				g_cancellable_disconnect (cancellable, handler_id);

			e_flag_free (flag);

			LOCK (cnc);

			cnc->priv->backoff_for_usec = 0;
		}

		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			UNLOCK (cnc);

			soup_message_set_status (message, SOUP_STATUS_CANCELLED);

			return FALSE;
		}

		soup_session = cnc->priv->soup_session ? g_object_ref (cnc->priv->soup_session) : NULL;

		g_clear_pointer (&cnc->priv->ssl_certificate_pem, g_free);
		cnc->priv->ssl_certificate_errors = 0;
		cnc->priv->ssl_info_set = FALSE;

		UNLOCK (cnc);

		if (soup_session &&
		    m365_connection_utils_prepare_message (cnc, soup_session, message, cancellable)) {
			GInputStream *input_stream;

			input_stream = soup_session_send (soup_session, message, cancellable, error);

			success = input_stream != NULL;

			if (success && m365_log_enabled ())
				input_stream = e_soup_logger_attach (message, input_stream);

			/* Throttling - https://docs.microsoft.com/en-us/graph/throttling  */
			if (message->status_code == 429 ||
			    /* https://docs.microsoft.com/en-us/graph/best-practices-concept#handling-expected-errors */
			    message->status_code == SOUP_STATUS_SERVICE_UNAVAILABLE) {
				need_retry = TRUE;
			} else if (message->status_code == SOUP_STATUS_SSL_FAILED) {
				m365_connection_extract_ssl_data (cnc, message);
			}

			if (need_retry) {
				const gchar *retry_after_str;
				gint64 retry_after;

				retry_after_str = message->response_headers ? soup_message_headers_get_one (message->response_headers, "Retry-After") : NULL;

				if (retry_after_str && *retry_after_str)
					retry_after = g_ascii_strtoll (retry_after_str, NULL, 10);
				else
					retry_after = 0;

				if (retry_after > 0)
					need_retry_seconds = retry_after;
				else if (need_retry_seconds < 120)
					need_retry_seconds *= 2;

				LOCK (cnc);

				if (cnc->priv->backoff_for_usec < need_retry_seconds * G_USEC_PER_SEC)
					cnc->priv->backoff_for_usec = need_retry_seconds * G_USEC_PER_SEC;

				if (message->status_code == SOUP_STATUS_SERVICE_UNAVAILABLE)
					soup_session_abort (soup_session);

				UNLOCK (cnc);

				success = FALSE;
			} else if (success && raw_data_func && SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
				success = raw_data_func (cnc, message, input_stream, func_user_data, cancellable, error);
			} else if (success) {
				JsonNode *node = NULL;

				success = e_m365_connection_json_node_from_message (message, input_stream, &node, cancellable, error);

				if (success) {
					gchar *next_link = NULL;

					success = response_func && response_func (cnc, message, input_stream, node, func_user_data, &next_link, cancellable, error);

					if (success && next_link && *next_link) {
						SoupURI *suri;

						suri = soup_uri_new (next_link);

						/* Check whether the server returned correct nextLink URI */
						g_warn_if_fail (suri != NULL);

						if (suri) {
							need_retry = TRUE;

							soup_message_set_uri (message, suri);
							soup_uri_free (suri);
						}
					}

					g_free (next_link);
				} else if (error && !*error && message->status_code && !SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
					if (message->status_code == SOUP_STATUS_CANCELLED) {
						g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
							message->reason_phrase ? message->reason_phrase : soup_status_get_phrase (message->status_code));
					} else {
						g_set_error_literal (error, SOUP_HTTP_ERROR, message->status_code,
							message->reason_phrase ? message->reason_phrase : soup_status_get_phrase (message->status_code));
					}
				}

				if (node)
					json_node_unref (node);
			}

			g_clear_object (&input_stream);
		} else {
			if (!message->status_code)
				soup_message_set_status (message, SOUP_STATUS_CANCELLED);

			if (message->status_code == SOUP_STATUS_CANCELLED) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
					message->reason_phrase ? message->reason_phrase : soup_status_get_phrase (message->status_code));
			} else {
				g_set_error_literal (error, SOUP_HTTP_ERROR, message->status_code,
					message->reason_phrase ? message->reason_phrase : soup_status_get_phrase (message->status_code));
			}
		}

		g_clear_object (&soup_session);

		if (need_retry) {
			success = FALSE;
			g_clear_error (error);
		}
	}

	return success;
}

static gboolean
e_m365_read_no_response_cb (EM365Connection *cnc,
			    SoupMessage *message,
			    GInputStream *raw_data_stream,
			    gpointer user_data,
			    GCancellable *cancellable,
			    GError **error)
{
	/* This is used when no response is expected from the server.
	   Read the data stream only if debugging is on, in case
	   the server returns anything interesting. */

	if (m365_log_enabled ()) {
		gchar buffer[4096];

		while (g_input_stream_read (raw_data_stream, buffer, sizeof (buffer), cancellable, error) > 0) {
			/* Do nothing, just read it, thus it's shown in the debug output */
		}
	}

	return TRUE;
}

static gboolean
e_m365_read_to_byte_array_cb (EM365Connection *cnc,
			      SoupMessage *message,
			      GInputStream *raw_data_stream,
			      gpointer user_data,
			      GCancellable *cancellable,
			      GError **error)
{
	GByteArray **out_byte_array = user_data;
	gchar buffer[4096];
	gssize n_read;

	g_return_val_if_fail (message != NULL, FALSE);
	g_return_val_if_fail (out_byte_array != NULL, FALSE);

	if (!*out_byte_array) {
		goffset content_length;

		content_length = soup_message_headers_get_content_length (message->response_headers);

		if (!content_length || content_length > 65536)
			content_length = 65535;

		*out_byte_array = g_byte_array_sized_new (content_length);
	}

	while (n_read = g_input_stream_read (raw_data_stream, buffer, sizeof (buffer), cancellable, error), n_read > 0) {
		g_byte_array_append (*out_byte_array, (const guint8 *) buffer, n_read);
	}

	return !n_read;
}

typedef struct _EM365ResponseData {
	EM365ConnectionJsonFunc json_func;
	gpointer func_user_data;
	gboolean read_only_once; /* To be able to just try authentication */
	GSList **out_items; /* JsonObject * */
	gchar **out_delta_link; /* set only if available and not NULL */
	GPtrArray *inout_requests; /* SoupMessage *, for the batch request */
} EM365ResponseData;

static gboolean
e_m365_read_valued_response_cb (EM365Connection *cnc,
				SoupMessage *message,
				GInputStream *input_stream,
				JsonNode *node,
				gpointer user_data,
				gchar **out_next_link,
				GCancellable *cancellable,
				GError **error)
{
	EM365ResponseData *response_data = user_data;
	JsonObject *object;
	JsonArray *value;
	const gchar *delta_link;
	GSList *items = NULL;
	gboolean can_continue = TRUE;
	guint ii, len;

	g_return_val_if_fail (response_data != NULL, FALSE);
	g_return_val_if_fail (out_next_link != NULL, FALSE);
	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), FALSE);

	object = json_node_get_object (node);
	g_return_val_if_fail (object != NULL, FALSE);

	if (!response_data->read_only_once)
		*out_next_link = g_strdup (e_m365_json_get_string_member (object, "@odata.nextLink", NULL));

	delta_link = e_m365_json_get_string_member (object, "@odata.deltaLink", NULL);

	if (delta_link && response_data->out_delta_link)
		*response_data->out_delta_link = g_strdup (delta_link);

	value = e_m365_json_get_array_member (object, "value");
	g_return_val_if_fail (value != NULL, FALSE);

	len = json_array_get_length (value);

	for (ii = 0; ii < len; ii++) {
		JsonNode *elem = json_array_get_element (value, ii);

		g_warn_if_fail (JSON_NODE_HOLDS_OBJECT (elem));

		if (JSON_NODE_HOLDS_OBJECT (elem)) {
			JsonObject *elem_object = json_node_get_object (elem);

			if (elem_object) {
				if (response_data->out_items)
					*response_data->out_items = g_slist_prepend (*response_data->out_items, json_object_ref (elem_object));
				else
					items = g_slist_prepend (items, json_object_ref (elem_object));
			}
		}
	}

	if (response_data->json_func)
		can_continue = response_data->json_func (cnc, items, response_data->func_user_data, cancellable, error);

	g_slist_free_full (items, (GDestroyNotify) json_object_unref);

	return can_continue;
}

static gboolean
e_m365_read_json_object_response_cb (EM365Connection *cnc,
				     SoupMessage *message,
				     GInputStream *input_stream,
				     JsonNode *node,
				     gpointer user_data,
				     gchar **out_next_link,
				     GCancellable *cancellable,
				     GError **error)
{
	JsonObject **out_json_object = user_data;
	JsonObject *object;

	g_return_val_if_fail (out_json_object != NULL, FALSE);
	g_return_val_if_fail (out_next_link != NULL, FALSE);
	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), FALSE);

	object = json_node_get_object (node);
	g_return_val_if_fail (object != NULL, FALSE);

	*out_json_object = json_object_ref (object);

	return TRUE;
}

static SoupMessage *
m365_connection_new_soup_message (const gchar *method,
				  const gchar *uri,
				  CSMFlags csm_flags,
				  GError **error)
{
	SoupMessage *message;

	g_return_val_if_fail (method != NULL, NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	message = soup_message_new (method, uri);

	if (message) {
		soup_message_headers_append (message->request_headers, "Connection", "Close");
		soup_message_headers_append (message->request_headers, "User-Agent", "Evolution-M365/" VERSION);

		/* Disable caching for proxies (RFC 4918, section 10.4.5) */
		soup_message_headers_append (message->request_headers, "Cache-Control", "no-cache");
		soup_message_headers_append (message->request_headers, "Pragma", "no-cache");

		if ((csm_flags & CSM_DISABLE_RESPONSE) != 0)
			soup_message_headers_append (message->request_headers, "Prefer", "return=minimal");
	} else {
		g_set_error (error, SOUP_HTTP_ERROR, SOUP_STATUS_MALFORMED, _("Malformed URI: “%s”"), uri);
	}

	return message;
}

gboolean
e_m365_connection_get_ssl_error_details (EM365Connection *cnc,
					 gchar **out_certificate_pem,
					 GTlsCertificateFlags *out_certificate_errors)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_certificate_pem != NULL, FALSE);
	g_return_val_if_fail (out_certificate_errors != NULL, FALSE);

	LOCK (cnc);

	if (!cnc->priv->ssl_info_set) {
		UNLOCK (cnc);
		return FALSE;
	}

	*out_certificate_pem = g_strdup (cnc->priv->ssl_certificate_pem);
	*out_certificate_errors = cnc->priv->ssl_certificate_errors;

	UNLOCK (cnc);

	return TRUE;
}

ESourceAuthenticationResult
e_m365_connection_authenticate_sync (EM365Connection *cnc,
				     const gchar *user_override,
				     EM365FolderKind kind,
				     const gchar *group_id,
				     const gchar *folder_id,
				     gchar **out_certificate_pem,
				     GTlsCertificateFlags *out_certificate_errors,
				     GCancellable *cancellable,
				     GError **error)
{
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ERROR;
	JsonObject *object = NULL;
	gboolean success = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), result);

	switch (kind) {
	default:
		g_warn_if_reached ();
		/* Falls through */
	case E_M365_FOLDER_KIND_UNKNOWN:
	case E_M365_FOLDER_KIND_MAIL:
		if (!folder_id || !*folder_id)
			folder_id = "inbox";

		success = e_m365_connection_get_mail_folder_sync (cnc, user_override, folder_id, "displayName", &object, cancellable, &local_error);
		break;
	case E_M365_FOLDER_KIND_CONTACTS:
		if (!folder_id || !*folder_id)
			folder_id = "contacts";

		success = e_m365_connection_get_contacts_folder_sync (cnc, user_override, folder_id, "displayName", &object, cancellable, &local_error);
		break;
	case E_M365_FOLDER_KIND_CALENDAR:
		if (folder_id && !*folder_id)
			folder_id = NULL;

		success = e_m365_connection_get_calendar_folder_sync (cnc, user_override, group_id, folder_id, "name", &object, cancellable, error);
		break;
	}

	if (success) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	} else {
		if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_CANCELLED)) {
			local_error->domain = G_IO_ERROR;
			local_error->code = G_IO_ERROR_CANCELLED;
		} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
			result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;

			if (out_certificate_pem || out_certificate_errors)
				e_m365_connection_get_ssl_error_details (cnc, out_certificate_pem, out_certificate_errors);
		} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
			ESoupAuthBearer *bearer;

			bearer = e_m365_connection_ref_bearer_auth (cnc);

			if (bearer) {
				LOCK (cnc);

				if (cnc->priv->impersonate_user) {
					g_propagate_error (error, local_error);
					local_error = NULL;
				} else {
					result = E_SOURCE_AUTHENTICATION_REJECTED;
				}

				UNLOCK (cnc);
			} else {
				result = E_SOURCE_AUTHENTICATION_REQUIRED;
			}

			g_clear_object (&bearer);
			g_clear_error (&local_error);
		}

		if (local_error) {
			g_propagate_error (error, local_error);
			local_error = NULL;
		}
	}

	if (object)
		json_object_unref (object);

	g_clear_error (&local_error);

	return result;
}

gboolean
e_m365_connection_disconnect_sync (EM365Connection *cnc,
				   GCancellable *cancellable,
				   GError **error)
{
	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	LOCK (cnc);

	soup_session_abort (cnc->priv->soup_session);

	UNLOCK (cnc);

	return TRUE;
}

/* Expects NULL-terminated pair of parameters 'name', 'value'; if 'value' is NULL, the parameter is skipped.
   An empty 'name' can add the 'value' into the path. These can be only before query parameters. */
gchar *
e_m365_connection_construct_uri (EM365Connection *cnc,
				 gboolean include_user,
				 const gchar *user_override,
				 EM365ApiVersion api_version,
				 const gchar *api_part,
				 const gchar *resource,
				 const gchar *id,
				 const gchar *path,
				 ...)
{
	va_list args;
	const gchar *name, *value;
	gboolean first_param = TRUE;
	GString *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);

	if (!api_part)
		api_part = "users";

	uri = g_string_sized_new (128);

	/* https://graph.microsoft.com/v1.0/users/XUSERX/mailFolders */

	g_string_append (uri, "https://graph.microsoft.com");

	switch (api_version) {
	case E_M365_API_V1_0:
		g_string_append_c (uri, '/');
		g_string_append (uri, "v1.0");
		break;
	case E_M365_API_BETA:
		g_string_append_c (uri, '/');
		g_string_append (uri, "beta");
		break;
	}

	if (*api_part) {
		g_string_append_c (uri, '/');
		g_string_append (uri, api_part);
	}

	if (include_user) {
		const gchar *use_user;

		LOCK (cnc);

		if (user_override)
			use_user = user_override;
		else if (cnc->priv->impersonate_user)
			use_user = cnc->priv->impersonate_user;
		else
			use_user = cnc->priv->user;

		if (use_user) {
			gchar *encoded;

			encoded = soup_uri_encode (use_user, NULL);

			g_string_append_c (uri, '/');
			g_string_append (uri, encoded);

			g_free (encoded);
		}

		UNLOCK (cnc);
	}

	if (resource && *resource) {
		g_string_append_c (uri, '/');
		g_string_append (uri, resource);
	}

	if (id && *id) {
		g_string_append_c (uri, '/');
		g_string_append (uri, id);
	}

	if (path && *path) {
		g_string_append_c (uri, '/');
		g_string_append (uri, path);
	}

	va_start (args, path);

	name = va_arg (args, const gchar *);

	while (name) {
		value = va_arg (args, const gchar *);

		if (*name && value) {
			g_string_append_c (uri, first_param ? '?' : '&');

			first_param = FALSE;

			g_string_append (uri, name);
			g_string_append_c (uri, '=');

			if (*value) {
				gchar *encoded;

				encoded = soup_uri_encode (value, NULL);

				g_string_append (uri, encoded);

				g_free (encoded);
			}
		} else if (!*name && value && *value) {
			/* Warn when adding path after additional query parameters */
			g_warn_if_fail (first_param);

			g_string_append_c (uri, '/');
			g_string_append (uri, value);
		}

		name = va_arg (args, const gchar *);
	}

	va_end (args);

	return g_string_free (uri, FALSE);
}

static void
e_m365_connection_set_json_body (SoupMessage *message,
				 JsonBuilder *builder)
{
	JsonGenerator *generator;
	JsonNode *node;
	gchar *data;
	gsize data_length = 0;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (builder != NULL);

	node = json_builder_get_root (builder);

	generator = json_generator_new ();
	json_generator_set_root (generator, node);

	data = json_generator_to_data (generator, &data_length);

	soup_message_headers_set_content_type (message->request_headers, "application/json", NULL);

	if (data)
		soup_message_body_append_take (message->request_body, (guchar *) data, data_length);

	g_object_unref (generator);
	json_node_unref (node);
}

static void
e_m365_fill_message_headers_cb (JsonObject *object,
				const gchar *member_name,
				JsonNode *member_node,
				gpointer user_data)
{
	SoupMessage *message = user_data;

	g_return_if_fail (message != NULL);
	g_return_if_fail (member_name != NULL);
	g_return_if_fail (member_node != NULL);

	if (JSON_NODE_HOLDS_VALUE (member_node)) {
		const gchar *value;

		value = json_node_get_string (member_node);

		if (value)
			soup_message_headers_replace (message->response_headers, member_name, value);
	}
}

static void
e_m365_connection_fill_batch_response (SoupMessage *message,
				       JsonObject *object)
{
	JsonObject *subobject;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (object != NULL);

	message->status_code = e_m365_json_get_int_member (object, "status", SOUP_STATUS_MALFORMED);

	subobject = e_m365_json_get_object_member (object, "headers");

	if (subobject)
		json_object_foreach_member (subobject, e_m365_fill_message_headers_cb, message);

	subobject = e_m365_json_get_object_member (object, "body");

	if (subobject)
		g_object_set_data_full (G_OBJECT (message), X_EVO_M365_DATA, json_object_ref (subobject), (GDestroyNotify) json_object_unref);
}

static gboolean
e_m365_read_batch_response_cb (EM365Connection *cnc,
			       SoupMessage *message,
			       GInputStream *input_stream,
			       JsonNode *node,
			       gpointer user_data,
			       gchar **out_next_link,
			       GCancellable *cancellable,
			       GError **error)
{
	GPtrArray *requests = user_data;
	JsonObject *object;
	JsonArray *responses;
	guint ii, len;

	g_return_val_if_fail (requests != NULL, FALSE);
	g_return_val_if_fail (out_next_link != NULL, FALSE);
	g_return_val_if_fail (JSON_NODE_HOLDS_OBJECT (node), FALSE);

	object = json_node_get_object (node);
	g_return_val_if_fail (object != NULL, FALSE);

	*out_next_link = g_strdup (e_m365_json_get_string_member (object, "@odata.nextLink", NULL));

	responses = e_m365_json_get_array_member (object, "responses");
	g_return_val_if_fail (responses != NULL, FALSE);

	len = json_array_get_length (responses);

	for (ii = 0; ii < len; ii++) {
		JsonNode *elem = json_array_get_element (responses, ii);

		g_warn_if_fail (JSON_NODE_HOLDS_OBJECT (elem));

		if (JSON_NODE_HOLDS_OBJECT (elem)) {
			JsonObject *elem_object = json_node_get_object (elem);

			if (elem_object) {
				const gchar *id_str;

				id_str = e_m365_json_get_string_member (elem_object, "id", NULL);

				if (id_str) {
					guint id;

					id = (guint) g_ascii_strtoull (id_str, NULL, 10);

					if (id < requests->len)
						e_m365_connection_fill_batch_response (g_ptr_array_index (requests, id), elem_object);
				}
			}
		}
	}

	return TRUE;
}

/* https://docs.microsoft.com/en-us/graph/json-batching */

static gboolean
e_m365_connection_batch_request_internal_sync (EM365Connection *cnc,
					       EM365ApiVersion api_version,
					       GPtrArray *requests, /* SoupMessage * */
					       GCancellable *cancellable,
					       GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success = TRUE;
	gchar *uri, buff[128];
	guint ii;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (requests != NULL, FALSE);
	g_return_val_if_fail (requests->len > 0, FALSE);
	g_return_val_if_fail (requests->len <= E_M365_BATCH_MAX_REQUESTS, FALSE);

	uri = e_m365_connection_construct_uri (cnc, FALSE, NULL, api_version, "",
		"$batch", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_begin_array_member (builder, "requests");

	for (ii = 0; success && ii < requests->len; ii++) {
		SoupMessageHeadersIter iter;
		SoupMessage *submessage;
		SoupURI *suri;
		gboolean has_headers = FALSE;
		const gchar *hdr_name, *hdr_value, *use_uri;
		gboolean is_application_json = FALSE;

		submessage = g_ptr_array_index (requests, ii);

		if (!submessage)
			continue;

		submessage->status_code = SOUP_STATUS_IO_ERROR;

		suri = soup_message_get_uri (submessage);
		uri = suri ? soup_uri_to_string (suri, TRUE) : NULL;

		if (!uri) {
			submessage->status_code = SOUP_STATUS_MALFORMED;
			continue;
		}

		use_uri = uri;

		/* The 'url' is without the API part, it is derived from the main request */
		if (g_str_has_prefix (use_uri, "/v1.0/") ||
		    g_str_has_prefix (use_uri, "/beta/"))
			use_uri += 5;

		g_snprintf (buff, sizeof (buff), "%d", ii);

		e_m365_json_begin_object_member (builder, NULL);

		e_m365_json_add_string_member (builder, "id", buff);
		e_m365_json_add_string_member (builder, "method", submessage->method);
		e_m365_json_add_string_member (builder, "url", use_uri);

		g_free (uri);

		soup_message_headers_iter_init (&iter, submessage->request_headers);

		while (soup_message_headers_iter_next (&iter, &hdr_name, &hdr_value)) {
			if (hdr_name && *hdr_name && hdr_value &&
			    !camel_strcase_equal (hdr_name, "Connection") &&
			    !camel_strcase_equal (hdr_name, "User-Agent")) {
				if (camel_strcase_equal (hdr_name, "Content-Type") &&
				    camel_strcase_equal (hdr_value, "application/json"))
					is_application_json = TRUE;

				if (!has_headers) {
					has_headers = TRUE;

					e_m365_json_begin_object_member (builder, "headers");
				}

				e_m365_json_add_string_member (builder, hdr_name, hdr_value);
			}
		}

		if (has_headers)
			e_m365_json_end_object_member (builder); /* headers */

		if (submessage->request_body) {
			SoupBuffer *sbuffer;

			sbuffer = soup_message_body_flatten (submessage->request_body);

			if (sbuffer && sbuffer->length > 0) {
				if (is_application_json) {
					/* The server needs it unpacked, not as a plain string */
					JsonParser *parser;
					JsonNode *node;

					parser = json_parser_new_immutable ();

					success = json_parser_load_from_data (parser, sbuffer->data, sbuffer->length, error);

					if (!success)
						g_prefix_error (error, "%s", _("Failed to parse own Json data"));

					node = success ? json_parser_steal_root (parser) : NULL;

					if (node) {
						json_builder_set_member_name (builder, "body");
						json_builder_add_value (builder, node);
					}

					g_clear_object (&parser);
				} else {
					e_m365_json_add_string_member (builder, "body", sbuffer->data);
				}
			}

			if (sbuffer)
				soup_buffer_free (sbuffer);
		}

		e_m365_json_end_object_member (builder); /* unnamed object */
	}

	e_m365_json_end_array_member (builder);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	soup_message_headers_append (message->request_headers, "Accept", "application/json");

	g_object_unref (builder);

	success = success && m365_connection_send_request_sync (cnc, message, e_m365_read_batch_response_cb, NULL, requests, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* The 'requests' contains a SoupMessage * objects, from which are read
   the request data and on success the SoupMessage's 'response' properties
   are filled accordingly.
 */
gboolean
e_m365_connection_batch_request_sync (EM365Connection *cnc,
				      EM365ApiVersion api_version,
				      GPtrArray *requests, /* SoupMessage * */
				      GCancellable *cancellable,
				      GError **error)
{
	GPtrArray *use_requests;
	gint need_retry_seconds = 5;
	gboolean success, need_retry = TRUE;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (requests != NULL, FALSE);
	g_return_val_if_fail (requests->len > 0, FALSE);
	g_return_val_if_fail (requests->len <= E_M365_BATCH_MAX_REQUESTS, FALSE);

	use_requests = requests;

	while (need_retry) {
		need_retry = FALSE;

		success = e_m365_connection_batch_request_internal_sync (cnc, api_version, use_requests, cancellable, error);

		if (success) {
			GPtrArray *new_requests = NULL;
			gint delay_seconds = 0;
			gint ii;

			for (ii = 0; ii < use_requests->len; ii++) {
				SoupMessage *message = g_ptr_array_index (use_requests, ii);

				if (!message)
					continue;

				/* Throttling - https://docs.microsoft.com/en-us/graph/throttling  */
				if (message->status_code == 429 ||
				    /* https://docs.microsoft.com/en-us/graph/best-practices-concept#handling-expected-errors */
				    message->status_code == SOUP_STATUS_SERVICE_UNAVAILABLE) {
					const gchar *retry_after_str;
					gint64 retry_after;

					need_retry = TRUE;

					if (!new_requests)
						new_requests = g_ptr_array_sized_new (use_requests->len);

					g_ptr_array_add (new_requests, message);

					retry_after_str = message->response_headers ? soup_message_headers_get_one (message->response_headers, "Retry-After") : NULL;

					if (retry_after_str && *retry_after_str)
						retry_after = g_ascii_strtoll (retry_after_str, NULL, 10);
					else
						retry_after = 0;

					if (retry_after > 0)
						delay_seconds = MAX (delay_seconds, retry_after);
					else
						delay_seconds = MAX (delay_seconds, need_retry_seconds);
				}
			}

			if (new_requests) {
				if (delay_seconds)
					need_retry_seconds = delay_seconds;
				else if (need_retry_seconds < 120)
					need_retry_seconds *= 2;

				LOCK (cnc);

				if (cnc->priv->backoff_for_usec < need_retry_seconds * G_USEC_PER_SEC)
					cnc->priv->backoff_for_usec = need_retry_seconds * G_USEC_PER_SEC;

				UNLOCK (cnc);

				if (use_requests != requests)
					g_ptr_array_free (use_requests, TRUE);

				use_requests = new_requests;
			}
		}
	}

	if (use_requests != requests)
		g_ptr_array_free (use_requests, TRUE);

	return success;
}

/* This can be used as a EM365ConnectionJsonFunc function, it only
   copies items of 'results' into 'user_data', which is supposed
   to be a pointer to a GSList *. */
gboolean
e_m365_connection_call_gather_into_slist (EM365Connection *cnc,
					  const GSList *results, /* JsonObject * - the returned objects from the server */
					  gpointer user_data, /* expects GSList **, aka pointer to a GSList *, where it copies the 'results' */
					  GCancellable *cancellable,
					  GError **error)
{
	GSList **out_results = user_data, *link;

	g_return_val_if_fail (out_results != NULL, FALSE);

	for (link = (GSList *) results; link; link = g_slist_next (link)) {
		JsonObject *obj = link->data;

		if (obj)
			*out_results = g_slist_prepend (*out_results, json_object_ref (obj));
	}

	return TRUE;
}

/* https://docs.microsoft.com/en-us/graph/api/outlookuser-list-mastercategories?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_categories_sync (EM365Connection *cnc,
				       const gchar *user_override,
				       GSList **out_categories, /* EM365Category * */
				       GCancellable *cancellable,
				       GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_categories != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"outlook",
		"masterCategories",
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_categories;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-list-mailfolders?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_mail_folders_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  const gchar *from_path, /* path for the folder to read, NULL for top user folder */
					  const gchar *select, /* properties to select, nullable */
					  GSList **out_folders, /* EM365MailFolder * - the returned mailFolder objects */
					  GCancellable *cancellable,
					  GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_folders != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		NULL,
		from_path,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_folders;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_get_folders_delta_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  EM365FolderKind kind,
					  const gchar *select, /* properties to select, nullable */
					  const gchar *delta_link, /* previous delta link */
					  guint max_page_size, /* 0 for default by the server */
					  EM365ConnectionJsonFunc func, /* function to call with each result set */
					  gpointer func_user_data, /* user data passed into the 'func' */
					  gchar **out_delta_link,
					  GCancellable *cancellable,
					  GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_delta_link != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (delta_link)
		message = m365_connection_new_soup_message (SOUP_METHOD_GET, delta_link, CSM_DEFAULT, NULL);

	if (!message) {
		const gchar *kind_str = NULL;
		gchar *uri;

		switch (kind) {
		case E_M365_FOLDER_KIND_CONTACTS:
			kind_str = "contactFolders";
			break;
		case E_M365_FOLDER_KIND_MAIL:
			kind_str = "mailFolders";
			break;
		default:
			g_warn_if_reached ();
			break;
		}

		g_return_val_if_fail (kind_str != NULL, FALSE);

		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			kind_str,
			NULL,
			"delta",
			"$select", select,
			NULL);

		message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

		if (!message) {
			g_free (uri);

			return FALSE;
		}

		g_free (uri);
	}

	if (max_page_size > 0) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("odata.maxpagesize=%u", max_page_size);

		soup_message_headers_append (message->request_headers, "Prefer", prefer_value);

		g_free (prefer_value);
	}

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.json_func = func;
	rd.func_user_data = func_user_data;
	rd.out_delta_link = out_delta_link;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_mail_folder_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *folder_id, /* nullable - then the 'inbox' is used */
					const gchar *select, /* nullable - properties to select */
					EM365MailFolder **out_folder,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_folder != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		folder_id ? folder_id : "inbox",
		NULL,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-mailfolders?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/mailfolder-post-childfolders?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_mail_folder_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *parent_folder_id, /* NULL for the folder root */
					   const gchar *display_name,
					   EM365MailFolder **out_mail_folder,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);
	g_return_val_if_fail (out_mail_folder != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		parent_folder_id,
		parent_folder_id ? "childFolders" : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "displayName", display_name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_mail_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_mail_folder_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *folder_id,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders", folder_id, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-copy?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/mailfolder-move?view=graph-rest-1.0&tabs=http
 */
gboolean
e_m365_connection_copy_move_mail_folder_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *src_folder_id,
					      const gchar *des_folder_id,
					      gboolean do_copy,
					      EM365MailFolder **out_mail_folder,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (src_folder_id != NULL, FALSE);
	g_return_val_if_fail (des_folder_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		src_folder_id,
		do_copy ? "copy" : "move",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "destinationId", des_folder_id);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_mail_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/mailfolder-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_rename_mail_folder_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *folder_id,
					   const gchar *display_name,
					   EM365MailFolder **out_mail_folder,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (display_name != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"mailFolders",
		folder_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "displayName", display_name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_mail_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-delta?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/contact-delta?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_objects_delta_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  EM365FolderKind kind,
					  const gchar *folder_id, /* folder ID to get delta messages in */
					  const gchar *select, /* properties to select, nullable */
					  const gchar *delta_link, /* previous delta link */
					  guint max_page_size, /* 0 for default by the server */
					  EM365ConnectionJsonFunc func, /* function to call with each result set */
					  gpointer func_user_data, /* user data passed into the 'func' */
					  gchar **out_delta_link,
					  GCancellable *cancellable,
					  GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (out_delta_link != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (delta_link)
		message = m365_connection_new_soup_message (SOUP_METHOD_GET, delta_link, CSM_DEFAULT, NULL);

	if (!message) {
		const gchar *kind_str = NULL, *kind_path_str = NULL;
		gchar *uri;

		switch (kind) {
		case E_M365_FOLDER_KIND_CONTACTS:
			kind_str = "contactFolders";
			kind_path_str = "contacts";
			break;
		case E_M365_FOLDER_KIND_MAIL:
			kind_str = "mailFolders";
			kind_path_str = "messages";
			break;
		default:
			g_warn_if_reached ();
			break;
		}

		g_return_val_if_fail (kind_str != NULL && kind_path_str != NULL, FALSE);

		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			kind_str,
			folder_id,
			kind_path_str,
			"", "delta",
			"$select", select,
			NULL);

		message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

		if (!message) {
			g_free (uri);

			return FALSE;
		}

		g_free (uri);
	}

	if (max_page_size > 0) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("odata.maxpagesize=%u", max_page_size);

		soup_message_headers_append (message->request_headers, "Prefer", prefer_value);

		g_free (prefer_value);
	}

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.json_func = func;
	rd.func_user_data = func_user_data;
	rd.out_delta_link = out_delta_link;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_mail_message_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 const gchar *folder_id,
					 const gchar *message_id,
					 EM365ConnectionRawDataFunc func,
					 gpointer func_user_data,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (message_id != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		"$value",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, func, func_user_data, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-messages?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_mail_message_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *folder_id, /* if NULL, then goes to the Drafts folder */
					    JsonBuilder *mail_message, /* filled mailMessage object */
					    EM365MailMessage **out_created_message, /* free with json_object_unref() */
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (mail_message != NULL, FALSE);
	g_return_val_if_fail (out_created_message != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "mailFolders" : "messages",
		folder_id,
		folder_id ? "messages" : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, mail_message);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_message, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-post-attachments?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_add_mail_message_attachment_sync (EM365Connection *cnc,
						    const gchar *user_override, /* for which user, NULL to use the account user */
						    const gchar *message_id, /* the message to add it to */
						    JsonBuilder *attachment, /* filled attachment object */
						    gchar **out_attachment_id,
						    GCancellable *cancellable,
						    GError **error)
{
	SoupMessage *message;
	JsonObject *added_attachment = NULL;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (attachment != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		"attachments",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, attachment);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &added_attachment, cancellable, error);

	if (success && added_attachment && out_attachment_id)
		*out_attachment_id = g_strdup (e_m365_attachment_get_id (added_attachment));

	if (added_attachment)
		json_object_unref (added_attachment);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-update?view=graph-rest-1.0&tabs=http */

SoupMessage *
e_m365_connection_prepare_update_mail_message (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *message_id,
					       JsonBuilder *mail_message, /* values to update, as a mailMessage object */
					       GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (message_id != NULL, NULL);
	g_return_val_if_fail (mail_message != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		NULL,
		NULL);

	/* The server returns the mailMessage object back, but it can be ignored here */
	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return NULL;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, mail_message);

	return message;
}

gboolean
e_m365_connection_update_mail_message_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *message_id,
					    JsonBuilder *mail_message, /* values to update, as a mailMessage object */
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_id != NULL, FALSE);
	g_return_val_if_fail (mail_message != NULL, FALSE);

	message = e_m365_connection_prepare_update_mail_message (cnc, user_override, message_id, mail_message, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-copy?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/message-move?view=graph-rest-1.0&tabs=http
 */
static SoupMessage *
e_m365_connection_prepare_copy_move_mail_message (EM365Connection *cnc,
						  const gchar *user_override,
						  const gchar *message_id,
						  const gchar *des_folder_id,
						  gboolean do_copy,
						  GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (message_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		do_copy ? "copy" : "move",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "destinationId", des_folder_id);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	return message;
}

/* out_des_message_ids: Camel-pooled gchar *, new ids, in the same order as in message_ids; can be partial */
gboolean
e_m365_connection_copy_move_mail_messages_sync (EM365Connection *cnc,
						const gchar *user_override, /* for which user, NULL to use the account user */
						const GSList *message_ids, /* const gchar * */
						const gchar *des_folder_id,
						gboolean do_copy,
						GSList **out_des_message_ids, /* Camel-pooled gchar * */
						GCancellable *cancellable,
						GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_ids != NULL, FALSE);
	g_return_val_if_fail (des_folder_id != NULL, FALSE);
	g_return_val_if_fail (out_des_message_ids != NULL, FALSE);

	*out_des_message_ids = NULL;

	if (g_slist_next (message_ids)) {
		GPtrArray *requests;
		GSList *link;
		guint total, done = 0;

		total = g_slist_length ((GSList *) message_ids);
		requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

		for (link = (GSList *) message_ids; link && success; link = g_slist_next (link)) {
			const gchar *id = link->data;
			SoupMessage *message;

			message = e_m365_connection_prepare_copy_move_mail_message (cnc, user_override, id, des_folder_id, do_copy, error);

			if (!message) {
				success = FALSE;
				break;
			}

			g_ptr_array_add (requests, message);

			if (requests->len == E_M365_BATCH_MAX_REQUESTS || !link->next) {
				if (requests->len == 1) {
					JsonObject *response = NULL;

					success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &response, cancellable, error);

					if (response) {
						*out_des_message_ids = g_slist_prepend (*out_des_message_ids,
							(gpointer) camel_pstring_strdup (e_m365_mail_message_get_id (response)));
						json_object_unref (response);
					} else {
						success = FALSE;
					}
				} else {
					success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);

					if (success) {
						guint ii;

						for (ii = 0; success && ii < requests->len; ii++) {
							JsonNode *node = NULL;

							message = g_ptr_array_index (requests, ii);

							success = e_m365_connection_json_node_from_message (message, NULL, &node, cancellable, error);

							if (success && node && JSON_NODE_HOLDS_OBJECT (node)) {
								JsonObject *response;

								response = json_node_get_object (node);

								if (response) {
									*out_des_message_ids = g_slist_prepend (*out_des_message_ids,
										(gpointer) camel_pstring_strdup (e_m365_mail_message_get_id (response)));
								} else {
									success = FALSE;
								}
							} else {
								success = FALSE;
							}

							if (node)
								json_node_unref (node);
						}
					}
				}

				g_ptr_array_remove_range (requests, 0, requests->len);

				done += requests->len;

				camel_operation_progress (cancellable, done * 100.0 / total);
			}
		}

		g_ptr_array_free (requests, TRUE);
	} else {
		SoupMessage *message;

		message = e_m365_connection_prepare_copy_move_mail_message (cnc, user_override, message_ids->data, des_folder_id, do_copy, error);

		if (message) {
			JsonObject *response = NULL;

			success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &response, cancellable, error);

			if (response) {
				*out_des_message_ids = g_slist_prepend (*out_des_message_ids,
					(gpointer) camel_pstring_strdup (e_m365_mail_message_get_id (response)));
				json_object_unref (response);
			} else {
				success = FALSE;
			}

			g_clear_object (&message);
		} else {
			success = FALSE;
		}
	}

	*out_des_message_ids = g_slist_reverse (*out_des_message_ids);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-delete?view=graph-rest-1.0&tabs=http */

static SoupMessage *
e_m365_connection_prepare_delete_mail_message (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *message_id,
					       GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (message_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return NULL;
	}

	g_free (uri);

	return message;
}

gboolean
e_m365_connection_delete_mail_messages_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const GSList *message_ids, /* const gchar * */
					     GSList **out_deleted_ids, /* (transfer container): const gchar *, borrowed from message_ids */
					     GCancellable *cancellable,
					     GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_ids != NULL, FALSE);

	if (g_slist_next (message_ids)) {
		GPtrArray *requests;
		GSList *link, *from_link = (GSList *) message_ids;
		guint total, done = 0;

		total = g_slist_length ((GSList *) message_ids);
		requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

		for (link = (GSList *) message_ids; link && success; link = g_slist_next (link)) {
			const gchar *id = link->data;
			SoupMessage *message;

			message = e_m365_connection_prepare_delete_mail_message (cnc, user_override, id, error);

			if (!message) {
				success = FALSE;
				break;
			}

			g_ptr_array_add (requests, message);

			if (requests->len == E_M365_BATCH_MAX_REQUESTS || !link->next) {
				if (requests->len == 1) {
					success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);
				} else {
					success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);
				}

				if (success && out_deleted_ids) {
					while (from_link) {
						*out_deleted_ids = g_slist_prepend (*out_deleted_ids, from_link->data);

						if (from_link == link)
							break;

						from_link = g_slist_next (from_link);
					}
				}

				g_ptr_array_remove_range (requests, 0, requests->len);
				from_link = g_slist_next (link);

				done += requests->len;

				camel_operation_progress (cancellable, done * 100.0 / total);
			}
		}

		g_ptr_array_free (requests, TRUE);
	} else {
		SoupMessage *message;

		message = e_m365_connection_prepare_delete_mail_message (cnc, user_override, message_ids->data, error);

		if (message) {
			success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

			if (success && out_deleted_ids)
				*out_deleted_ids = g_slist_prepend (*out_deleted_ids, message_ids->data);

			g_clear_object (&message);
		} else {
			success = FALSE;
		}
	}

	if (out_deleted_ids && *out_deleted_ids && g_slist_next (*out_deleted_ids))
		*out_deleted_ids = g_slist_reverse (*out_deleted_ids);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/message-send?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_send_mail_message_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *message_id,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (message_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"messages",
		message_id,
		"send",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	soup_message_headers_append (message->request_headers, "Content-Length", "0");

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-sendmail?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_send_mail_sync (EM365Connection *cnc,
				  const gchar *user_override, /* for which user, NULL to use the account user */
				  JsonBuilder *request, /* filled sendMail object */
				  GCancellable *cancellable,
				  GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (request != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"sendMail", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, request);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contactfolder-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_contacts_folder_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *folder_id, /* nullable - then the default 'contacts' folder is returned */
					    const gchar *select, /* nullable - properties to select */
					    EM365Folder **out_folder,
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_folder != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"contactFolders",
		folder_id ? folder_id : "contacts",
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_folder, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/profilephoto-get?view=graph-rest-1.0 */

gboolean
e_m365_connection_get_contact_photo_sync (EM365Connection *cnc,
					  const gchar *user_override, /* for which user, NULL to use the account user */
					  const gchar *folder_id,
					  const gchar *contact_id,
					  GByteArray **out_photo,
					  GCancellable *cancellable,
					  GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);
	g_return_val_if_fail (out_photo != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"contactFolders",
		folder_id,
		"contacts",
		"", contact_id,
		"", "photo",
		"", "$value",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_to_byte_array_cb, out_photo, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/profilephoto-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_contact_photo_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *folder_id,
					     const gchar *contact_id,
					     const GByteArray *jpeg_photo, /* nullable, to remove the photo */
					     GCancellable *cancellable,
					     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"contactFolders",
		folder_id,
		"contacts",
		"", contact_id,
		"", "photo",
		"", "$value",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_PUT, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	soup_message_headers_set_content_type (message->request_headers, "image/jpeg", NULL);
	soup_message_headers_set_content_length (message->request_headers, jpeg_photo ? jpeg_photo->len : 0);

	if (jpeg_photo)
		soup_message_body_append (message->request_body, SOUP_MEMORY_STATIC, jpeg_photo->data, jpeg_photo->len);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contact-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_contact_sync (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *folder_id,
				    const gchar *contact_id,
				    EM365Contact **out_contact,
				    GCancellable *cancellable,
				    GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (folder_id != NULL, FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"contactFolders",
		folder_id,
		"contacts",
		"", contact_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_contact, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-contacts?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_contact_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *folder_id, /* if NULL, then goes to the Drafts folder */
				       JsonBuilder *contact, /* filled contact object */
				       EM365Contact **out_created_contact, /* free with json_object_unref() */
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);
	g_return_val_if_fail (out_created_contact != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "contactFolders" : "contacts",
		folder_id,
		folder_id ? "contacts" : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, contact);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_contact, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contact-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_contact_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *folder_id,
				       const gchar *contact_id,
				       JsonBuilder *contact, /* values to update, as a contact object */
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);
	g_return_val_if_fail (contact != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "contactFolders" : "contacts",
		folder_id,
		folder_id ? "contacts" : contact_id,
		"", folder_id ? contact_id : NULL,
		NULL);

	/* The server returns the contact object back, but it can be ignored here */
	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, contact);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/contact-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_contact_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *folder_id,
				       const gchar *contact_id,
				       GCancellable *cancellable,
				       GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (contact_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		folder_id ? "contactFolders" : "contacts",
		folder_id,
		folder_id ? "contacts" : contact_id,
		"", folder_id ? contact_id : NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-list-calendargroups?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_calendar_groups_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     GSList **out_groups, /* EM365CalendarGroup * - the returned calendarGroup objects */
					     GCancellable *cancellable,
					     GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_groups != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_groups;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-calendargroups?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_calendar_group_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *name,
					      EM365CalendarGroup **out_created_group,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (out_created_group != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups", NULL, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "name", name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_group, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_calendar_group_sync (EM365Connection *cnc,
					   const gchar *user_override, /* for which user, NULL to use the account user */
					   const gchar *group_id,
					   EM365CalendarGroup **out_group,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);
	g_return_val_if_fail (out_group != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups",
		group_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_group, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_calendar_group_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *group_id,
					      const gchar *name,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups",
		group_id,
		NULL,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_string_member (builder, "name", name);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_calendar_group_sync (EM365Connection *cnc,
					      const gchar *user_override, /* for which user, NULL to use the account user */
					      const gchar *group_id,
					      GCancellable *cancellable,
					      GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		"calendarGroups", group_id, NULL, NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/resources/calendar?view=graph-rest-1.0 */

gboolean
e_m365_connection_list_calendars_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *group_id, /* nullable, calendar group id for group calendars */
				       const gchar *select, /* properties to select, nullable */
				       GSList **out_calendars, /* EM365Calendar * - the returned calendar objects */
				       GCancellable *cancellable,
				       GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_calendars != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_calendars;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendargroup-post-calendars?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_calendar_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *group_id, /* nullable, then the default group is used */
					JsonBuilder *calendar,
					EM365Calendar **out_created_calendar,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar != NULL, FALSE);
	g_return_val_if_fail (out_created_calendar != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendarGroup",
		group_id,
		"calendars",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, calendar);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_calendar, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendar-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_calendar_folder_sync (EM365Connection *cnc,
					    const gchar *user_override, /* for which user, NULL to use the account user */
					    const gchar *group_id, /* nullable - then the default group is used */
					    const gchar *calendar_id, /* nullable - then the default calendar is used */
					    const gchar *select, /* nullable - properties to select */
					    EM365Calendar **out_calendar,
					    GCancellable *cancellable,
					    GError **error)
{
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (out_calendar != NULL, FALSE);

	if (group_id && calendar_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendarGroups",
			group_id,
			"calendars",
			"", calendar_id,
			"$select", select,
			NULL);
	} else if (group_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, "groups",
			group_id,
			"calendar",
			NULL,
			"$select", select,
			NULL);
	} else if (calendar_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendars",
			calendar_id,
			NULL,
			"$select", select,
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendar",
			NULL,
			NULL,
			"$select", select,
			NULL);
	}

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_calendar, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendar-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_calendar_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *group_id, /* nullable - then the default group is used */
					const gchar *calendar_id,
					const gchar *name, /* nullable - to keep the existing name */
					EM365CalendarColorType color,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	JsonBuilder *builder;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);

	/* Nothing to change */
	if (!name && (color == E_M365_CALENDAR_COLOR_NOT_SET || color == E_M365_CALENDAR_COLOR_UNKNOWN))
		return TRUE;

	if (group_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendarGroups",
			group_id,
			"calendars",
			"", calendar_id,
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendars",
			calendar_id,
			NULL,
			NULL);
	}

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_calendar_add_name (builder, name);
	e_m365_calendar_add_color (builder, color);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/calendar-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_calendar_sync (EM365Connection *cnc,
					const gchar *user_override, /* for which user, NULL to use the account user */
					const gchar *group_id, /* nullable - then the default group is used */
					const gchar *calendar_id,
					GCancellable *cancellable,
					GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);

	if (group_id) {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendarGroups",
			group_id,
			"calendars",
			"", calendar_id,
			NULL);
	} else {
		uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
			"calendars",
			calendar_id,
			NULL,
			NULL);
	}

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-list-events?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_events_sync (EM365Connection *cnc,
				    const gchar *user_override, /* for which user, NULL to use the account user */
				    const gchar *group_id, /* nullable, calendar group id for group calendars */
				    const gchar *calendar_id,
				    const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				    const gchar *select, /* nullable - properties to select */
				    GSList **out_events, /* EM365Event * - the returned event objects */
				    GCancellable *cancellable,
				    GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (out_events != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	if (prefer_outlook_timezone && *prefer_outlook_timezone) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("outlook.timezone=\"%s\"", prefer_outlook_timezone);

		soup_message_headers_append (message->request_headers, "Prefer", prefer_value);

		g_free (prefer_value);
	}

	soup_message_headers_append (message->request_headers, "Prefer", "outlook.body-content-type=\"text\"");

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_events;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/user-post-events?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/group-post-events?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_create_event_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable, then the default group is used */
				     const gchar *calendar_id,
				     JsonBuilder *event,
				     EM365Calendar **out_created_event,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (out_created_event != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, event);

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_created_event, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-get?view=graph-rest-1.0&tabs=http */

SoupMessage *
e_m365_connection_prepare_get_event (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable, then the default group is used */
				     const gchar *calendar_id,
				     const gchar *event_id,
				     const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				     const gchar *select, /* nullable - properties to select */
				     GError **error)
{
	SoupMessage *message;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), NULL);
	g_return_val_if_fail (calendar_id != NULL, NULL);
	g_return_val_if_fail (event_id != NULL, NULL);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return NULL;
	}

	g_free (uri);

	if (prefer_outlook_timezone && *prefer_outlook_timezone) {
		gchar *prefer_value;

		prefer_value = g_strdup_printf ("outlook.timezone=\"%s\"", prefer_outlook_timezone);

		soup_message_headers_append (message->request_headers, "Prefer", prefer_value);

		g_free (prefer_value);
	}

	soup_message_headers_append (message->request_headers, "Prefer", "outlook.body-content-type=\"text\"");

	return message;
}

gboolean
e_m365_connection_get_event_sync (EM365Connection *cnc,
				  const gchar *user_override, /* for which user, NULL to use the account user */
				  const gchar *group_id, /* nullable, then the default group is used */
				  const gchar *calendar_id,
				  const gchar *event_id,
				  const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				  const gchar *select, /* nullable - properties to select */
				  EM365Event **out_event,
				  GCancellable *cancellable,
				  GError **error)
{
	SoupMessage *message;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (out_event != NULL, FALSE);

	message = e_m365_connection_prepare_get_event (cnc, user_override, group_id, calendar_id, event_id, prefer_outlook_timezone, select, error);

	if (!message)
		return FALSE;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, out_event, cancellable, error);

	g_clear_object (&message);

	return success;
}

gboolean
e_m365_connection_get_events_sync (EM365Connection *cnc,
				   const gchar *user_override, /* for which user, NULL to use the account user */
				   const gchar *group_id, /* nullable, then the default group is used */
				   const gchar *calendar_id,
				   const GSList *event_ids, /* const gchar * */
				   const gchar *prefer_outlook_timezone, /* nullable - then UTC, otherwise that zone for the returned times */
				   const gchar *select, /* nullable - properties to select */
				   GSList **out_events, /* EM365Event *, in the same order as event_ids; can return partial list */
				   GCancellable *cancellable,
				   GError **error)
{
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_ids != NULL, FALSE);
	g_return_val_if_fail (out_events != NULL, FALSE);

	if (g_slist_next (event_ids)) {
		GPtrArray *requests;
		GSList *link;
		guint total, done = 0;

		total = g_slist_length ((GSList *) event_ids);
		requests = g_ptr_array_new_full (MIN (E_M365_BATCH_MAX_REQUESTS, MIN (total, 50)), g_object_unref);

		for (link = (GSList *) event_ids; link && success; link = g_slist_next (link)) {
			const gchar *id = link->data;
			SoupMessage *message;

			message = e_m365_connection_prepare_get_event (cnc, user_override, group_id, calendar_id, id, prefer_outlook_timezone, select, error);

			if (!message) {
				success = FALSE;
				break;
			}

			g_ptr_array_add (requests, message);

			if (requests->len == E_M365_BATCH_MAX_REQUESTS || !link->next) {
				if (requests->len == 1) {
					EM365Event *event = NULL;

					success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &event, cancellable, error);

					if (success)
						*out_events = g_slist_prepend (*out_events, event);
				} else {
					success = e_m365_connection_batch_request_sync (cnc, E_M365_API_V1_0, requests, cancellable, error);

					if (success) {
						guint ii;

						for (ii = 0; ii < requests->len && success; ii++) {
							JsonNode *node = NULL;

							message = requests->pdata[ii];
							success = e_m365_connection_json_node_from_message (message, NULL, &node, cancellable, error);

							if (success && node && JSON_NODE_HOLDS_OBJECT (node)) {
								JsonObject *response;

								response = json_node_get_object (node);

								if (response) {
									*out_events = g_slist_prepend (*out_events, json_object_ref (response));
								} else {
									success = FALSE;
								}
							} else {
								success = FALSE;
							}

							if (node)
								json_node_unref (node);
						}
					}
				}

				g_ptr_array_remove_range (requests, 0, requests->len);

				done += requests->len;

				camel_operation_progress (cancellable, done * 100.0 / total);
			}
		}

		g_ptr_array_free (requests, TRUE);
	} else {
		SoupMessage *message;

		message = e_m365_connection_prepare_get_event (cnc, user_override, group_id, calendar_id, event_ids->data, prefer_outlook_timezone, select, error);

		if (message) {
			EM365Event *event = NULL;

			success = m365_connection_send_request_sync (cnc, message, e_m365_read_json_object_response_cb, NULL, &event, cancellable, error);

			if (success)
				*out_events = g_slist_prepend (*out_events, event);

			g_clear_object (&message);
		} else {
			success = FALSE;
		}
	}

	*out_events = g_slist_reverse (*out_events);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-update?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_update_event_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable - then the default group is used */
				     const gchar *calendar_id,
				     const gchar *event_id,
				     JsonBuilder *event,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		NULL);

	message = m365_connection_new_soup_message ("PATCH", uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, event);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_event_sync (EM365Connection *cnc,
				     const gchar *user_override, /* for which user, NULL to use the account user */
				     const gchar *group_id, /* nullable - then the default group is used */
				     const gchar *calendar_id,
				     const gchar *event_id,
				     GCancellable *cancellable,
				     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-accept?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/event-tentativelyaccept?view=graph-rest-1.0&tabs=http
   https://docs.microsoft.com/en-us/graph/api/event-decline?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_response_event_sync (EM365Connection *cnc,
				       const gchar *user_override, /* for which user, NULL to use the account user */
				       const gchar *group_id, /* nullable - then the default group is used */
				       const gchar *calendar_id,
				       const gchar *event_id,
				       EM365ResponseType response, /* uses only accepted/tentatively accepted/declined values */
				       const gchar *comment, /* nullable */
				       gboolean send_response,
				       GCancellable *cancellable,
				       GError **error)
{
	JsonBuilder *builder;
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (response == E_M365_RESPONSE_ACCEPTED || response == E_M365_RESPONSE_TENTATIVELY_ACCEPTED || response == E_M365_RESPONSE_DECLINED, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", response == E_M365_RESPONSE_TENTATIVELY_ACCEPTED ? "tentativelyAccept" :
		    response == E_M365_RESPONSE_DECLINED ? "decline" : "accept",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	builder = json_builder_new_immutable ();

	e_m365_json_begin_object_member (builder, NULL);
	e_m365_json_add_nonempty_string_member (builder, "comment", comment);
	e_m365_json_add_boolean_member (builder, "sendResponse", send_response);
	e_m365_json_end_object_member (builder);

	e_m365_connection_set_json_body (message, builder);

	g_object_unref (builder);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-dismissreminder?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_dismiss_reminder_sync (EM365Connection *cnc,
					 const gchar *user_override, /* for which user, NULL to use the account user */
					 const gchar *group_id, /* nullable - then the default group is used */
					 const gchar *calendar_id,
					 const gchar *event_id,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "dismissReminder",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-list-attachments?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_list_event_attachments_sync (EM365Connection *cnc,
					       const gchar *user_override, /* for which user, NULL to use the account user */
					       const gchar *group_id, /* nullable, then the default group is used */
					       const gchar *calendar_id,
					       const gchar *event_id,
					       const gchar *select, /* nullable - properties to select */
					       GSList **out_attachments, /* EM365Attachment * */
					       GCancellable *cancellable,
					       GError **error)
{
	EM365ResponseData rd;
	SoupMessage *message;
	gchar *uri;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (out_attachments != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "attachments",
		"$select", select,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	memset (&rd, 0, sizeof (EM365ResponseData));

	rd.out_items = out_attachments;

	success = m365_connection_send_request_sync (cnc, message, e_m365_read_valued_response_cb, NULL, &rd, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/attachment-get?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_get_event_attachment_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *group_id, /* nullable, then the default group is used */
					     const gchar *calendar_id,
					     const gchar *event_id,
					     const gchar *attachment_id,
					     EM365ConnectionRawDataFunc func,
					     gpointer func_user_data,
					     GCancellable *cancellable,
					     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (attachment_id != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "attachments",
		"", attachment_id,
		"", "$value",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_GET, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, func, func_user_data, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/event-post-attachments?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_add_event_attachment_sync (EM365Connection *cnc,
					     const gchar *user_override, /* for which user, NULL to use the account user */
					     const gchar *group_id, /* nullable, then the default group is used */
					     const gchar *calendar_id,
					     const gchar *event_id,
					     JsonBuilder *in_attachment,
					     EM365Attachment **out_attachment, /* nullable */
					     GCancellable *cancellable,
					     GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (in_attachment != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "attachments",
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_POST, uri, out_attachment ? CSM_DEFAULT : CSM_DISABLE_RESPONSE, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	e_m365_connection_set_json_body (message, in_attachment);

	success = m365_connection_send_request_sync (cnc, message, out_attachment ? e_m365_read_json_object_response_cb : NULL,
		out_attachment ? NULL : e_m365_read_no_response_cb, out_attachment, cancellable, error);

	g_clear_object (&message);

	return success;
}

/* https://docs.microsoft.com/en-us/graph/api/attachment-delete?view=graph-rest-1.0&tabs=http */

gboolean
e_m365_connection_delete_event_attachment_sync (EM365Connection *cnc,
						const gchar *user_override, /* for which user, NULL to use the account user */
						const gchar *group_id, /* nullable, then the default group is used */
						const gchar *calendar_id,
						const gchar *event_id,
						const gchar *attachment_id,
						GCancellable *cancellable,
						GError **error)
{
	SoupMessage *message;
	gboolean success;
	gchar *uri;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (calendar_id != NULL, FALSE);
	g_return_val_if_fail (event_id != NULL, FALSE);
	g_return_val_if_fail (attachment_id != NULL, FALSE);

	uri = e_m365_connection_construct_uri (cnc, TRUE, user_override, E_M365_API_V1_0, NULL,
		group_id ? "calendarGroups" : "calendars",
		group_id,
		group_id ? "calendars" : NULL,
		"", calendar_id,
		"", "events",
		"", event_id,
		"", "attachments",
		"", attachment_id,
		NULL);

	message = m365_connection_new_soup_message (SOUP_METHOD_DELETE, uri, CSM_DEFAULT, error);

	if (!message) {
		g_free (uri);

		return FALSE;
	}

	g_free (uri);

	success = m365_connection_send_request_sync (cnc, message, NULL, e_m365_read_no_response_cb, NULL, cancellable, error);

	g_clear_object (&message);

	return success;
}
