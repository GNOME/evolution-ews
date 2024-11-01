/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_OAUTH2_SERVICE_OFFICE365_H
#define E_OAUTH2_SERVICE_OFFICE365_H

#include <gmodule.h>
#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_OAUTH2_SERVICE_OFFICE365 \
	(e_oauth2_service_office365_get_type ())
#define E_OAUTH2_SERVICE_OFFICE365(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_OAUTH2_SERVICE_OFFICE365, EOAuth2ServiceOffice365))
#define E_OAUTH2_SERVICE_OFFICE365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_OAUTH2_SERVICE_OFFICE365, EOAuth2ServiceOffice365Class))
#define E_IS_OAUTH2_SERVICE_OFFICE365(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_OAUTH2_SERVICE_OFFICE365))
#define E_IS_OAUTH2_SERVICE_OFFICE365_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_OAUTH2_SERVICE_OFFICE365))
#define E_OAUTH2_SERVICE_OFFICE365_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_OAUTH2_SERVICE_OFFICE365, EOAuth2ServiceOffice365Class))

#define OFFICE365_FALLBACK_RESOURCE_URI "https://outlook.office365.com"

G_BEGIN_DECLS

typedef struct _EOAuth2ServiceOffice365 EOAuth2ServiceOffice365;
typedef struct _EOAuth2ServiceOffice365Class EOAuth2ServiceOffice365Class;
typedef struct _EOAuth2ServiceOffice365Private EOAuth2ServiceOffice365Private;

struct _EOAuth2ServiceOffice365 {
	EOAuth2ServiceBase parent;
	EOAuth2ServiceOffice365Private *priv;
};

struct _EOAuth2ServiceOffice365Class {
	EOAuth2ServiceBaseClass parent_class;
};

GType		e_oauth2_service_office365_get_type	(void) G_GNUC_CONST;

void		e_oauth2_service_office365_type_register
							(GTypeModule *type_module);

G_END_DECLS

#endif /* E_OAUTH2_SERVICE_OFFICE365_H */
