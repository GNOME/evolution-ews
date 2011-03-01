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
	
	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	/* Quit the test suite */
	//g_signal_connect (cnc, "shutdown", G_CALLBACK (ews_conn_shutdown), NULL);
}

static void
con_test_autodiscover()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	const gchar *email, *domain;
	gchar *wrong_username;
	GError *error = NULL;

	/* FIXME username not needed here */
	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);

	util_get_email_from_env (&email);
	g_assert_cmpstr (email, !=, NULL);
	g_print("%s %s : password : %s \n", G_STRLOC, G_STRFUNC, password);
	g_print("%s %s : email : %s \n", G_STRLOC, G_STRFUNC, email);

	g_print ("Testing postive case... \n");
	uri = e_ews_autodiscover_ws_url (email, password, &error);
	if (error) {
		g_print ("Error code:%d desc: %s \n", error->code, error->message);
		g_clear_error (&error);
	}
	g_assert_cmpstr (uri, !=, NULL);

	g_print ("Testing wrong password... \n");
	uri = e_ews_autodiscover_ws_url (email, "wrongpassword", &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing email without domain ... \n");
	uri = e_ews_autodiscover_ws_url ("wronguseremail", password, &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing wrong email address and password... \n");
	uri = e_ews_autodiscover_ws_url ("godknows@donknow.com", "wrongpassword", &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing wrong user name ... \n");
	domain = g_strstr_len (email, -1, "@");
	wrong_username = g_strconcat ("godknows", domain, NULL);
	uri = e_ews_autodiscover_ws_url (wrong_username, password, &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_free (wrong_username);
	g_assert_cmpstr (uri, ==, NULL);
}

/*Run tests*/
void connection_tests_run ()
{
	g_printf ("Testing Connection..... \n");
	con_test_create_new_connection ();
}

void autodiscovery_tests_run ()
{
	g_printf ("Testing Autodiscovery.... \n");
	con_test_autodiscover();
}
