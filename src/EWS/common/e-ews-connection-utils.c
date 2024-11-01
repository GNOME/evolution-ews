/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "e-ews-connection-utils.h"
#include "camel-ews-settings.h"

void
e_ews_connection_utils_set_user_agent_header (SoupMessage *message,
					      CamelEwsSettings *settings)
{
	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (CAMEL_IS_EWS_SETTINGS (settings));

	if (camel_ews_settings_get_override_user_agent (settings)) {
		gchar *user_agent;

		user_agent = camel_ews_settings_dup_user_agent (settings);

		if (user_agent && *user_agent) {
			soup_message_headers_replace (soup_message_get_request_headers (message), "User-Agent", user_agent);
		}

		g_free (user_agent);
	} else {
		soup_message_headers_replace (soup_message_get_request_headers (message), "User-Agent", "Evolution/" VERSION);
	}
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
	SoupMessageHeaders *response_headers;
	gboolean any_found = FALSE;
	const gchar *header;

	if (!message)
		return FALSE;

	response_headers = soup_message_get_response_headers (message);

	if (!response_headers)
		return FALSE;

	header = soup_message_headers_get_list (response_headers, "X-MS-Credential-Service-CredExpired");
	if (header && g_ascii_strcasecmp (header, "true") == 0) {
		any_found = TRUE;

		if (out_expired)
			*out_expired = TRUE;
	}

	header = soup_message_headers_get_list (response_headers, "X-MS-Credentials-Expire");
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
		header = soup_message_headers_get_list (response_headers, "X-MS-Credential-Service-Url");

		*out_service_url = g_strdup (header);
	}

	return any_found;
}
