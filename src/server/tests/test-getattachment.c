/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Mandy Wu <mandy.wu@intel.com>
 *
 * Copyright © 2011 Intel Corporation. (www.intel.com)
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

/* APIs : e_ews_connection_get_attachments */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "utils.h"
#include <e-ews-connection.h>
#include <e-ews-item.h>
#include <e-ews-message.h>


static void op_test_get_attachments ();
void get_attachments_tests_run ();

static GMainLoop *main_loop;

static void
get_attachments_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GError *error = NULL;
	GSList *ids = NULL, *l;

	e_ews_connection_get_attachments_finish	(cnc, res, &ids, &error);

	if (error != NULL) {
		g_print ("Unable to get attachment: %s :%d \n", error->message, error->code);
		goto quit;
	}

	for (l = ids; l != NULL; l = g_slist_next (l)) {
		g_print ("Attachment uri is: %s\n", (gchar *) l->data);
		g_free (l->data);
	}

quit:
	g_main_loop_quit(main_loop);
}

static void
op_test_get_attachments()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	const gchar *attachmentid;
	EEwsConnection *cnc;
	GCancellable *cancellable;
	GSList *ids = NULL;
	gchar *tmpdir;
	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL, NULL, NULL);
	g_assert (cnc != NULL);

	attachmentid = "AAASAG1hbmR5Lnd1QGludGVsLmNvbQBGAAAAAACdSXexmsgJTpd3WpdX6ulXBwAm9E+BClHfQqEnvCoGvhheAAAAjpb6AACIeDU1D80fTrC3245yXdhOADUAPRB8AAABEgAQAIlh9YZzdzdMtvWW9ZI7+vM=";
	ids = g_slist_append (ids, (gpointer *)attachmentid);
	attachmentid = "AAASAG1hbmR5Lnd1QGludGVsLmNvbQBGAAAAAACdSXexmsgJTpd3WpdX6ulXBwAm9E+BClHfQqEnvCoGvhheAAAAjpb6AACIeDU1D80fTrC3245yXdhOADUAPRB8AAABEgAQADgh/XHkRSZEoCsn9BHi5Fc=";
	ids = g_slist_append (ids, (gpointer *)attachmentid);

	tmpdir = g_build_filename ("/home/xwu1/evo-cache", NULL); //a test directory that exists already

	e_ews_connection_get_attachments_start	(cnc, EWS_PRIORITY_MEDIUM,
						 "xuidx", ids, tmpdir, TRUE,
						 get_attachments_ready_callback,
						 NULL, NULL,
						 cancellable, NULL);
	g_free(tmpdir);
}

static gboolean
idle_cb (gpointer data)
{
	g_printf ("Testing get attachments..... \n");
	op_test_get_attachments ();

	return FALSE;
}

/*Run tests*/
void get_attachments_tests_run ()
{
	g_type_init ();
	g_thread_init (NULL);

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
