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

#ifndef CAMEL_SASL_XOAUTH2_OFFICE365_H
#define CAMEL_SASL_XOAUTH2_OFFICE365_H

#include <gmodule.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_XOAUTH2_OFFICE365 \
	(camel_sasl_xoauth2_office365_get_type ())
#define CAMEL_SASL_XOAUTH2_OFFICE365(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_OFFICE365, CamelSaslXOAuth2Office365))
#define CAMEL_SASL_XOAUTH2_OFFICE365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_XOAUTH2_OFFICE365, CamelSaslXOAuth2Office365Class))
#define CAMEL_IS_SASL_XOAUTH2_OFFICE365(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_OFFICE365))
#define CAMEL_IS_SASL_XOAUTH2_OFFICE365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_XOAUTH2_OFFICE365))
#define CAMEL_SASL_XOAUTH2_OFFICE365_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_OFFICE365, CamelSaslXOAuth2Office365Class))

G_BEGIN_DECLS

typedef struct _CamelSaslXOAuth2Office365 CamelSaslXOAuth2Office365;
typedef struct _CamelSaslXOAuth2Office365Class CamelSaslXOAuth2Office365Class;
typedef struct _CamelSaslXOAuth2Office365Private CamelSaslXOAuth2Office365Private;

struct _CamelSaslXOAuth2Office365 {
	CamelSaslXOAuth2 parent;
	CamelSaslXOAuth2Office365Private *priv;
};

struct _CamelSaslXOAuth2Office365Class {
	CamelSaslXOAuth2Class parent_class;
};

GType		camel_sasl_xoauth2_office365_get_type	(void) G_GNUC_CONST;

void		camel_sasl_xoauth2_office365_type_register
							(GTypeModule *type_module);

G_END_DECLS

#endif /* CAMEL_SASL_XOAUTH2_OFFICE365_H */
