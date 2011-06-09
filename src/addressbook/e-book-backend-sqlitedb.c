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

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <libedataserver/e-sexp.h>
#include <libedata-book/e-book-backend-sexp.h>

#include <sqlite3.h>

#include "e-sqlite3-vfs.h"
#include "e-book-backend-sqlitedb.h"

#define d(x)

#define DB_FILENAME "contacts.db"
#define READER_LOCK(ebsdb) g_static_rw_lock_reader_lock (&ebsdb->priv->rwlock)
#define READER_UNLOCK(ebsdb) g_static_rw_lock_reader_unlock (&ebsdb->priv->rwlock)
#define WRITER_LOCK(ebssdb) g_static_rw_lock_writer_lock (&ebsdb->priv->rwlock)
#define WRITER_UNLOCK(ebssdb) g_static_rw_lock_writer_unlock (&ebsdb->priv->rwlock)

struct _EBookBackendSqliteDBPrivate {
	sqlite3 *db;
	gchar *path;
	gchar *hash_key;

	gboolean store_vcard;
	GStaticRWLock rwlock;
};

G_DEFINE_TYPE (EBookBackendSqliteDB, e_book_backend_sqlitedb, G_TYPE_OBJECT)

#define E_BOOK_SDB_ERROR \
	(e_book_backend_sqlitedb_error_quark ())

static GHashTable *db_connections = NULL;
static GStaticMutex dbcon_lock = G_STATIC_MUTEX_INIT;

static int
store_data_to_vcard (gpointer ref, gint ncol, gchar **cols, gchar **name);

static GQuark
e_book_backend_sqlitedb_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "e-book-backend-sqlitedb-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

static void
e_book_backend_sqlitedb_finalize (GObject *object)
{
	EBookBackendSqliteDBPrivate *priv;

	priv = E_BOOK_BACKEND_SQLITEDB (object)->priv;

	g_static_rw_lock_free (&priv->rwlock);

	sqlite3_close (priv->db);
	priv->db = NULL;

	g_free (priv->path);
	priv->path = NULL;

	g_static_mutex_lock (&dbcon_lock);
	if (db_connections != NULL) {
		g_hash_table_remove (db_connections, priv->hash_key);

		if (g_hash_table_size (db_connections) == 0) {
			g_hash_table_destroy (db_connections);
			db_connections = NULL;
		}

		g_free (priv->hash_key);
		priv->hash_key = NULL;
	}
	g_static_mutex_unlock (&dbcon_lock);

	g_free (priv);
	priv = NULL;

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_sqlitedb_parent_class)->finalize (object);
}

static void
e_book_backend_sqlitedb_class_init (EBookBackendSqliteDBClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EBookBackendSqliteDBPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = e_book_backend_sqlitedb_finalize;
}

static void
e_book_backend_sqlitedb_init (EBookBackendSqliteDB *ebsdb)
{
	ebsdb->priv = g_new0 (EBookBackendSqliteDBPrivate, 1) ;

	ebsdb->priv->store_vcard = TRUE;
	g_static_rw_lock_init (&ebsdb->priv->rwlock);
}


static void
e_book_sqlitedb_match_func (sqlite3_context *ctx, gint nArgs, sqlite3_value **values)
{
	gboolean matches = FALSE;
	const gchar *what, *where;

	g_return_if_fail (ctx != NULL);
	g_return_if_fail (nArgs == 2);
	g_return_if_fail (values != NULL);

	what = (const gchar *) sqlite3_value_text (values[0]);
	where = (const gchar *) sqlite3_value_text (values[1]);

	if (what && where && !*what) {
		matches = TRUE;
	} else if (what && where) {
		gboolean word = TRUE;
		gint i, j;

		for (i = 0, j = 0; where[i] && !matches; i++) {
			gchar c = where[i];

			if (c == ' ') {
				word = TRUE;
				j = 0;
			} else if (word && tolower (c) == tolower (what[j])) {
				j++;
				if (what[j] == 0 && (where[i + 1] == 0 || isspace (where[i + 1])))
					matches = TRUE;
			} else {
				word = FALSE;
			}
		}
	}

	sqlite3_result_int (ctx, matches ? 1 : 0);
}

/**
 * e_book_sql_exec
 * @db:
 * @stmt:
 * @callback:
 * @data:
 * @error:
 *
 *  Callers should hold the rw lock depending on read or write operation
 * Returns:
 **/
