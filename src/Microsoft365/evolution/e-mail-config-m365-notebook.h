/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_CONFIG_M365_NOTEBOOK_H
#define E_MAIL_CONFIG_M365_NOTEBOOK_H

#include <libebackend/libebackend.h>

G_BEGIN_DECLS

#define E_TYPE_MAIL_CONFIG_M365_NOTEBOOK (e_mail_config_m365_notebook_get_type ())
G_DECLARE_FINAL_TYPE (EMailConfigM365Notebook, e_mail_config_m365_notebook, E, MAIL_CONFIG_M365_NOTEBOOK, EExtension)

void		e_mail_config_m365_notebook_type_register	(GTypeModule *type_module);

G_END_DECLS

#endif /* E_MAIL_CONFIG_M365_NOTEBOOK_H */
