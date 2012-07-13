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

/* API : e_ews_connection_get_oof_settings */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-message.h"

#include "utils.h"

void get_oof_settings_test_run ();
static GMainLoop *main_loop;

static void
get_oof_settings_cb (GObject *object,
                     GAsyncResult *res,
                     gpointer data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	OOFSettings *oof_settings;
	GError *error = NULL;

	e_ews_connection_get_oof_settings_finish (cnc, res, &oof_settings,
						  &error);
	if (error != NULL) {
		g_warning ("Unable to get out of office settings: %s \n", error->message);
		g_clear_error (&error);
		goto quit;
	}

	g_print ("Success : Fetched out of office settings successfully \n");

quit:
	g_main_loop_quit (main_loop);
}

static void
op_test_get_oof_settings ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri, *email;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	util_get_email_from_env (&email);

	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);
	g_assert_cmpstr (email, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL, 30, NULL, NULL, NULL);
	g_assert (cnc != NULL);

	e_ews_connection_set_mailbox (cnc, email);

	e_ews_connection_get_oof_settings (
		cnc, EWS_PRIORITY_MEDIUM, cancellable,
		get_oof_settings_cb, NULL);
}

static gboolean
idle_cb (gpointer data)
{
	op_test_get_oof_settings ();
	return FALSE;
}

void
get_oof_settings_test_run ()
{
	g_type_init ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
