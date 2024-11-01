/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_H
#define E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_H

#include <gtk/gtk.h>
#include <libedataserver/libedataserver.h>

#include <mail/e-mail-config-page.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE \
	(e_mail_config_ews_folder_sizes_page_get_type ())
#define E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE, EMailConfigEwsFolderSizesPage))
#define E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE, EMailConfigEwsFolderSizesPageClass))
#define E_IS_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE))
#define E_IS_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE))
#define E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE, EMailConfigEwsFolderSizesPage))

G_BEGIN_DECLS

typedef struct _EMailConfigEwsFolderSizesPage EMailConfigEwsFolderSizesPage;
typedef struct _EMailConfigEwsFolderSizesPageClass EMailConfigEwsFolderSizesPageClass;
typedef struct _EMailConfigEwsFolderSizesPagePrivate EMailConfigEwsFolderSizesPagePrivate;

struct _EMailConfigEwsFolderSizesPage {
	GtkScrolledWindow parent;
	EMailConfigEwsFolderSizesPagePrivate *priv;
};

struct _EMailConfigEwsFolderSizesPageClass {
	GtkScrolledWindowClass parent_class;
};

GType			e_mail_config_ews_folder_sizes_page_get_type		(void) G_GNUC_CONST;
void			e_mail_config_ews_folder_sizes_page_type_register	(GTypeModule *type_module);
EMailConfigPage *	e_mail_config_ews_folder_sizes_page_new			(ESource *account_source,
										 ESource *collection_source,
										 ESourceRegistry *registry);

ESource *		e_mail_config_ews_folder_sizes_page_get_account_source	(EMailConfigEwsFolderSizesPage *page);
ESource *		e_mail_config_ews_folder_sizes_page_get_collection_source
										(EMailConfigEwsFolderSizesPage *page);
ESourceRegistry *	e_mail_config_ews_folder_sizes_page_get_source_registry	(EMailConfigEwsFolderSizesPage *page);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_FOLDER_SIZES_PAGE_H */
