/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* e2k-autoconfig: Automatic account configuration backend code */

/* Note on gtk-doc: Several functions in this file have intentionally-
 * broken gtk-doc comments (that have only a single "*" after the
 * opening "/") so that they can be overridden by versions in
 * docs/reference/tmpl/e2k-autoconfig.sgml that use better markup.
 * If you change the docs here, be sure to change them there as well.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>

#ifndef G_OS_WIN32
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>
#ifndef DNS_TYPE_SRV
#define DNS_TYPE_SRV 33
#endif
#endif

#include "e2k-autoconfig.h"
#include "e2k-context.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"
#include "xntlm.h"

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-url.h>
#include <libedataserverui/e-passwords.h>
#include <gconf/gconf-client.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>

#ifdef G_OS_WIN32
#undef CONNECTOR_PREFIX
#define CONNECTOR_PREFIX e_util_get_prefix ()
#endif

static gchar *find_olson_timezone (const gchar *windows_timezone);
static void set_account_uri_string (E2kAutoconfig *ac);

/**
 * e2k_autoconfig_new:
 * @owa_uri: the OWA URI, or %NULL to (try to) use a default
 * @username: the username (or DOMAIN\username), or %NULL to use a default
 * @password: the password, or %NULL if not yet known
 * @auth_pref: information about what auth type to use
 *
 * Creates an autoconfig context, based on information stored in the
 * config file or provided as arguments.
 *
 * Return value: an autoconfig context
 **/
E2kAutoconfig *
e2k_autoconfig_new (const gchar *owa_uri, const gchar *username,
		    const gchar *password, E2kAutoconfigAuthPref auth_pref)
{
	E2kAutoconfig *ac;

	ac = g_new0 (E2kAutoconfig, 1);

	if (e2k_autoconfig_lookup_option ("Disable-Plaintext")) {
		ac->auth_pref = E2K_AUTOCONFIG_USE_NTLM;
		ac->require_ntlm = TRUE;
	} else
		ac->auth_pref = auth_pref;

	e2k_autoconfig_set_owa_uri (ac, owa_uri);
	/* use same auth for gal as for the server */
	e2k_autoconfig_set_gc_server (ac, NULL, -1, ac->auth_pref == E2K_AUTOCONFIG_USE_BASIC ? E2K_AUTOCONFIG_USE_GAL_BASIC :
						    ac->auth_pref == E2K_AUTOCONFIG_USE_NTLM  ? E2K_AUTOCONFIG_USE_GAL_NTLM  :
						    E2K_AUTOCONFIG_USE_GAL_DEFAULT);
	e2k_autoconfig_set_username (ac, username);
	e2k_autoconfig_set_password (ac, password);

	return ac;
}

/**
 * e2k_autoconfig_free:
 * @ac: an autoconfig context
 *
 * Frees @ac.
 **/
void
e2k_autoconfig_free (E2kAutoconfig *ac)
{
	g_free (ac->owa_uri);
	g_free (ac->gc_server);
	g_free (ac->username);
	g_free (ac->password);
	g_free (ac->display_name);
	g_free (ac->email);
	g_free (ac->account_uri);
	g_free (ac->exchange_server);
	g_free (ac->timezone);
	g_free (ac->nt_domain);
	g_free (ac->w2k_domain);
	g_free (ac->home_uri);
	g_free (ac->exchange_dn);
	g_free (ac->pf_server);

	g_free (ac);
}

static void
reset_gc_derived (E2kAutoconfig *ac)
{
	if (ac->display_name) {
		g_free (ac->display_name);
		ac->display_name = NULL;
	}
	if (ac->email) {
		g_free (ac->email);
		ac->email = NULL;
	}
	if (ac->account_uri) {
		g_free (ac->account_uri);
		ac->account_uri = NULL;
	}
}

static void
reset_owa_derived (E2kAutoconfig *ac)
{
	/* Clear the information we explicitly get from OWA */
	if (ac->timezone) {
		g_free (ac->timezone);
		ac->timezone = NULL;
	}
	if (ac->exchange_dn) {
		g_free (ac->exchange_dn);
		ac->exchange_dn = NULL;
	}
	if (ac->pf_server) {
		g_free (ac->pf_server);
		ac->pf_server = NULL;
	}
	if (ac->home_uri) {
		g_free (ac->home_uri);
		ac->home_uri = NULL;
	}

	/* Reset domain info we may have implicitly got */
	ac->use_ntlm = (ac->auth_pref != E2K_AUTOCONFIG_USE_BASIC);
	if (ac->nt_domain_defaulted) {
		g_free (ac->nt_domain);
		ac->nt_domain = g_strdup (e2k_autoconfig_lookup_option ("NT-Domain"));
		ac->nt_domain_defaulted = FALSE;
	}
	if (ac->w2k_domain)
		g_free (ac->w2k_domain);
	ac->w2k_domain = g_strdup (e2k_autoconfig_lookup_option ("Domain"));

	/* Reset GC-derived information since it depends on the
	 * OWA-derived information too.
	 */
	reset_gc_derived (ac);
}

/**
 * e2k_autoconfig_set_owa_uri:
 * @ac: an autoconfig context
 * @owa_uri: the new OWA URI, or %NULL
 *
 * Sets @ac's #owa_uri field to @owa_uri (or the default if @owa_uri is
 * %NULL), and resets any fields whose values had been set based on
 * the old value of #owa_uri.
 **/
void
e2k_autoconfig_set_owa_uri (E2kAutoconfig *ac, const gchar *owa_uri)
{
	reset_owa_derived (ac);
	if (ac->gc_server_autodetected)
		e2k_autoconfig_set_gc_server (ac, NULL, -1, ac->gal_auth);
	g_free (ac->owa_uri);

	if (owa_uri) {
		if (!strncmp (owa_uri, "http", 4))
			ac->owa_uri = g_strdup (owa_uri);
		else
			ac->owa_uri = g_strdup_printf ("http://%s", owa_uri);
	} else
		ac->owa_uri = g_strdup (e2k_autoconfig_lookup_option ("OWA-URL"));
}

/**
 * e2k_autoconfig_set_gc_server:
 * @ac: an autoconfig context
 * @gc_server: the new GC server, or %NULL
 * @gal_limit: GAL search size limit, or -1 for no limit
 * @gal_auth: Preferred authentication method for gal
 *
 * Sets @ac's #gc_server field to @gc_server (or the default if
 * @gc_server is %NULL) and the #gal_limit field to @gal_limit, and
 * resets any fields whose values had been set based on the old value
 * of #gc_server.
 **/
void
e2k_autoconfig_set_gc_server (E2kAutoconfig *ac, const gchar *gc_server,
			      gint gal_limit, E2kAutoconfigGalAuthPref gal_auth)
{
	const gchar *default_gal_limit;

	reset_gc_derived (ac);
	g_free (ac->gc_server);

	if (gc_server)
		ac->gc_server = g_strdup (gc_server);
	else
		ac->gc_server = g_strdup (e2k_autoconfig_lookup_option ("Global-Catalog"));
	ac->gc_server_autodetected = FALSE;

	if (gal_limit == -1) {
		default_gal_limit = e2k_autoconfig_lookup_option ("GAL-Limit");
		if (default_gal_limit)
			gal_limit = atoi (default_gal_limit);
	}
	ac->gal_limit = gal_limit;
	ac->gal_auth = gal_auth;
}

/**
 * e2k_autoconfig_set_username:
 * @ac: an autoconfig context
 * @username: the new username (or DOMAIN\username), or %NULL
 *
 * Sets @ac's #username field to @username (or the default if
 * @username is %NULL), and resets any fields whose values had been
 * set based on the old value of #username.
 **/
