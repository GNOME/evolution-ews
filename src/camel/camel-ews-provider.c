/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-ews-provider.c: EWS provider registration code */

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

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include "server/camel-sasl-xoauth2-office365.h"

#include "camel-ews-store.h"
#include "camel-ews-transport.h"

static void add_hash (guint *hash, gchar *s);
static guint ews_url_hash (gconstpointer key);
static gint ews_url_equal (gconstpointer a, gconstpointer b);

static CamelProviderConfEntry ews_conf_entries[] = {
	/* override the labels/defaults of the standard settings */

	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for new mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check-all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "listen-notifications", NULL,
	  N_("_Listen for server change notifications"), "1" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-inbox", NULL,
	  N_("_Apply filters to new messages in Inbox on this server"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk-inbox", "filter-junk",
	  N_("Only check for Junk messages in the IN_BOX folder"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "stay-synchronized", NULL,
	  N_("Synchroni_ze remote mail locally in all folders"), "0" },
	{ CAMEL_PROVIDER_CONF_PLACEHOLDER, "ews-limit-by-age-placeholder", NULL },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_SECTION_START, "connection", NULL, N_("Connection") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "timeout", NULL,
	  /* Translators: '%s' is preplaced with a widget, where "
	   * user can select how long the timeout should be. */
	  N_("Connection _timeout (in seconds) %s"), "0:1:0:32768" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "override-user-agent", NULL,
	  N_("Override _User-Agent header value"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "user-agent", "override-user-agent", "" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider ews_provider = {
	"ews",
	N_("Exchange Web Services"),

	N_("For accessing Exchange servers using Web Services"),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_EXTERNAL,

	CAMEL_URL_ALLOW_USER | CAMEL_URL_ALLOW_AUTH | CAMEL_URL_HIDDEN_HOST,

	ews_conf_entries,

	/* ... */
};

CamelServiceAuthType camel_ews_ntlm_authtype = {
	N_("NTLM"),

	N_("This option will connect to the Exchange server using a "
	   "plaintext password with NTLM authentication."),

	"",
	TRUE
};

CamelServiceAuthType camel_ews_basic_authtype = {
	N_("Basic"),

	N_("This option will connect to the Exchange server using a "
	   "plaintext password with Basic authentication."),

	"PLAIN",
	TRUE
};

CamelServiceAuthType camel_ews_gssapi_authtype = {
	N_("Kerberos"),

	N_("This option will connect to the Exchange server using a "
	   "Kerberos/GSSAPI authentication."),

	"GSSAPI",
	FALSE
};

void
camel_provider_module_init (void)
{
	ews_provider.url_hash = ews_url_hash;
	ews_provider.url_equal = ews_url_equal;
	ews_provider.authtypes = g_list_append (g_list_append (g_list_append (NULL,
		&camel_ews_ntlm_authtype),
		&camel_ews_basic_authtype),
		&camel_ews_gssapi_authtype);
	ews_provider.translation_domain = GETTEXT_PACKAGE;

	ews_provider.object_types[CAMEL_PROVIDER_STORE] =  CAMEL_TYPE_EWS_STORE;
	ews_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = CAMEL_TYPE_EWS_TRANSPORT;

	bindtextdomain (GETTEXT_PACKAGE, EXCHANGE_EWS_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	g_type_ensure (CAMEL_TYPE_SASL_XOAUTH2_OFFICE365);

	camel_provider_register (&ews_provider);
}

static void
add_hash (guint *hash,
          gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
ews_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *) key;
	guint hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
ews_url_equal (gconstpointer a,
               gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return ((g_strcmp0 (u1->protocol, u2->protocol) == 0)
		&& (g_strcmp0 (u1->user, u2->user) == 0)
		&& (g_strcmp0 (u1->host, u2->host) == 0)
		&& (u1->port == u2->port));
}