static gint
book_backend_sql_exec	(sqlite3 *db,
			 const gchar *stmt,
			 gint (*callback)(void*,gint,gchar**,gchar**),
			 gpointer data,
			 GError **error)
{
	gchar *errmsg = NULL;
	gint   ret = -1;

	ret = sqlite3_exec (db, stmt, callback, data, &errmsg);
	while (ret == SQLITE_BUSY || ret == SQLITE_LOCKED || ret == -1) {
		if (errmsg) {
			sqlite3_free (errmsg);
			errmsg = NULL;
		}
		ret = sqlite3_exec (db, stmt, NULL, NULL, &errmsg);
	}

	if (ret != SQLITE_OK) {
		d(g_print ("Error in SQL EXEC statement: %s [%s].\n", stmt, errmsg));
		g_set_error (
			error, E_BOOK_SDB_ERROR,
			0, "%s", errmsg);
		sqlite3_free (errmsg);
		errmsg = NULL;
		return -1;
	}

	if (errmsg) {
		sqlite3_free (errmsg);
		errmsg = NULL;
	}

	return 0;
}

static void
book_backend_sqlitedb_start_transaction (EBookBackendSqliteDB *ebsdb, GError **error)
{
	book_backend_sql_exec (ebsdb->priv->db, "BEGIN", NULL, NULL, error);
}

static void
book_backend_sqlitedb_end_transaction (EBookBackendSqliteDB *ebsdb, GError **error)
{
	if (!error || !*error)
		book_backend_sql_exec (ebsdb->priv->db, "COMMIT", NULL, NULL, error);
	else
		book_backend_sql_exec (ebsdb->priv->db, "ROLLBACK", NULL, NULL, NULL);
}

static void
create_folders_table	(EBookBackendSqliteDB *ebsdb,
			 GError **error)
{
	GError *err = NULL;
	/* sync_data points to syncronization data, it could be last_modified time
	   or a sequence number or some text depending on the backend.

	   parial_content says whether the contents are partially downloaded for
	   auto-completion or if it has the complete content.

	   Have not included a bdata here since the keys table should suffice any
	   additional need that arises.
	 */
	const gchar *stmt = "CREATE TABLE IF NOT EXISTS folders"
			     "( folder_id  TEXT PRIMARY KEY,"
			     " folder_name TEXT,"
			     "  sync_data TEXT,"
			     " is_populated INTEGER,"
			     "  partial_content INTEGER)";

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err)
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL , &err);


	/* Create a child table to store key/value pairs for a folder */
	if (!err) {
		stmt =	"CREATE TABLE IF NOT EXISTS keys"
			"( key TEXT PRIMARY KEY, value TEXT,"
			" folder_id TEXT REFERENCES folders)";
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
	}

	if (!err) {
		stmt = "CREATE INDEX IF NOT EXISTS keysindex ON keys(folder_id)";
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return;
}


static gint
folder_found_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	gboolean *found = ref;

	*found = TRUE;

	return 0;
}

static gboolean
folder_exists	(EBookBackendSqliteDB *ebsdb,
		 const gchar *folderid,
		 GError **error)
{
	gchar *stmt;
	gboolean found = FALSE;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT folder_id FROM folders WHERE folder_id = %Q", folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, folder_found_cb , &found, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return found;
}

static void
add_folder_into_db	(EBookBackendSqliteDB *ebsdb,
			 const gchar *folderid,
			 const gchar *folder_name,
			 GError **error)
{
	gchar *stmt;
	GError *err = NULL;

	if (folder_exists (ebsdb, folderid, error))
		return;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err) {
		stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO folders VALUES ( %Q, %Q, %Q, %d, %d ) ",
		                        folderid, folder_name, NULL, 0, 0);

		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);

		sqlite3_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return;
}


/* The column names match the fields used in book-backend-sexp */
static gint
create_contacts_table	(EBookBackendSqliteDB *ebsdb,
			 const gchar *folderid,
			 GError **error)
{
	gint ret;
	gchar *stmt, *tmp;
	GError *err = NULL;

	stmt = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q"
				"( uid  TEXT PRIMARY KEY,"
				" nickname TEXT, full_name TEXT,"
				" given_name TEXT, family_name TEXT,"
				" file_as TEXT,"
				" email_1 TEXT, email_2 TEXT,"
				" email_3 TEXT, email_4 TEXT,"
				" partial_content INTEGER,"
				" is_list INTEGER, list_show_addresses INTEGER,"
				" wants_html INTEGER,"
				" vcard TEXT, bdata TEXT)", folderid);

	WRITER_LOCK (ebsdb);
	ret = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL , &err);
	sqlite3_free (stmt);


	/* Create indexes on full_name and email_1 as autocompletion queries would mainly
	   rely on this. Assuming that the frequency of matching on these would be higher than
	   on the other fields like email_2, surname etc. email_1 should be the primary email */
	if (!err) {
		tmp = g_strdup_printf("FNINDEX-%s", folderid);
		stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (full_name)", tmp, folderid);
		ret = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		g_free (tmp);
		sqlite3_free (stmt);
	}

	if (!err) {
		tmp = g_strdup_printf("EMINDEX-%s", folderid);
		stmt = sqlite3_mprintf ("CREATE INDEX IF NOT EXISTS %Q ON %Q (email_1)", tmp, folderid);
		ret = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		g_free (tmp);
		sqlite3_free (stmt);
	}

	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return ret;
}

