/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Punit Jain <jpunit@suse.com>
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

/* API : e_ews_oof_settings_submit */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-message.h"
#include "server/e-ews-oof-settings.h"

#include "utils.h"

void oof_settings_submit_test_run (void);
static GMainLoop *main_loop;
EEwsOofSettings *oof_settings = NULL;

static void
set_arbit_values (EEwsConnection *cnc)
{
	GDateTime *tm = g_date_time_new_now_local ();

	oof_settings = e_ews_oof_settings_new_sync (cnc, NULL, NULL);

	e_ews_oof_settings_set_state (oof_settings, E_EWS_OOF_STATE_ENABLED);
	e_ews_oof_settings_set_external_audience (oof_settings,
			E_EWS_EXTERNAL_AUDIENCE_ALL);
	e_ews_oof_settings_set_start_time (oof_settings,
			g_date_time_add_days (tm, -1));
	e_ews_oof_settings_set_end_time (oof_settings,
			g_date_time_add_days (tm, 1));
	e_ews_oof_settings_set_internal_reply (oof_settings, "My Internal Reply");
	e_ews_oof_settings_set_external_reply (oof_settings, "My External Reply");
}

static void
oof_settings_submit_cb (GObject *object,
                     GAsyncResult *res,
                     gpointer data)
{
	GError *error = NULL;

	e_ews_oof_settings_submit_finish (E_EWS_OOF_SETTINGS (object), res, &error);

	if (error != NULL) {
		g_warning ("Unable to set out of office settings: %s \n", error->message);
		g_clear_error (&error);
		goto quit;
	}

	g_print ("Success : Set out office successfully \n");

quit:
	g_main_loop_quit (main_loop);
}

static void
op_test_oof_settings_submit (void)
{
	const gchar *username;
	const gchar *password;
	const gchar *uri, *email;
	EEwsConnection *cnc;
	GCancellable *cancellable;
	CamelEwsSettings *settings;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	util_get_email_from_env (&email);

	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);
	g_assert_cmpstr (email, !=, NULL);

	settings = g_object_new (
		CAMEL_TYPE_EWS_SETTINGS,
		"user", username, NULL);

	cnc = e_ews_connection_new (uri, settings);
	e_ews_connection_set_password (cnc, password);

	g_object_unref (settings);

	e_ews_connection_set_mailbox (cnc, email);

	set_arbit_values (cnc);
	e_ews_oof_settings_submit (
		oof_settings, cancellable,
		oof_settings_submit_cb, NULL);
}

static gboolean
idle_cb (gpointer data)
{
	op_test_oof_settings_submit ();
	return FALSE;
}

void
oof_settings_submit_test_run (void)
{
	g_type_init ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
