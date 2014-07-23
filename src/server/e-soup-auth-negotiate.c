/*
 * e-soup-auth-negotiate.c
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

#include <camel/camel.h>
#include <libsoup/soup.h>
#include "e-soup-auth-negotiate.h"

/*
 * The following set of functions hacks in Negotiate/GSSAPI support for EWS.
 * Ideally, libsoup should provide the support already.  Unfortunately, it
 * doesn't: https://bugzilla.gnome.org/show_bug.cgi?id=587145
 *
 * Authentication could possibly be configured as connection-based, but it
 * seems to be message-based by default.  This is fine with us, since it
 * facilitates attaching a GSSAPI context to the message, and there is no
 * need to resort to dirty tricks to discover the connection pointer.
 *
 * Connection-based auth should also work with this scheme since we only
 * attempt authentication when told to do so.  If libsoup offered a way to
 * peek at the connection, though, it would allow use of the "Persistent-Auth"
 * header to predict that case and avoid an extra round trip.
 *
 */

typedef struct {
	CamelSasl *gssapi_sasl;
	gchar *token;
	gchar *challenge;
	gint  auth_started;
	gint  challenge_available;
} SoupMessageState;

static GHashTable *msgs_table;

static gchar *
e_soup_auth_negotiate_gssapi_challenge (CamelSasl *sasl, const gchar *what,
					gboolean is_base64, GError **error)
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
			g_byte_array_append (ain, (const guchar *) what,
					     strlen (what));
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

static gboolean e_soup_auth_negotiate_update (SoupAuth *auth, SoupMessage *msg,
					      GHashTable *auth_params);
static gboolean e_soup_auth_negotiate_is_ready (SoupAuth *auth,
						SoupMessage *msg);
static void e_soup_auth_negotiate_message_finished (SoupMessage *msg,
						    gpointer user_data);

static void
e_soup_auth_negotiate_delete_context (SoupMessage *msg, gpointer user_data)
{
	SoupMessageState *state = g_hash_table_lookup (msgs_table, msg);

	g_hash_table_remove (msgs_table, msg);
	g_signal_handlers_disconnect_by_func (
		msg, G_CALLBACK (e_soup_auth_negotiate_message_finished),
		user_data);

	if (state->auth_started)
		g_object_unref (state->gssapi_sasl);
	g_free (state->token);
	g_free (state->challenge);
	g_slice_free (SoupMessageState, state);
}

static void
e_soup_auth_negotiate_message_finished (SoupMessage *msg, gpointer user_data)
{
	/*
	 * Feed the remaining GSSAPI data through SASL
	 */
	SoupAuth *auth = SOUP_AUTH (user_data);

	if (msg->status_code == 200 &&
	    e_soup_auth_negotiate_update (auth, msg, NULL))
		e_soup_auth_negotiate_is_ready (auth, msg);

	e_soup_auth_negotiate_delete_context (msg, user_data);
}

static SoupMessageState *
e_soup_auth_negotiate_get_message_state (SoupMessage *msg, SoupAuth *auth)
{
	SoupMessageState *state;

	state = g_hash_table_lookup (msgs_table, msg);
	if (!state) {
		state = g_slice_new0 (SoupMessageState);
		g_hash_table_insert (msgs_table, msg, state);
		g_signal_connect (
			msg, "finished",
			G_CALLBACK (e_soup_auth_negotiate_message_finished),
			auth);
	}

	return state;
}

G_DEFINE_TYPE (ESoupAuthNegotiate, e_soup_auth_negotiate, SOUP_TYPE_AUTH)

static void
e_soup_auth_negotiate_init (ESoupAuthNegotiate *negotiate)
{
}

static void
e_soup_auth_negotiate_finalize (GObject *object)
{
	G_OBJECT_CLASS (e_soup_auth_negotiate_parent_class)->finalize (object);
}