static gboolean
book_backend_sqlitedb_load	(EBookBackendSqliteDB *ebsdb,
				 const gchar *filename,
				 GError **error)
{
	EBookBackendSqliteDBPrivate *priv;
	gint ret;

	priv = ebsdb->priv;

	e_sqlite3_vfs_init ();

	ret = sqlite3_open (filename, &priv->db);
	if (ret) {
		if (!priv->db) {
			g_set_error (
				error, E_BOOK_SDB_ERROR,
				0,
				_("Insufficient memory"));
		} else {
			const gchar *errmsg;
			errmsg = sqlite3_errmsg (priv->db);
			d(g_print("Can't open database %s: %s\n", path, errmsg));
			g_set_error (
				error, E_BOOK_SDB_ERROR,
				0, "%s", errmsg);
			sqlite3_close (priv->db);
		}
		return FALSE;
	}

	sqlite3_create_function (priv->db, "MATCH", 2, SQLITE_UTF8, NULL, e_book_sqlitedb_match_func, NULL, NULL);

	WRITER_LOCK (ebsdb);

	book_backend_sql_exec (priv->db, "ATTACH DATABASE ':memory:' AS mem", NULL, NULL, NULL);
	book_backend_sql_exec (priv->db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

	WRITER_UNLOCK (ebsdb);

	create_folders_table (ebsdb, error);

	return TRUE;
}

/**
 * e_book_backend_sqlitedb_new
 * @path: location where the db would be created
 * @emailid: email id of the user
 * @folderid: folder id of the address-book
 * @folder_name: name of the address-book
 * @store_vcard: True if the vcard should be stored inside db, if FALSE only the summary fields would be stored inside db.
 * @error:
 *
 * If the path for multiple addressbooks are same, the contacts from all addressbooks
 * would be stored in same db in different tables.
 *
 * Returns:
 **/
EBookBackendSqliteDB *
e_book_backend_sqlitedb_new	(const gchar *path,
				 const gchar *emailid,
				 const gchar *folderid,
				 const gchar *folder_name,
				 gboolean store_vcard,
				 GError **error)
{
	EBookBackendSqliteDB *ebsdb;
	gchar *hash_key, *filename;
	GError *err = NULL;

	g_static_mutex_lock (&dbcon_lock);

	hash_key = g_strdup_printf ("%s@%s", emailid, path);
	if (db_connections != NULL) {
		ebsdb = g_hash_table_lookup (db_connections, hash_key);

		if (ebsdb) {
			g_object_ref (ebsdb);
			g_static_mutex_unlock (&dbcon_lock);
			g_free (hash_key);
			goto exit;
		}
	}

	ebsdb = g_object_new	(E_TYPE_BOOK_BACKEND_SQLITEDB, NULL);
	ebsdb->priv->path = g_strdup (path);
	ebsdb->priv->store_vcard = store_vcard;
	if (g_mkdir_with_parents (path, 0777) < 0) {
		g_set_error (error, E_BOOK_SDB_ERROR,
				0, "Can not make parent directory: errno %d", errno);
		return NULL;
	}
	filename = g_build_filename (path, DB_FILENAME, NULL);

	book_backend_sqlitedb_load (ebsdb, filename, &err);
	g_free (filename);

	if (db_connections == NULL)
		db_connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_hash_table_insert (db_connections, hash_key, ebsdb);
	ebsdb->priv->hash_key = g_strdup (hash_key);

	g_static_mutex_unlock (&dbcon_lock);

exit:
	if (!err)
		add_folder_into_db (ebsdb, folderid, folder_name, &err);
	if (!err)
		create_contacts_table (ebsdb, folderid, &err);

	if (err)
		g_propagate_error (error, err);

	return ebsdb;
}

/* Add Contact */
static gchar *
insert_stmt_from_contact	(EContact *contact,
				 gboolean partial_content,
				 const gchar *folderid,
				 gboolean store_vcard)
{
	gchar *stmt = NULL;
	gchar *id, *nickname, *full_name;
	gchar *given_name, *surname, *file_as;
	gchar *email_1, *email_2, *email_3, *email_4;
	gchar *vcard_str = NULL;
	gint wants_html, is_list, list_show_addresses;

	id          = e_contact_get (contact, E_CONTACT_UID);
	nickname    = e_contact_get (contact, E_CONTACT_NICKNAME);
	full_name   = e_contact_get (contact, E_CONTACT_FULL_NAME);
	given_name  = e_contact_get (contact, E_CONTACT_GIVEN_NAME);
	surname     = e_contact_get (contact, E_CONTACT_FAMILY_NAME);
	file_as     = e_contact_get (contact, E_CONTACT_FILE_AS);
	email_1     = e_contact_get (contact, E_CONTACT_EMAIL_1);
	email_2     = e_contact_get (contact, E_CONTACT_EMAIL_2);
	email_3     = e_contact_get (contact, E_CONTACT_EMAIL_3);
	email_4     = e_contact_get (contact, E_CONTACT_EMAIL_4);
	is_list     = GPOINTER_TO_INT (e_contact_get (contact, E_CONTACT_IS_LIST));
	wants_html  = GPOINTER_TO_INT (e_contact_get (contact, E_CONTACT_WANTS_HTML));
	list_show_addresses = GPOINTER_TO_INT (e_contact_get (contact, E_CONTACT_LIST_SHOW_ADDRESSES));

	if (store_vcard)
		vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

	stmt = sqlite3_mprintf ("INSERT or REPLACE INTO %Q VALUES (%Q, %Q, %Q, "
		"%Q, %Q, %Q, %Q, %Q, %Q, %Q, %d, %d, %d, %d, %Q, %Q)", folderid, id, nickname,
				full_name, given_name, surname, file_as, email_1,
				email_2, email_3, email_4, partial_content, is_list, wants_html, list_show_addresses, vcard_str, NULL);

	g_free (id);
	g_free (nickname);
	g_free (given_name);
	g_free (surname);
	g_free (file_as);
	g_free (email_1);
	g_free (email_2);
	g_free (email_3);
	g_free (email_4);
	g_free (vcard_str);

	return stmt;
}

/**
 * e_book_backend_sqlitedb_add_contact
 * @ebsdb:
 * @folderid: folder id
 * @contact: EContact to be added
 * @partial_content: contact does not contain full information. Used when
 * the backend cache's partial information for auto-completion.
 * @error:
 *
 * This is a convenience wrapper for e_book_backend_sqlitedb_add_contacts,
 * which is the preferred means to add multiple contacts when possible.
 *
 * Returns: TRUE on success.
 **/
gboolean
e_book_backend_sqlitedb_add_contact	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 EContact *contact,
					 gboolean partial_content,
					 GError **error)
{
	GSList l;
	l.data = contact;
	l.next = NULL;
	return e_book_backend_sqlitedb_add_contacts (ebsdb, folderid, &l,
						     partial_content, error);
}

