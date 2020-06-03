/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include "camel-o365-store.h"
#include "camel-o365-transport.h"

static void add_hash (guint *hash, gchar *s);
static guint o365_url_hash (gconstpointer key);
static gint o365_url_equal (gconstpointer a, gconstpointer b);

static CamelProviderConfEntry o365_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for new mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check-all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
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
	{ CAMEL_PROVIDER_CONF_PLACEHOLDER, "o365-limit-by-age-placeholder", NULL },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_SECTION_START, "connection", NULL, N_("Connection") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "timeout", NULL,
	  /* Translators: '%s' is preplaced with a widget, where "
	   * user can select how long the timeout should be. */
	  N_("Connection _timeout (in seconds) %s"), "0:1:0:32768" },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "concurrent-connections", NULL,
	  N_("Numbe_r of concurrent connections to use"), "y:1:1:7" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider o365_provider = {
	"office365",
	N_("Office 365"),

	N_("For accessing Microsoft Office 365 server"),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_EXTERNAL,

	CAMEL_URL_ALLOW_USER | CAMEL_URL_ALLOW_AUTH | CAMEL_URL_HIDDEN_HOST,

	o365_conf_entries,

	/* ... */
};

void
camel_provider_module_init (void)
{
	bindtextdomain (GETTEXT_PACKAGE, O365_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	o365_provider.url_hash = o365_url_hash;
	o365_provider.url_equal = o365_url_equal;
	o365_provider.authtypes = NULL;
	o365_provider.translation_domain = GETTEXT_PACKAGE;
	o365_provider.object_types[CAMEL_PROVIDER_STORE] =  CAMEL_TYPE_O365_STORE;
	o365_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = CAMEL_TYPE_O365_TRANSPORT;

	camel_provider_register (&o365_provider);
}

static void
add_hash (guint *hash,
          gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
o365_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *) key;
	guint hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
o365_url_equal (gconstpointer a,
		gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return ((g_strcmp0 (u1->protocol, u2->protocol) == 0)
		&& (g_strcmp0 (u1->user, u2->user) == 0)
		&& (g_strcmp0 (u1->host, u2->host) == 0)
		&& (u1->port == u2->port));
}
