/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
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