/**
 * e_book_backend_sqlitedb_add_contacts
 * @ebsdb:
 * @folderid: folder id
 * @contacts: list of EContacts
 * @partial_content: contact does not contain full information. Used when
 * the backend cache's partial information for auto-completion.
 * @error:
 *
 *
 * Returns: TRUE on success.
 **/
gboolean
e_book_backend_sqlitedb_add_contacts	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 GSList *contacts,
					 gboolean partial_content,
					 GError **error)
{
	GSList *l;
	GError *err = NULL;
	gboolean ret = TRUE;
	EBookBackendSqliteDBPrivate *priv;

	priv = ebsdb->priv;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	for (l = contacts; !err && l != NULL; l = g_slist_next (l)) {
		gchar *stmt;
		EContact *contact = (EContact *) l->data;

		stmt = insert_stmt_from_contact (contact, partial_content, folderid,
		 				 priv->store_vcard);
		book_backend_sql_exec (priv->db, stmt, NULL, NULL, &err);

		sqlite3_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);

	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return ret && !err;
}

gboolean
e_book_backend_sqlitedb_remove_contact	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 const gchar *uid,
					 GError **error)
{
	GSList l;
	l.data = (char*)uid; /* Won't modify it, I promise :) */
	l.next = NULL;
	return e_book_backend_sqlitedb_remove_contacts (ebsdb, folderid, &l,
							error);
}

gboolean
e_book_backend_sqlitedb_remove_contacts	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 GSList *uids,
					 GError **error)
{
	GSList *l;
	GError *err = NULL;
	GString *str;
	gchar *tmp;
	EBookBackendSqliteDBPrivate *priv;

	priv = ebsdb->priv;
	str = g_string_new ("DELETE FROM ");

	tmp = sqlite3_mprintf ("%Q WHERE uid IN (", folderid);
	g_string_append (str, tmp);
	sqlite3_free (tmp);

	for (l = uids; l != NULL; l = g_slist_next (l)) {
		gchar *uid = (gchar *) uids->data;

		tmp = sqlite3_mprintf ("%Q", uid);
		g_string_append_printf (str, " %s ,", tmp);
		sqlite3_free (tmp);
	}

	/* remove the last comma */
	g_string_truncate (str, str->len - 1);
	g_string_append (str, ")");

	WRITER_LOCK (ebsdb);

	if (!err)
		book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err)
		book_backend_sql_exec (priv->db, str->str, NULL, NULL, &err);

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	g_string_free (str, TRUE);

	if (err)
		g_propagate_error (error, err);

	return !err;
}

struct _contact_info {
	gboolean exists;
	gboolean partial_content;
};

