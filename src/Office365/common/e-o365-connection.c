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

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-o365-settings.h"

#include "e-o365-connection.h"

#define LOCK(x) g_rec_mutex_lock (&(x->priv->property_lock))
#define UNLOCK(x) g_rec_mutex_unlock (&(x->priv->property_lock))

struct _EO365ConnectionPrivate {
	GRecMutex property_lock;

	ESource *source;
	CamelO365Settings *settings;
	SoupSession *soup_session;
	GProxyResolver *proxy_resolver;
	ESoupAuthBearer *bearer_auth;

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
	PROP_CONCURRENT_CONNECTIONS
};

G_DEFINE_TYPE_WITH_PRIVATE (EO365Connection, e_o365_connection, G_TYPE_OBJECT)

static GHashTable *opened_connections = NULL;
G_LOCK_DEFINE_STATIC (opened_connections);

static SoupSession *
o365_connection_ref_soup_session (EO365Connection *cnc)
{
	SoupSession *soup_session = NULL;

	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), NULL);

	LOCK (cnc);

	if (cnc->priv->soup_session)
		soup_session = g_object_ref (cnc->priv->soup_session);

	UNLOCK (cnc);

	return soup_session;
}

static void
o365_connection_utils_ensure_bearer_auth_usage (SoupSession *session,
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
o365_connection_utils_setup_bearer_auth (EO365Connection *cnc,
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

	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (E_IS_SOUP_AUTH_BEARER (bearer), FALSE);

	source = e_o365_connection_get_source (cnc);

	success = e_source_get_oauth2_access_token_sync (source, cancellable,
		&access_token, &expires_in_seconds, error);

	if (success) {
		e_soup_auth_bearer_set_access_token (bearer, access_token, expires_in_seconds);

		if (!is_in_authenticate_handler) {
			if (session)
				g_object_ref (session);
			else
				session = o365_connection_ref_soup_session (cnc);

			o365_connection_utils_ensure_bearer_auth_usage (session, message, bearer);

			g_clear_object (&session);
		}
	}

	g_free (access_token);

	return success;
}

static gboolean
o365_connection_utils_prepare_bearer_auth (EO365Connection *cnc,
					   SoupSession *session,
					   SoupMessage *message,
					   GCancellable *cancellable)
{
	ESource *source;
	ESoupAuthBearer *using_bearer_auth;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), FALSE);

	source = e_o365_connection_get_source (cnc);
	if (!source)
		return TRUE;

	using_bearer_auth = e_o365_connection_ref_bearer_auth (cnc);
	if (using_bearer_auth) {
		success = o365_connection_utils_setup_bearer_auth (cnc, session, message, FALSE, using_bearer_auth, cancellable, &local_error);
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

		success = o365_connection_utils_setup_bearer_auth (cnc, session, message, FALSE, E_SOUP_AUTH_BEARER (soup_auth), cancellable, &local_error);
		if (success)
			e_o365_connection_set_bearer_auth (cnc, E_SOUP_AUTH_BEARER (soup_auth));

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
o365_connection_authenticate (SoupSession *session,
			      SoupMessage *msg,
			      SoupAuth *auth,
			      gboolean retrying,
			      gpointer user_data)
{
	EO365Connection *cnc = user_data;
	ESoupAuthBearer *using_bearer_auth;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_O365_CONNECTION (cnc));

	using_bearer_auth = e_o365_connection_ref_bearer_auth (cnc);

	if (E_IS_SOUP_AUTH_BEARER (auth)) {
		g_object_ref (auth);
		g_warn_if_fail ((gpointer) using_bearer_auth == (gpointer) auth);

		g_clear_object (&using_bearer_auth);
		using_bearer_auth = E_SOUP_AUTH_BEARER (auth);

		e_o365_connection_set_bearer_auth (cnc, using_bearer_auth);
	}

	if (!using_bearer_auth) {
		g_warn_if_reached ();
		return;
	}

	o365_connection_utils_setup_bearer_auth (cnc, session, msg, TRUE, E_SOUP_AUTH_BEARER (auth), NULL, &local_error);

	if (local_error)
		soup_message_set_status_full (msg, SOUP_STATUS_IO_ERROR, local_error->message);

	g_object_unref (using_bearer_auth);
	g_clear_error (&local_error);
}

