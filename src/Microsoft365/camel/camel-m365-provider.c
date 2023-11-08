/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include "common/camel-sasl-xoauth2-microsoft365.h"

#include "camel-m365-store.h"
#include "camel-m365-transport.h"

static void add_hash (guint *hash, gchar *s);
static guint m365_url_hash (gconstpointer key);
static gint m365_url_equal (gconstpointer a, gconstpointer b);

static CamelProviderConfEntry m365_conf_entries[] = {
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
	{ CAMEL_PROVIDER_CONF_PLACEHOLDER, "m365-limit-by-age-placeholder", NULL },
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

static CamelProvider m365_provider = {
	"microsoft365",
	N_("Microsoft 365"),

	N_("For accessing Microsoft 365 server"),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_EXTERNAL,

	CAMEL_URL_ALLOW_USER | CAMEL_URL_ALLOW_AUTH | CAMEL_URL_HIDDEN_HOST,

	m365_conf_entries,

	/* ... */
};

void
camel_provider_module_init (void)
{
	bindtextdomain (GETTEXT_PACKAGE, M365_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	m365_provider.url_hash = m365_url_hash;
	m365_provider.url_equal = m365_url_equal;
	m365_provider.authtypes = NULL;
	m365_provider.translation_domain = GETTEXT_PACKAGE;
	m365_provider.object_types[CAMEL_PROVIDER_STORE] =  CAMEL_TYPE_M365_STORE;
	m365_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = CAMEL_TYPE_M365_TRANSPORT;

	g_type_ensure (CAMEL_TYPE_SASL_XOAUTH2_MICROSOFT365);

	camel_provider_register (&m365_provider);
}

static void
add_hash (guint *hash,
          gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
m365_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *) key;
	guint hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
m365_url_equal (gconstpointer a,
		gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return ((g_strcmp0 (u1->protocol, u2->protocol) == 0)
		&& (g_strcmp0 (u1->user, u2->user) == 0)
		&& (g_strcmp0 (u1->host, u2->host) == 0)
		&& (u1->port == u2->port));
}