void
e2k_autoconfig_set_username (E2kAutoconfig *ac, const gchar *username)
{
	gint dlen;

	reset_owa_derived (ac);
	g_free (ac->username);

	if (username) {
		/* If the username includes a domain name, split it out */
		dlen = strcspn (username, "/\\");
		if (username[dlen]) {
			g_free (ac->nt_domain);
			ac->nt_domain = g_strndup (username, dlen);
			ac->username = g_strdup (username + dlen + 1);
			ac->nt_domain_defaulted = FALSE;
		} else
			ac->username = g_strdup (username);
	} else
		ac->username = g_strdup (g_get_user_name ());
}

/**
 * e2k_autoconfig_set_password:
 * @ac: an autoconfig context
 * @password: the new password, or %NULL to clear
 *
 * Sets or clears @ac's #password field.
 **/
void
e2k_autoconfig_set_password (E2kAutoconfig *ac, const gchar *password)
{
	g_free (ac->password);
	ac->password = g_strdup (password);
}

static void
get_ctx_auth_handler (SoupMessage *msg, gpointer user_data)
{
	E2kAutoconfig *ac = user_data;
	GSList *headers;
	const gchar *challenge_hdr;

	ac->saw_ntlm = ac->saw_basic = FALSE;
	headers = e2k_http_get_headers (msg->response_headers,
					"WWW-Authenticate");
	while (headers) {
		challenge_hdr = headers->data;

		if (!strcmp (challenge_hdr, "NTLM"))
			ac->saw_ntlm = TRUE;
		else if (!strncmp (challenge_hdr, "Basic ", 6))
			ac->saw_basic = TRUE;

		if (!strncmp (challenge_hdr, "NTLM ", 5) &&
		    (!ac->w2k_domain || !ac->nt_domain)) {
			guchar *challenge;
			gsize length = 0;

			challenge = g_base64_decode (challenge_hdr + 5, &length);
			if (!ac->nt_domain)
				ac->nt_domain_defaulted = TRUE;
			xntlm_parse_challenge (challenge, length,
					       NULL,
					       ac->nt_domain ? NULL : &ac->nt_domain,
					       ac->w2k_domain ? NULL : &ac->w2k_domain);
			g_free (challenge);
			ac->saw_ntlm = TRUE;
			g_slist_free (headers);
			return;
		}

		headers = headers->next;
	}
	g_slist_free (headers);
}

/*
 * e2k_autoconfig_get_context:
 * @ac: an autoconfig context
 * @op: an #E2kOperation, for cancellation
 * @result: on output, a result code
 *
 * Checks if @ac's URI and authentication parameters work, and if so
 * returns an #E2kContext using them. On return, *@result (which
 * may not be %NULL) will contain a result code as follows:
 *
 *   %E2K_AUTOCONFIG_OK: success
 *   %E2K_AUTOCONFIG_REDIRECT: The server issued a valid-looking
 *     redirect. @ac->owa_uri has been updated and the caller
 *     should try again.
 *   %E2K_AUTOCONFIG_TRY_SSL: The server requires SSL.
 *     @ac->owa_uri has been updated and the caller should try
 *     again.
 *   %E2K_AUTOCONFIG_AUTH_ERROR: Generic authentication failure.
 *     Probably password incorrect
 *   %E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN: Authentication failed.
 *     Including an NT domain with the username (or using NTLM)
 *     may fix the problem.
 *   %E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC: Caller requested NTLM
 *     auth, but only Basic was available.
 *   %E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM: Caller requested Basic
 *     auth, but only NTLM was available.
 *   %E2K_AUTOCONFIG_EXCHANGE_5_5: Server appears to be Exchange 5.5.
 *   %E2K_AUTOCONFIG_NOT_EXCHANGE: Server does not appear to be
 *     any version of Exchange
 *   %E2K_AUTOCONFIG_NO_OWA: Server may be Exchange 2000, but OWA
 *     is not present at the given URL.
 *   %E2K_AUTOCONFIG_NO_MAILBOX: OWA claims the user has no mailbox.
 *   %E2K_AUTOCONFIG_CANT_RESOLVE: Could not resolve hostname.
 *   %E2K_AUTOCONFIG_CANT_CONNECT: Could not connect to server.
 *   %E2K_AUTOCONFIG_CANCELLED: User cancelled
 *   %E2K_AUTOCONFIG_FAILED: Other error.
 *
 * Return value: the new context, or %NULL
 *
 * (If you change this comment, see the note at the top of this file.)
 **/
