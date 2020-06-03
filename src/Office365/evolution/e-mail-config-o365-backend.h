/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef E_MAIL_CONFIG_O365_BACKEND_H
#define E_MAIL_CONFIG_O365_BACKEND_H

#include <mail/e-mail-config-service-backend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_O365_BACKEND \
	(e_mail_config_o365_backend_get_type ())
#define E_MAIL_CONFIG_O365_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_O365_BACKEND, EMailConfigO365Backend))
#define E_MAIL_CONFIG_O365_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_O365_BACKEND, EMailConfigO365BackendClass))
#define E_IS_MAIL_CONFIG_O365_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_O365_BACKEND))
#define E_IS_MAIL_CONFIG_O365_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_O365_BACKEND))
#define E_MAIL_CONFIG_O365_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_O365_BACKEND, EMailConfigO365BackendClass))

G_BEGIN_DECLS

typedef struct _EMailConfigO365Backend EMailConfigO365Backend;
typedef struct _EMailConfigO365BackendClass EMailConfigO365BackendClass;
typedef struct _EMailConfigO365BackendPrivate EMailConfigO365BackendPrivate;

struct _EMailConfigO365Backend {
	EMailConfigServiceBackend parent;
	EMailConfigO365BackendPrivate *priv;
};

struct _EMailConfigO365BackendClass {
	EMailConfigServiceBackendClass parent_class;
};

GType		e_mail_config_o365_backend_get_type
						(void) G_GNUC_CONST;
void		e_mail_config_o365_backend_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_MAIL_CONFIG_O365_BACKEND_H */