static gboolean
o365_connection_utils_prepare_message (EO365Connection *cnc,
				       SoupSession *session,
				       SoupMessage *message,
				       GCancellable *cancellable)
{
	ESoupAuthBearer *using_bearer_auth;
	ESource *source;
	GError *local_error = NULL;

	source = e_o365_connection_get_source (cnc);
	if (source)
		e_soup_ssl_trust_connect (message, source);

	if (!o365_connection_utils_prepare_bearer_auth (cnc, session, message, cancellable))
		return FALSE;

	using_bearer_auth = e_o365_connection_ref_bearer_auth (cnc);

	if (using_bearer_auth &&
	    e_soup_auth_bearer_is_expired (using_bearer_auth) &&
	    !o365_connection_utils_setup_bearer_auth (cnc, session, message, FALSE, using_bearer_auth, cancellable, &local_error)) {
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
o365_connection_set_settings (EO365Connection *cnc,
			      CamelO365Settings *settings)
{
	g_return_if_fail (E_IS_O365_CONNECTION (cnc));
	g_return_if_fail (CAMEL_IS_O365_SETTINGS (settings));
	g_return_if_fail (cnc->priv->settings == NULL);

	cnc->priv->settings = g_object_ref (settings);
}

static void
o365_connection_set_source (EO365Connection *cnc,
			    ESource *source)
{
	g_return_if_fail (E_IS_O365_CONNECTION (cnc));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (cnc->priv->source == NULL);

	cnc->priv->source = g_object_ref (source);
}

static void
o365_connection_set_property (GObject *object,
			      guint property_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PROXY_RESOLVER:
			e_o365_connection_set_proxy_resolver (
				E_O365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SETTINGS:
			o365_connection_set_settings (
				E_O365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_SOURCE:
			o365_connection_set_source (
				E_O365_CONNECTION (object),
				g_value_get_object (value));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			e_o365_connection_set_concurrent_connections (
				E_O365_CONNECTION (object),
				g_value_get_uint (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
o365_connection_get_property (GObject *object,
			      guint property_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PROXY_RESOLVER:
			g_value_take_object (
				value,
				e_o365_connection_ref_proxy_resolver (
				E_O365_CONNECTION (object)));
			return;

		case PROP_SETTINGS:
			g_value_set_object (
				value,
				e_o365_connection_get_settings (
				E_O365_CONNECTION (object)));
			return;

		case PROP_SOURCE:
			g_value_set_object (
				value,
				e_o365_connection_get_source (
				E_O365_CONNECTION (object)));
			return;

		case PROP_CONCURRENT_CONNECTIONS:
			g_value_set_uint (
				value,
				e_o365_connection_get_concurrent_connections (
				E_O365_CONNECTION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
o365_connection_constructed (GObject *object)
{
	EO365Connection *cnc = E_O365_CONNECTION (object);
	static gint log_enabled = -1;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_connection_parent_class)->constructed (object);

	if (log_enabled == -1)
		log_enabled = g_strcmp0 (g_getenv ("O365_DEBUG"), "1") == 0 ? 1 : 0;

	if (log_enabled) {
		SoupLogger *logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);

		soup_session_add_feature (cnc->priv->soup_session, SOUP_SESSION_FEATURE (logger));

		g_object_unref (logger);
	}

	soup_session_add_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_COOKIE_JAR);
	soup_session_add_feature_by_type (cnc->priv->soup_session, E_TYPE_SOUP_AUTH_BEARER);
	soup_session_remove_feature_by_type (cnc->priv->soup_session, SOUP_TYPE_AUTH_BASIC);

	g_signal_connect (
		cnc->priv->soup_session, "authenticate",
		G_CALLBACK (o365_connection_authenticate), cnc);

	cnc->priv->hash_key = camel_network_settings_dup_user (CAMEL_NETWORK_SETTINGS (cnc->priv->settings));

	if (!cnc->priv->hash_key)
		cnc->priv->hash_key = g_strdup ("no-user");

	e_binding_bind_property (
		cnc->priv->settings, "timeout",
		cnc->priv->soup_session, SOUP_SESSION_TIMEOUT,
		G_BINDING_SYNC_CREATE);
}

static void
o365_connection_dispose (GObject *object)
{
	EO365Connection *cnc = E_O365_CONNECTION (object);

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
			o365_connection_authenticate, object);
	}

	g_clear_object (&cnc->priv->source);
	g_clear_object (&cnc->priv->settings);
	g_clear_object (&cnc->priv->soup_session);
	g_clear_object (&cnc->priv->proxy_resolver);
	g_clear_object (&cnc->priv->bearer_auth);

	UNLOCK (cnc);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_connection_parent_class)->dispose (object);
}

static void
o365_connection_finalize (GObject *object)
{
	EO365Connection *cnc = E_O365_CONNECTION (object);

	g_rec_mutex_clear (&cnc->priv->property_lock);
	g_free (cnc->priv->hash_key);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_o365_connection_parent_class)->finalize (object);
}

static void
e_o365_connection_class_init (EO365ConnectionClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = o365_connection_set_property;
	object_class->get_property = o365_connection_get_property;
	object_class->constructed = o365_connection_constructed;
	object_class->dispose = o365_connection_dispose;
	object_class->finalize = o365_connection_finalize;

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
			CAMEL_TYPE_O365_SETTINGS,
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
			/* Do not construct it, otherwise it overrides the value derived from CamelO365Settings */
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_o365_connection_init (EO365Connection *cnc)
{
	cnc->priv = e_o365_connection_get_instance_private (cnc);

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

EO365Connection *
e_o365_connection_new (ESource *source,
		       CamelO365Settings *settings)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (CAMEL_IS_O365_SETTINGS (settings), NULL);

	return e_o365_connection_new_full (source, settings, TRUE);
}

EO365Connection *
e_o365_connection_new_for_backend (EBackend *backend,
				   ESourceRegistry *registry,
				   ESource *source,
				   CamelO365Settings *settings)
{
	ESource *backend_source, *parent_source;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (CAMEL_IS_O365_SETTINGS (settings), NULL);

	backend_source = e_backend_get_source (backend);

	if (!backend_source)
		return e_o365_connection_new (source, settings);

	parent_source = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

	if (parent_source) {
		EO365Connection *cnc;

		cnc = e_o365_connection_new (parent_source, settings);

		g_object_unref (parent_source);

		return cnc;
	}

	return e_o365_connection_new (source, settings);
}

EO365Connection *
e_o365_connection_new_full (ESource *source,
			    CamelO365Settings *settings,
			    gboolean allow_reuse)
{
	EO365Connection *cnc;

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

	cnc = g_object_new (E_TYPE_O365_CONNECTION,
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
e_o365_connection_get_source (EO365Connection *cnc)
{
	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), NULL);

	return cnc->priv->source;
}

CamelO365Settings *
e_o365_connection_get_settings (EO365Connection *cnc)
{
	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), NULL);

	return cnc->priv->settings;
}

guint
e_o365_connection_get_concurrent_connections (EO365Connection *cnc)
{
	guint current_cc = 0;

	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), 1);

	LOCK (cnc);

	g_object_get (G_OBJECT (cnc->priv->soup_session), SOUP_SESSION_MAX_CONNS, &current_cc, NULL);

	UNLOCK (cnc);

	return current_cc;
}