static gint
contact_found_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	struct _contact_info *cinfo = ref;

	cinfo->exists = TRUE;
	cinfo->partial_content = cols [0] ? strtoul (cols [0], NULL, 10) : 0;

	return 0;
}

gboolean
e_book_backend_sqlitedb_has_contact	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 const gchar *uid,
					 gboolean *partial_content,
					 GError **error)
{
	GError *err = NULL;
	gchar *stmt;
	struct _contact_info cinfo;

	cinfo.exists = FALSE;
	cinfo.partial_content = FALSE;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT partial_content FROM %Q WHERE uid = %Q", folderid, uid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, contact_found_cb , &cinfo, &err);
	sqlite3_free (stmt);

	if (!err)
		*partial_content = cinfo.partial_content;
	else
		g_propagate_error (error, err);

	READER_UNLOCK (ebsdb);

	return cinfo.exists;
}

static gint
get_vcard_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	gchar **vcard_str = ref;

	if (cols [0])
		*vcard_str = g_strdup (cols [0]);

	return 0;
}

EContact *
e_book_backend_sqlitedb_get_contact	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 const gchar *uid,
					 GError **error) 
{
	GError *err = NULL;
	EContact *contact = NULL;
	gchar *vcard = e_book_backend_sqlitedb_get_vcard_string (ebsdb, folderid, uid, &err);
	if (!err) {
		contact = e_contact_new_from_vcard(vcard);
		g_free (vcard);
	} else
		g_propagate_error (error, err);

	return contact;
}

gchar *
e_book_backend_sqlitedb_get_vcard_string	(EBookBackendSqliteDB *ebsdb,
						 const gchar *folderid,
						 const gchar *uid,
						 GError **error)
{
	gchar *stmt;
	gchar *vcard_str = NULL;

	READER_LOCK (ebsdb);

	if (!ebsdb->priv->store_vcard) {
		GSList *vcards = NULL;
		
		stmt = sqlite3_mprintf ("SELECT uid, nickname, full_name, given_name, family_name, file_As, email_1, email_2, " 
					"email_3, is_list, list_show_addresses, wants_html FROM %Q WHERE uid = %Q", folderid, uid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, store_data_to_vcard, &vcards, error);
		sqlite3_free (stmt);

		if (vcards) {
			vcard_str = vcards->data;
			g_slist_free (vcards);
			vcards = NULL;
		}
	} else {
		stmt = sqlite3_mprintf ("SELECT vcard FROM %Q WHERE uid = %Q", folderid, uid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, get_vcard_cb , &vcard_str, error);
		sqlite3_free (stmt);
	}

	READER_UNLOCK (ebsdb);

	return vcard_str;
}

