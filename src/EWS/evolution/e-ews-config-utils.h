/*
 * SPDX-FileCopyrightText: (C) 2012 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_CONFIG_UTILS_H
#define E_EWS_CONFIG_UTILS_H

#include <gtk/gtk.h>

#include <shell/e-shell-view.h>

#include "common/e-ews-connection.h"
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

void			e_ews_config_utils_run_in_thread			(GObject *with_object,
										 EEwsSetupFunc thread_func,
										 EEwsSetupFunc idle_func,
										 gpointer user_data,
										 GDestroyNotify free_user_data,
										 GCancellable *cancellable);

typedef ESourceAuthenticationResult
			(* EEwsConfigUtilTryCredentialsFunc)			(EEwsConnection *cnc,
										 const ENamedParameters *credentials,
										 gpointer user_data,
										 GCancellable *cancellable,
										 GError **error);

EEwsConnection	*	e_ews_config_utils_open_connection_for			(ESource *source,
										 CamelEwsSettings *ews_settings,
										 const gchar *connect_url,
										 EEwsConfigUtilTryCredentialsFunc try_credentials_func,
										 gpointer user_data,
										 GCancellable *cancellable,
										 GError **perror);

void			e_ews_config_utils_init_ui				(EShellView *shell_view,
										 const gchar *ui_manager_id);

gboolean		e_ews_config_utils_is_online				(void);

GtkWindow *		e_ews_config_utils_get_widget_toplevel_window		(GtkWidget *widget);

void			e_ews_config_utils_unref_in_thread			(GObject *object);

void			e_ews_config_utils_run_folder_sizes_dialog		(GtkWindow *parent,
										 ESourceRegistry *registry,
										 ESource *source,
										 CamelEwsStore *ews_store);


#endif /* E_EWS_CONFIG_UTILS */