E2kContext *
e2k_autoconfig_get_context (E2kAutoconfig *ac, E2kOperation *op,
			    E2kAutoconfigResult *result)
{
	E2kContext *ctx;
	SoupMessage *msg;
	E2kHTTPStatus status;
	const gchar *ms_webstorage;
	xmlDoc *doc;
	xmlNode *node;
	xmlChar *equiv, *content, *href;

	ctx = e2k_context_new (ac->owa_uri);
	if (!ctx) {
		*result = E2K_AUTOCONFIG_FAILED;
		return NULL;
	}
	e2k_context_set_auth (ctx, ac->username, ac->nt_domain,
			      ac->use_ntlm ? "NTLM" : "Basic", ac->password);

	msg = e2k_soup_message_new (ctx, ac->owa_uri, SOUP_METHOD_GET);

	g_return_val_if_fail (msg != NULL, NULL);
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), NULL);

	soup_message_headers_append (msg->request_headers, "Accept-Language",
				     e2k_http_accept_language ());
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	soup_message_add_status_code_handler (msg, "got-headers",
					      E2K_HTTP_UNAUTHORIZED,
					      G_CALLBACK (get_ctx_auth_handler),
					      ac);

 try_again:
	e2k_context_send_message (ctx, op, msg);
	status = msg->status_code;

	/* Check for cancellation or other transport error. */
	if (E2K_HTTP_STATUS_IS_TRANSPORT_ERROR (status)) {
		if (status == E2K_HTTP_CANCELLED)
			*result = E2K_AUTOCONFIG_CANCELLED;
		else if (status == E2K_HTTP_CANT_RESOLVE)
			*result = E2K_AUTOCONFIG_CANT_RESOLVE;
		else
			*result = E2K_AUTOCONFIG_CANT_CONNECT;
		goto done;
	}

	/* Check for an authentication failure. This could be because
	 * the password is incorrect, or because we used Basic auth
	 * without specifying a domain and the server doesn't have a
	 * default domain, or because we tried to use an auth type the
	 * server doesn't allow.
	 */
	if (status == E2K_HTTP_UNAUTHORIZED) {
		if (!ac->use_ntlm && !ac->nt_domain)
			*result = E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN;
		else if (ac->use_ntlm && !ac->saw_ntlm)
			*result = E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC;
		else if (!ac->use_ntlm && !ac->saw_basic)
			*result = E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM;
		else
			*result = E2K_AUTOCONFIG_AUTH_ERROR;
		goto done;
	}

	/* A redirection to "logon.asp" means this is Exchange 5.5
	 * OWA. A redirection to "owalogon.asp" means this is Exchange
	 * 2003 forms-based authentication. A redirection to
	 * "CookieAuth.dll" means that it's an Exchange 2003 server
	 * behind an ISA Server 2004 proxy. Other redirections most
	 * likely indicate that the user's mailbox has been moved to a
	 * new server.
	 */
	if (E2K_HTTP_STATUS_IS_REDIRECTION (status)) {
		const gchar *location;
		gchar *new_uri;

		location = soup_message_headers_get (msg->response_headers,
						     "Location");
		if (!location) {
			*result = E2K_AUTOCONFIG_FAILED;
			goto done;
		}

		if (strstr (location, "/logon.asp")) {
			*result = E2K_AUTOCONFIG_EXCHANGE_5_5;
			goto done;
		} else if (strstr (location, "/owalogon.asp") ||
			   strstr (location, "/CookieAuth.dll")) {
			if (e2k_context_fba (ctx, msg))
				goto try_again;
			*result = E2K_AUTOCONFIG_AUTH_ERROR;
			goto done;
		}

		new_uri = e2k_strdup_with_trailing_slash (location);
		e2k_autoconfig_set_owa_uri (ac, new_uri);
		g_free (new_uri);
		*result = E2K_AUTOCONFIG_REDIRECT;
		goto done;
	}

	/* If the server requires SSL, it will send back 403 Forbidden
	 * with a body explaining that.
	 */
	if (status == E2K_HTTP_FORBIDDEN &&
	    !strncmp (ac->owa_uri, "http:", 5) && msg->response_body->length > 0) {
		if (strstr (msg->response_body->data, "SSL")) {
			gchar *new_uri =
				g_strconcat ("https:", ac->owa_uri + 5, NULL);
			e2k_autoconfig_set_owa_uri (ac, new_uri);
			g_free (new_uri);
			*result = E2K_AUTOCONFIG_TRY_SSL;
			goto done;
		}
	}

	/* Figure out some stuff about the server */
	ms_webstorage = soup_message_headers_get (msg->response_headers,
						  "MS-WebStorage");
	if (ms_webstorage) {
		if (!strncmp (ms_webstorage, "6.0.", 4))
			ac->version = E2K_EXCHANGE_2000;
		else if (!strncmp (ms_webstorage, "6.5.", 4))
			ac->version = E2K_EXCHANGE_2003;
		else
			ac->version = E2K_EXCHANGE_FUTURE;
	} else {
		const gchar *server = soup_message_headers_get (msg->response_headers, "Server");

		/* If the server explicitly claims to be something
		 * other than IIS, then return the "not windows"
		 * error.
		 */
		if (server && !strstr (server, "IIS")) {
			*result = E2K_AUTOCONFIG_NOT_EXCHANGE;
			goto done;
		}

		/* It's probably Exchange 2000... older versions
		 * didn't include the MS-WebStorage header here. But
		 * we don't know for sure.
		 */
		ac->version = E2K_EXCHANGE_UNKNOWN;
	}

	/* If we're talking to OWA, then 404 Not Found means you don't
	 * have a mailbox. Otherwise, it means you're not talking to
	 * Exchange (even 5.5).
	 */
	if (status == E2K_HTTP_NOT_FOUND) {
		if (ms_webstorage)
			*result = E2K_AUTOCONFIG_NO_MAILBOX;
		else
			*result = E2K_AUTOCONFIG_NOT_EXCHANGE;
		goto done;
	}

	/* Any other error else gets generic failure */
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		*result = E2K_AUTOCONFIG_FAILED;
		goto done;
	}

	/* Parse the returned HTML. */
	doc = e2k_parse_html (msg->response_body->data, msg->response_body->length);
	if (!doc) {
		/* Not HTML? */
		*result = ac->version == E2K_EXCHANGE_UNKNOWN ?
			E2K_AUTOCONFIG_NO_OWA :
			E2K_AUTOCONFIG_FAILED;
		goto done;
	}

	/* Make sure it's not Exchange 5.5 */
	if (ac->version == E2K_EXCHANGE_UNKNOWN &&
	    strstr (ac->owa_uri, "/logon.asp")) {
		*result = E2K_AUTOCONFIG_EXCHANGE_5_5;
		goto done;
	}

	/* Make sure it's not trying to redirect us to Exchange 5.5 */
	for (node = doc->children; node; node = e2k_xml_find (node, "meta")) {
		gboolean ex55 = FALSE;

		equiv = xmlGetProp (node, (xmlChar *) "http-equiv");
		content = xmlGetProp (node, (xmlChar *) "content");
		if (equiv && content &&
		    !g_ascii_strcasecmp ((gchar *) equiv, "REFRESH") &&
		    xmlStrstr (content, (xmlChar *) "/logon.asp"))
			ex55 = TRUE;
		if (equiv)
			xmlFree (equiv);
		if (content)
			xmlFree (content);

		if (ex55) {
			*result = E2K_AUTOCONFIG_EXCHANGE_5_5;
			goto done;
		}
	}

	/* Try to find the base URI */
	node = e2k_xml_find (doc->children, "base");
	if (node) {
		/* We won */
		*result = E2K_AUTOCONFIG_OK;
		href = xmlGetProp (node, (xmlChar *) "href");
		g_free (ac->home_uri);
		ac->home_uri = g_strdup ((gchar *) href);
		xmlFree (href);
	} else
		*result = E2K_AUTOCONFIG_FAILED;
	xmlFreeDoc (doc);

 done:
	g_object_unref (msg);

	if (*result != E2K_AUTOCONFIG_OK) {
		g_object_unref (ctx);
		ctx = NULL;
	}
	return ctx;
}

static const gchar *home_properties[] = {
	PR_STORE_ENTRYID,
	E2K_PR_EXCHANGE_TIMEZONE
};

/*
 * e2k_autoconfig_check_exchange:
 * @ac: an autoconfiguration context
 * @op: an #E2kOperation, for cancellation
 *
 * Tries to connect to the the Exchange server using the OWA URL,
 * username, and password in @ac. Attempts to determine the domain
 * name and home_uri, and then given the home_uri, looks up the
 * user's mailbox entryid (used to find his Exchange 5.5 DN) and
 * default timezone.
 *
 * The returned codes are the same as for e2k_autoconfig_get_context()
 * with the following changes/additions/removals:
 *
 *   %E2K_AUTOCONFIG_REDIRECT: URL returned in first redirect returned
 *     another redirect, which was not followed.
 *   %E2K_AUTOCONFIG_CANT_BPROPFIND: The server does not allow
 *     BPROPFIND due to IIS Lockdown configuration
 *   %E2K_AUTOCONFIG_TRY_SSL: Not used; always handled internally by
 *     e2k_autoconfig_check_exchange()
 *
 * Return value: an #E2kAutoconfigResult
 *
 * (If you change this comment, see the note at the top of this file.)
 **/
