/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_M365_CONFIG_UI_EXTENSION_H
#define E_M365_CONFIG_UI_EXTENSION_H

#include <libebackend/libebackend.h>

G_BEGIN_DECLS

typedef struct _EM365ConfigUIExtension EM365ConfigUIExtension;
typedef struct _EM365ConfigUIExtensionClass EM365ConfigUIExtensionClass;

struct _EM365ConfigUIExtension
{
	EExtension parent;
};

struct _EM365ConfigUIExtensionClass
{
	EExtensionClass parent;
};

GType	e_m365_config_ui_extension_get_type	(void);
void	e_m365_config_ui_extension_type_register(GTypeModule *type_module);

G_END_DECLS

#endif /* E_M365_CONFIG_UI_EXTENSION_H */
