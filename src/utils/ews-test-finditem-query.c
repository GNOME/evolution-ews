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

/* APIs : e_ews_connection_find_folder_items; e_ews_query_to_restriction; */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-folder.h"
#include "server/e-ews-item.h"
#include "utils/e-ews-query-to-restriction.h"

/*Some simple queries are defined here we can replace them to more complex ones;
 * based on these restriction you might not get items at all, to get all items use NULL */
#define CONTACTS_QUERY "(contains \"x-evolution-any-field\" \"test\")"
#define CALENDAR_QUERY "(contains? \"summary\"  \"test\")"
#define MAIL_QUERY "(match-all (header-contains \"Subject\"  \"test\"))" 

GMainLoop *main_loop;
static gint iter = 0;

static void
find_folder_item_callback (GObject *object,
                           GAsyncResult *res,
                           gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GError *error = NULL;
	GSList *items = NULL, *l = NULL;
	gboolean include_last;

	e_ews_connection_find_folder_items_finish (cnc, res, &include_last, &items, &error);

	if (error != NULL) {
		g_print ("Unable to find items in %s folder: %s :%d \n", (gchar *) user_data, error->message, error->code);

		++iter;

		/*Check whether we have got responses of all three folder requests*/
		if (iter == 3)
			g_main_loop_quit (main_loop);

		return;
	}

	g_print ("Success : find items in %s folder:", (gchar *) user_data);

	for (l = items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = l->data;
		const EwsId *id = e_ews_item_get_id (item);

		g_print ("FindItem: Id is %s \n", id->id);
	}

	g_slist_foreach (items, (GFunc) g_object_unref, NULL);
	g_slist_free (items);

	++iter;

	/*Check whether we have got responses of all three folder requests*/
	if (iter == 3)
		g_main_loop_quit (main_loop);
}

static void
get_login_info_from_env (const gchar **username,
                         const gchar **password,
                         const gchar **uri)
{
	*username = g_getenv ("EWS_TEST_USERNAME");
	*password = g_getenv ("EWS_TEST_PASSWORD");
	*uri = g_getenv ("EWS_TEST_URI");
}

static void
op_test_finditem_run (void)
{
	const gchar *username;
	const gchar *password;
	const gchar *uri, *folder_name = NULL;
	EEwsConnection *cnc;
	EwsFolderType folder_type;
	EwsFolderId *fid = NULL;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL, 30, NULL, NULL, NULL);
	g_assert (cnc != NULL);

	/*Along with finditem api we check query conversion too*/

	/*Check for contact folder*/
	folder_name = "contacts";
	folder_type = EWS_FOLDER_TYPE_CONTACTS;
	fid = g_new0 (EwsFolderId, 1);
	fid->id = g_strdup (folder_name);
	fid->is_distinguished_id = TRUE;

	e_ews_connection_find_folder_items (
		cnc, EWS_PRIORITY_MEDIUM, fid,
		"IdOnly", NULL, NULL, CONTACTS_QUERY,
		folder_type, (EwsConvertQueryCallback) (e_ews_query_to_restriction),
		cancellable, find_folder_item_callback, (gpointer) folder_name);
	e_ews_folder_id_free (fid);

	/*Check for calendar folder*/
	folder_name = "calendar";
	folder_type = EWS_FOLDER_TYPE_CALENDAR;
	fid = g_new0 (EwsFolderId, 1);
	fid->id = g_strdup (folder_name);
	fid->is_distinguished_id = TRUE;

	e_ews_connection_find_folder_items (
		cnc, EWS_PRIORITY_MEDIUM, fid,
		"IdOnly", NULL, NULL, CALENDAR_QUERY,
		folder_type, (EwsConvertQueryCallback) (e_ews_query_to_restriction),
		cancellable, find_folder_item_callback, (gpointer) folder_name);
	e_ews_folder_id_free (fid);

	/*Check for mail folder*/
	folder_name = "inbox";
	folder_type = EWS_FOLDER_TYPE_MAILBOX;
	fid = g_new0 (EwsFolderId, 1);
	fid->id = g_strdup (folder_name);
	fid->is_distinguished_id = TRUE;

	e_ews_connection_find_folder_items (
		cnc, EWS_PRIORITY_MEDIUM, fid,
		"IdOnly", NULL, NULL, MAIL_QUERY,
		folder_type, (EwsConvertQueryCallback) (e_ews_query_to_restriction),
		cancellable, find_folder_item_callback, (gpointer) folder_name);
	e_ews_folder_id_free (fid);
}

static gboolean
idle_cb (gpointer data)
{

	g_print ("\nTesting the finditem... \n");
	op_test_finditem_run ();

	return FALSE;
}

gint main ()
{
	g_print ("Testing operations... \n");

	g_type_init ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);

	return 0;
}