E2kAutoconfigResult
e2k_autoconfig_check_exchange (E2kAutoconfig *ac, E2kOperation *op)
{
	xmlDoc *doc;
	xmlNode *node;
	E2kHTTPStatus status;
	E2kAutoconfigResult result;
	gchar *new_uri, *pf_uri;
	E2kContext *ctx;
	gboolean redirected = FALSE;
	E2kResultIter *iter;
	E2kResult *results;
	GByteArray *entryid;
	const gchar *exchange_dn, *timezone, *hrefs[] = { "" };
	xmlChar *prop;
	SoupBuffer *response;
	E2kUri *euri;

	g_return_val_if_fail (ac->owa_uri != NULL, E2K_AUTOCONFIG_FAILED);
	g_return_val_if_fail (ac->username != NULL, E2K_AUTOCONFIG_FAILED);
	g_return_val_if_fail (ac->password != NULL, E2K_AUTOCONFIG_FAILED);

 try_again:
	ctx = e2k_autoconfig_get_context (ac, op, &result);

	switch (result) {
	case E2K_AUTOCONFIG_OK:
		break;

	case E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC:
		if (ac->use_ntlm && !ac->require_ntlm) {
			ac->use_ntlm = FALSE;
			goto try_again;
		} else
			return E2K_AUTOCONFIG_AUTH_ERROR;

	case E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM:
		return E2K_AUTOCONFIG_AUTH_ERROR;

	case E2K_AUTOCONFIG_REDIRECT:
		if (!redirected) {
			redirected = TRUE;
			goto try_again;
		} else
			return result;

	case E2K_AUTOCONFIG_TRY_SSL:
		goto try_again;

	case E2K_AUTOCONFIG_NO_OWA:
	default:
		/* If the provided OWA URI had no path, try appending
		 * /exchange.
		 */
		euri = e2k_uri_new (ac->owa_uri);
		g_return_val_if_fail (euri != NULL, result);
		if (!euri->path || !strcmp (euri->path, "/")) {
			e2k_uri_free (euri);
			new_uri = e2k_uri_concat (ac->owa_uri, "exchange/");
			e2k_autoconfig_set_owa_uri (ac, new_uri);
			g_free (new_uri);
			goto try_again;
		}
		e2k_uri_free (euri);
		return result;
	}

	/* Find the link to the public folders */
	if (ac->version < E2K_EXCHANGE_2003)
		pf_uri = g_strdup_printf ("%s/?Cmd=contents", ac->owa_uri);
	else
		pf_uri = g_strdup_printf ("%s/?Cmd=navbar", ac->owa_uri);

	status = e2k_context_get_owa (ctx, NULL, pf_uri, FALSE, &response);
	g_free (pf_uri);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		doc = e2k_parse_html (response->data, response->length);
		soup_buffer_free (response);
	} else
		doc = NULL;

	if (doc) {
		for (node = e2k_xml_find (doc->children, "img"); node; node = e2k_xml_find (node, "img")) {
			prop = xmlGetProp (node, (xmlChar *) "src");
			if (prop && xmlStrstr (prop, (xmlChar *) "public") && node->parent) {
				node = node->parent;
				xmlFree (prop);
				prop = xmlGetProp (node, (xmlChar *) "href");
				if (prop) {
					euri = e2k_uri_new ((gchar *) prop);
					ac->pf_server = g_strdup (euri->host);
					e2k_uri_free (euri);
					xmlFree (prop);
				}
				break;
			}
		}
		xmlFreeDoc (doc);
	} else
		g_warning ("Could not parse pf page");

	/* Now find the store entryid and default timezone. We
	 * gratuitously use BPROPFIND in order to test if they
	 * have the IIS Lockdown problem.
	 */
	iter = e2k_context_bpropfind_start (ctx, op,
					    ac->home_uri, hrefs, 1,
					    home_properties,
					    G_N_ELEMENTS (home_properties));
	results = e2k_result_iter_next (iter);
	if (results) {
		timezone = e2k_properties_get_prop (results->props,
						    E2K_PR_EXCHANGE_TIMEZONE);
		if (timezone)
			ac->timezone = find_olson_timezone (timezone);

		entryid = e2k_properties_get_prop (results->props,
						   PR_STORE_ENTRYID);
		if (entryid) {
			exchange_dn = e2k_entryid_to_dn (entryid);
			if (exchange_dn)
				ac->exchange_dn = g_strdup (exchange_dn);
		}
	}
	status = e2k_result_iter_free (iter);
	g_object_unref (ctx);

	if (status == E2K_HTTP_UNAUTHORIZED) {
		if (ac->use_ntlm && !ac->require_ntlm) {
			ac->use_ntlm = FALSE;
			goto try_again;
		} else
			return E2K_AUTOCONFIG_AUTH_ERROR;
	} else if (status == E2K_HTTP_NOT_FOUND)
		return E2K_AUTOCONFIG_CANT_BPROPFIND;
	else if (status == E2K_HTTP_CANCELLED)
		return E2K_AUTOCONFIG_CANCELLED;
	else if (status == E2K_HTTP_CANT_RESOLVE)
		return E2K_AUTOCONFIG_CANT_RESOLVE;
	else if (E2K_HTTP_STATUS_IS_TRANSPORT_ERROR (status))
		return E2K_AUTOCONFIG_CANT_CONNECT;
	else if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return E2K_AUTOCONFIG_FAILED;

	return ac->exchange_dn ? E2K_AUTOCONFIG_OK : E2K_AUTOCONFIG_FAILED;
}

/* FIXME: make this cancellable */
static void
find_global_catalog (E2kAutoconfig *ac)
{
#ifndef G_OS_WIN32
	gint count, len;
	guchar answer[1024], namebuf[1024], *end, *p;
	guint16 type, qclass, rdlength, priority, weight, port;
	guint32 ttl;
	HEADER *header;

	if (!ac->w2k_domain)
		return;

	len = res_querydomain ("_gc._tcp", ac->w2k_domain, C_IN, T_SRV,
			       answer, sizeof (answer));
	if (len == -1)
		return;

	header = (HEADER *)answer;
	p = answer + sizeof (HEADER);
	end = answer + len;

	/* See RFCs 1035 and 2782 for details of the parsing */

	/* Skip query */
	count = ntohs (header->qdcount);
	while (count-- && p < end) {
		p += dn_expand (answer, end, p, (gchar *) namebuf, sizeof (namebuf));
		p += 4;
	}

	/* Read answers */
	while (count-- && p < end) {
		p += dn_expand (answer, end, p, (gchar *) namebuf, sizeof (namebuf));
		GETSHORT (type, p);
		GETSHORT (qclass, p);
		GETLONG (ttl, p);
		GETSHORT (rdlength, p);

		if (type != T_SRV || qclass != C_IN) {
			p += rdlength;
			continue;
		}

		GETSHORT (priority, p);
		GETSHORT (weight, p);
		GETSHORT (port, p);
		p += dn_expand (answer, end, p, (gchar *) namebuf, sizeof (namebuf));

		/* FIXME: obey priority and weight */
		ac->gc_server = g_strdup ((gchar *) namebuf);
		ac->gc_server_autodetected = TRUE;
		return;
	}

	return;
#else
	gchar *name, *casefolded_name;
	PDNS_RECORD dnsrecp, rover;

	name = g_strconcat ("_gc._tcp.", ac->w2k_domain, NULL);
	casefolded_name = g_utf8_strdown (name, -1);
	g_free (name);

	if (DnsQuery_UTF8 (casefolded_name, DNS_TYPE_SRV, DNS_QUERY_STANDARD,
			   NULL, &dnsrecp, NULL) != ERROR_SUCCESS) {
		g_free (casefolded_name);
		return;
	}

	for (rover = dnsrecp; rover != NULL; rover = rover->pNext) {
		if (rover->wType != DNS_TYPE_SRV ||
		    strcmp (rover->pName, casefolded_name) != 0)
			continue;
		ac->gc_server = g_strdup (rover->Data.SRV.pNameTarget);
		ac->gc_server_autodetected = TRUE;
		g_free (casefolded_name);
		DnsRecordListFree (dnsrecp, DnsFreeRecordList);
		return;
	}

	g_free (casefolded_name);
	DnsRecordListFree (dnsrecp, DnsFreeRecordList);
	return;
#endif
}

/**
 * e2k_autoconfig_get_global_catalog
 * @ac: an autoconfig context
 * @op: an #E2kOperation, for cancellation
 *
 * Tries to connect to the global catalog associated with @ac
 * (trying to figure it out from the domain name if the server
 * name is not yet known).
 *
 * Return value: the global catalog, or %NULL if the GC server name
 * wasn't provided and couldn't be autodetected.
 */
