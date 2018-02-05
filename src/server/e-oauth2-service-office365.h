/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
 *
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
