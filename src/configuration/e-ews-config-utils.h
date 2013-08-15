/*
 * e-ews-config-utils.h
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

#ifndef E_EWS_CONFIG_UTILS_H
#define E_EWS_CONFIG_UTILS_H

#include <gtk/gtk.h>

#include <shell/e-shell-view.h>

#include "server/e-ews-connection.h"
#include "camel/camel-ews-store.h"

typedef void		(* EEwsSetupFunc)					(GObject *with_object,
										 gpointer user_data,
										 GCancellable *cancellable,
										 GError **perror);

void			e_ews_config_utils_run_in_thread_with_feedback		(GtkWindow *parent,
										 GObject *with_object,
										 const gchar *description,
										 EEwsSetupFunc thread_func,
										 EEwsSetupFunc idle_func,
										 gpointer user_data,
										 GDestroyNotify free_user_data);

void			e_ews_config_utils_run_in_thread_with_feedback_modal	(GtkWindow *parent,
										 GObject *with_object,
										 const gchar *description,
										 EEwsSetupFunc thread_func,
										 EEwsSetupFunc idle_func,
										 gpointer user_data,
										 GDestroyNotify free_user_data);

EEwsConnection	*	e_ews_config_utils_open_connection_for			(ESourceRegistry *registry,
										 ESource *source,
										 CamelEwsSettings *ews_settings,
										 GCancellable *cancellable,
										 GError **perror);

void			e_ews_config_utils_init_ui				(EShellView *shell_view,
										 const gchar *ui_manager_id,
										 gchar **ui_definition);

gboolean		e_ews_config_utils_is_online				(void);

GtkWindow *		e_ews_config_utils_get_widget_toplevel_window		(GtkWidget *widget);

void			e_ews_config_utils_unref_in_thread			(GObject *object);

void			e_ews_config_utils_run_folder_sizes_dialog		(GtkWindow *parent,
										 ESourceRegistry *registry,
										 ESource *source,
										 CamelEwsStore *ews_store);


#endif /* E_EWS_CONFIG_UTILS */
