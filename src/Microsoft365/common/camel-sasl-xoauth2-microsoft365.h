/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_SASL_XOAUTH2_MICROSOFT365_H
#define CAMEL_SASL_XOAUTH2_MICROSOFT365_H

#include <gmodule.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_XOAUTH2_MICROSOFT365 \
	(camel_sasl_xoauth2_microsoft365_get_type ())
#define CAMEL_SASL_XOAUTH2_MICROSOFT365(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_MICROSOFT365, CamelSaslXOAuth2Microsoft365))
#define CAMEL_SASL_XOAUTH2_MICROSOFT365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_XOAUTH2_MICROSOFT365, CamelSaslXOAuth2Microsoft365Class))
#define CAMEL_IS_SASL_XOAUTH2_MICROSOFT365(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_MICROSOFT365))
#define CAMEL_IS_SASL_XOAUTH2_MICROSOFT365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_XOAUTH2_MICROSOFT365))
#define CAMEL_SASL_XOAUTH2_MICROSOFT365_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_XOAUTH2_MICROSOFT365, CamelSaslXOAuth2Microsoft365Class))

G_BEGIN_DECLS

typedef struct _CamelSaslXOAuth2Microsoft365 CamelSaslXOAuth2Microsoft365;
typedef struct _CamelSaslXOAuth2Microsoft365Class CamelSaslXOAuth2Microsoft365Class;
typedef struct _CamelSaslXOAuth2Microsoft365Private CamelSaslXOAuth2Microsoft365Private;

struct _CamelSaslXOAuth2Microsoft365 {
	CamelSaslXOAuth2 parent;
	CamelSaslXOAuth2Microsoft365Private *priv;
};

struct _CamelSaslXOAuth2Microsoft365Class {
	CamelSaslXOAuth2Class parent_class;
};

GType		camel_sasl_xoauth2_microsoft365_get_type(void) G_GNUC_CONST;

void		camel_sasl_xoauth2_microsoft365_type_register
							(GTypeModule *type_module);

G_END_DECLS

#endif /* CAMEL_SASL_XOAUTH2_MICROSOFT365_H */
