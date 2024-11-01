/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "camel-sasl-xoauth2-office365.h"

static CamelServiceAuthType sasl_xoauth2_office365_auth_type = {
	N_("OAuth2 (Office365)"),
	N_("This option will use an OAuth 2.0 "
	   "access token to connect to the Office365.com server"),
	"Office365",
	FALSE
};

G_DEFINE_DYNAMIC_TYPE (CamelSaslXOAuth2Office365, camel_sasl_xoauth2_office365, CAMEL_TYPE_SASL_XOAUTH2)

static void
camel_sasl_xoauth2_office365_class_init (CamelSaslXOAuth2Office365Class *klass)
{
	CamelSaslClass *sasl_class;

	sasl_class = CAMEL_SASL_CLASS (klass);
	sasl_class->auth_type = &sasl_xoauth2_office365_auth_type;
}

static void
camel_sasl_xoauth2_office365_class_finalize (CamelSaslXOAuth2Office365Class *klass)
{
}

static void
camel_sasl_xoauth2_office365_init (CamelSaslXOAuth2Office365 *sasl)
{
}

void
camel_sasl_xoauth2_office365_type_register (GTypeModule *type_module)
{
	camel_sasl_xoauth2_office365_register_type (type_module);
}
