/*
 * e-ews-config-ui-extension.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <shell/e-shell-view.h>

#include "e-ews-config-utils.h"

#include "e-ews-config-ui-extension.h"

G_DEFINE_DYNAMIC_TYPE (
	EEwsConfigUIExtension,
	e_ews_config_ui_extension,
	E_TYPE_EXTENSION)

static void
e_ews_config_ui_extension_shell_view_toggled_cb (EShellView *shell_view,
                                                 EEwsConfigUIExtension *ui_ext)
{
	EShellViewClass *shell_view_class;
	EShellWindow *shell_window;
	GtkUIManager *ui_manager;
	gpointer key = NULL, value = NULL;
	const gchar *ui_def;
	gboolean is_active, need_update;

	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));
	g_return_if_fail (ui_ext != NULL);

	shell_view_class = E_SHELL_VIEW_GET_CLASS (shell_view);
	g_return_if_fail (shell_view_class != NULL);

	shell_window = e_shell_view_get_shell_window (shell_view);
	ui_manager = e_shell_window_get_ui_manager (shell_window);

	need_update = ui_ext->current_ui_id != 0;
	if (ui_ext->current_ui_id) {
		gtk_ui_manager_remove_ui (ui_manager, ui_ext->current_ui_id);
		ui_ext->current_ui_id = 0;
	}

	is_active = e_shell_view_is_active (shell_view);
	if (!is_active) {
		if (need_update)
			gtk_ui_manager_ensure_update (ui_manager);

		return;
	}

	if (!g_hash_table_lookup_extended (ui_ext->ui_definitions, shell_view_class->ui_manager_id, &key, &value)) {
		gchar *ui_definition = NULL;

		e_ews_config_utils_init_ui (shell_view, shell_view_class->ui_manager_id, &ui_definition);
		g_hash_table_insert (ui_ext->ui_definitions, g_strdup (shell_view_class->ui_manager_id), ui_definition);
	}

	ui_def = g_hash_table_lookup (ui_ext->ui_definitions, shell_view_class->ui_manager_id);
	if (ui_def) {
		GError *error = NULL;

		ui_ext->current_ui_id = gtk_ui_manager_add_ui_from_string (ui_manager, ui_def, -1, &error);
		need_update = TRUE;

		if (error) {
			g_warning ("%s: Failed to add ui definition: %s", G_STRFUNC, error->message);
			g_error_free (error);
		}
	}

	if (need_update)
		gtk_ui_manager_ensure_update (ui_manager);
}

static void
e_ews_config_ui_extension_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_ews_config_ui_extension_parent_class)->constructed (object);

	g_signal_connect (E_SHELL_VIEW (extensible), "toggled", G_CALLBACK (e_ews_config_ui_extension_shell_view_toggled_cb), extension);
}

static void
e_ews_config_ui_extension_finalize (GObject *object)
{
	EEwsConfigUIExtension *ui_ext = (EEwsConfigUIExtension *) object;

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ews_config_ui_extension_parent_class)->finalize (object);

	g_hash_table_destroy (ui_ext->ui_definitions);
}

static void
e_ews_config_ui_extension_class_init (EEwsConfigUIExtensionClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = e_ews_config_ui_extension_constructed;
	object_class->finalize = e_ews_config_ui_extension_finalize;

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
	extension->current_ui_id = 0;
	extension->ui_definitions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

void
e_ews_config_ui_extension_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_ews_config_ui_extension_register_type (type_module);
}
