/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_CONFIG_EWS_GAL_H
#define E_MAIL_CONFIG_EWS_GAL_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_GAL \
	(e_mail_config_ews_gal_get_type ())
#define E_MAIL_CONFIG_EWS_GAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_GAL, EMailConfigEwsGal))
#define E_MAIL_CONFIG_EWS_GAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_GAL, EMailConfigEwsGalClass))
#define E_IS_MAIL_CONFIG_EWS_GAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_GAL))
#define E_IS_MAIL_CONFIG_EWS_GAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_GAL))
#define E_MAIL_CONFIG_EWS_GAL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_GAL, EMailConfigEwsGalClass))

G_BEGIN_DECLS

typedef struct _EMailConfigEwsGal EMailConfigEwsGal;
typedef struct _EMailConfigEwsGalClass EMailConfigEwsGalClass;
typedef struct _EMailConfigEwsGalPrivate EMailConfigEwsGalPrivate;

struct _EMailConfigEwsGal {
	EExtension parent;
	EMailConfigEwsGalPrivate *priv;
};

struct _EMailConfigEwsGalClass {
	EExtensionClass parent_class;
};

GType		e_mail_config_ews_gal_get_type	(void) G_GNUC_CONST;
void		e_mail_config_ews_gal_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_GAL_H */

