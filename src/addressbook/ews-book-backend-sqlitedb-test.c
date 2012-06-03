/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* ews-book-backend-sqlitedb-test.c
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
#include <libebook/libebook.h>
#include "ews-book-backend-sqlitedb.h"

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
add_contacts (EwsBookBackendSqliteDB *ebsdb)
{
	GSList *contacts = NULL;
	EContact *con;

	g_print ("Adding contact \n");
	op = "add contact";

	con = e_contact_new_from_vcard (vcard_str);
	contacts = g_slist_append (contacts, con);
	ews_book_backend_sqlitedb_add_contacts (ebsdb, folderid, contacts, FALSE, &error);

	g_object_unref (con);
}

static void
search_db (EwsBookBackendSqliteDB *ebsdb,
           const gchar *type,
           const gchar *sexp)
{
	GSList *vcards;
	EwsSdbSearchData *s_data;

	g_print ("%s - query: %s \n", type, sexp);
	op = type;
	vcards = ews_book_backend_sqlitedb_search (ebsdb, folderid, sexp, NULL, NULL, NULL, &error);
	if (error)
		return;

	s_data = vcards->data;
	g_print ("Result: %s \n", s_data->vcard);
	ews_book_backend_sqlitedb_search_data_free (s_data);
}

static gboolean
start_tests (gpointer data)
{
	EwsBookBackendSqliteDB *ebsdb;
	gboolean populated = FALSE;
	gchar *vcard_str = NULL, *sexp;
	EBookQuery *q;
	GSList *uids = NULL;
	gboolean store_vcard = FALSE;

	g_print ("Creating the sqlitedb \n");
	op = "create sqlitedb";
	ebsdb = ews_book_backend_sqlitedb_new
					(cache_path, email, folderid, folder_name,
					 store_vcard, &error);
	if (error)
		goto exit;

	add_contacts (ebsdb);
	if (error)
		goto exit;

	g_print ("Getting is_populated \n");
	op = "set is_populated";
	ews_book_backend_sqlitedb_set_is_populated (ebsdb, folderid, TRUE, &error);
	if (error)
		goto exit;

	g_print ("Setting is_populated \n");
	op = "set is_populated";
	populated = ews_book_backend_sqlitedb_get_is_populated (ebsdb, folderid, &error);
	if (error)
		goto exit;
	g_print ("Populated: %d \n", populated);

	g_print ("Setting key value \n");
	op = "set key/value";
	ews_book_backend_sqlitedb_set_key_value (ebsdb, folderid, "customkey", "stored", &error);
	if (error)
		goto exit;

	g_print ("Get Vcard string \n");
	op = "get vcard string";
	vcard_str = ews_book_backend_sqlitedb_get_vcard_string (ebsdb, folderid, uid, NULL, NULL, &error);
	if (error)
		goto exit;
	g_print ("VCard: %s \n", vcard_str);
	g_free (vcard_str);

	q = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_CONTAINS, "test");
	sexp = e_book_query_to_string (q);
	search_db (ebsdb, "summary query", sexp);
	e_book_query_unref (q);
	g_free (sexp);
	if (error)
		goto exit;

	if (store_vcard) {
		q = e_book_query_any_field_contains ("word");
		sexp = e_book_query_to_string (q);
		search_db (ebsdb, "full_search query", sexp);
		e_book_query_unref (q);
		g_free (sexp);
		if (error)
			goto exit;
	}

	g_print ("Delete contact \n");
	op = "delete contact";
	uids = g_slist_append (uids, (gchar *) uid);
	ews_book_backend_sqlitedb_remove_contacts (ebsdb, folderid, uids, &error);
	g_slist_free (uids);
	if (error)
		goto exit;

	g_print ("Delete addressbook \n");
	op = "delete addressbook";
	ews_book_backend_sqlitedb_delete_addressbook (ebsdb, folderid, &error);

exit:
	g_object_unref (ebsdb);
	quit_tests ();
	return FALSE;
}

gint
main (gint argc,
      gchar *argv[])
{
	g_type_init ();

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
