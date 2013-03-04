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

#include "server/e-ews-connection.h"

#include "utils.h"

static void con_test_create_new_connection ();
void connection_tests_run ();
void autodiscovery_tests_run ();

EwsFolderId *inbox_folder_id = NULL;
GSList *ids = NULL;
GMainLoop *main_loop;

/*Test cases*/

static void
con_test_create_new_connection (void)
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	CamelEwsSettings *settings;

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	settings = g_object_new (
		CAMEL_TYPE_EWS_SETTINGS,
		"user", username, NULL);

	cnc = e_ews_connection_new (uri, settings);
	e_ews_connection_set_password (cnc, password);

	g_object_unref (settings);

	g_print ("\nSuccess : Created a new connection\n");

	/* Quit the test suite */
	//g_signal_connect (cnc, "shutdown", G_CALLBACK (ews_conn_shutdown), NULL);
}

struct _cb_data {
	gboolean positive_case;
	gchar *test_case;
	gboolean quit;
};

static void
autodiscover_cb (GObject *object,
                 GAsyncResult *res,
                 gpointer user_data)
{
	CamelEwsSettings *settings = CAMEL_EWS_SETTINGS (object);
	struct _cb_data *data = (struct _cb_data *) user_data;
	gboolean quit = data->quit;
	const gchar *host_url, *oab_url;
	GError *error = NULL;

	g_print ("Response for test case : %s \n", data->test_case);
	e_ews_autodiscover_ws_url_finish (settings, res, &error);

	host_url = camel_ews_settings_get_hosturl (settings);
	oab_url = camel_ews_settings_get_oaburl (settings);

	if (data->positive_case)
		g_assert (host_url != NULL && oab_url != NULL);
	else
		g_assert (host_url == NULL || oab_url == NULL);

	if (error != NULL) {
		g_print ("Error code: %d desc: %s\n", error->code, error->message);
		g_clear_error (&error);
	}

	if (host_url || oab_url)
		g_print ("ASUrl: %s\nOABUrl: %s\n", host_url, oab_url);

	g_free (data->test_case);
	g_free (data);

	if (quit)
		g_main_loop_quit (main_loop);
}

static void
con_test_autodiscover (void)
{
	CamelEwsSettings *settings;
	const gchar *username;
	const gchar *password;
	const gchar *email;
	gchar *wrong_username, *domain;
	const gchar *uri;
	struct _cb_data *user_data;

	/* FIXME username not needed here */
	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);

	util_get_email_from_env (&email);
	g_assert_cmpstr (email, !=, NULL);
	g_print ("%s %s : password : %s \n", G_STRLOC, G_STRFUNC, password);
	g_print ("%s %s : email : %s \n", G_STRLOC, G_STRFUNC, email);

	settings = g_object_new (
		CAMEL_TYPE_EWS_SETTINGS,
		"user", username,
		NULL);
	camel_ews_settings_set_hosturl (settings, uri);

	user_data = g_new0 (struct _cb_data, 1);
	user_data->test_case = g_strdup ("positive case... \n");
	user_data->positive_case = TRUE;
	g_print ("Testing %s \n", user_data->test_case);
	e_ews_autodiscover_ws_url (
			settings,
			email,
			password,
			NULL,
			autodiscover_cb,
			user_data);

	user_data = g_new0 (struct _cb_data, 1);
	user_data->test_case = g_strdup ("wrong password... \n");
	/* It does respond properly with the url, Check it out */
	user_data->positive_case = TRUE;
	g_print ("Testing %s \n", user_data->test_case);
	e_ews_autodiscover_ws_url (
			settings,
			email,
			"wrongpassword",
			NULL,
			autodiscover_cb,
			user_data);

	user_data = g_new0 (struct _cb_data, 1);
	user_data->test_case = g_strdup ("email without domain ... \n");
	user_data->positive_case = FALSE;
	g_print ("Testing %s \n", user_data->test_case);
	e_ews_autodiscover_ws_url (
			settings,
			"wronguseremail",
			password,
			NULL,
			autodiscover_cb,
			user_data);

	user_data = g_new0 (struct _cb_data, 1);
	user_data->test_case = g_strdup ("wrong email address and password... \n");
	user_data->positive_case = FALSE;
	g_print ("Testing %s \n", user_data->test_case);
	e_ews_autodiscover_ws_url (
			settings,
			"godknows@dontknow.com",
			"wrongpassword",
			NULL,
			autodiscover_cb,
			user_data);

	domain = g_strstr_len (email, -1, "@");
	wrong_username = g_strconcat ("godknows", domain, NULL);
	user_data = g_new0 (struct _cb_data, 1);
	user_data->test_case = g_strdup ("wrong user name ... \n");
	user_data->positive_case = TRUE;
	g_print ("Testing %s \n", user_data->test_case);
	user_data->quit = TRUE;
	e_ews_autodiscover_ws_url (
			settings,
			wrong_username,
			password,
			NULL,
			autodiscover_cb,
			user_data);
	g_free (wrong_username);
}

static gboolean
idle_cb (gpointer data)
{
	g_printf ("Test Connections");
	con_test_create_new_connection ();

	g_printf ("Testing Autodiscovery.... \n");
	con_test_autodiscover ();

	return FALSE;
}

/*Run tests*/
void connection_tests_run (void)
{
	g_type_init ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
