/*
 * e-ews-connection-utils.c
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

#include "evolution-ews-config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "e-ews-connection-utils.h"
#include "e-soup-auth-negotiate.h"
#include "camel-ews-settings.h"

static gpointer
ews_unref_in_thread_func (gpointer data)
{
	g_object_unref (G_OBJECT (data));

	return NULL;
}

void
e_ews_connection_utils_unref_in_thread (gpointer object)
{
	GThread *thread;

	g_return_if_fail (G_IS_OBJECT (object));

	thread = g_thread_new (NULL, ews_unref_in_thread_func, object);
	g_thread_unref (thread);
}

/* Do not call this directly; use E_EWS_CONNECTION_UTILS_CHECK_ELEMENT macro instead. */
gboolean
e_ews_connection_utils_check_element (const gchar *function_name,
				      const gchar *element_name,
				      const gchar *expected_name)
{
	g_return_val_if_fail (function_name != NULL, FALSE);
	g_return_val_if_fail (element_name != NULL, FALSE);
	g_return_val_if_fail (expected_name != NULL, FALSE);

	if (!g_str_equal (element_name, expected_name)) {
		g_warning (
			"%s: Expected <%s> but got <%s>",
			function_name, expected_name, element_name);
		return FALSE;
	}

	return TRUE;
}

static gboolean force_off_ntlm_auth_check = FALSE;

static gboolean
ews_connect_check_ntlm_available (void)
{
#ifndef G_OS_WIN32
	const gchar *helper;
	CamelStream *stream;
	const gchar *cp;
	const gchar *user;
	gchar buf[1024];
	gsize s;
	gchar *command;
	gint ret;

	if (force_off_ntlm_auth_check)
		return FALSE;

	/* We are attempting to predict what libsoup will do. */
	helper = g_getenv ("SOUP_NTLM_AUTH_DEBUG");
	if (!helper)
		helper = "/usr/bin/ntlm_auth";
	else if (!helper[0])
		return FALSE;

	if (g_access (helper, X_OK))
		return FALSE;

	user = g_getenv ("NTLMUSER");
	if (!user)
		user = g_get_user_name();

	cp = strpbrk (user, "\\/");
	if (cp != NULL) {
		command = g_strdup_printf (
			"%s --helper-protocol ntlmssp-client-1 "
			"--use-cached-creds --username '%s' "
			"--domain '%.*s'", helper,
			cp + 1, (gint)(cp - user), user);
	} else {
		command = g_strdup_printf (
			"%s --helper-protocol ntlmssp-client-1 "
			"--use-cached-creds --username '%s'",
			helper, user);
	}

	stream = camel_stream_process_new ();

	ret = camel_stream_process_connect (CAMEL_STREAM_PROCESS (stream),
					    command, NULL, NULL);

	g_free (command);

	if (ret) {
		g_object_unref (stream);
		return FALSE;
	}

	if (camel_stream_write_string (stream, "YR\n", NULL, NULL) < 0) {
		g_object_unref (stream);
		return FALSE;
	}

	s = camel_stream_read (stream, buf, sizeof (buf), NULL, NULL);
	if (s < 4) {
		g_object_unref (stream);
		return FALSE;
	}

	if (buf[0] != 'Y' || buf[1] != 'R' || buf[2] != ' ' || buf[s - 1] != '\n') {
		g_object_unref (stream);
		return FALSE;
	}

	g_object_unref (stream);

	return TRUE;
#else
	/* Win32 should be able to use SSPI here. */
	return FALSE;
#endif
}

void
e_ews_connection_utils_force_off_ntlm_auth_check (void)
{
	force_off_ntlm_auth_check = TRUE;
}

/* Should we bother to attempt a connection without a password? Remember,
 * this is *purely* an optimisation to avoid that extra round-trip if we
 * *KNOW* it's going to fail. So if unsure, return TRUE to avoid pestering
 * the user for a password which might not even get used.
 *
 * We *have* to handle the case where the passwordless attempt  fails
 * and we have to fall back to asking for a password anyway. */
gboolean
e_ews_connection_utils_get_without_password (CamelEwsSettings *ews_settings)
{
	switch (camel_ews_settings_get_auth_mechanism (ews_settings)) {
	case EWS_AUTH_TYPE_GSSAPI:
	case EWS_AUTH_TYPE_OAUTH2:
		return TRUE;

	case EWS_AUTH_TYPE_NTLM:
		return ews_connect_check_ntlm_available ();

	case EWS_AUTH_TYPE_BASIC:
		return FALSE;

	/* No default: case (which should never be used anyway). That
	 * means the compiler will warn if we ever add a new mechanism
	 * to the enum and don't handle it here. */
	}

	return FALSE;
}

