/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* "OOO" = "Out of Office" */

#ifndef E_MAIL_CONFIG_M365_OOO_PAGE_H
#define E_MAIL_CONFIG_M365_OOO_PAGE_H

#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>

#include <mail/e-mail-config-page.h>
#include <mail/e-mail-config-activity-page.h>

#define E_MAIL_CONFIG_M365_OOO_PAGE_SORT_ORDER (550)

G_BEGIN_DECLS

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EMailConfigActivityPage, g_object_unref)
#endif

#define E_TYPE_MAIL_CONFIG_M365_OOO_PAGE (e_mail_config_m365_ooo_page_get_type ())
G_DECLARE_FINAL_TYPE (EMailConfigM365OooPage, e_mail_config_m365_ooo_page, E, MAIL_CONFIG_M365_OOO_PAGE, EMailConfigActivityPage)

void		e_mail_config_m365_ooo_page_type_register
						(GTypeModule *type_module);
EMailConfigPage *
		e_mail_config_m365_ooo_page_new	(ESourceRegistry *registry,
						 ESource *account_source,
						 ESource *identity_source,
						 ESource *collection_source);
void		e_mail_config_m365_ooo_page_refresh
						(EMailConfigM365OooPage *self);

G_END_DECLS

#endif /* E_MAIL_CONFIG_M365_OOO_PAGE_H */