static ESExpResult *
func_check (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	gint truth = FALSE;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *query_name = argv[0]->value.string;

		if (!strcmp (query_name, "nickname") ||
		    !strcmp (query_name, "full_name") ||
		    !strcmp (query_name, "file_as") ||
		    !strcmp (query_name, "email")) {
			truth = TRUE;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = truth;

	return r;
}

/* 'builtin' functions */
static const struct {
	const gchar *name;
	ESExpFunc *func;
	gint type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} check_symbols[] = {
	{ "contains", func_check, 0 },
	{ "is", func_check, 0 },
	{ "beginswith", func_check, 0 },
	{ "endswith", func_check, 0 },
	{ "exists", func_check, 0 }
};

static gboolean
book_backend_sqlitedb_is_summary_query (const gchar *query)
{
	ESExp *sexp;
	ESExpResult *r;
	gboolean retval;
	gint i;
	gint esexp_error;

	sexp = e_sexp_new ();

	for (i = 0; i < G_N_ELEMENTS (check_symbols); i++) {
		if (check_symbols[i].type == 1) {
			e_sexp_add_ifunction (sexp, 0, check_symbols[i].name,
					     (ESExpIFunc *)check_symbols[i].func, NULL);
		} else {
			e_sexp_add_function (sexp, 0, check_symbols[i].name,
					    check_symbols[i].func, NULL);
		}
	}

	e_sexp_input_text (sexp, query, strlen (query));
	esexp_error = e_sexp_parse (sexp);

	if (esexp_error == -1) {
		return FALSE;
	}

	r = e_sexp_eval (sexp);

	retval = (r && r->type == ESEXP_RES_BOOL && r->value.boolean);

	e_sexp_result_free (sexp, r);

	e_sexp_unref (sexp);

	return retval;
}

static ESExpResult *
func_or (ESExp *f, gint argc, struct _ESExpTerm **argv, gpointer data)
{
	ESExpResult *r, *r1;
	GString *string;
	gint i;

	string = g_string_new("( ");
	for (i = 0; i < argc; i++) {
		r1 = e_sexp_term_eval (f, argv[i]);

		if (r1->type != ESEXP_RES_STRING) {
			e_sexp_result_free (f, r1);
			continue;
		}
		g_string_append_printf(string, "%s%s", r1->value.string, ((argc>1) && (i != argc-1)) ?  " OR ":"");
		e_sexp_result_free (f, r1);
	}
	g_string_append(string, " )");

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	r->value.string = string->str;
	g_string_free (string, FALSE);
	return r;
}


typedef enum {
	MATCH_CONTAINS,
	MATCH_IS,
	MATCH_BEGINS_WITH,
	MATCH_ENDS_WITH
} match_type;

static ESExpResult *
convert_match_exp (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data, match_type match)
{
	ESExpResult *r;
	gchar *str=NULL;

	/* are we inside a match-all? */
	if (argc>1 && argv[0]->type == ESEXP_RES_STRING) {
		const gchar *field;

		/* only a subset of headers are supported .. */
		field = argv[0]->value.string;

		if (argv[1]->type == ESEXP_RES_STRING && argv[1]->value.string [0] != 0) {
			gchar *value=NULL;

			if (match == MATCH_CONTAINS) {
				value = g_strdup_printf ("'%%%s%%'", argv[1]->value.string);
			} else if (match == MATCH_ENDS_WITH) {
				value = g_strdup_printf ("'%%%s'", argv[1]->value.string);
			} else if (match == MATCH_BEGINS_WITH) {
				value = g_strdup_printf ("'%s%%'", argv[1]->value.string);
			} else if (match == MATCH_IS) {
				value = g_strdup_printf ("'%%%s%%'", argv[1]->value.string);
			}

			if (!strcmp (field, "full_name")) {
				gchar *full, *sur, *given, *nick;

				full = g_strdup_printf("(full_name IS NOT NULL AND full_name LIKE %s)",value);
				sur = g_strdup_printf("(family_name IS NOT NULL AND family_name LIKE %s)",value);
				given = g_strdup_printf("(given_name IS NOT NULL AND given_name LIKE %s)",value);
				nick = g_strdup_printf("(nickname IS NOT NULL AND nickname LIKE %s)",value);

				str = g_strdup_printf (" %s OR %s OR %s OR %s ", full, sur, given, nick);

				g_free (full);
				g_free (sur);
				g_free (given);
				g_free (nick);
			} else if (!strcmp (field, "email")) {
				gint i;
				GString *emails = g_string_new (NULL);

				for (i = 1; i < 4; i++) {
					g_string_append_printf (emails, "(email_%d IS NOT NULL AND email_%d LIKE %s)", i, i, value);
					g_string_append (emails, " OR ");
				}
				g_string_append_printf (emails, "(email_4 IS NOT NULL AND email_4 LIKE %s)", value);

				str = emails->str;
				g_string_free (emails, FALSE);
			} else
				str = g_strdup_printf("(%s IS NOT NULL AND %s LIKE %s)", field, field, value);
			g_free (value);
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_STRING);
	r->value.string = str;

	return r;
}

static ESExpResult *
func_contains (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_CONTAINS);
}

static ESExpResult *
func_is (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_IS);
}

static ESExpResult *
func_beginswith (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_BEGINS_WITH);
}

static ESExpResult *
func_endswith (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_ENDS_WITH);
}

/* 'builtin' functions */
static struct {
	const gchar *name;
	ESExpFunc *func;
	guint immediate :1;
} symbols[] = {
	{ "or", (ESExpFunc *) func_or, 1},

	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
};

static char *
sexp_to_sql_query (const gchar *query)
{
	ESExp *sexp;
	ESExpResult *r;
	gint i;
	gchar *res;

	sexp = e_sexp_new ();

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].immediate)
			e_sexp_add_ifunction (sexp, 0, symbols[i].name,
					     (ESExpIFunc *) symbols[i].func, NULL);
		else
			e_sexp_add_function (sexp, 0, symbols[i].name,
					    symbols[i].func, NULL);
	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);
	if (!r)
		return NULL;
	if (r->type == ESEXP_RES_STRING) {
		res = g_strdup (r->value.string);
	} else
		g_assert (0);

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);
	return res;
}

static gint
addto_vcard_list_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	GList **vcards = ref;

	if (cols [0])
		*vcards = g_list_prepend (*vcards, g_strdup (cols [0]));

	return 0;
}

static gint
addto_slist_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	GSList **uids = ref;

	if (cols [0])
		*uids = g_slist_prepend (*uids, g_strdup (cols [0]));

	return 0;
}

