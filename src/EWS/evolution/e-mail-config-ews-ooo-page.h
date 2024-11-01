/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* "OOO" = "Out of Office" */

#ifndef E_MAIL_CONFIG_EWS_OOO_PAGE_H
#define E_MAIL_CONFIG_EWS_OOO_PAGE_H

#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>

#include <mail/e-mail-config-page.h>
#include <mail/e-mail-config-activity-page.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE \
	(e_mail_config_ews_ooo_page_get_type ())
#define E_MAIL_CONFIG_EWS_OOO_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE, EMailConfigEwsOooPage))
#define E_MAIL_CONFIG_EWS_OOO_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE, EMailConfigEwsOooPageClass))
#define E_IS_MAIL_CONFIG_EWS_OOO_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE))
#define E_IS_MAIL_CONFIG_EWS_OOO_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE))
#define E_MAIL_CONFIG_EWS_OOO_PAGE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_OOO_PAGE, EMailConfigEwsOooPageClass))

#define E_MAIL_CONFIG_EWS_OOO_PAGE_SORT_ORDER (550)

G_BEGIN_DECLS

typedef struct _EMailConfigEwsOooPage EMailConfigEwsOooPage;
typedef struct _EMailConfigEwsOooPageClass EMailConfigEwsOooPageClass;
typedef struct _EMailConfigEwsOooPagePrivate EMailConfigEwsOooPagePrivate;

struct _EMailConfigEwsOooPage {
	EMailConfigActivityPage parent;
	EMailConfigEwsOooPagePrivate *priv;
};

struct _EMailConfigEwsOooPageClass {
	EMailConfigActivityPageClass parent_class;
};

GType		e_mail_config_ews_ooo_page_get_type
						(void) G_GNUC_CONST;
void		e_mail_config_ews_ooo_page_type_register
						(GTypeModule *type_module);
EMailConfigPage *
		e_mail_config_ews_ooo_page_new	(ESourceRegistry *registry,
						 ESource *account_source,
						 ESource *identity_source,
						 ESource *collection_source);
void		e_mail_config_ews_ooo_page_refresh
						(EMailConfigEwsOooPage *page);
ESourceRegistry *
		e_mail_config_ews_ooo_page_get_registry
						(EMailConfigEwsOooPage *page);
ESource *	e_mail_config_ews_ooo_page_get_account_source
						(EMailConfigEwsOooPage *page);
ESource *	e_mail_config_ews_ooo_page_get_identity_source
						(EMailConfigEwsOooPage *page);
ESource *	e_mail_config_ews_ooo_page_get_collection_source
						(EMailConfigEwsOooPage *page);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_OOO_PAGE_H */

