/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_CONFIG_EWS_NOTEBOOK_H
#define E_MAIL_CONFIG_EWS_NOTEBOOK_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_EWS_NOTEBOOK \
	(e_mail_config_ews_notebook_get_type ())
#define E_MAIL_CONFIG_EWS_NOTEBOOK(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_EWS_NOTEBOOK, EMailConfigEwsNotebook))
#define E_MAIL_CONFIG_EWS_NOTEBOOK_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_EWS_NOTEBOOK, EMailConfigEwsNotebookClass))
#define E_IS_MAIL_CONFIG_EWS_NOTEBOOK(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_EWS_NOTEBOOK))
#define E_IS_MAIL_CONFIG_EWS_NOTEBOOK_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_EWS_NOTEBOOK))
#define E_MAIL_CONFIG_EWS_NOTEBOOK_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_EWS_NOTEBOOK, EMailConfigEwsNotebookClass))

G_BEGIN_DECLS

typedef struct _EMailConfigEwsNotebook EMailConfigEwsNotebook;
typedef struct _EMailConfigEwsNotebookClass EMailConfigEwsNotebookClass;
typedef struct _EMailConfigEwsNotebookPrivate EMailConfigEwsNotebookPrivate;

struct _EMailConfigEwsNotebook {
	EExtension parent;
	EMailConfigEwsNotebookPrivate *priv;
};

struct _EMailConfigEwsNotebookClass {
	EExtensionClass parent_class;
};

GType		e_mail_config_ews_notebook_get_type
						(void) G_GNUC_CONST;
void		e_mail_config_ews_notebook_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_MAIL_CONFIG_EWS_NOTEBOOK_H */

