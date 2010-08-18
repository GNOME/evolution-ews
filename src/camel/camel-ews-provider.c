/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-provider.c: GroupWise provider registration code */

/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *           Sivaiah Nallagatla <snallagatla@novell.com>
 *           Rodrigo Moya <rodrigo@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include "camel-ews-store.h"
#include "camel-ews-transport.h"

static void add_hash (guint *hash, gchar *s);
static guint ews_url_hash (gconstpointer key);
static gint check_equal (gchar *s1, gchar *s2);
static gint ews_url_equal (gconstpointer a, gconstpointer b);

static CamelProviderConfEntry ews_conf_entries[] = {
	/* override the labels/defaults of the standard settings */

	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for new mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check_all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter", NULL,
	  N_("_Apply filters to new messages in Inbox on this server"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk", NULL,
	  N_("Check new messages for J_unk contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk_inbox", "filter_junk",
	  N_("Only check for Junk messages in the IN_BOX folder"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "sync_offline", NULL,
	  N_("Automatically synchroni_ze account locally"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	/* extra GroupWise  configuration settings */
	{CAMEL_PROVIDER_CONF_SECTION_START, "soapport", NULL,
	  N_("SOAP Settings") },

	{ CAMEL_PROVIDER_CONF_ENTRY, "soap_port", NULL,
	  N_("Post Office Agent SOAP _Port:"), "7191" },

	{ CAMEL_PROVIDER_CONF_HIDDEN, "auth-domain", NULL,
	  NULL, "Ews" },

	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider ews_provider = {
	"ews",
	N_("Exchange Web Services"),

	N_("For accessing Exchange servers using EWS"),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL | CAMEL_PROVIDER_DISABLE_SENT_FOLDER,

	CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH,

	ews_conf_entries,

	/* ... */
};

CamelServiceAuthType camel_ews_password_authtype = {
	N_("Password"),

	N_("This option will connect to the Exchange server using a "
	   "plaintext password."),

	"",
	TRUE
};

static gint
ews_auto_detect_cb (CamelURL *url, GHashTable **auto_detected,
			 GError **error)
{
	*auto_detected = g_hash_table_new (g_str_hash, g_str_equal);

	g_hash_table_insert (*auto_detected, g_strdup ("poa"),
			     g_strdup (url->host));

	return 0;
}

void
camel_provider_module_init(void)
{
	CamelProvider *imap_provider = NULL;
	gboolean use_imap = g_getenv ("USE_IMAP") != NULL;

	if (use_imap)
		imap_provider = camel_provider_get("imapx://", NULL);

	g_print ("\n EWS camel provider init");
	ews_provider.url_hash = ews_url_hash;
	ews_provider.url_equal = ews_url_equal;
	ews_provider.auto_detect = ews_auto_detect_cb;
	ews_provider.authtypes = g_list_prepend (ews_provider.authtypes, &camel_ews_password_authtype);
	ews_provider.translation_domain = GETTEXT_PACKAGE;

	if (use_imap)
		ews_provider.object_types[CAMEL_PROVIDER_STORE] = imap_provider->object_types [CAMEL_PROVIDER_STORE];
	else	{
		ews_provider.object_types[CAMEL_PROVIDER_STORE] =  camel_ews_store_get_type ();
		ews_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = camel_ews_transport_get_type ();
	}

	camel_provider_register (&ews_provider);
}

static void
add_hash (guint *hash, gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
ews_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *)key;
	guint hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
check_equal (gchar *s1, gchar *s2)
{
	if (s1 == NULL) {
		if (s2 == NULL)
			return TRUE;
		else
			return FALSE;
	}

	if (s2 == NULL)
		return FALSE;

	return strcmp (s1, s2) == 0;
}

static gint
ews_url_equal (gconstpointer a, gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return check_equal (u1->protocol, u2->protocol)
		&& check_equal (u1->user, u2->user)
		&& check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}