E2kGlobalCatalog *
e2k_autoconfig_get_global_catalog (E2kAutoconfig *ac, E2kOperation *op)
{
/*	if (!ac->gc_server) {
		find_global_catalog (ac);
		if (!ac->gc_server)
			return NULL;
	}

	return e2k_global_catalog_new (ac->gc_server, ac->gal_limit,
				       ac->username, ac->nt_domain,
				       ac->password, ac->gal_auth);
*/	return NULL;
}

/*
 * e2k_autoconfig_check_global_catalog
 * @ac: an autoconfig context
 * @op: an #E2kOperation, for cancellation
 *
 * Tries to connect to the global catalog associated with @ac
 * (trying to figure it out from the domain name if the server
 * name is not yet known). On success it will look up the user's
 * full name and email address (based on his Exchange DN).
 *
 * Possible return values are:
 *
 *   %E2K_AUTOCONFIG_OK: Success
 *   %E2K_AUTOCONFIG_CANT_RESOLVE: Could not determine GC server
 *   %E2K_AUTOCONFIG_NO_MAILBOX: Could not find information for
 *     the user
 *   %E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN: Plaintext password auth
 *     failed: need to specify NT domain
 *   %E2K_AUTOCONFIG_CANCELLED: Operation was cancelled
 *   %E2K_AUTOCONFIG_FAILED: Other error.
 *
 * Return value: an #E2kAutoconfigResult.
 *
 * (If you change this comment, see the note at the top of this file.)
 */
E2kAutoconfigResult
e2k_autoconfig_check_global_catalog (E2kAutoconfig *ac, E2kOperation *op)
{
/*	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;
	E2kAutoconfigResult result;

	g_return_val_if_fail (ac->exchange_dn != NULL, E2K_AUTOCONFIG_FAILED);

	gc = e2k_autoconfig_get_global_catalog (ac, op);
	if (!gc)
		return E2K_AUTOCONFIG_CANT_RESOLVE;

	set_account_uri_string (ac);

	status = e2k_global_catalog_lookup (
		gc, op, E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
		ac->exchange_dn, E2K_GLOBAL_CATALOG_LOOKUP_EMAIL |
		E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX, &entry);

	if (status == E2K_GLOBAL_CATALOG_OK) {
		ac->display_name = g_strdup (entry->display_name);
		ac->email = g_strdup (entry->email);
		result = E2K_AUTOCONFIG_OK;
	} else if (status == E2K_GLOBAL_CATALOG_CANCELLED)
		result = E2K_AUTOCONFIG_CANCELLED;
#ifndef HAVE_LDAP_NTLM_BIND
	else if (status == E2K_GLOBAL_CATALOG_AUTH_FAILED &&
		 !ac->nt_domain)
		result = E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN;
#endif
	else if (status == E2K_GLOBAL_CATALOG_ERROR)
		result = E2K_AUTOCONFIG_FAILED;
	else
		result = E2K_AUTOCONFIG_NO_MAILBOX;

	g_object_unref (gc);
	return result;
*/	return E2K_AUTOCONFIG_OK;
}

static void
set_account_uri_string (E2kAutoconfig *ac)
{
	E2kUri *owa_uri, *home_uri;
	gchar *path, *mailbox;
	GString *uri;

	owa_uri = e2k_uri_new (ac->owa_uri);
	home_uri = e2k_uri_new (ac->home_uri);

	uri = g_string_new ("exchange://");
	if (ac->nt_domain && (!ac->use_ntlm || !ac->nt_domain_defaulted)) {
		e2k_uri_append_encoded (uri, ac->nt_domain, FALSE, "\\;:@/");
		g_string_append_c (uri, '\\');
	}
	e2k_uri_append_encoded (uri, ac->username, FALSE, ";:@/");

	if (!ac->use_ntlm)
		g_string_append (uri, ";auth=Basic");

	g_string_append_c (uri, '@');
	e2k_uri_append_encoded (uri, owa_uri->host, FALSE, ":/");
	if (owa_uri->port)
		g_string_append_printf (uri, ":%d", owa_uri->port);
	g_string_append_c (uri, '/');

	if (!strcmp (owa_uri->protocol, "https"))
		g_string_append (uri, ";use_ssl=always");
	g_string_append (uri, ";ad_server=");
	e2k_uri_append_encoded (uri, ac->gc_server, FALSE, ";?");
	if (ac->gal_limit != -1)
		g_string_append_printf (uri, ";ad_limit=%d", ac->gal_limit);
	if (ac->gal_auth != E2K_AUTOCONFIG_USE_GAL_DEFAULT) {
		const gchar *value = NULL;

		switch (ac->gal_auth) {
		case E2K_AUTOCONFIG_USE_GAL_BASIC: value = "basic"; break;
		case E2K_AUTOCONFIG_USE_GAL_NTLM:  value = "ntlm";  break;
		case E2K_AUTOCONFIG_USE_GAL_DEFAULT: /* should not get here */ break;
		}

		if (value)
			g_string_append_printf (uri, ";ad_auth=%s", value);
	}

	path = g_strdup (home_uri->path + 1);
	mailbox = strrchr (path, '/');
	if (mailbox && !mailbox[1]) {
		*mailbox = '\0';
		mailbox = strrchr (path, '/');
	}
	if (mailbox) {
		*mailbox++ = '\0';
		g_string_append (uri, ";mailbox=");
		e2k_uri_append_encoded (uri, mailbox, FALSE, ";?");
	}
	g_string_append (uri, ";owa_path=/");
	e2k_uri_append_encoded (uri, path, FALSE, ";?");
	g_free (path);

	g_string_append (uri, ";pf_server=");
	e2k_uri_append_encoded (uri, ac->pf_server ? ac->pf_server : home_uri->host, FALSE, ";?");

	ac->account_uri = uri->str;
	ac->exchange_server = g_strdup (home_uri->host);
	g_string_free (uri, FALSE);
	e2k_uri_free (home_uri);
	e2k_uri_free (owa_uri);
}

/* Approximate mapping from Exchange timezones to Olson ones. Exchange
 * is less specific, so we factor in the language/country info from
 * the locale in our guess.
 *
 * We strip " Standard Time" / " Daylight Time" from the Windows
 * timezone names. (Actually, we just strip the last two words.)
 */