void
e_ews_connection_utils_expired_password_to_error (const gchar *service_url,
						  GError **error)
{
	if (!error)
		return;

	if (service_url) {
		g_set_error (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_PASSWORDEXPIRED,
			_("Password expired. Change password at “%s”."), service_url);
	} else {
		g_set_error_literal (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_PASSWORDEXPIRED,
			_("Password expired."));
	}
}

gboolean
e_ews_connection_utils_check_x_ms_credential_headers (SoupMessage *message,
						      gint *out_expire_in_days,
						      gboolean *out_expired,
						      gchar **out_service_url)
{
	gboolean any_found = FALSE;
	const gchar *header;

	if (!message || !message->response_headers)
		return FALSE;

	header = soup_message_headers_get_list (message->response_headers, "X-MS-Credential-Service-CredExpired");
	if (header && g_ascii_strcasecmp (header, "true") == 0) {
		any_found = TRUE;

		if (out_expired)
			*out_expired = TRUE;
	}

	header = soup_message_headers_get_list (message->response_headers, "X-MS-Credentials-Expire");
	if (header) {
		gint in_days;

		in_days = g_ascii_strtoll (header, NULL, 10);
		if (in_days <= 30 && in_days >= 0) {
			any_found = TRUE;

			if (out_expire_in_days)
				*out_expire_in_days = in_days;
		}
	}

	if (any_found && out_service_url) {
		header = soup_message_headers_get_list (message->response_headers, "X-MS-Credential-Service-Url");

		*out_service_url = g_strdup (header);
	}

	return any_found;
}

void
e_ews_connection_utils_prepare_auth_method (SoupSession *soup_session,
					    EwsAuthType auth_method)
{
	/* We used to disable Basic auth to avoid it getting in the way of
	 * our GSSAPI hacks. But leave it enabled in the case where NTLM is
	 * enabled, which is the default configuration. It's a useful fallback
	 * which people may be relying on. */
	if (auth_method == EWS_AUTH_TYPE_GSSAPI) {
		soup_session_add_feature_by_type (soup_session, E_SOUP_TYPE_AUTH_NEGOTIATE);
		soup_session_remove_feature_by_type (soup_session, SOUP_TYPE_AUTH_BASIC);
	} else if (auth_method == EWS_AUTH_TYPE_OAUTH2) {
		soup_session_add_feature_by_type (soup_session, E_TYPE_SOUP_AUTH_BEARER);
		soup_session_remove_feature_by_type (soup_session, SOUP_TYPE_AUTH_BASIC);
	} else if (auth_method == EWS_AUTH_TYPE_NTLM) {
		soup_session_add_feature_by_type (soup_session, SOUP_TYPE_AUTH_NTLM);
	}
}

static void
ews_connection_utils_ensure_bearer_auth_usage (SoupSession *session,
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

	soup_auth_manager_use_auth (
		SOUP_AUTH_MANAGER (feature),
		soup_uri, SOUP_AUTH (bearer));

	soup_uri_free (soup_uri);
}

static gboolean
ews_connection_utils_setup_bearer_auth (EEwsConnection *cnc,
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

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (E_IS_SOUP_AUTH_BEARER (bearer), FALSE);

	source = e_ews_connection_get_source (cnc);

	success = e_source_get_oauth2_access_token_sync (source, cancellable,
		&access_token, &expires_in_seconds, error);

	if (success) {
		e_soup_auth_bearer_set_access_token (bearer, access_token, expires_in_seconds);

		if (!is_in_authenticate_handler) {
			SoupSession *session;

			session = e_ews_connection_ref_soup_session (cnc);

			ews_connection_utils_ensure_bearer_auth_usage (session, message, bearer);

			g_clear_object (&session);
		}
	}

	g_free (access_token);

	return success;
}

