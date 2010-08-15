/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

#include "e2k-sid.h"

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libxml/xmlmemory.h>

typedef struct {
	guint8  Revision;
	guint8  SubAuthorityCount;
	guint8  zero_pad[5];
	guint8  IdentifierAuthority;
	guint32 SubAuthority[1];
} E2kSid_SID;
#define E2K_SID_SID_REVISION 1

struct _E2kSidPrivate {
	E2kSidType type;
	E2kSid_SID *binary_sid;
	gchar *string_sid;
	gchar *display_name;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

static void dispose (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class->dispose = dispose;
}

static void
init (GObject *object)
{
	E2kSid *sid = E2K_SID (object);

	sid->priv = g_new0 (E2kSidPrivate, 1);
}

static void
dispose (GObject *object)
{
	E2kSid *sid = E2K_SID (object);

	if (sid->priv) {
		if (sid->priv->string_sid)
			g_free (sid->priv->string_sid);
		if (sid->priv->binary_sid)
			g_free (sid->priv->binary_sid);
		g_free (sid->priv->display_name);

		g_free (sid->priv);
		sid->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

E2K_MAKE_TYPE (e2k_sid, E2kSid, class_init, init, PARENT_TYPE)

static E2kSid *
sid_new_internal (E2kSidType type, const gchar *display_name,
		  const gchar *string_sid, const guint8 *binary_sid)
{
	E2kSid *sid;

	sid = g_object_new (E2K_TYPE_SID, NULL);
	sid->priv->type = type;

	if (binary_sid)
		sid->priv->binary_sid = g_memdup (binary_sid, E2K_SID_BINARY_SID_LEN (binary_sid));
	if (string_sid)
		sid->priv->string_sid = g_strdup (string_sid);
	else if (!display_name)
		e2k_sid_get_string_sid (sid);

	if (!display_name) {
		if (type == E2K_SID_TYPE_WELL_KNOWN_GROUP) {
			if (!strcmp (string_sid, E2K_SID_WKS_ANONYMOUS))
				display_name = _(E2K_SID_WKS_ANONYMOUS_NAME);
			else if (!strcmp (string_sid, E2K_SID_WKS_EVERYONE))
				display_name = _(E2K_SID_WKS_EVERYONE_NAME);
		}
		if (!display_name)
			display_name = string_sid;
	}
	sid->priv->display_name = g_strdup (display_name);

	return sid;
}

/**
 * e2k_sid_new_from_string_sid:
 * @type: the type of SID that @string_sid is
 * @string_sid: the string form of a Windows Security Identifier
 * @display_name: UTF-8 display name of the user/group/etc identified
 * by @string_sid
 *
 * Creates an %E2kSid from the given information
 *
 * Return value: the new SID
 **/
E2kSid *
e2k_sid_new_from_string_sid (E2kSidType type, const gchar *string_sid,
			     const gchar *display_name)
{
	g_return_val_if_fail (string_sid != NULL, NULL);

	if (strlen (string_sid) < 6 || strncmp (string_sid, "S-1-", 4) != 0)
		return NULL;

	return sid_new_internal (type, display_name, string_sid, NULL);
}

/**
 * e2k_sid_new_from_binary_sid:
 * @type: the type of SID that @binary_sid is
 * @binary_sid: the binary form of a Windows Security Identifier
 * @display_name: UTF-8 display name of the user/group/etc identified
 * by @string_sid
 *
 * Creates an %E2kSid from the given information
 *
 * Return value: the new SID
 **/
E2kSid *
e2k_sid_new_from_binary_sid (E2kSidType    type,
			     const guint8 *binary_sid,
			     const gchar   *display_name)
{
	g_return_val_if_fail (binary_sid != NULL, NULL);

	return sid_new_internal (type, display_name, NULL, binary_sid);
}

/**
 * e2k_sid_get_sid_type:
 * @sid: a SID
 *
 * Returns the type of @sid (user, group, etc)
 *
 * Return value: the %E2kSidType
 **/
E2kSidType
e2k_sid_get_sid_type (E2kSid *sid)
{
	g_return_val_if_fail (E2K_IS_SID (sid), E2K_SID_TYPE_USER);

	return sid->priv->type;
}

/**
 * e2k_sid_get_string_sid:
 * @sid: a SID
 *
 * Returns the string form of @sid
 *
 * Return value: the string SID
 **/
const gchar *
e2k_sid_get_string_sid (E2kSid *sid)
{
	g_return_val_if_fail (E2K_IS_SID (sid), NULL);

	if (!sid->priv->string_sid) {
		GString *string;
		gint sa;

		string = g_string_new (NULL);

		/* Revision and IdentifierAuthority. */
		g_string_append_printf (string, "S-%u-%u",
					sid->priv->binary_sid->Revision,
					sid->priv->binary_sid->IdentifierAuthority);

		/* Subauthorities. */
		for (sa = 0; sa < sid->priv->binary_sid->SubAuthorityCount; sa++) {
			g_string_append_printf (string, "-%lu",
						(gulong) GUINT32_FROM_LE (sid->priv->binary_sid->SubAuthority[sa]));
		}

		sid->priv->string_sid = string->str;
		g_string_free (string, FALSE);
	}

	return sid->priv->string_sid;
}

/**
 * e2k_sid_get_binary_sid:
 * @sid: a SID
 *
 * Returns the binary form of @sid. Since the SID data is self-delimiting,
 * no length value is needed. Use E2K_SID_BINARY_SID_LEN() if you need to
 * know the size of the binary data.
 *
 * Return value: the binary SID
 **/
const guint8 *
e2k_sid_get_binary_sid (E2kSid *sid)
{
	g_return_val_if_fail (E2K_IS_SID (sid), NULL);

	if (!sid->priv->binary_sid) {
		gint sa, subauth_count;
		guint32 subauthority;
		gchar *p;

		p = sid->priv->string_sid + 4;
		subauth_count = 0;
		while ((p = strchr (p, '-'))) {
			subauth_count++;
			p++;
		}

		sid->priv->binary_sid = g_malloc0 (sizeof (E2kSid_SID) + 4 * (subauth_count - 1));
		sid->priv->binary_sid->Revision = E2K_SID_SID_REVISION;
		sid->priv->binary_sid->IdentifierAuthority = strtoul (sid->priv->string_sid + 4, &p, 10);
		sid->priv->binary_sid->SubAuthorityCount = subauth_count;

		sa = 0;
		while (*p == '-' && sa < subauth_count) {
			subauthority = strtoul (p + 1, &p, 10);
			sid->priv->binary_sid->SubAuthority[sa++] =
				GUINT32_TO_LE (subauthority);
		}
	}

	return (guint8 *)sid->priv->binary_sid;
}

/**
 * e2k_sid_get_display_name:
 * @sid: a SID
 *
 * Returns the display name of the entity identified by @sid
 *
 * Return value: the UTF-8 display name
 **/
const gchar *
e2k_sid_get_display_name (E2kSid *sid)
{
	g_return_val_if_fail (E2K_IS_SID (sid), NULL);

	return sid->priv->display_name;
}

/**
 * e2k_sid_binary_sid_equal:
 * @a: pointer to a binary SID
 * @b: pointer to another binary SID
 *
 * Determines if @a and @b contain the same SID data. For use
 * with #GHashTable.
 *
 * Return value: %TRUE or %FALSE
 **/
gint
e2k_sid_binary_sid_equal (gconstpointer a, gconstpointer b)
{
	const guint8 *bsida = (const guint8 *)a;
	const guint8 *bsidb = (const guint8 *)b;

	if (E2K_SID_BINARY_SID_LEN (bsida) !=
	    E2K_SID_BINARY_SID_LEN (bsidb))
		return FALSE;
	return memcmp (bsida, bsidb, E2K_SID_BINARY_SID_LEN (bsida)) == 0;
}

/**
 * e2k_sid_binary_sid_hash:
 * @key: pointer to a binary SID
 *
 * Hashes @key, a binary SID. For use with #GHashTable.
 *
 * Return value: the hash value
 **/
guint
e2k_sid_binary_sid_hash (gconstpointer key)
{
	const guint8 *bsid = (const guint8 *)key;
	guint32 final_sa;

	/* The majority of SIDs will differ only in the last
	 * subauthority value.
	 */
	memcpy (&final_sa, bsid + E2K_SID_BINARY_SID_LEN (bsid) - 4, 4);
	return final_sa;
}