static struct {
	const gchar *windows_name, *lang, *country, *olson_name;
} zonemap[] = {
	/* (GMT-12:00) Eniwetok, Kwajalein */
	{ "Dateline", NULL, NULL, "Pacific/Kwajalein" },

	/* (GMT-11:00) Midway Island, Samoa */
	{ "Samoa", NULL, NULL, "Pacific/Midway" },

	/* (GMT-10:00) Hawaii */
	{ "Hawaiian", NULL, NULL, "Pacific/Honolulu" },

	/* (GMT-09:00) Alaska */
	{ "Alaskan", NULL, NULL, "America/Juneau" },

	/* (GMT-08:00) Pacific Time (US & Canada); Tijuana */
	{ "Pacific", NULL, "CA", "America/Vancouver" },
	{ "Pacific", "es", "MX", "America/Tijuana" },
	{ "Pacific", NULL, NULL, "America/Los_Angeles" },

	/* (GMT-07:00) Arizona */
	{ "US Mountain", NULL, NULL, "America/Phoenix" },

	/* (GMT-07:00) Mountain Time (US & Canada) */
	{ "Mountain", NULL, "CA", "America/Edmonton" },
	{ "Mountain", NULL, NULL, "America/Denver" },

	/* (GMT-06:00) Central America */
	{ "Central America", NULL, "BZ", "America/Belize" },
	{ "Central America", NULL, "CR", "America/Costa_Rica" },
	{ "Central America", NULL, "GT", "America/Guatemala" },
	{ "Central America", NULL, "HN", "America/Tegucigalpa" },
	{ "Central America", NULL, "NI", "America/Managua" },
	{ "Central America", NULL, "SV", "America/El_Salvador" },

	/* (GMT-06:00) Central Time (US & Canada) */
	{ "Central", NULL, NULL, "America/Chicago" },

	/* (GMT-06:00) Mexico City */
	{ "Mexico", NULL, NULL, "America/Mexico_City" },

	/* (GMT-06:00) Saskatchewan */
	{ "Canada Central", NULL, NULL, "America/Regina" },

	/* (GMT-05:00) Bogota, Lima, Quito */
	{ "SA Pacific", NULL, "BO", "America/Bogota" },
	{ "SA Pacific", NULL, "EC", "America/Guayaquil" },
	{ "SA Pacific", NULL, "PA", "America/Panama" },
	{ "SA Pacific", NULL, "PE", "America/Lima" },

	/* (GMT-05:00) Eastern Time (US & Canada) */
	{ "Eastern", "fr", "CA", "America/Montreal" },
	{ "Eastern", NULL, NULL, "America/New_York" },

	/* (GMT-05:00) Indiana (East) */
	{ "US Eastern", NULL, NULL, "America/Indiana/Indianapolis" },

	/* (GMT-04:00) Atlantic Time (Canada) */
	{ "Atlantic", "es", "US", "America/Puerto_Rico" },
	{ "Atlantic", NULL, "VI", "America/St_Thomas" },
	{ "Atlantic", NULL, "CA", "America/Halifax" },

	/* (GMT-04:00) Caracas, La Paz */
	{ "SA Western", NULL, "BO", "America/La_Paz" },
	{ "SA Western", NULL, "VE", "America/Caracas" },

	/* (GMT-04:00) Santiago */
	{ "Pacific SA", NULL, NULL, "America/Santiago" },

	/* (GMT-03:30) Newfoundland */
	{ "Newfoundland", NULL, NULL, "America/St_Johns" },

	/* (GMT-03:00) Brasilia */
	{ "E. South America", NULL, NULL, "America/Sao_Paulo" },

	/* (GMT-03:00) Greenland */
	{ "Greenland", NULL, NULL, "America/Godthab" },

	/* (GMT-03:00) Buenos Aires, Georgetown */
	{ "SA Eastern", NULL, NULL, "America/Buenos_Aires" },

	/* (GMT-02:00) Mid-Atlantic */
	{ "Mid-Atlantic", NULL, NULL, "America/Noronha" },

	/* (GMT-01:00) Azores */
	{ "Azores", NULL, NULL, "Atlantic/Azores" },

	/* (GMT-01:00) Cape Verde Is. */
	{ "Cape Verde", NULL, NULL, "Atlantic/Cape_Verde" },

	/* (GMT) Casablanca, Monrovia */
	{ "Greenwich", NULL, "LR", "Africa/Monrovia" },
	{ "Greenwich", NULL, "MA", "Africa/Casablanca" },

	/* (GMT) Greenwich Mean Time : Dublin, Edinburgh, Lisbon, London */
	{ "GMT", "ga", "IE", "Europe/Dublin" },
	{ "GMT", "pt", "PT", "Europe/Lisbon" },
	{ "GMT", NULL, NULL, "Europe/London" },

	/* (GMT+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna */
	{ "W. Europe", "nl", "NL", "Europe/Amsterdam" },
	{ "W. Europe", "it", "IT", "Europe/Rome" },
	{ "W. Europe", "sv", "SE", "Europe/Stockholm" },
	{ "W. Europe", NULL, "CH", "Europe/Zurich" },
	{ "W. Europe", NULL, "AT", "Europe/Vienna" },
	{ "W. Europe", "de", "DE", "Europe/Berlin" },

	/* (GMT+01:00) Belgrade, Bratislava, Budapest, Ljubljana, Prague */
	{ "Central Europe", "sr", "YU", "Europe/Belgrade" },
	{ "Central Europe", "sk", "SK", "Europe/Bratislava" },
	{ "Central Europe", "hu", "HU", "Europe/Budapest" },
	{ "Central Europe", "sl", "SI", "Europe/Ljubljana" },
	{ "Central Europe", "cz", "CZ", "Europe/Prague" },

	/* (GMT+01:00) Brussels, Copenhagen, Madrid, Paris */
	{ "Romance", NULL, "BE", "Europe/Brussels" },
	{ "Romance", "da", "DK", "Europe/Copenhagen" },
	{ "Romance", "es", "ES", "Europe/Madrid" },
	{ "Romance", "fr", "FR", "Europe/Paris" },

	/* (GMT+01:00) Sarajevo, Skopje, Sofija, Vilnius, Warsaw, Zagreb */
	{ "Central European", "bs", "BA", "Europe/Sarajevo" },
	{ "Central European", "mk", "MK", "Europe/Skopje" },
	{ "Central European", "bg", "BG", "Europe/Sofia" },
	{ "Central European", "lt", "LT", "Europe/Vilnius" },
	{ "Central European", "pl", "PL", "Europe/Warsaw" },
	{ "Central European", "hr", "HR", "Europe/Zagreb" },

	/* (GMT+01:00) West Central Africa */
	{ "W. Central Africa", NULL, NULL, "Africa/Kinshasa" },

	/* (GMT+02:00) Athens, Istanbul, Minsk */
	{ "GTB", "el", "GR", "Europe/Athens" },
	{ "GTB", "tr", "TR", "Europe/Istanbul" },
	{ "GTB", "be", "BY", "Europe/Minsk" },

	/* (GMT+02:00) Bucharest */
	{ "E. Europe", NULL, NULL, "Europe/Bucharest" },

	/* (GMT+02:00) Cairo */
	{ "Egypt", NULL, NULL, "Africa/Cairo" },

	/* (GMT+02:00) Harare, Pretoria */
	{ "South Africa", NULL, NULL, "Africa/Johannesburg" },

	/* (GMT+02:00) Helsinki, Riga, Tallinn */
	{ "FLE", "lv", "LV", "Europe/Riga" },
	{ "FLE", "et", "EE", "Europe/Tallinn" },
	{ "FLE", "fi", "FI", "Europe/Helsinki" },

	/* (GMT+02:00) Jerusalem */
	{ "Israel", NULL, NULL, "Asia/Jerusalem" },

	/* (GMT+03:00) Baghdad */
	{ "Arabic", NULL, NULL, "Asia/Baghdad" },

	/* (GMT+03:00) Kuwait, Riyadh */
	{ "Arab", NULL, "KW", "Asia/Kuwait" },
	{ "Arab", NULL, "SA", "Asia/Riyadh" },

	/* (GMT+03:00) Moscow, St. Petersburg, Volgograd */
	{ "Russian", NULL, NULL, "Europe/Moscow" },

	/* (GMT+03:00) Nairobi */
	{ "E. Africa", NULL, NULL, "Africa/Nairobi" },

	/* (GMT+03:30) Tehran */
	{ "Iran", NULL, NULL, "Asia/Tehran" },

	/* (GMT+04:00) Abu Dhabi, Muscat */
	{ "Arabian", NULL, NULL, "Asia/Muscat" },

	/* (GMT+04:00) Baku, Tbilisi, Yerevan */
	{ "Caucasus", NULL, NULL, "Asia/Baku" },

	/* (GMT+04:30) Kabul */
	{ "Afghanistan", NULL, NULL, "Asia/Kabul" },

	/* (GMT+05:00) Ekaterinburg */
	{ "Ekaterinburg", NULL, NULL, "Asia/Yekaterinburg" },

	/* (GMT+05:00) Islamabad, Karachi, Tashkent */
	{ "West Asia", NULL, NULL, "Asia/Karachi" },

	/* (GMT+05:30) Kolkata, Chennai, Mumbai, New Delhi */
	{ "India", NULL, NULL, "Asia/Calcutta" },

	/* (GMT+05:45) Kathmandu */
	{ "Nepal", NULL, NULL, "Asia/Katmandu" },

	/* (GMT+06:00) Almaty, Novosibirsk */
	{ "N. Central Asia", NULL, NULL, "Asia/Almaty" },

	/* (GMT+06:00) Astana, Dhaka */
	{ "Central Asia", NULL, NULL, "Asia/Dhaka" },

	/* (GMT+06:00) Sri Jayawardenepura */
	{ "Sri Lanka", NULL, NULL, "Asia/Colombo" },

	/* (GMT+06:30) Rangoon */
	{ "Myanmar", NULL, NULL, "Asia/Rangoon" },

	/* (GMT+07:00) Bangkok, Hanoi, Jakarta */
	{ "SE Asia", "th", "TH", "Asia/Bangkok" },
	{ "SE Asia", "vi", "VN", "Asia/Saigon" },
	{ "SE Asia", "id", "ID", "Asia/Jakarta" },

	/* (GMT+07:00) Krasnoyarsk */
	{ "North Asia", NULL, NULL, "Asia/Krasnoyarsk" },

	/* (GMT+08:00) Beijing, Chongqing, Hong Kong, Urumqi */
	{ "China", NULL, "HK", "Asia/Hong_Kong" },
	{ "China", NULL, NULL, "Asia/Shanghai" },

	/* (GMT+08:00) Irkutsk, Ulaan Bataar */
	{ "North Asia East", NULL, NULL, "Asia/Irkutsk" },

	/* (GMT+08:00) Perth */
	{ "W. Australia", NULL, NULL, "Australia/Perth" },

	/* (GMT+08:00) Kuala Lumpur, Singapore */
	{ "Singapore", NULL, NULL, "Asia/Kuala_Lumpur" },

	/* (GMT+08:00) Taipei */
	{ "Taipei", NULL, NULL, "Asia/Taipei" },

	/* (GMT+09:00) Osaka, Sapporo, Tokyo */
	{ "Tokyo", NULL, NULL, "Asia/Tokyo" },

	/* (GMT+09:00) Seoul */
	{ "Korea", NULL, "KP", "Asia/Pyongyang" },
	{ "Korea", NULL, "KR", "Asia/Seoul" },

	/* (GMT+09:00) Yakutsk */
	{ "Yakutsk", NULL, NULL, "Asia/Yakutsk" },

	/* (GMT+09:30) Adelaide */
	{ "Cen. Australia", NULL, NULL, "Australia/Adelaide" },

	/* (GMT+09:30) Darwin */
	{ "AUS Central", NULL, NULL, "Australia/Darwin" },

	/* (GMT+10:00) Brisbane */
	{ "E. Australia", NULL, NULL, "Australia/Brisbane" },

	/* (GMT+10:00) Canberra, Melbourne, Sydney */
	{ "AUS Eastern", NULL, NULL, "Australia/Sydney" },

	/* (GMT+10:00) Guam, Port Moresby */
	{ "West Pacific", NULL, NULL, "Pacific/Guam" },

	/* (GMT+10:00) Hobart */
	{ "Tasmania", NULL, NULL, "Australia/Hobart" },

	/* (GMT+10:00) Vladivostok */
	{ "Vladivostok", NULL, NULL, "Asia/Vladivostok" },

	/* (GMT+11:00) Magadan, Solomon Is., New Caledonia */
	{ "Central Pacific", NULL, NULL, "Pacific/Midway" },

	/* (GMT+12:00) Auckland, Wellington */
	{ "New Zealand", NULL, NULL, "Pacific/Auckland" },

	/* (GMT+12:00) Fiji, Kamchatka, Marshall Is. */
	{ "Fiji", "ru", "RU", "Asia/Kamchatka" },
	{ "Fiji", NULL, NULL, "Pacific/Fiji" },

	/* (GMT+13:00) Nuku'alofa */
	{ "Tonga", NULL, NULL, "Pacific/Tongatapu" }
};