static gboolean
e_soup_auth_negotiate_update (SoupAuth *auth, SoupMessage *msg,
			      GHashTable *auth_params)
{
	/*
	 * Basically all we do here is update the challenge
	 */
	SoupMessageState *state;
	const gchar *auths_lst;
	gchar **auths;
	gint ii;

	/*
	 * The auth_params is supposed to have the challenge.  But the
	 * Negotiate challenge doesn't fit the standard so it must be extracted
	 * from the msg instead.
	 */
	auths_lst = soup_message_headers_get_list (msg->response_headers,
						   "WWW-Authenticate");
	if (!auths_lst)
		return FALSE;

	state = e_soup_auth_negotiate_get_message_state (msg, auth);

	auths = g_strsplit (auths_lst, ",", -1);
	for (ii = 0; auths && auths[ii]; ii++) {
		if (g_ascii_strncasecmp (auths[ii], "Negotiate", 9) == 0) {
			const gchar *chlg = auths[ii] + 9;

			if (state->challenge)
				g_free (state->challenge);
			if (*chlg)
				chlg++;
			if (!*chlg)
				chlg = NULL;
			state->challenge = g_strdup (chlg);
			state->challenge_available = TRUE;
			return TRUE;
		}
	}

	return FALSE;
}

static GSList *
e_soup_auth_negotiate_get_protection_space (SoupAuth *auth, SoupURI *source_uri)
{
	gchar *space, *p;

	space = g_strdup (source_uri->path);

	/* Strip filename component */
	p = strrchr (space, '/');
	if (p && p != space && p[1])
		*p = '\0';

	return g_slist_prepend (NULL, space);
}

static gboolean
e_soup_auth_negotiate_is_ready (SoupAuth *auth, SoupMessage *msg)
{
	SoupMessageState *state;

	/*
	 * Here we finally update the token and then inform the auth manager
	 * that it's ready.
	 */

	state = e_soup_auth_negotiate_get_message_state (msg, auth);

	if (state->challenge_available) {
		GError *error = NULL;

		if (!state->auth_started) {
			CamelSasl *gssapi_sasl;
			SoupURI *soup_uri;
			char const *host;

			gssapi_sasl = g_object_new (
				camel_sasl_gssapi_get_type (),
				"mechanism", "GSSAPI",
				"service-name", "HTTP",
				NULL);

			soup_uri = soup_message_get_uri (msg);
			host = soup_uri_get_host (soup_uri);

			/* We are required to pass a username, but it doesn't
			   ever get used since we're not actually doing SASL
			   here. So "" is fine. */
			camel_sasl_gssapi_override_host_and_user (
				CAMEL_SASL_GSSAPI (gssapi_sasl), host, "");

			state->gssapi_sasl = gssapi_sasl;

			state->auth_started = TRUE;
		}

		g_free (state->token);
		state->token = e_soup_auth_negotiate_gssapi_challenge (
				state->gssapi_sasl,
				state->challenge ? state->challenge : "\r\n",
				state->challenge != NULL,
				&error);

		g_free (state->challenge);
		state->challenge = NULL;
		state->challenge_available = FALSE;

		if (error) {
			/* cannot use SOUP_STATUS_UNAUTHORIZED, because it may hide an
			 * error message, which is a local error of Kerberos/GSSAPI
			 * call
			 */
			soup_message_set_status_full (
				msg, SOUP_STATUS_BAD_REQUEST, error->message);
			return FALSE;
		}
	}

	if (state->token != NULL)
		return TRUE;
	return FALSE;
}

static gboolean
e_soup_auth_negotiate_is_authenticated (SoupAuth *auth)
{
	return TRUE;
}

static gchar *
e_soup_auth_negotiate_get_authorization (SoupAuth *auth, SoupMessage *msg)
{
	SoupMessageState *state;
	gchar *token;

	state = e_soup_auth_negotiate_get_message_state (msg, auth);
	token = g_strdup_printf ("Negotiate %s", state->token);

	g_free (state->token);
	state->token = NULL;

	return token;
}

static void
e_soup_auth_negotiate_class_init (ESoupAuthNegotiateClass *auth_negotiate_class)
{
	SoupAuthClass *auth_class = SOUP_AUTH_CLASS (auth_negotiate_class);
	GObjectClass *object_class = G_OBJECT_CLASS (auth_negotiate_class);

	auth_class->scheme_name = "Negotiate";
	auth_class->strength = 1;

	auth_class->update = e_soup_auth_negotiate_update;
	auth_class->get_protection_space = e_soup_auth_negotiate_get_protection_space;
	auth_class->is_ready = e_soup_auth_negotiate_is_ready;
	auth_class->is_authenticated = e_soup_auth_negotiate_is_authenticated;
	auth_class->get_authorization = e_soup_auth_negotiate_get_authorization;

	object_class->finalize = e_soup_auth_negotiate_finalize;

	msgs_table = g_hash_table_new (NULL, NULL);
}
