/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 1999-2004 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "e2k-uri.h"

/**
 * e2k_uri_new:
 * @uri_string: the URI
 *
 * Parses @uri_string.
 *
 * Return value: a parsed %E2kUri
 **/
E2kUri *
e2k_uri_new (const gchar *uri_string)
{
	E2kUri *uri;
	const gchar *end, *hash, *colon, *semi, *at, *slash;
	const gchar *question, *p;

	uri = g_new0 (E2kUri, 1);

	/* Find fragment. */
	end = hash = strchr (uri_string, '#');
	if (hash && hash[1]) {
		uri->fragment = g_strdup (hash + 1);
		e2k_uri_decode (uri->fragment);
	} else
		end = uri_string + strlen (uri_string);

	/* Find protocol: initial [a-z+.-]* substring until ":" */
	p = uri_string;
	while (p < end && (isalnum ((guchar)*p) ||
			   *p == '.' || *p == '+' || *p == '-'))
		p++;

	if (p > uri_string && *p == ':') {
		uri->protocol = g_ascii_strdown (uri_string, p - uri_string);
		uri_string = p + 1;
	}

	if (!*uri_string)
		return uri;

	/* Check for authority */
	if (strncmp (uri_string, "//", 2) == 0) {
		uri_string += 2;

		slash = uri_string + strcspn (uri_string, "/#");
		at = strchr (uri_string, '@');
		if (at && at < slash) {
			gchar *backslash;

			colon = strchr (uri_string, ':');
			if (colon && colon < at) {
				uri->passwd = g_strndup (colon + 1,
							 at - colon - 1);
				e2k_uri_decode (uri->passwd);
			} else {
				uri->passwd = NULL;
				colon = at;
			}

			semi = strchr(uri_string, ';');
			if (semi && semi < colon &&
			    !g_ascii_strncasecmp (semi, ";auth=", 6)) {
				uri->authmech = g_strndup (semi + 6,
							   colon - semi - 6);
				e2k_uri_decode (uri->authmech);
			} else {
				uri->authmech = NULL;
				semi = colon;
			}

			uri->user = g_strndup (uri_string, semi - uri_string);
			e2k_uri_decode (uri->user);
			uri_string = at + 1;

			backslash = strchr (uri->user, '\\');
			if (!backslash)
				backslash = strchr (uri->user, '/');
			if (backslash) {
				uri->domain = uri->user;
				*backslash = '\0';
				uri->user = g_strdup (backslash + 1);
			}
		} else
			uri->user = uri->passwd = uri->domain = NULL;

		/* Find host and port. */
		colon = strchr (uri_string, ':');
		if (colon && colon < slash) {
			uri->host = g_strndup (uri_string, colon - uri_string);
			uri->port = strtoul (colon + 1, NULL, 10);
		} else {
			uri->host = g_strndup (uri_string, slash - uri_string);
			e2k_uri_decode (uri->host);
			uri->port = 0;
		}

		uri_string = slash;
	}

	/* Find query */
	question = memchr (uri_string, '?', end - uri_string);
	if (question) {
		if (question[1]) {
			uri->query = g_strndup (question + 1,
						end - (question + 1));
			e2k_uri_decode (uri->query);
		}
		end = question;
	}

	/* Find parameters */
	semi = memchr (uri_string, ';', end - uri_string);
	if (semi) {
		if (semi[1]) {
			const gchar *cur, *p, *eq;
			gchar *name, *value;

			for (cur = semi + 1; cur < end; cur = p + 1) {
				p = memchr (cur, ';', end - cur);
				if (!p)
					p = end;
				eq = memchr (cur, '=', p - cur);
				if (eq) {
					name = g_strndup (cur, eq - cur);
					value = g_strndup (eq + 1, p - (eq + 1));
					e2k_uri_decode (value);
				} else {
					name = g_strndup (cur, p - cur);
					value = g_strdup ("");
				}
				e2k_uri_decode (name);
				g_datalist_set_data_full (&uri->params, name,
							  value, g_free);
				g_free (name);
			}
		}
		end = semi;
	}

	if (end != uri_string) {
		uri->path = g_strndup (uri_string, end - uri_string);
		e2k_uri_decode (uri->path);
	}

	return uri;
}

/**
 * e2k_uri_free:
 * @uri: an %E2kUri
 *
 * Frees @uri
 **/
void
e2k_uri_free (E2kUri *uri)
{
	if (uri) {
		g_free (uri->protocol);
		g_free (uri->user);
		g_free (uri->domain);
		g_free (uri->authmech);
		g_free (uri->passwd);
		g_free (uri->host);
		g_free (uri->path);
		g_datalist_clear (&uri->params);
		g_free (uri->query);
		g_free (uri->fragment);

		g_free (uri);
	}
}

/**
 * e2k_uri_get_param:
 * @uri: an %E2kUri
 * @name: name of the parameter
 *
 * Fetches a parameter from @uri
 *
 * Return value: the value of @name, or %NULL if it is not set
 **/
const gchar *
e2k_uri_get_param (E2kUri *uri, const gchar *name)
{
	return g_datalist_get_data (&uri->params, name);
}

#define HEXVAL(c) (isdigit (c) ? (c) - '0' : g_ascii_tolower (c) - 'a' + 10)

