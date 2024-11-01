/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-ews-debug.h"
#include "e-ews-request.h"

gint
e_ews_debug_get_log_level (void)
{
	static gint level = -1;

	if (level < 0) {
		const gchar *envvar = g_getenv ("EWS_DEBUG");
		if (envvar != NULL)
			/*
			 * We don't care about error here.
			 * If something goes wrong, level is set to 0
			 */
			level = g_ascii_strtoll (envvar, NULL, 0);
		level = MAX (level, 0);
	}

	return level;
}

void
e_ews_debug_print (const gchar *format,
		   ...)
{
	if (e_ews_debug_get_log_level () != 0) {
		va_list args;

		va_start (args, format);
		e_util_debug_printv ("EWS", format, args);
		va_end (args);
	}
}

const gchar *
e_ews_connection_get_server_version_string (EEwsConnection *cnc)
{
	switch (e_ews_connection_get_server_version(cnc)) {
		case E_EWS_EXCHANGE_2007:
			return "2007";
		case E_EWS_EXCHANGE_2007_SP1:
			return "2007_SP1";
		case E_EWS_EXCHANGE_2010:
			return "2010";
		case E_EWS_EXCHANGE_2010_SP1:
			return "2010_SP1";
		case E_EWS_EXCHANGE_2010_SP2:
			return "2010_SP2";
		case E_EWS_EXCHANGE_2013:
		case E_EWS_EXCHANGE_FUTURE:
			return "2013";
		case E_EWS_EXCHANGE_UNKNOWN:
			return "Unknown";
		default:
			return NULL;
	}
}

EEwsServerVersion
e_ews_debug_get_server_version_from_string (const gchar *version)
{
	if (g_strcmp0 (version, "Exchange2007") == 0)
		return E_EWS_EXCHANGE_2007;
	else if (g_strcmp0 (version, "Exchange2007_SP1") == 0 ||
		 (version && g_str_has_prefix (version, "Exchange2007")))
		return E_EWS_EXCHANGE_2007_SP1;
	else if (g_strcmp0 (version, "Exchange2010") == 0)
		return E_EWS_EXCHANGE_2010;
	else if (g_strcmp0 (version, "Exchange2010_SP1") == 0)
		return E_EWS_EXCHANGE_2010_SP1;
	else if (g_strcmp0 (version, "Exchange2010_SP2") == 0 ||
		 (version && g_str_has_prefix (version, "Exchange2010")))
		return E_EWS_EXCHANGE_2010_SP2;
	else if (g_strcmp0 (version, "Exchange2013") == 0 ||
		 (version && g_str_has_prefix (version, "Exchange2013")))
		return E_EWS_EXCHANGE_2013;
	else
		return E_EWS_EXCHANGE_FUTURE;
}

const gchar *
e_ews_debug_redact_headers (gchar direction,
			    const gchar *data)
{
	const gchar *filtered_data = NULL;
	gint log_level = e_ews_debug_get_log_level ();

	if (log_level == 1 || log_level == 2 || log_level > 3) {
		if (direction == '>' && g_ascii_strncasecmp (data, "Host:", 5) == 0)
			filtered_data = "Host: <redacted>";
		else if (direction == '>' && g_ascii_strncasecmp (data, "Authorization:", 14) == 0)
			filtered_data = "Authorization: <redacted>";
		else if (direction == '<' && g_ascii_strncasecmp (data, "Set-Cookie:", 11) == 0)
			filtered_data = "Set-Cookie: <redacted>";
		else if (direction == '>' && g_ascii_strncasecmp (data, "Cookie:", 7) == 0)
			filtered_data = "Cookie: <redacted>";
	}

	return filtered_data ? filtered_data : data;
}

void
e_ews_debug_soup_log_printer_stdout (SoupLogger *logger,
				     SoupLoggerLogLevel level,
				     char direction,
				     const gchar *data,
				     gpointer user_data)
{
	g_print ("%c %s\n", direction, e_ews_debug_redact_headers (direction, data));
}