static int
store_data_to_vcard (gpointer ref, gint ncol, gchar **cols, gchar **name)
{
	GSList **vcards = ref;
	EContact *contact = e_contact_new ();
	gchar *vcard;
	gint i;

	/* parse through cols, this will be useful if the api starts supporting field restrictions */
	for (i = 0; i < ncol; i++)
	{
		if (!name[i] || !cols[i])
			continue;

		if (!strcmp (name [i], "uid"))
			e_contact_set (contact, E_CONTACT_UID, cols [i]);
		else if (!strcmp (name [i], "nickname"))
			e_contact_set (contact, E_CONTACT_NICKNAME, cols [i]);
		else if (!strcmp (name [i], "full_name"))
			e_contact_set (contact, E_CONTACT_FULL_NAME, cols [i]);
		else if (!strcmp (name [i], "given_name"))
			e_contact_set (contact, E_CONTACT_GIVEN_NAME, cols [i]);
		else if (!strcmp (name [i], "family_name"))
			e_contact_set (contact, E_CONTACT_FAMILY_NAME, cols [i]);
		else if (!strcmp (name [i], "file_As"))
			e_contact_set (contact, E_CONTACT_FILE_AS, cols [i]);
		else if (!strcmp (name [i], "email_1"))
			e_contact_set (contact, E_CONTACT_EMAIL_1, cols [i]);
		else if (!strcmp (name [i], "email_2"))
			e_contact_set (contact, E_CONTACT_EMAIL_2, cols [i]);
		else if (!strcmp (name [i], "email_3"))
			e_contact_set (contact, E_CONTACT_EMAIL_3, cols [i]);
		else if (!strcmp (name [i], "is_list")) {
			gint val = cols[i] ? strtoul (cols[i], NULL, 10) : 0;
			e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (val));
		} else if (!strcmp (name [i], "list_show_addresses")) {
			gint val = cols[i] ? strtoul (cols[i], NULL, 10) : 0;
			e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (val));
		} else if (!strcmp (name [i], "wants_html")) {
			gint val = cols[i] ? strtoul (cols[i], NULL, 10) : 0;
			e_contact_set (contact, E_CONTACT_WANTS_HTML, GINT_TO_POINTER (val));
		}
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	*vcards = g_slist_prepend (*vcards, vcard);

	return 0;
}

static GList *
book_backend_sqlitedb_search_query (EBookBackendSqliteDB *ebsdb, const gchar *sql, const gchar *folderid, GError **error)
{
	GError *err = NULL;
	GList *vcards = NULL;
	gchar *stmt;

	READER_LOCK (ebsdb);

	if (!ebsdb->priv->store_vcard) {
		stmt = sqlite3_mprintf ("SELECT uid, nickname, full_name, given_name, family_name, file_As, email_1, email_2, " 
					"email_3, is_list, list_show_addresses, wants_html FROM %Q WHERE %s", folderid, sql);
		book_backend_sql_exec (ebsdb->priv->db, stmt, store_data_to_vcard, &vcards, &err);
		sqlite3_free (stmt);
	} else {
		stmt = sqlite3_mprintf ("SELECT vcard FROM %Q WHERE %s", folderid, sql);
		book_backend_sql_exec (ebsdb->priv->db, stmt, addto_vcard_list_cb , &vcards, &err);
		sqlite3_free (stmt);
	}

	READER_UNLOCK (ebsdb);

	if (vcards)
		vcards = g_list_reverse (vcards);

	if (err)
		g_propagate_error (error, err);

	return vcards;
}

static GList *
book_backend_sqlitedb_search_full (EBookBackendSqliteDB *ebsdb, const gchar *sexp, const gchar *folderid, GError **error)
{
	GError *err = NULL;
	GList *vcards = NULL, *all = NULL, *l;
	EBookBackendSExp *bsexp = NULL;
	gchar *stmt;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT vcard FROM %Q", folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, addto_vcard_list_cb , &all, &err);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	if (!err) {
		bsexp = e_book_backend_sexp_new (sexp);

		for (l = all; l != NULL; l = g_list_next (l)) {
			if (e_book_backend_sexp_match_vcard (bsexp, l->data))
				vcards = g_list_prepend (vcards, g_strdup (l->data));
		}

		g_object_unref (bsexp);
	}

	g_list_foreach (all, (GFunc) g_free, NULL);
	g_list_free (all);

	return vcards;
}

/**
 * e_book_backend_sqlitedb_search 
 * @ebsdb: 
 * @folderid: 
 * @sexp: search expression.
 * @error: 
 *  Search on summary fields is only supported. Search expression containing
 *  any other field is supported only if backend chooses to store the vcard inside the db.
 * 
 * Returns: 
 **/
GList *
e_book_backend_sqlitedb_search	(EBookBackendSqliteDB *ebsdb,
				 const gchar *folderid,
				 const gchar *sexp,
				 GError **error)
{
	GList *vcards = NULL;

	if (book_backend_sqlitedb_is_summary_query (sexp)) {
		char *sql_query;

		sql_query = sexp_to_sql_query (sexp);
		vcards = book_backend_sqlitedb_search_query (ebsdb, sql_query, folderid, error);
	} else if (ebsdb->priv->store_vcard)
		vcards = book_backend_sqlitedb_search_full (ebsdb, sexp, folderid, error);
	else {
		g_set_error (error, E_BOOK_SDB_ERROR,
				0, "Full vcards are not stored in cache. Hence only summary query is supported.");
	}

	return vcards;
}