static gchar *
find_olson_timezone (const gchar *windows_timezone)
{
	gint i, tzlen;
	const gchar *locale, *p;
	gchar lang[3] = { 0 }, country[3] = { 0 };

	/* Strip " Standard Time" / " Daylight Time" from name */
	p = windows_timezone + strlen (windows_timezone) - 1;
	while (p > windows_timezone && *p-- != ' ')
		;
	while (p > windows_timezone && *p-- != ' ')
		;
	tzlen = p - windows_timezone + 1;

	/* Find the first entry in zonemap with a matching name */
	for (i = 0; i < G_N_ELEMENTS (zonemap); i++) {
		if (!g_ascii_strncasecmp (windows_timezone,
					  zonemap[i].windows_name,
					  tzlen))
			break;
	}
	if (i == G_N_ELEMENTS (zonemap))
		return NULL; /* Shouldn't happen... */

	/* If there's only one choice, go with it */
	if (!zonemap[i].lang && !zonemap[i].country)
		return g_strdup (zonemap[i].olson_name);

	/* Find our language/country (hopefully). */
#ifndef G_OS_WIN32
	locale = getenv ("LANG");
#else
	locale = g_win32_getlocale ();
#endif
	if (locale) {
		strncpy (lang, locale, 2);
		locale = strchr (locale, '_');
		if (locale++)
			strncpy (country, locale, 2);
	}
#ifdef G_OS_WIN32
	g_free ((gchar *) locale);
#endif

	/* Look for an entry where either the country or the
	 * language matches.
	 */
	do {
		if ((zonemap[i].lang && !strcmp (zonemap[i].lang, lang)) ||
		    (zonemap[i].country && !strcmp (zonemap[i].country, country)))
			return g_strdup (zonemap[i].olson_name);
	} while (++i < G_N_ELEMENTS (zonemap) &&
		 !g_ascii_strncasecmp (windows_timezone,
				       zonemap[i].windows_name,
				       tzlen));

	/* None of the hints matched, so (semi-arbitrarily) return the
	 * last of the entries with the right Windows timezone name.
	 */
	return g_strdup (zonemap[i - 1].olson_name);
}

/* Config file handling */

static GHashTable *config_options;

static void
read_config (void)
{
	struct stat st;
	gchar *p, *name, *value;
	gchar *config_data;
	gint fd = -1;

	config_options = g_hash_table_new (e2k_ascii_strcase_hash,
					    e2k_ascii_strcase_equal);

#ifndef G_OS_WIN32
	fd = g_open ("/etc/ximian/connector.conf", O_RDONLY, 0);
#endif
	if (fd == -1) {
		gchar *filename = NULL;
//		gchar *filename = g_build_filename (CONNECTOR_PREFIX,
//						    "etc/connector.conf",
//						    NULL);

		fd = g_open (filename, O_RDONLY, 0);
		g_free (filename);
	}
	if (fd == -1)
		return;
	if (fstat (fd, &st) == -1) {
		g_warning ("Could not stat connector.conf: %s",
			   g_strerror (errno));
		close (fd);
		return;
	}

	config_data = g_malloc (st.st_size + 1);
	if (read (fd, config_data, st.st_size) != st.st_size) {
		g_warning ("Could not read connector.conf: %s",
			   g_strerror (errno));
		close (fd);
		g_free (config_data);
		return;
	}
	close (fd);
	config_data[st.st_size] = '\0';

	/* Read config data */
	p = config_data;

	while (1) {
		for (name = p; isspace ((guchar)*name); name++)
			;

		p = strchr (name, ':');
		if (!p || !p[1])
			break;
		*p = '\0';
		value = p + 2;
		p = strchr (value, '\n');
		if (!p)
			break;
		if (*(p - 1) == '\r')
			*(p - 1) = '\0';
		*p = '\0';
		p++;

		if (g_ascii_strcasecmp (value, "false") &&
		    g_ascii_strcasecmp (value, "no"))
			g_hash_table_insert (config_options, name, value);
	};

	g_free (config_data);
}

