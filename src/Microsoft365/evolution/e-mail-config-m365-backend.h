/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_CONFIG_M365_BACKEND_H
#define E_MAIL_CONFIG_M365_BACKEND_H

#include <mail/e-mail-config-service-backend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_M365_BACKEND \
	(e_mail_config_m365_backend_get_type ())
#define E_MAIL_CONFIG_M365_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_M365_BACKEND, EMailConfigM365Backend))
#define E_MAIL_CONFIG_M365_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_M365_BACKEND, EMailConfigM365BackendClass))
#define E_IS_MAIL_CONFIG_M365_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_M365_BACKEND))
#define E_IS_MAIL_CONFIG_M365_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_M365_BACKEND))
#define E_MAIL_CONFIG_M365_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_M365_BACKEND, EMailConfigM365BackendClass))

G_BEGIN_DECLS

typedef struct _EMailConfigM365Backend EMailConfigM365Backend;
typedef struct _EMailConfigM365BackendClass EMailConfigM365BackendClass;
typedef struct _EMailConfigM365BackendPrivate EMailConfigM365BackendPrivate;

struct _EMailConfigM365Backend {
	EMailConfigServiceBackend parent;
	EMailConfigM365BackendPrivate *priv;
};

struct _EMailConfigM365BackendClass {
	EMailConfigServiceBackendClass parent_class;
};

GType		e_mail_config_m365_backend_get_type
						(void) G_GNUC_CONST;
void		e_mail_config_m365_backend_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_MAIL_CONFIG_M365_BACKEND_H */
