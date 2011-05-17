/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Johnny Jacob <johnnyjacob@gmail.com>
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

/* APIs : e_ews_autodiscover_ws_url, e_ews_connection_new */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "utils.h"
#include <e-ews-connection.h>


static void con_test_create_new_connection ();
void connection_tests_run ();
void autodiscovery_tests_run ();

EwsFolderId *inbox_folder_id = NULL;
GSList *ids = NULL;
GMainLoop *main_loop;

/*Test cases*/

static void
con_test_create_new_connection ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL, NULL, NULL);
	g_assert (cnc != NULL);

	/* Quit the test suite */
	//g_signal_connect (cnc, "shutdown", G_CALLBACK (ews_conn_shutdown), NULL);
}

struct _cb_data {
	gboolean positive_case;
	gboolean quit;
};

static void
autodiscover_cb (gchar *uri, gpointer user_data, GError *error)
{
	struct _cb_data *data = (struct _cb_data *) user_data;
	gboolean quit = data->quit;

	if (data->positive_case) {
		g_assert_cmpstr (uri, !=, NULL);
	} else
		g_assert_cmpstr (uri, ==, NULL);
	
	if (error)
		g_print ("Error code:%d desc: %s \n", error->code, error->message);
	
	g_clear_error (&error);
	g_free (data);
	
	if (quit)
		g_main_loop_quit (main_loop);
}

static void
con_test_autodiscover()
{
	const gchar *username;
	const gchar *password;
	const gchar *email;
//	gchar *wrong_username, *domain;
	const gchar *uri;
	struct _cb_data *user_data;

	/* FIXME username not needed here */
	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);

	util_get_email_from_env (&email);
	g_assert_cmpstr (email, !=, NULL);
	g_print("%s %s : password : %s \n", G_STRLOC, G_STRFUNC, password);
	g_print("%s %s : email : %s \n", G_STRLOC, G_STRFUNC, email);

	g_print ("Testing postive case... \n");
	user_data = g_new0 (struct _cb_data, 1);
	user_data->positive_case = TRUE;
	user_data->quit = TRUE;
	e_ews_autodiscover_ws_url (autodiscover_cb, user_data, email, password);

	g_print ("Testing wrong password... \n");
	user_data = g_new0 (struct _cb_data, 1);
	e_ews_autodiscover_ws_url (autodiscover_cb, user_data, email, "wrongpassword");

	g_print ("Testing email without domain ... \n");
	user_data = g_new0 (struct _cb_data, 1);
	e_ews_autodiscover_ws_url (autodiscover_cb, user_data, "wronguseremail", password);

	g_print ("Testing wrong email address and password... \n");
	user_data = g_new0 (struct _cb_data, 1);
	e_ews_autodiscover_ws_url (autodiscover_cb, user_data, "godknows@donknow.com", "wrongpassword");

	g_print ("Testing wrong user name ... \n");
	domain = g_strstr_len (email, -1, "@");
	wrong_username = g_strconcat ("godknows", domain, NULL);
	user_data = g_new0 (struct _cb_data, 1);
	user_data->quit = TRUE;
	e_ews_autodiscover_ws_url (autodiscover_cb, user_data, wrong_username, password);
	g_free (wrong_username);
}

static gboolean
idle_cb (gpointer data)
{
	g_printf ("Test Connections");
	con_test_create_new_connection ();

	g_printf ("Testing Autodiscovery.... \n");
	con_test_autodiscover();
	
	return FALSE;
}

/*Run tests*/
void connection_tests_run ()
{
	g_type_init ();
	g_thread_init (NULL);

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
