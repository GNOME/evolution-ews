/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Vibha Yadav <yvibha@novell.com>
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

/* API : e_ews_connection_delete_folders */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-folder.h"
#include "server/e-ews-message.h"

#include "utils.h"

static GMainLoop *main_loop;

void deletefolder_tests_run (gconstpointer data);

static void
delete_folder_cb (GObject *object,
                  GAsyncResult *res,
                  gpointer data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GError *error = NULL;

	e_ews_connection_delete_folder_finish	(cnc, res,
						 &error);
	if (error != NULL) {
		g_warning ("Unable to create: %s \n", error->message);
		g_clear_error (&error);
		goto quit;
	}

	g_print ("Folder is successfully Deleted. \n");

quit:
	g_main_loop_quit (main_loop);
}

static void
op_test_delete_folder (gpointer data)
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;
	CamelEwsSettings *settings;
	EwsFolderId **fid = (EwsFolderId **) data;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	settings = g_object_new (
		CAMEL_TYPE_EWS_SETTINGS,
		"user", username, NULL);

	cnc = e_ews_connection_new (uri, password, settings, NULL);
	g_assert (cnc != NULL);

	g_object_unref (settings);

	e_ews_connection_delete_folder (
		cnc, EWS_PRIORITY_MEDIUM, (*fid)->id,
		FALSE ,"HardDelete", cancellable,
		delete_folder_cb, NULL);

}

static gboolean
idle_cb (gpointer data)
{
	op_test_delete_folder (data);
	return FALSE;
}

void
deletefolder_tests_run (gconstpointer data)
{
	EwsFolderId **fid = (EwsFolderId **) data;

	g_return_if_fail (*fid != NULL);

	g_type_init ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, fid);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
