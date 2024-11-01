/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_OAUTH2_SERVICE_MICROSOFT365_H
#define E_OAUTH2_SERVICE_MICROSOFT365_H

#include <gmodule.h>
#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_OAUTH2_SERVICE_MICROSOFT365 \
	(e_oauth2_service_microsoft365_get_type ())
#define E_OAUTH2_SERVICE_MICROSOFT365(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_OAUTH2_SERVICE_MICROSOFT365, EOAuth2ServiceMicrosoft365))
#define E_OAUTH2_SERVICE_MICROSOFT365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_OAUTH2_SERVICE_MICROSOFT365, EOAuth2ServiceMicrosoft365Class))
#define E_IS_OAUTH2_SERVICE_MICROSOFT365(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_OAUTH2_SERVICE_MICROSOFT365))
#define E_IS_OAUTH2_SERVICE_MICROSOFT365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_OAUTH2_SERVICE_MICROSOFT365))
#define E_OAUTH2_SERVICE_MICROSOFT365_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_OAUTH2_SERVICE_MICROSOFT365, EOAuth2ServiceMicrosoft365Class))

G_BEGIN_DECLS

typedef struct _EOAuth2ServiceMicrosoft365 EOAuth2ServiceMicrosoft365;
typedef struct _EOAuth2ServiceMicrosoft365Class EOAuth2ServiceMicrosoft365Class;
typedef struct _EOAuth2ServiceMicrosoft365Private EOAuth2ServiceMicrosoft365Private;

struct _EOAuth2ServiceMicrosoft365 {
	EOAuth2ServiceBase parent;
	EOAuth2ServiceMicrosoft365Private *priv;
};

struct _EOAuth2ServiceMicrosoft365Class {
	EOAuth2ServiceBaseClass parent_class;
};

GType		e_oauth2_service_microsoft365_get_type	(void) G_GNUC_CONST;

void		e_oauth2_service_microsoft365_type_register
							(GTypeModule *type_module);

G_END_DECLS

#endif /* E_OAUTH2_SERVICE_MICROSOFT365_H */
