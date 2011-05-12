/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-backend-sqlitedb.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include <glib.h>
#include <glib-object.h>
#include "e-book-backend-sqlitedb.h"

static GMainLoop *main_loop;
static gchar *cache_path;
const gchar *op;
GError *error;

const gchar *email="test@localhost";
const gchar *folderid = "test_folder_id";
const gchar *folder_name = "test_folder";
const gchar *uid ="pas-id-4DCB9FF200000000";

const gchar *vcard_str =
"BEGIN:VCARD\n"
"VERSION:3.0\n"
"URL:test.com\n"
"TITLE:\n"
"ROLE:\n"
"X-EVOLUTION-MANAGER:\n"
"X-EVOLUTION-ASSISTANT:\n"
"NICKNAME:\n"
"X-EVOLUTION-SPOUSE:\n"
"NOTE:\n"
"FN:test\n"
"N:;test;;;\n"
"X-EVOLUTION-BLOG-URL:test.wordpress.com\n"
"CALURI:\n"
"FBURL:\n"
"X-EVOLUTION-VIDEO-URL:\n"
"X-MOZILLA-HTML:FALSE\n"
"X-EVOLUTION-FILE-AS:test\n"
"EMAIL;X-EVOLUTION-UI-SLOT=1;TYPE=WORK:test@localhost.com\n"
"EMAIL;X-EVOLUTION-UI-SLOT=2;TYPE=HOME:test@localhome.com\n"
"UID:pas-id-4DCB9FF200000000\n"
"REV:2011-05-12T08:53:06Z\n"
"END:VCARD";

static void
quit_tests (void)
{
	
	if (error != NULL) {
		g_print ("Tests failed: %s - %s \n", op, error->message);
		g_clear_error (&error);
	}
	
	g_main_loop_quit (main_loop);
}

static void
add_contacts (EBookBackendSqliteDB *ebsdb)
{
	GSList *contacts = NULL;
	EContact *con;

	g_print ("Adding contact \n");
	op = "add contact";
	
	con = e_contact_new_from_vcard (vcard_str);
	contacts = g_slist_append (contacts, con);
	e_book_backend_sqlitedb_add_contacts (ebsdb, folderid, contacts, FALSE, &error);
	
	g_object_unref (con);
}

static gboolean
start_tests (gpointer data)
{
	EBookBackendSqliteDB *ebsdb;

	g_print ("Creating the sqlitedb \n");
	op = "create sqlitedb";
	ebsdb = e_book_backend_sqlitedb_new 
					(cache_path, email, folderid, folder_name,
					 FALSE, &error);
	if (error)
		goto exit;

	add_contacts (ebsdb);
	
exit:
	g_object_unref (ebsdb);
	quit_tests ();
	return FALSE;	
}

gint
main (gint argc, gchar *argv[])
{
	g_type_init ();
	g_thread_init (NULL);

	if (argc != 2) {
		g_print ("Please enter a path to store the cache \n");
		return -1;
	}

	cache_path = argv[1];

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) start_tests, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);

	return 0;
}
