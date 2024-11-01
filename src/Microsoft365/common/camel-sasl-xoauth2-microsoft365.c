/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "camel-sasl-xoauth2-microsoft365.h"

static CamelServiceAuthType sasl_xoauth2_microsoft365_auth_type = {
	N_("OAuth2 (Microsoft 365)"),
	N_("This option will use an OAuth 2.0 access token to connect to the Microsoft 365 server"),
	"Microsoft365",
	FALSE
};

G_DEFINE_DYNAMIC_TYPE (CamelSaslXOAuth2Microsoft365, camel_sasl_xoauth2_microsoft365, CAMEL_TYPE_SASL_XOAUTH2)

static void
camel_sasl_xoauth2_microsoft365_class_init (CamelSaslXOAuth2Microsoft365Class *klass)
{
	CamelSaslClass *sasl_class;

	sasl_class = CAMEL_SASL_CLASS (klass);
	sasl_class->auth_type = &sasl_xoauth2_microsoft365_auth_type;
}

static void
camel_sasl_xoauth2_microsoft365_class_finalize (CamelSaslXOAuth2Microsoft365Class *klass)
{
}

static void
camel_sasl_xoauth2_microsoft365_init (CamelSaslXOAuth2Microsoft365 *sasl)
{
}

void
camel_sasl_xoauth2_microsoft365_type_register (GTypeModule *type_module)
{
	camel_sasl_xoauth2_microsoft365_register_type (type_module);
}
