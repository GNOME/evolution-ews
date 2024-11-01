/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_CONFIG_EWS_BACKEND_H
#define E_MAIL_CONFIG_EWS_BACKEND_H

#include <mail/e-mail-config-service-backend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_BACKEND \
	(e_mail_config_ews_backend_get_type ())
#define E_MAIL_CONFIG_EWS_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_BACKEND, EMailConfigEwsBackend))
#define E_MAIL_CONFIG_EWS_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_BACKEND, EMailConfigEwsBackendClass))
#define E_IS_MAIL_CONFIG_EWS_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_BACKEND))
#define E_IS_MAIL_CONFIG_EWS_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_BACKEND))
#define E_MAIL_CONFIG_EWS_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_BACKEND, EMailConfigEwsBackendClass))

G_BEGIN_DECLS

typedef struct _EMailConfigEwsBackend EMailConfigEwsBackend;
typedef struct _EMailConfigEwsBackendClass EMailConfigEwsBackendClass;
typedef struct _EMailConfigEwsBackendPrivate EMailConfigEwsBackendPrivate;

struct _EMailConfigEwsBackend {
	EMailConfigServiceBackend parent;
	EMailConfigEwsBackendPrivate *priv;
};

struct _EMailConfigEwsBackendClass {
	EMailConfigServiceBackendClass parent_class;
};

GType		e_mail_config_ews_backend_get_type
						(void) G_GNUC_CONST;
void		e_mail_config_ews_backend_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_BACKEND_H */

