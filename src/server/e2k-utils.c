/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
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
#include "config.h"
#endif

#include "e2k-utils.h"
#include "e2k-autoconfig.h"
#include "e2k-propnames.h"
#include "e2k-rule.h"

#include <libedataserver/e-time-utils.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Do not internationalize */
const gchar *e2k_rfc822_months [] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/**
 * e2k_parse_timestamp:
 * @timestamp: an ISO8601 timestamp returned by the Exchange server
 *
 * Converts @timestamp to a %time_t value. @timestamp must be in one
 * of the two ISO8601 variants used by Exchange.
 *
 * Note that the timestamps used (in most contexts) by Exchange have
 * millisecond resolution, so converting them to %time_t loses
 * resolution. Since ISO8601 timestamps can be compared using
 * strcmp(), it is often best to keep them as strings.
 *
 * Return value: the %time_t corresponding to @timestamp, or -1 on
 * error.
 **/
time_t
e2k_parse_timestamp (const gchar *timestamp)
{
	struct tm tm;

	tm.tm_year = strtoul (timestamp, (gchar **)&timestamp, 10) - 1900;
	if (*timestamp++ != '-')
		return -1;
	tm.tm_mon = strtoul (timestamp, (gchar **)&timestamp, 10) - 1;
	if (*timestamp++ != '-')
		return -1;
	tm.tm_mday = strtoul (timestamp, (gchar **)&timestamp, 10);
	if (*timestamp++ != 'T')
		return -1;
	tm.tm_hour = strtoul (timestamp, (gchar **)&timestamp, 10);
	if (*timestamp++ != ':')
		return -1;
	tm.tm_min = strtoul (timestamp, (gchar **)&timestamp, 10);
	if (*timestamp++ != ':')
		return -1;
	tm.tm_sec = strtoul (timestamp, (gchar **)&timestamp, 10);
	if (*timestamp != '.' && *timestamp != 'Z')
		return -1;

	return e_mktime_utc (&tm);
}

/**
 * e2k_make_timestamp:
 * @when: the %time_t to convert to an ISO8601 timestamp
 *
 * Creates an ISO8601 timestamp (in an format acceptable to Exchange)
 * corresponding to @when.
 *
 * Return value: the timestamp, which the caller must free.
 **/
