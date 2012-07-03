/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Chenthill Palanisamy <pchenthill@novell.com>
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

/* API : e_ews_connection_resolve_names */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>
#include "utils.h"
#include <e-ews-connection.h>

void autocompletion_tests_run ();

static GMainLoop *main_loop;

static void
resolve_names_cb (GObject *object,
                  GAsyncResult *res,
                  gpointer data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GSList *mailboxes = NULL, *contact_items = NULL, *l;
	gboolean has_last_item = FALSE;
	GError *error = NULL;

	e_ews_connection_resolve_names_finish	(cnc, res, &mailboxes, &contact_items,
						 &has_last_item, &error);
	if (error != NULL) {
		g_warning ("Unable to autocomplete: %s \n", error->message);
		g_clear_error (&error);
		goto quit;
	}

	if (mailboxes) {
		for (l = mailboxes; l != NULL; l = g_slist_next (l)) {
			EwsMailbox *mb = (EwsMailbox *) l->data;
			g_print ("%s:%s \n", mb->name, mb->email);

			e_ews_mailbox_free (mb);
		}

		g_slist_free (mailboxes);
	}

	g_print ("\n Last item in range : %d \n", has_last_item);

	/* FIXME once we fetch contact items free them as well */

quit:
	g_main_loop_quit (main_loop);
}

static void
op_test_resolve_names ()
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

	cnc = e_ews_connection_new (uri, username, password, TRUE, NULL, NULL, NULL);
	g_assert (cnc != NULL);
	e_ews_connection_resolve_names_start	(cnc, EWS_PRIORITY_MEDIUM, username,
						 EWS_SEARCH_AD, NULL, FALSE, resolve_names_cb,
						 cancellable, NULL);

}

static gboolean
idle_cb (gpointer data)
{
	g_print ("\nTesting resolve names... \n");
	op_test_resolve_names ();
	return FALSE;
}

void
autocompletion_tests_run ()
{
	g_print ("Testing autocompletion... \n");

	g_type_init ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
