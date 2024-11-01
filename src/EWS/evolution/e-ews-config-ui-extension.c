/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <shell/e-shell-view.h>

#include "e-ews-config-utils.h"

#include "e-ews-config-ui-extension.h"

G_DEFINE_DYNAMIC_TYPE (EEwsConfigUIExtension, e_ews_config_ui_extension, E_TYPE_EXTENSION)

static void
e_ews_config_ui_extension_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;
	EShellViewClass *shell_view_class;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_ews_config_ui_extension_parent_class)->constructed (object);

	shell_view_class = E_SHELL_VIEW_GET_CLASS (extensible);
	g_return_if_fail (shell_view_class != NULL);

	e_ews_config_utils_init_ui (E_SHELL_VIEW (extensible), shell_view_class->ui_manager_id);
}

static void
e_ews_config_ui_extension_class_init (EEwsConfigUIExtensionClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = e_ews_config_ui_extension_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SHELL_VIEW;
}

static void
e_ews_config_ui_extension_class_finalize (EEwsConfigUIExtensionClass *class)
{
}

static void
e_ews_config_ui_extension_init (EEwsConfigUIExtension *extension)
{
}

void
e_ews_config_ui_extension_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_config_ui_extension_register_type (type_module);
}
