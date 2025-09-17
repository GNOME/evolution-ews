/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 * SPDX-FileContributor: Sivaiah Nallagatla <snallagatla@novell.com>
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <libedataserver/libedataserver.h>

#include "common/camel-sasl-xoauth2-office365.h"

#include "camel-ews-store.h"
#include "camel-ews-transport.h"

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
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "show-public-folders", NULL,
	  N_("Show _Public Folders beside user folders"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_SECTION_START, "connection", NULL, N_("Connection") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "timeout", NULL,
	  /* Translators: '%s' is preplaced with a widget, where "
	   * user can select how long the timeout should be. */
	  N_("Connection _timeout (in seconds) %s"), "0:1:0:32768" },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "concurrent-connections", NULL,
	  N_("Numbe_r of concurrent connections to use"), "y:1:1:7" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "override-user-agent", NULL,
	  N_("Override _User-Agent header value"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "user-agent", "override-user-agent", "" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "force-http1", NULL,
	  N_("Al_ways connect with HTTP/1"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider ews_provider = {
	.protocol = "ews",
	.name = N_("Exchange Web Services"),
	.description = N_("For accessing Exchange servers using Web Services"),
	.domain = "mail",
	.flags = CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE | CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_EXTERNAL,
	.url_flags = CAMEL_URL_ALLOW_USER | CAMEL_URL_ALLOW_AUTH | CAMEL_URL_HIDDEN_HOST,
	.extra_conf = ews_conf_entries,
};

CamelServiceAuthType camel_ews_ntlm_authtype = {
	.name = N_("NTLM"),
	.description = N_("This option will connect to the Exchange server using a "
			  "plaintext password with NTLM authentication."),
	.authproto = "",
	.need_password = TRUE
};

CamelServiceAuthType camel_ews_basic_authtype = {
	.name = N_("Basic"),
	.description = N_("This option will connect to the Exchange server using a "
			  "plaintext password with Basic authentication."),
	.authproto = "PLAIN",
	.need_password = TRUE
};

CamelServiceAuthType camel_ews_gssapi_authtype = {
	.name = N_("Kerberos"),
	.description = N_("This option will connect to the Exchange server using a "
			  "Kerberos/GSSAPI authentication."),
	.authproto = "GSSAPI",
	.need_password = FALSE
};

void
camel_provider_module_init (void)
{
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

	/* Hide the option when not supported */
	if (!e_soup_session_util_get_force_http1_supported ()) {
		gint ii;

		for (ii = G_N_ELEMENTS (ews_conf_entries) - 1; ii >= 0 ; ii--) {
			if (ews_conf_entries[ii].type == CAMEL_PROVIDER_CONF_CHECKBOX &&
			    g_strcmp0 (ews_conf_entries[ii].name, "force-http1") == 0) {
				ews_conf_entries[ii].type = CAMEL_PROVIDER_CONF_HIDDEN;
				break;
			}
		}
	}

	camel_provider_register (&ews_provider);
}
