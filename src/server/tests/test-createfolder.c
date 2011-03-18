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

/* API : e_ews_connection_create_folders */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "utils.h"
#include <e-ews-connection.h>
#include <e-ews-folder.h>
#include <e-ews-message.h>

void createfolder_tests_run ();

static GMainLoop *main_loop;

EwsFolderId *folder_id;

static void
create_folder_cb (GObject *object, GAsyncResult *res, gpointer data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GError *error = NULL;
	GCancellable *cancellable;
	EwsFolderId *fid = NULL;

	e_ews_connection_create_folder_finish	(cnc, res, &fid,
						 cancellable, &error);
	if (error != NULL) {
		g_warning ("Unable to create: %s \n", error->message);
		g_clear_error (&error);
		goto quit;
	}

	g_print ("Folder is successfully created. The Folder Id is %s \n", fid->id);

	/*Assigning the folder_id which would be used for testing delete folder */
	folder_id = g_new0 (EwsFolderId, 1);
	folder_id->id = g_strdup (fid->id);
	folder_id->change_key = g_strdup (fid->change_key);

	e_ews_folder_free_fid (fid);

quit:	
	g_main_loop_quit(main_loop);
}

static void
op_test_create_folder ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);
	e_ews_connection_create_folder_start	(cnc, EWS_PRIORITY_MEDIUM, "inbox", 
						 TRUE ,"test", 
						 create_folder_cb,
						 cancellable, NULL);

}

static gboolean
idle_cb (gpointer data)
{
	op_test_create_folder ();
	return FALSE;
}

void 
createfolder_tests_run ()
{
	g_type_init ();
	g_thread_init (NULL);

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