gchar *
e2k_make_timestamp (time_t when)
{
	struct tm *tm;

	tm = gmtime (&when);
	return g_strdup_printf ("%04d-%02d-%02dT%02d:%02d:%02dZ",
				tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/**
 * e2k_make_timestamp_rfc822:
 * @when: the %time_t to convert to an RFC822 timestamp
 *
 * Creates an RFC822 Date header value corresponding to @when, in the
 * locale timezone.
 *
 * Return value: the timestamp, which the caller must free.
 **/
gchar *
e2k_make_timestamp_rfc822 (time_t when)
{
	struct tm tm;
	gint offset;

	e_localtime_with_offset (when, &tm, &offset);
	offset = (offset / 3600) * 100 + (offset / 60) % 60;

	return g_strdup_printf ("%02d %s %04d %02d:%02d:%02d %+05d",
				tm.tm_mday, e2k_rfc822_months[tm.tm_mon],
				tm.tm_year + 1900,
				tm.tm_hour, tm.tm_min, tm.tm_sec,
				offset);
}

/* SYSTIME_OFFSET is the number of minutes between the Windows epoch
 * (1601-01-01T00:00:00Z) and the time_t epoch (1970-01-01T00:00:00Z):
 * 369 years, 89 of which are leap years.
 */
#define SYSTIME_OFFSET 194074560UL

/**
 * e2k_systime_to_time_t:
 * @systime: a MAPI PT_SYSTIME value (minutes since Windows epoch)
 *
 * Converts the MAPI PT_SYSTIME value @systime to a corresponding
 * %time_t value (assuming it is within the valid range of a %time_t).
 *
 * Return value: a %time_t corresponding to @systime.
 **/
time_t
e2k_systime_to_time_t (guint32 systime)
{
	return (systime - SYSTIME_OFFSET) * 60;
}

/**
 * e2k_systime_from_time_t:
 * @tt: a %time_t value
 *
 * Converts the %time_t value @tt to a corresponding MAPI PT_SYSTIME
 * value, losing some precision if @tt does not fall on a minute
 * boundary.
 *
 * Return value: the Windows systime value corresponding to @tt
 **/
guint32
e2k_systime_from_time_t (time_t tt)
{
	return (tt / 60) + SYSTIME_OFFSET;
}

/**
 * e2k_filetime_to_time_t:
 * @filetime: a Windows FILETIME value (100ns intervals since
 * Windows epoch)
 *
 * Converts the Windows FILETIME value @filetime to a corresponding
 * %time_t value (assuming it is within the valid range of a %time_t),
 * truncating to a second boundary.
 *
 * Return value: a %time_t corresponding to @filetime.
 **/
time_t
e2k_filetime_to_time_t (guint64 filetime)
{
	return (time_t)(filetime / 10000000 - SYSTIME_OFFSET * 60);
}

/**
 * e2k_filetime_from_time_t:
 * @tt: a %time_t value
 *
 * Converts the %time_t value @tt to a corresponding Windows FILETIME
 * value.
 *
 * Return value: the Windows FILETIME value corresponding to @tt
 **/
guint64
e2k_filetime_from_time_t (time_t tt)
{
	return (((guint64)tt) + ((guint64)SYSTIME_OFFSET) * 60) * 10000000;
}

/**
 * e2k_lf_to_crlf:
 * @in: input text in UNIX ("\n") format
 *
 * Creates a copy of @in with all LFs converted to CRLFs.
 *
 * Return value: the converted text, which the caller must free.
 **/
gchar *
e2k_lf_to_crlf (const gchar *in)
{
	gint len;
	const gchar *s;
	gchar *out, *d;

	g_return_val_if_fail (in != NULL, NULL);

	len = strlen (in);
	for (s = strchr (in, '\n'); s; s = strchr (s + 1, '\n'))
		len++;

	out = g_malloc (len + 1);
	for (s = in, d = out; *s; s++) {
		if (*s == '\n')
			*d++ = '\r';
		*d++ = *s;
	}
	*d = '\0';

	return out;
}

/**
 * e2k_crlf_to_lf:
 * @in: input text in network ("\r\n") format
 *
 * Creates a copy of @in with all CRLFs converted to LFs. (Actually,
 * it just strips CRs, so any raw CRs will be removed.)
 *
 * Return value: the converted text, which the caller must free.
 **/
gchar *
e2k_crlf_to_lf (const gchar *in)
{
	gint len;
	const gchar *s;
	gchar *out;
	GString *str;

	g_return_val_if_fail (in != NULL, NULL);

	str = g_string_new ("");

	len = strlen (in);
	for (s = in; *s; s++) {
		if (*s != '\r')
			str = g_string_append_c (str, *s);
	}

	out = str->str;
	g_string_free (str, FALSE);

	return out;
}

/**
 * e2k_strdup_with_trailing_slash:
 * @path: a URI or path
 *
 * Copies @path, appending a "/" to it if and only if it did not
 * already end in "/".
 *
 * Return value: the path, which the caller must free
 **/
gchar *
e2k_strdup_with_trailing_slash (const gchar *path)
{
	gchar *p;

	if (!path || !*path)
		return NULL;

	p = strrchr (path, '/');
	if (p && !p[1])
		return g_strdup (path);
	else
		return g_strdup_printf ("%s/", path);
}

/**
 * e2k_entryid_to_dn:
 * @entryid: an Exchange entryid
 *
 * Finds an Exchange 5.5 DN inside a binary entryid property (such as
 * #PR_STORE_ENTRYID or an element of #PR_DELEGATES_ENTRYIDS).
 *
 * Return value: the entryid, which is a pointer into @entryid's data.
 **/
const gchar *
e2k_entryid_to_dn (GByteArray *entryid)
{
	gchar *p;

	p = ((gchar *)entryid->data) + entryid->len - 1;
	if (*p == 0) {
		while (*(p - 1) && p > (gchar *)entryid->data)
			p--;
		if (*p == '/')
			return p;
	}
	return NULL;
}

static void
append_permanenturl_section (GString *url, guint8 *entryid)
{
	gint i = 0;

	/* First part */
	while (i < 16)
		g_string_append_printf (url, "%02x", entryid[i++]);

	/* Replace 0s with a single '-' */
	g_string_append_c (url, '-');
	while (i < 22 && entryid[i] == 0)
		i++;

	/* Last part; note that if the first non-0 byte can be
	 * expressed in a single hex digit, we do so. (ie, the 0
	 * in the 16's place was also accumulated into the
	 * preceding '-'.)
	 */
	if (i < 22 && entryid[i] < 0x10)
		g_string_append_printf (url, "%01x", entryid[i++]);
	while (i < 22)
		g_string_append_printf (url, "%02x", entryid[i++]);
}

#define E2K_PERMANENTURL_INFIX "-FlatUrlSpace-"
#define E2K_PERMANENTURL_INFIX_LEN (sizeof (E2K_PERMANENTURL_INFIX) - 1)

/**
 * e2k_entryid_to_permanenturl:
 * @entryid: an ENTRYID (specifically, a PR_SOURCE_KEY)
 * @base_uri: base URI of the store containing @entryid
 *
 * Creates a permanenturl based on @entryid and @base_uri.
 *
 * Return value: the permanenturl, which the caller must free.
 **/
gchar *
e2k_entryid_to_permanenturl (GByteArray *entryid, const gchar *base_uri)
{
	GString *url;
	gchar *ret;

	g_return_val_if_fail (entryid->len == 22 || entryid->len == 44, NULL);

	url = g_string_new (base_uri);
	if (url->str[url->len - 1] != '/')
		g_string_append_c (url, '/');
	g_string_append (url, E2K_PERMANENTURL_INFIX);
	g_string_append_c (url, '/');

	append_permanenturl_section (url, entryid->data);
	if (entryid->len > 22) {
		g_string_append_c (url, '/');
		append_permanenturl_section (url, entryid->data + 22);
	}

	ret = url->str;
	g_string_free (url, FALSE);
	return ret;
}

#define HEXVAL(c) (isdigit (c) ? (c) - '0' : g_ascii_tolower (c) - 'a' + 10)

static gboolean
append_entryid_section (GByteArray *entryid, const gchar **permanenturl)
{
	const gchar *p;
	guint8 buf[44], byte;
	gint endlen;

	p = *permanenturl;
	if (strspn (p, "0123456789abcdefABCDEF") != 32)
		return FALSE;
	if (p[32] != '-')
		return FALSE;
	endlen = strspn (p + 33, "0123456789abcdefABCDEF");
	if (endlen > 6)
		return FALSE;

	/* Expand to the full form by replacing the "-" with "0"s */
	memcpy (buf, p, 32);
	memset (buf + 32, '0', sizeof (buf) - 32 - endlen);
	memcpy (buf + sizeof (buf) - endlen, p + 33, endlen);

	p = (gchar *) buf;
	while (p < (gchar *) buf + sizeof (buf)) {
		byte = (HEXVAL (*p) << 4) + HEXVAL (*(p + 1));
		g_byte_array_append (entryid, &byte, 1);
		p += 2;
	}

	*permanenturl += 33 + endlen;
	return TRUE;
}

/**
 * e2k_permanenturl_to_entryid:
 * @permanenturl: an Exchange permanenturl
 *
 * Creates an ENTRYID (specifically, a PR_SOURCE_KEY) based on
 * @permanenturl
 *
 * Return value: the entryid
 **/
GByteArray *
e2k_permanenturl_to_entryid (const gchar *permanenturl)
{
	GByteArray *entryid;

	permanenturl = strstr (permanenturl, E2K_PERMANENTURL_INFIX);
	if (!permanenturl)
		return NULL;
	permanenturl += E2K_PERMANENTURL_INFIX_LEN;

	entryid = g_byte_array_new ();
	while (*permanenturl++ == '/') {
		if (!append_entryid_section (entryid, &permanenturl)) {
			g_byte_array_free (entryid, TRUE);
			return NULL;
		}
	}

	return entryid;
}

/**
 * e2k_ascii_strcase_equal
 * @v: a string
 * @v2: another string
 *
 * ASCII-case-insensitive comparison function for use with #GHashTable.
 *
 * Return value: %TRUE if @v and @v2 are ASCII-case-insensitively
 * equal, %FALSE if not.
 **/
gint
e2k_ascii_strcase_equal (gconstpointer v, gconstpointer v2)
{
	return !g_ascii_strcasecmp (v, v2);
}

/**
 * e2k_ascii_strcase_hash
 * @v: a string
 *
 * ASCII-case-insensitive hash function for use with #GHashTable.
 *
 * Return value: An ASCII-case-insensitive hashing of @v.
 **/
guint
e2k_ascii_strcase_hash (gconstpointer v)
{
	/* case-insensitive g_str_hash */

	const guchar *p = v;
	guint h = g_ascii_tolower (*p);

	if (h) {
		for (p += 1; *p != '\0'; p++)
			h = (h << 5) - h + g_ascii_tolower (*p);
	}

	return h;
}

/**
 * e2k_restriction_folders_only:
 * @rn: a restriction
 *
 * Examines @rn, and determines if it can only return folders
 *
 * Return value: %TRUE if @rn will cause only folders to be returned
 **/
gboolean
e2k_restriction_folders_only (E2kRestriction *rn)
{
	gint i;

	if (!rn)
		return FALSE;

	switch (rn->type) {
	case E2K_RESTRICTION_PROPERTY:
		if (strcmp (rn->res.property.pv.prop.name,
			    E2K_PR_DAV_IS_COLLECTION) != 0)
			return FALSE;

		/* return TRUE if it's "= TRUE" or "!= FALSE" */
		return (rn->res.property.relop == E2K_RELOP_EQ) ==
			(rn->res.property.pv.value != NULL);

	case E2K_RESTRICTION_AND:
		for (i = 0; i < rn->res.and.nrns; i++) {
			if (e2k_restriction_folders_only (rn->res.and.rns[i]))
				return TRUE;
		}
		return FALSE;

	case E2K_RESTRICTION_OR:
		for (i = 0; i < rn->res.or.nrns; i++) {
			if (!e2k_restriction_folders_only (rn->res.or.rns[i]))
				return FALSE;
		}
		return TRUE;

	case E2K_RESTRICTION_NOT:
		return e2k_restriction_folders_only (rn->res.not.rn);

	case E2K_RESTRICTION_COMMENT:
		return e2k_restriction_folders_only (rn->res.comment.rn);

	default:
		return FALSE;
	}
}

/* From MAPIDEFS.H */
static const gchar MAPI_ONE_OFF_UID[] = {
	0x81, 0x2b, 0x1f, 0xa4, 0xbe, 0xa3, 0x10, 0x19,
	0x9d, 0x6e, 0x00, 0xdd, 0x01, 0x0f, 0x54, 0x02
};
#define MAPI_ONE_OFF_UNICODE	  0x8000
#define MAPI_ONE_OFF_NO_RICH_INFO 0x0001
#define MAPI_ONE_OFF_MYSTERY_FLAG 0x1000

/**
 * e2k_entryid_generate_oneoff:
 * @display_name: the display name of the user
 * @email: the email address
 * @unicode: %TRUE to generate a Unicode ENTRYID (in which case
 * @display_name should be UTF-8), %FALSE for an ASCII ENTRYID.
 *
 * Constructs a "one-off" ENTRYID value that can be used as a MAPI
 * recipient (eg, for a message forwarding server-side rule),
 * corresponding to @display_name and @email.
 *
 * Return value: the recipient ENTRYID
 **/
GByteArray *
e2k_entryid_generate_oneoff (const gchar *display_name, const gchar *email, gboolean unicode)
{
	GByteArray *entryid;

	entryid = g_byte_array_new ();

	e2k_rule_append_uint32 (entryid, 0);
	g_byte_array_append (entryid, (guint8 *) MAPI_ONE_OFF_UID, sizeof (MAPI_ONE_OFF_UID));
	e2k_rule_append_uint16 (entryid, 0);
	e2k_rule_append_uint16 (entryid,
				MAPI_ONE_OFF_NO_RICH_INFO |
				MAPI_ONE_OFF_MYSTERY_FLAG |
				(unicode ? MAPI_ONE_OFF_UNICODE : 0));

	if (unicode) {
		e2k_rule_append_unicode (entryid, display_name);
		e2k_rule_append_unicode (entryid, "SMTP");
		e2k_rule_append_unicode (entryid, email);
	} else {
		e2k_rule_append_string (entryid, display_name);
		e2k_rule_append_string (entryid, "SMTP");
		e2k_rule_append_string (entryid, email);
	}

	return entryid;
}

static const gchar MAPI_LOCAL_UID[] = {
	0xdc, 0xa7, 0x40, 0xc8, 0xc0, 0x42, 0x10, 0x1a,
	0xb4, 0xb9, 0x08, 0x00, 0x2b, 0x2f, 0xe1, 0x82
};

/**
 * e2k_entryid_generate_local:
 * @exchange_dn: the Exchange 5.5-style DN of the local user
 *
 * Constructs an ENTRYID value that can be used as a MAPI
 * recipient (eg, for a message forwarding server-side rule),
 * corresponding to the local user identified by @exchange_dn.
 *
 * Return value: the recipient ENTRYID
 **/
GByteArray *
e2k_entryid_generate_local (const gchar *exchange_dn)
{
	GByteArray *entryid;

	entryid = g_byte_array_new ();

	e2k_rule_append_uint32 (entryid, 0);
	g_byte_array_append (entryid, (guint8 *) MAPI_LOCAL_UID, sizeof (MAPI_LOCAL_UID));
	e2k_rule_append_uint16 (entryid, 1);
	e2k_rule_append_uint16 (entryid, 0);
	e2k_rule_append_string (entryid, exchange_dn);

	return entryid;
}

static const gchar MAPI_CONTACT_UID[] = {
	0xfe, 0x42, 0xaa, 0x0a, 0x18, 0xc7, 0x1a, 0x10,
	0xe8, 0x85, 0x0b, 0x65, 0x1c, 0x24, 0x00, 0x00
};

/**
 * e2k_entryid_generate_contact:
 * @contact_entryid: the #PR_ENTRYID of an item in the user's Contacts
 * folder.
 * @nth_address: which of the contact's email addresses to use.
 *
 * Constructs an ENTRYID value that can be used as a MAPI recipient
 * (eg, for a message forwarding server-side rule), corresponding to
 * the Contacts folder entry identified by @contact_entryid.
 *
 * Return value: the recipient ENTRYID
 **/
GByteArray *
e2k_entryid_generate_contact (GByteArray *contact_entryid, gint nth_address)
{
	GByteArray *entryid;

	entryid = g_byte_array_new ();

	e2k_rule_append_uint32 (entryid, 0);
	g_byte_array_append (entryid, (guint8 *) MAPI_CONTACT_UID, sizeof (MAPI_CONTACT_UID));
	e2k_rule_append_uint32 (entryid, 3);
	e2k_rule_append_uint32 (entryid, 4);
	e2k_rule_append_uint32 (entryid, nth_address);
	e2k_rule_append_uint32 (entryid, contact_entryid->len);
	g_byte_array_append (entryid, contact_entryid->data, contact_entryid->len);

	return entryid;
}

/**
 * e2k_search_key_generate:
 * @addrtype: the type of @address (usually "SMTP" or "EX")
 * @address: the address data
 *
 * Constructs a PR_SEARCH_KEY value for @address
 *
 * Return value: the search key
 **/
GByteArray *
e2k_search_key_generate (const gchar *addrtype, const gchar *address)
{
	GByteArray *search_key;
	guint8 *p;

	search_key = g_byte_array_new ();
	g_byte_array_append (search_key, (guint8 *) addrtype, strlen (addrtype));
	g_byte_array_append (search_key, (guint8 *) ":", 1);
	g_byte_array_append (search_key, (guint8 *) address, strlen (address));
	g_byte_array_append (search_key, (guint8 *) "", 1);

	for (p = search_key->data; *p; p++)
		*p = g_ascii_toupper (*p);

	return search_key;
}
