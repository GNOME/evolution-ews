/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_CONFIG_EWS_AUTODISCOVER_H
#define E_MAIL_CONFIG_EWS_AUTODISCOVER_H

#include <mail/e-mail-config-service-backend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER \
	(e_mail_config_ews_autodiscover_get_type ())
#define E_MAIL_CONFIG_EWS_AUTODISCOVER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER, EMailConfigEwsAutodiscover))
#define E_MAIL_CONFIG_EWS_AUTODISCOVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER, EMailConfigEwsAutodiscoverClass))
#define E_IS_MAIL_CONFIG_EWS_AUTODISCOVER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER))
#define E_IS_MAIL_CONFIG_EWS_AUTODISCOVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER))
#define E_MAIL_CONFIG_EWS_AUTODISCOVER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_AUTODISCOVER, EMailConfigEwsAutodiscoverClass))

G_BEGIN_DECLS

typedef struct _EMailConfigEwsAutodiscover EMailConfigEwsAutodiscover;
typedef struct _EMailConfigEwsAutodiscoverClass EMailConfigEwsAutodiscoverClass;
typedef struct _EMailConfigEwsAutodiscoverPrivate EMailConfigEwsAutodiscoverPrivate;

struct _EMailConfigEwsAutodiscover {
	GtkButton parent;
	EMailConfigEwsAutodiscoverPrivate *priv;
};

struct _EMailConfigEwsAutodiscoverClass {
	GtkButtonClass parent_class;
};

GType		e_mail_config_ews_autodiscover_get_type
					(void) G_GNUC_CONST;
void		e_mail_config_ews_autodiscover_type_register
					(GTypeModule *type_module);
GtkWidget *	e_mail_config_ews_autodiscover_new
					(EMailConfigServiceBackend *backend);
EMailConfigServiceBackend *
		e_mail_config_ews_autodiscover_get_backend
					(EMailConfigEwsAutodiscover *autodiscover);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_AUTODISCOVER_H */