/**
 * e2k_autoconfig_lookup_option:
 * @option: option name to look up
 *
 * Looks up an autoconfiguration hint in the config file (if present)
 *
 * Return value: the string value of the option, or %NULL if it is unset.
 **/
const gchar *
e2k_autoconfig_lookup_option (const gchar *option)
{
	if (!config_options)
		read_config ();
	return g_hash_table_lookup (config_options, option);
}

static gboolean
validate (const gchar *owa_url, gchar *user, gchar *password, ExchangeParams *exchange_params, E2kAutoconfigResult *result)
{
	E2kAutoconfig *ac;
	E2kOperation op;        /* FIXME */
	E2kUri *euri;
	gboolean valid = FALSE;
	const gchar *old, *new;
	gchar *path, *mailbox;

	ac = e2k_autoconfig_new (owa_url, user, password,
				 E2K_AUTOCONFIG_USE_EITHER);

	e2k_operation_init (&op);
	/* e2k_autoconfig_set_gc_server (ac, ad_server, gal_limit) FIXME */
	/* e2k_autoconfig_set_gc_server (ac, NULL, -1); */
	*result = e2k_autoconfig_check_exchange (ac, &op);

	if (*result == E2K_AUTOCONFIG_OK) {
		/*
		 * On error code 403 and SSL seen in server response
		 * e2k_autoconfig_get_context() tries to
		 * connect using https if owa url has http and vice versa.
		 * And also re-sets the owa_uri in E2kAutoconfig.
		 * So even if the uri is incorrect,
		 * e2k_autoconfig_check_exchange() will return success.
		 * In this case of account set up, owa_url paramter will still
		 * have wrong url entered, and we throw the error, instead of
		 * going ahead with account setup and failing later.
		 */
		if (g_str_has_prefix (ac->owa_uri, "http:")) {
		    if (!g_str_has_prefix (owa_url, "http:"))
			*result = E2K_AUTOCONFIG_CANT_CONNECT;
		}
		else if (!g_str_has_prefix (owa_url, "https:"))
			*result = E2K_AUTOCONFIG_CANT_CONNECT;
	}

	if (*result == E2K_AUTOCONFIG_OK) {
		gint len;

		*result = e2k_autoconfig_check_global_catalog (ac, &op);
		e2k_operation_free (&op);

		/* find mailbox and owa_path values */
		euri = e2k_uri_new (ac->home_uri);
		path = g_strdup (euri->path + 1);
		e2k_uri_free (euri);

		/* no slash at the end of path */
		len = strlen (path);
		while (len && path [len - 1] == '/') {
			path [len - 1] = '\0';
			len--;
		}

		/* change a mailbox only if not set by the caller */
		if (!exchange_params->mailbox || !*exchange_params->mailbox) {
			mailbox = strrchr (path, '/');
			if (mailbox && !mailbox[1]) {
				*mailbox = '\0';
				mailbox = strrchr (path, '/');
			}
			if (mailbox)
				*mailbox++ = '\0';

			g_free (exchange_params->mailbox);
			exchange_params->mailbox  = g_strdup (mailbox);
		} else {
			/* always strip the mailbox part from the path */
			gchar *slash = strrchr (path, '/');

			if (slash)
				*slash = '\0';
		}

		exchange_params->owa_path = g_strdup_printf ("%s%s", "/", path);
		g_free (path);
		exchange_params->host = g_strdup (ac->pf_server);
		if (ac->gc_server)
			exchange_params->ad_server = g_strdup (ac->gc_server);
		exchange_params->is_ntlm = ac->saw_ntlm;

		valid = TRUE;
	}
	else {
		switch (*result) {

		case E2K_AUTOCONFIG_CANT_CONNECT:
			if (!strncmp (ac->owa_uri, "http:", 5)) {
				old = "http";
				new = "https";
			} else {
				old = "https";
				new = "http";
			}

			/* SURF : e_notice (NULL, GTK_MESSAGE_ERROR,
				  _("Could not connect to the Exchange "
				    "server.\nMake sure the URL is correct "
				    "(try \"%s\" instead of \"%s\"?) "
				    "and try again."), new, old);
			*/
			valid = FALSE;
			break;

		case E2K_AUTOCONFIG_CANT_RESOLVE:
		/* SURF :	e_notice (NULL, GTK_MESSAGE_ERROR,
				_("Could not locate Exchange server.\n"
				  "Make sure the server name is spelled correctly "
				  "and try again."));
		*/
			valid = FALSE;
			break;

		case E2K_AUTOCONFIG_AUTH_ERROR:
		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM:
		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC:
		/* SURF :	e_notice (NULL, GTK_MESSAGE_ERROR,
				_("Could not authenticate to the Exchange "
				  "server.\nMake sure the username and "
				  "password are correct and try again."));
		*/
			valid = FALSE;
			break;

		case E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN:
		/* SURF :		e_notice (NULL, GTK_MESSAGE_ERROR,
				_("Could not authenticate to the Exchange "
				  "server.\nMake sure the username and "
				  "password are correct and try again.\n\n"
				  "You may need to specify the Windows "
				  "domain name as part of your username "
				  "(eg, \"MY-DOMAIN\\%s\")."),
				  ac->username);
		*/
			valid = FALSE;
			break;

		case E2K_AUTOCONFIG_NO_OWA:
		case E2K_AUTOCONFIG_NOT_EXCHANGE:
		/* SURF :	e_notice (NULL, GTK_MESSAGE_ERROR,
				_("Could not find OWA data at the indicated URL.\n"
				  "Make sure the URL is correct and try again."));
		*/
			valid = FALSE;
			break;

		case E2K_AUTOCONFIG_CANT_BPROPFIND:
		/* SURF :	e_notice (
				NULL, GTK_MESSAGE_ERROR,
				_("Ximian Connector requires access to certain "
				"functionality on the Exchange Server that appears "
				"to be disabled or blocked.  (This is usually "
				"unintentional.)  Your Exchange Administrator will "
				"need to enable this functionality in order for "
				"you to be able to use Ximian Connector.\n\n"
				"For information to provide to your Exchange "
				"administrator, please follow the link below:\n"
				"http://support.novell.com/cgi-bin/search/searchtid.cgi?/ximian/ximian328.html "));
		*/
			valid = FALSE;
			break;

		case E2K_AUTOCONFIG_EXCHANGE_5_5:
		/* SURF :	e_notice (
				NULL, GTK_MESSAGE_ERROR,
				_("The Exchange server URL you provided is for an "
				"Exchange 5.5 Server. Ximian Connector supports "
				"Microsoft Exchange 2000 and 2003 only."));
		*/
			valid = FALSE;
			break;

		default:
		/* SURF :	e_notice (NULL, GTK_MESSAGE_ERROR,
				_("Could not configure Exchange account because "
				  "an unknown error occurred. Check the URL, "
				  "username, and password, and try again."));
		*/
			valid = FALSE; /* FIXME return valid */
			break;
		}
	}

	e2k_autoconfig_free (ac);
	return valid;
}