void
e_o365_connection_set_concurrent_connections (EO365Connection *cnc,
					      guint concurrent_connections)
{
	guint current_cc;

	g_return_if_fail (E_IS_O365_CONNECTION (cnc));

	concurrent_connections = CLAMP (concurrent_connections, MIN_CONCURRENT_CONNECTIONS, MAX_CONCURRENT_CONNECTIONS);

	current_cc = e_o365_connection_get_concurrent_connections (cnc);

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
e_o365_connection_ref_proxy_resolver (EO365Connection *cnc)
{
	GProxyResolver *proxy_resolver = NULL;

	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), NULL);

	LOCK (cnc);

	if (cnc->priv->proxy_resolver)
		proxy_resolver = g_object_ref (cnc->priv->proxy_resolver);

	UNLOCK (cnc);

	return proxy_resolver;
}

void
e_o365_connection_set_proxy_resolver (EO365Connection *cnc,
				      GProxyResolver *proxy_resolver)
{
	gboolean notify = FALSE;

	g_return_if_fail (E_IS_O365_CONNECTION (cnc));

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
e_o365_connection_ref_bearer_auth (EO365Connection *cnc)
{
	ESoupAuthBearer *res = NULL;

	g_return_val_if_fail (E_IS_O365_CONNECTION (cnc), NULL);

	LOCK (cnc);

	if (cnc->priv->bearer_auth)
		res = g_object_ref (cnc->priv->bearer_auth);

	UNLOCK (cnc);

	return res;
}

void
e_o365_connection_set_bearer_auth (EO365Connection *cnc,
				   ESoupAuthBearer *bearer_auth)
{
	g_return_if_fail (E_IS_O365_CONNECTION (cnc));

	LOCK (cnc);

	if (cnc->priv->bearer_auth != bearer_auth) {
		g_clear_object (&cnc->priv->bearer_auth);

		cnc->priv->bearer_auth = bearer_auth;

		if (cnc->priv->bearer_auth)
			g_object_ref (cnc->priv->bearer_auth);
	}

	UNLOCK (cnc);
}