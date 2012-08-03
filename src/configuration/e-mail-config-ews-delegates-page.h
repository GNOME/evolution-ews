/*
 * e-mail-config-ews-delegates-page.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_MAIL_CONFIG_EWS_DELEGATES_PAGE_H
#define E_MAIL_CONFIG_EWS_DELEGATES_PAGE_H

#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>

#include <mail/e-mail-config-page.h>
#include <mail/e-mail-config-activity-page.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_DELEGATES_PAGE \
	(e_mail_config_ews_delegates_page_get_type ())
#define E_MAIL_CONFIG_EWS_DELEGATES_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_DELEGATES_PAGE, EMailConfigEwsDelegatesPage))
#define E_MAIL_CONFIG_EWS_DELEGATES_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_DELEGATES_PAGE, EMailConfigEwsDelegatesPageClass))
#define E_IS_MAIL_CONFIG_EWS_DELEGATES_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_DELEGATES_PAGE))
#define E_IS_MAIL_CONFIG_EWS_DELEGATES_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_DELEGATES_PAGE))
#define E_MAIL_CONFIG_EWS_DELEGATES_PAGE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_DELEGATES_PAGE, EMailConfigEwsDelegatesPageClass))

#define E_MAIL_CONFIG_EWS_DELEGATES_PAGE_SORT_ORDER (551)

G_BEGIN_DECLS

typedef struct _EMailConfigEwsDelegatesPage EMailConfigEwsDelegatesPage;
typedef struct _EMailConfigEwsDelegatesPageClass EMailConfigEwsDelegatesPageClass;
typedef struct _EMailConfigEwsDelegatesPagePrivate EMailConfigEwsDelegatesPagePrivate;

struct _EMailConfigEwsDelegatesPage {
	EMailConfigActivityPage parent;
	EMailConfigEwsDelegatesPagePrivate *priv;
};

struct _EMailConfigEwsDelegatesPageClass {
	EMailConfigActivityPageClass parent_class;
};

GType		e_mail_config_ews_delegates_page_get_type	(void) G_GNUC_CONST;
void		e_mail_config_ews_delegates_page_type_register	(GTypeModule *type_module);
EMailConfigPage *
		e_mail_config_ews_delegates_page_new		(ESourceRegistry *registry,
								 ESource *account_source,
								 ESource *identity_source,
								 ESource *collection_source);
void		e_mail_config_ews_delegates_page_refresh	(EMailConfigEwsDelegatesPage *page);
ESourceRegistry *
		e_mail_config_ews_delegates_page_get_registry	(EMailConfigEwsDelegatesPage *page);
ESource *	e_mail_config_ews_delegates_page_get_account_source
								(EMailConfigEwsDelegatesPage *page);
ESource *	e_mail_config_ews_delegates_page_get_identity_source
								(EMailConfigEwsDelegatesPage *page);
ESource *	e_mail_config_ews_delegates_page_get_collection_source
								(EMailConfigEwsDelegatesPage *page);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_DELEGATES_PAGE_H */
