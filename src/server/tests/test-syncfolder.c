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

/* APIs : e_ews_connection_sync_folder_items; e_ews_connection_sync_folder_hierarchy */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <e-ews-folder.h>
#include <e-ews-item.h>

static void op_test_get_item ();
void op_tests_run ();

static void
folder_items_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GSList *items_created = NULL, *items_updated = NULL;
	GSList *items_deleted = NULL, *l;
	gchar *sync_state = NULL;
	GError *error = NULL;

	/* Only for test program */
	e_ews_connection_sync_folder_items_finish	(cnc, res, &sync_state,
							 &items_created, &items_updated,
							 &items_deleted, &error);

	if (error != NULL) {
		g_print ("Unable to sync the folder items: %s :%d \n", error->message, error->code);
		return;
	}

	g_print ("Sync state for folder is\n  %s \n", sync_state);

	g_print ("Items created \n");
	for (l = items_created; l != NULL;l = g_slist_next (l)) {
		EEwsItem *item = l->data;
		const EwsId *item_id = e_ews_item_get_id (item);

		g_print ("Subject: %s \n Id: %s  \n ChangeKey: %s \n\n", e_ews_item_get_subject (item), item_id->id, item_id->change_key);
		ids = g_slist_append (ids, g_strdup (item_id->id));
		g_object_unref (item);
	}

	g_slist_free (items_created);
	g_slist_free (items_updated);
	g_slist_free (items_deleted);
	g_free (sync_state);

	g_print ("\nTesting get item... \n");
	op_test_get_item ();
}

static void
op_test_sync_folder_items ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;
	/* const gchar *sync_state = NULL; */

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	e_ews_connection_sync_folder_items_start	(cnc, EWS_PRIORITY_MEDIUM, 
							 NULL, inbox_folder_id->id, 
							 "Default", NULL, 
							 500, folder_items_ready_callback, 
							 cancellable, NULL);
}

static void
folder_hierarchy_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL, *l;
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	gchar *sync_state = NULL;
	GError *error = NULL;

	e_ews_connection_sync_folder_hierarchy_finish	(cnc, res, &sync_state,
							 &folders_created, &folders_updated,
							 &folders_deleted, &error);

	if (error != NULL) {
		g_print ("Unable to fetch the folder hierarchy: %s :%d \n", error->message, error->code);
		return;
	}

	g_print ("Sync state \n  %s \n", sync_state);

	g_print ("Folders created \n");
	for (l = folders_created; l != NULL;l = g_slist_next (l)) {
		EEwsFolder *folder = l->data;
		const EwsFolderId *fid = e_ews_folder_get_id (folder);

		g_print ("Name: %s \n Id: %s  \n ChangeKey: %s \n\n", e_ews_folder_get_name (folder), fid->id, fid->change_key);
		if (!strcmp (e_ews_folder_get_name (folder), "Inbox")) {
			inbox_folder_id = g_new0 (EwsFolderId, 1);
			inbox_folder_id->id = g_strdup (fid->id);
			inbox_folder_id->change_key = g_strdup (fid->change_key);
		}
		g_object_unref (folder);
	}

	g_free (sync_state);
	g_slist_free (folders_created);
	g_slist_free (folders_updated);
	g_slist_free (folders_deleted);

	g_print ("\n Testing sync folder items... \n");
	op_test_sync_folder_items ();
}

static void 
op_test_sync_folder_hierarchy ()
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

	e_ews_connection_sync_folder_hierarchy_start	(cnc, EWS_PRIORITY_MEDIUM, 
							 NULL, folder_hierarchy_ready_callback, 
							 cancellable, NULL);

	/* FIXME Have a separate test for cancel without disrupting sync_hierarchy test 	
	thread = g_thread_create ((GThreadFunc) cancel_sync_folder_hierarchy, cancellable, FALSE, &error);
	if (error || !thread) {
		g_warning ("%s: Creation of the thread failed with error: %s", G_STRFUNC, error->message);
		g_error_free (error);
	} */
}

static void
get_item_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GError *error = NULL;
	GSList *items = NULL, *l;

	e_ews_connection_get_items_finish	(cnc, res, &items, &error);

	if (error != NULL) {
		g_print ("Unable to get item: %s :%d \n", error->message, error->code);
		return;
	}

	g_print ("\nMime content of first item is:\n%s\n", e_ews_item_get_mime_content (items->data));

	for (l = items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = l->data;

		g_print ("GetItem: Subject is %s \n", e_ews_item_get_subject (item));
	}

	g_slist_foreach (items, (GFunc) g_object_unref, NULL);
	g_slist_free (items);
}

static void 
op_test_get_item ()
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

	ids = g_slist_reverse (ids);
	e_ews_connection_get_items_start		(cnc, EWS_PRIORITY_MEDIUM, 
						 g_slist_last (ids), "IdOnly", "item:Subject item:DateTimeReceived item:DateTimeSent item:DateTimeCreated item:Size item:HasAttachments message:InternetMessageId message:From message:Sender message:ToRecipients message:CcRecipients message:BccRecipients message:IsRead item:MimeContent item:Importance", FALSE,
						 get_item_ready_callback, 
						 cancellable, NULL);

	g_slist_foreach (ids, (GFunc) g_free, NULL);
	g_slist_free (ids);
}

static void 
op_test_find_item ()
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

	/* FIXME api fix
	e_ews_connection_find_item (cnc, "contacts", cancellable); */
}

void op_tests_run ()
{
	g_print ("Testing operations... \n");

	g_type_init ();
	g_thread_init (NULL);

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}

static gboolean
idle_cb (gpointer data)
{

	g_print ("\nTesting the sync_hierarchy... \n");
	op_test_sync_folder_hierarchy ();

	g_print ("\nTesting find item... \n");
	op_test_find_item ();

	return FALSE;
}

