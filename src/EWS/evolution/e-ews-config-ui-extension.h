/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_CONFIG_UI_EXTENSION_H
#define E_EWS_CONFIG_UI_EXTENSION_H

#include <libebackend/libebackend.h>

G_BEGIN_DECLS

typedef struct _EEwsConfigUIExtension EEwsConfigUIExtension;
typedef struct _EEwsConfigUIExtensionClass EEwsConfigUIExtensionClass;

struct _EEwsConfigUIExtension
{
	EExtension parent;
};

struct _EEwsConfigUIExtensionClass
{
	EExtensionClass parent;
};

GType	e_ews_config_ui_extension_get_type	(void);
void	e_ews_config_ui_extension_type_register	(GTypeModule *type_module);

G_END_DECLS

#endif /* E_EWS_CONFIG_UI_EXTENSION_H */