/**
 * e2k_uri_decode:
 * @part: a piece of a URI
 *
 * Undoes URI-escaping in @part in-place.
 **/
void
e2k_uri_decode (gchar *part)
{
	guchar *s, *d;

	s = d = (guchar *)part;
	while (*s) {
		if (*s == '%') {
			if (isxdigit (s[1]) && isxdigit (s[2])) {
				*d++ = HEXVAL (s[1]) * 16 + HEXVAL (s[2]);
				s += 3;
			} else
				*d++ = *s++;
		} else
			*d++ = *s++;
	}
	*d = '\0';
}

static const gint uri_encoded_char[] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 0x00 - 0x0f */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 0x10 - 0x1f */
	1, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2,  /*  ' ' - '/'  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 2,  /*  '0' - '?'  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /*  '@' - 'O'  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 1, 0,  /*  'P' - '_'  */
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /*  '`' - 'o'  */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 1,  /*  'p' - 0x7f */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

/**
 * e2k_uri_append_encoded:
 * @str: a %GString containing part of a URI
 * @in: data to append to @str
 * @wss_encode: whether or not to use the special Web Storage System
 * encoding rules
 * @extra_enc_chars: additional characters beyond the normal URI-reserved
 * characters to encode when appending to @str
 *
 * Appends @in to @str, encoding URI-unsafe characters as needed
 * (optionally including some Exchange-specific encodings).
 *
 * When appending a path, you must append each segment separately;
 * e2k_uri_append_encoded() will encode any "/"s passed in.
 **/
void
e2k_uri_append_encoded (GString *str, const gchar *in,
			gboolean wss_encode, const gchar *extra_enc_chars)
{
	const guchar *s = (const guchar *)in;

	while (*s) {
		if (extra_enc_chars && strchr (extra_enc_chars, *s))
			goto escape;
		switch (uri_encoded_char[*s]) {
		case 2:
			if (!wss_encode)
				goto escape;
			switch (*s++) {
			case '/':
				g_string_append (str, "_xF8FF_");
				break;
			case '?':
				g_string_append (str, "_x003F_");
				break;
			case '\\':
				g_string_append (str, "_xF8FE_");
				break;
			case '~':
				g_string_append (str, "_x007E_");
				break;
			}
			break;
		case 1:
		escape:
			g_string_append_printf (str, "%%%02x", (gint)*s++);
			break;
		default:
			g_string_append_c (str, *s++);
			break;
		}
	}
}

/**
 * e2k_uri_encode:
 * @in: data to encode
 * @wss_encode: whether or not to use the special Web Storage System
 * encoding rules
 * @extra_enc_chars: additional characters beyond the normal URI-reserved
 * characters to encode when appending to @str
 *
 * Encodes URI-unsafe characters as in e2k_uri_append_encoded()
 *
 * Return value: the encoded string
 **/
gchar *
e2k_uri_encode (const gchar *in, gboolean wss_encode,
		const gchar *extra_enc_chars)
{
	GString *string;
	gchar *out;

	string = g_string_new (NULL);
	e2k_uri_append_encoded (string, in, wss_encode, extra_enc_chars);
	out = string->str;
	g_string_free (string, FALSE);

	return out;
}

/**
 * e2k_uri_path:
 * @uri_string: a well-formed absolute URI
 *
 * Returns the path component of @uri_string, including the initial
 * "/". (The return value is actually a pointer into the passed-in
 * string, meaning this will only really work if the URI has no
 * query/fragment/etc.)
 *
 * Return value: the path component of @uri_string.
 **/
const gchar *
e2k_uri_path (const gchar *uri_string)
{
	const gchar *p;

	p = strchr (uri_string, ':');
	if (p++) {
		if (!strncmp (p, "//", 2)) {
			p = strchr (p + 2, '/');
			if (p)
				return p;
		} else if (*p)
			return p;
	}
	return "";
}

/**
 * e2k_uri_concat:
 * @uri_prefix: an absolute URI
 * @tail: a relative path
 *
 * Constructs a new URI consisting of the concatenation of
 * @uri_prefix and @tail. If @uri_prefix does not end with a "/",
 * one will be inserted between @uri_prefix and @tail.
 *
 * Return value: the new URI
 **/
gchar *
e2k_uri_concat (const gchar *uri_prefix, const gchar *tail)
{
	const gchar *p;

	p = strrchr (uri_prefix, '/');
	if (p && !p[1])
		return g_strdup_printf ("%s%s", uri_prefix, tail);
	else
		return g_strdup_printf ("%s/%s", uri_prefix, tail);
}

/**
 * e2k_uri_relative:
 * @uri_prefix: an absolute URI
 * @uri: another URI, presumably a child of @uri_prefix
 *
 * Returns a URI describing @uri's relation to @uri_prefix; either a
 * relative URI consisting of the subpath of @uri underneath
 * @uri_prefix, or all of @uri if it is not a sub-uri of @uri_prefix.
 *
 * Return value: the relative URI
 **/
const gchar *
e2k_uri_relative (const gchar *uri_prefix, const gchar *uri)
{
	gint prefix_len = strlen (uri_prefix);

	if (!strncmp (uri_prefix, uri, prefix_len)) {
		uri += prefix_len;
		while (*uri == '/')
			uri++;
	}

	return uri;
}
