/*
 * e-ews-debug.c
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

#include "e-ews-debug.h"
#include "e-ews-message.h"

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

static void
print_header (const gchar *name,
	      const gchar *value,
	      gpointer user_data)
{
	fprintf (user_data, "%s: %s\n", name, value);
}

static void
e_ews_debug_dump_raw_soup_message (FILE *out,
				   SoupMessageHeaders *hdrs,
				   SoupMessageBody *body)
{
	if (body && soup_message_body_get_accumulate (body)) {
		SoupBuffer *buffer;

		buffer = soup_message_body_flatten (body);
		soup_buffer_free (buffer);
	}

	/* print body */
	fprintf (out, " =====================\n");
	if (hdrs)
		soup_message_headers_foreach (hdrs, print_header, out);
	else
		fprintf (out, " null headers\n");
	fputc ('\n', out);
	if (body && body->data) {
		fputs (body->data, out);
		fputc ('\n', out);
	}
	fflush (out);
}

void
e_ews_debug_dump_raw_soup_request (SoupMessage *msg)
{
	gint log_level;

	log_level = e_ews_debug_get_log_level ();
	if (log_level == 1) {
		/* print request body */
		printf ("\n URI: %s\n", soup_uri_to_string (soup_message_get_uri (msg),
							  TRUE));
		printf (" The request headers for message %p\n", msg);
		e_ews_debug_dump_raw_soup_message (stdout, msg->request_headers,
						   msg->request_body);
	}
}

void
e_ews_debug_dump_raw_soup_response (SoupMessage *msg)
{
	gint log_level;

	log_level = e_ews_debug_get_log_level ();
	if (log_level >= 1) {
		printf ("\n The response code: %d\n", msg->status_code);
		printf (" The response headers for message %p\n", msg);
		e_ews_debug_dump_raw_soup_message (stdout, msg->response_headers,
						   msg->response_body);
	}
}
