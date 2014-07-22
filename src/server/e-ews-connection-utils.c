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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "e-ews-connection-utils.h"

#define EWS_GSSAPI_SOUP_SESSION "ews-gssapi-soup-session"
#define EWS_GSSAPI_SASL		"ews-gssapi-sasl"
#define EWS_GSSAPI_CONNECTION	"ews-gssapi-connection"

static gchar *
ews_connection_utils_gssapi_challenge (CamelSasl *sasl,
				       const gchar *what,
				       gboolean is_base64,
				       GError **error)
{
	GByteArray *ain, *aout = NULL;
	gchar *response = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (sasl != NULL, NULL);

	ain = g_byte_array_new ();

	if (what && *what) {
		if (is_base64) {
			guchar *bytes;
			gsize len = 0;

			bytes = g_base64_decode (what, &len);
			if (bytes) {
				g_byte_array_append (ain, bytes, len);
				g_free (bytes);
			}
		} else {
			g_byte_array_append (ain, (const guchar *) what, strlen (what));
		}
	}

	aout = camel_sasl_challenge_sync (sasl, ain, NULL, &local_error);

	if (local_error) {
		g_propagate_error (error, local_error);
	} else if (aout && aout->len) {
		response = g_base64_encode (aout->data, aout->len);
	} else {
		response = g_strdup ("");
	}

	g_byte_array_unref (ain);

	if (aout)
		g_byte_array_unref (aout);

	return response;
}

static void
ews_connection_utils_authenticate_gssapi_cb (SoupMessage *message,
					     gpointer user_data)
{
	EEwsConnection *connection = g_object_get_data (G_OBJECT (message), EWS_GSSAPI_CONNECTION);
	SoupSession *session = g_object_get_data (G_OBJECT (message), EWS_GSSAPI_SOUP_SESSION);
	CamelSasl *sasl = g_object_get_data (G_OBJECT (message), EWS_GSSAPI_SASL);
	const gchar *auths_lst;
	gchar **auths;
	gint ii;

	g_return_if_fail (E_IS_EWS_CONNECTION (connection));
	g_return_if_fail (SOUP_IS_SESSION (session));
	g_return_if_fail (CAMEL_IS_SASL (sasl));

	if (SOUP_STATUS_IS_SUCCESSFUL (message->status_code))
		return;

	auths_lst = soup_message_headers_get_list (message->response_headers, "WWW-Authenticate");
	if (!auths_lst)
		return;

	auths = g_strsplit (auths_lst, ",", -1);
	for (ii = 0; auths && auths[ii]; ii++) {
		if (g_ascii_strncasecmp (auths[ii], "Negotiate", 9) == 0) {
			GError *error = NULL;
			const gchar *chlg = auths[ii] + 9;
			gchar *response;

			if (*chlg)
				chlg++;
			if (!*chlg)
				chlg = NULL;

			response = ews_connection_utils_gssapi_challenge (
				sasl, chlg ? chlg : "\r\n", chlg != NULL, &error);

			if (response && *response) {
				gchar *sasl_response = g_strconcat ("Negotiate ", response, NULL);

				soup_message_headers_remove (message->request_headers, "Authorization");
				soup_message_headers_append (message->request_headers, "Authorization", sasl_response);
				soup_session_requeue_message (session, message);

				g_free (sasl_response);
			} else if (error) {
				/* cannot use SOUP_STATUS_UNAUTHORIZED, because it may hide an error message,
				   which is a local error of Kerberos/GSSAPI call */
				soup_message_set_status_full (message, SOUP_STATUS_BAD_REQUEST, error->message);
			}

			g_free (response);
			break;
		}
	}

	g_strfreev (auths);
}

void
e_ews_connection_utils_setup_msg_gssapi_auth (EEwsConnection *connection,
					      SoupSession *session,
					      SoupMessage *message)
{
	CamelSasl *gssapi_sasl;
	CamelEwsSettings *ews_settings;
	CamelNetworkSettings *network_settings;
	SoupURI *soup_uri;
	const gchar *host, *user;

	if (!camel_sasl_gssapi_is_available ())
		return;

	g_return_if_fail (E_IS_EWS_CONNECTION (connection));
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	ews_settings = e_ews_connection_ref_settings (connection);
	network_settings = CAMEL_NETWORK_SETTINGS (ews_settings);
	gssapi_sasl = g_object_new (
		camel_sasl_gssapi_get_type (),
		"mechanism", "GSSAPI",
		"service-name", "HTTP",
		NULL);

	soup_uri = soup_message_get_uri (message);
	host = soup_uri_get_host (soup_uri);
	user = soup_uri_get_user (soup_uri);
	if (!host || !*host)
		host = camel_network_settings_get_host (network_settings);
	if (!user || !*user)
		user = camel_network_settings_get_user (network_settings);

	camel_sasl_gssapi_override_host_and_user (CAMEL_SASL_GSSAPI (gssapi_sasl), host, user);

	/* this might not be a cyclic ref dependency, as long as the message
	   is properly served through the session and freed */
	g_object_set_data_full (G_OBJECT (message), EWS_GSSAPI_SOUP_SESSION,
		g_object_ref (session), g_object_unref);
	g_object_set_data_full (G_OBJECT (message), EWS_GSSAPI_CONNECTION,
		g_object_ref (connection), e_ews_connection_utils_unref_in_thread);
	g_object_set_data_full (G_OBJECT (message), EWS_GSSAPI_SASL,
		gssapi_sasl, g_object_unref);

	soup_message_add_header_handler (message, "got_body", "WWW-Authenticate",
		G_CALLBACK (ews_connection_utils_authenticate_gssapi_cb), NULL);

	g_object_unref (ews_settings);
}

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

gboolean
e_ews_connection_utils_auth_mech_to_use_ntlm (GBinding *binding,
					      const GValue *source_value,
					      GValue *target_value,
					      gpointer user_data)
{
	const gchar *auth_mechanism;
	gboolean use_ntlm;

	/* Use NTLM unless the auth mechanism is "PLAIN" or "GSSAPI". */
	auth_mechanism = g_value_get_string (source_value);
	use_ntlm = g_strcmp0 (auth_mechanism, "PLAIN") != 0 &&
		   g_strcmp0 (auth_mechanism, "GSSAPI") != 0;
	g_value_set_boolean (target_value, use_ntlm);

	return TRUE;
}

/* Do not call this directly, use E_EWS_CONNECTION_UTILS_CHECK_ELEMENT macro instead. */
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
	gboolean result = FALSE;
	gchar *auth_mech = NULL;

	g_object_get (G_OBJECT (ews_settings), "auth-mechanism", &auth_mech,
		      NULL);

	if (g_strcmp0 (auth_mech, "GSSAPI") == 0)
		result = TRUE;
	else if (g_strcmp0 (auth_mech, "PLAIN") != 0)
		result = ews_connect_check_ntlm_available ();

	g_free (auth_mech);

	return result;
}