static gint
get_bool_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	gboolean *ret = ref;

	*ret = cols [0] ? strtoul (cols [0], NULL, 10) : 0;

	return 0;
}

gboolean
e_book_backend_sqlitedb_get_is_populated	(EBookBackendSqliteDB *ebsdb,
						 const gchar *folderid,
						 GError **error)
{
	gchar *stmt;
	gboolean ret = FALSE;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT is_populated FROM folders WHERE folder_id = %Q", folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, get_bool_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;

}


gboolean
e_book_backend_sqlitedb_set_is_populated	(EBookBackendSqliteDB *ebsdb,
						 const gchar *folderid,
						 gboolean populated,
						 GError **error)
{
	gchar *stmt = NULL;
	GError *err = NULL;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err) {
		stmt = sqlite3_mprintf ("UPDATE folders SET is_populated = %d WHERE folder_id = %Q",
					populated, folderid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		sqlite3_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return !err;
}

/**
 * e_book_backend_sqlitedb_get_has_partial_content 
 * @ebsdb: 
 * @folderid: 
 * @error: 
 * 
 * 
 * Returns: TRUE if the vcards stored in the db were downloaded partially. It is to indicate
 * the stored vcards does not contain the full data.
 **/
gboolean
e_book_backend_sqlitedb_get_has_partial_content	(EBookBackendSqliteDB *ebsdb,
						 const gchar *folderid,
						 GError **error)
{
	gchar *stmt;
	gboolean ret = FALSE;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT partial_content FROM folders WHERE folder_id = %Q", folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, get_bool_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;
}

gboolean
e_book_backend_sqlitedb_set_has_partial_content	(EBookBackendSqliteDB *ebsdb,
						 const gchar *folderid,
						 gboolean partial_content,
						 GError **error)
{
	gchar *stmt = NULL;
	GError *err = NULL;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err) {
		stmt = sqlite3_mprintf ("UPDATE folders SET partial_content = %d WHERE folder_id = %Q",
					partial_content, folderid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		sqlite3_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return !err;
}

static int
get_string_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	gchar **ret = ref;

	*ret = g_strdup (cols [0]);

	return 0;
}

gchar *
e_book_backend_sqlitedb_get_sync_data	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 GError **error)
{
	gchar *stmt, *ret = NULL;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT sync_data FROM folders WHERE folder_id = %Q", folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;
}

gboolean
e_book_backend_sqlitedb_set_sync_data	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 const gchar *sync_data,
					 GError **error)
{
	gchar *stmt = NULL;
	GError *err = NULL;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err) {
		stmt = sqlite3_mprintf ("UPDATE folders SET sync_data = %Q WHERE folder_id = %Q",
					sync_data, folderid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		sqlite3_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return !err;
}

gchar *
e_book_backend_sqlitedb_get_key_value	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 const gchar *key,
					 GError **error)
{
	gchar *stmt, *ret = NULL;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT value FROM keys WHERE folder_id = %Q AND key = %Q",
							folderid, key);
	book_backend_sql_exec (ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;
}

gboolean
e_book_backend_sqlitedb_set_key_value	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 const gchar *key,
					 const gchar *value,
					 GError **error)
{
	gchar *stmt = NULL;
	GError *err = NULL;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err) {
		stmt = sqlite3_mprintf ("INSERT or REPLACE INTO keys (key, value, folder_id)	\
					values (%Q, %Q, %Q)", key, value, folderid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		sqlite3_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return !err;
}

GSList *
e_book_backend_sqlitedb_get_partially_cached_ids	(EBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GError **error)
{
	gchar *stmt;
	GSList *uids = NULL;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT uid FROM %Q WHERE partial_content = 1",
							folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return uids;
}

gboolean
e_book_backend_sqlitedb_delete_addressbook	(EBookBackendSqliteDB *ebsdb,
						 const gchar *folderid,
						 GError **error)
{
	gchar *stmt;
	GError *err = NULL;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	/* delete the contacts table */
	if (!err) {
		stmt = sqlite3_mprintf ("DROP TABLE %Q ", folderid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		sqlite3_free (stmt);
	}

	/* delete the key/value pairs corresponding to this table */
	if (!err) {
		stmt = sqlite3_mprintf ("DELETE FROM keys WHERE folder_id = %Q", folderid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		sqlite3_free (stmt);
	}

	/* delete the folder from the folders table */
	if (!err) {
		stmt = sqlite3_mprintf ("DELETE FROM folders WHERE folder_id = %Q", folderid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL, &err);
		sqlite3_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);
	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return !err;
}