static gboolean
ews_connection_utils_maybe_prepare_bearer_auth (EEwsConnection *cnc,
						SoupMessage *message,
						GCancellable *cancellable)
{
	ESource *source;
	ESoupAuthBearer *using_bearer_auth;
	gchar *auth_method = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);

	source = e_ews_connection_get_source (cnc);
	if (!source)
		return TRUE;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *extension;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_method = e_source_authentication_dup_method (extension);
	} else {
		CamelEwsSettings *ews_settings;

		ews_settings = e_ews_connection_ref_settings (cnc);
		if (ews_settings) {
			if (camel_ews_settings_get_auth_mechanism (ews_settings) == EWS_AUTH_TYPE_OAUTH2)
				auth_method = g_strdup ("OAuth2");

			g_object_unref (ews_settings);
		}

		if (!auth_method)
			return TRUE;
	}

	if (g_strcmp0 (auth_method, "OAuth2") != 0 &&
	    g_strcmp0 (auth_method, "Office365") != 0 &&
	    !e_oauth2_services_is_oauth2_alias_static (auth_method)) {
		g_free (auth_method);
		return TRUE;
	}

	g_free (auth_method);

	using_bearer_auth = e_ews_connection_ref_bearer_auth (cnc);
	if (using_bearer_auth) {
		success = ews_connection_utils_setup_bearer_auth (cnc, message, FALSE, using_bearer_auth, cancellable, &local_error);
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

		success = ews_connection_utils_setup_bearer_auth (cnc, message, FALSE, E_SOUP_AUTH_BEARER (soup_auth), cancellable, &local_error);
		if (success)
			e_ews_connection_set_bearer_auth (cnc, E_SOUP_AUTH_BEARER (soup_auth));

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

/* Callback implementation for SoupSession::authenticate */
void
e_ews_connection_utils_authenticate (EEwsConnection *cnc,
				     SoupSession *session,
				     SoupMessage *msg,
				     SoupAuth *auth,
				     gboolean retrying)
{
	CamelNetworkSettings *network_settings;
	ESoupAuthBearer *using_bearer_auth;
	gchar *user, *password, *service_url = NULL;
	gboolean expired = FALSE;

	g_return_if_fail (cnc != NULL);

	using_bearer_auth = e_ews_connection_ref_bearer_auth (cnc);

	if (E_IS_SOUP_AUTH_BEARER (auth)) {
		g_object_ref (auth);
		g_warn_if_fail ((gpointer) using_bearer_auth == (gpointer) auth);

		g_clear_object (&using_bearer_auth);
		using_bearer_auth = E_SOUP_AUTH_BEARER (auth);

		e_ews_connection_set_bearer_auth (cnc, using_bearer_auth);
	}

	if (retrying)
		e_ews_connection_set_password (cnc, NULL);

	if (using_bearer_auth) {
		GError *local_error = NULL;

		ews_connection_utils_setup_bearer_auth (cnc, msg, TRUE, E_SOUP_AUTH_BEARER (auth), NULL, &local_error);

		if (local_error)
			soup_message_set_status_full (msg, SOUP_STATUS_IO_ERROR, local_error->message);

		g_object_unref (using_bearer_auth);
		g_clear_error (&local_error);

		return;
	}

	if (e_ews_connection_utils_check_x_ms_credential_headers (msg, NULL, &expired, &service_url) && expired) {
		GError *local_error = NULL;

		e_ews_connection_utils_expired_password_to_error (service_url, &local_error);

		if (local_error)
			soup_message_set_status_full (msg, SOUP_STATUS_IO_ERROR, local_error->message);

		g_clear_error (&local_error);
		g_free (service_url);

		return;
	}

	g_free (service_url);

	network_settings = CAMEL_NETWORK_SETTINGS (e_ews_connection_ref_settings (cnc));
	user = camel_network_settings_dup_user (network_settings);

	password = e_ews_connection_dup_password (cnc);
	if (password != NULL) {
		soup_auth_authenticate (auth, user, password);
	} else {
		/* The NTLM implementation in libsoup doesn't cope very well
		 * with recovering from authentication failures (bug 703181).
		 * So cancel the message now while it's in-flight, and we'll
		 * get a shiny new connection for the next attempt. */
		const gchar *scheme = soup_auth_get_scheme_name (auth);

		if (!g_ascii_strcasecmp(scheme, "NTLM")) {
			soup_session_cancel_message (session, msg, SOUP_STATUS_UNAUTHORIZED);
		}
	}

	g_clear_object (&network_settings);
	g_free (password);
	g_free (user);
}

/* Returns whether succeeded */
gboolean
e_ews_connection_utils_prepare_message (EEwsConnection *cnc,
					SoupMessage *message,
					GCancellable *cancellable)
{
	ESoupAuthBearer *using_bearer_auth;
	GError *local_error = NULL;

	if (!ews_connection_utils_maybe_prepare_bearer_auth (cnc, message, cancellable))
		return FALSE;

	using_bearer_auth = e_ews_connection_ref_bearer_auth (cnc);

	if (using_bearer_auth &&
	    e_soup_auth_bearer_is_expired (using_bearer_auth) &&
	    !ews_connection_utils_setup_bearer_auth (cnc, message, FALSE, using_bearer_auth, cancellable, &local_error)) {
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
