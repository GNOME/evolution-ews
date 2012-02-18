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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <sqlite3.h>

#include "libedataserver/e-sexp.h"
#include "libedata-book/e-book-backend-sexp.h"
#include "e-sqlite3-vfs.h"
#include "e-book-backend-sqlitedb.h"

#define d(x)

#define DB_FILENAME "contacts.db"
#define FOLDER_VERSION 1

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

typedef struct {
	EContactField field;            /* The EContact field */
	GType         fundamental_type; /* The fundamental type (string or gint) */
	const gchar  *dbname;           /* The key for this field in the sqlite3 table */
} SummeryField;

static SummeryField summary_fields[] = {
	{ E_CONTACT_UID,                 G_TYPE_STRING, "uid" },
	{ E_CONTACT_REV,                 G_TYPE_STRING, "rev" },
	{ E_CONTACT_FILE_AS,             G_TYPE_STRING, "file_as" },
	{ E_CONTACT_NICKNAME,            G_TYPE_STRING, "nickname" },
	{ E_CONTACT_FULL_NAME,           G_TYPE_STRING, "full_name" },
	{ E_CONTACT_GIVEN_NAME,          G_TYPE_STRING, "given_name" },
	{ E_CONTACT_FAMILY_NAME,         G_TYPE_STRING, "family_name" },
	{ E_CONTACT_EMAIL_1,             G_TYPE_STRING, "email_1" },
	{ E_CONTACT_EMAIL_2,             G_TYPE_STRING, "email_2" },
	{ E_CONTACT_EMAIL_3,             G_TYPE_STRING, "email_3" },
	{ E_CONTACT_EMAIL_4,             G_TYPE_STRING, "email_4" },
	{ E_CONTACT_IS_LIST,             G_TYPE_BOOLEAN, "is_list" },
	{ E_CONTACT_LIST_SHOW_ADDRESSES, G_TYPE_BOOLEAN, "list_show_addresses" },
	{ E_CONTACT_WANTS_HTML,          G_TYPE_BOOLEAN, "wants_html" }
};

static const gchar *
summary_dbname_from_field (EContactField field)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (summary_fields); i++) {
		if (summary_fields[i].field == field)
			return summary_fields[i].dbname;
	}
	return NULL;
}

static gint
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
	ebsdb->priv = g_new0 (EBookBackendSqliteDBPrivate, 1);

	ebsdb->priv->store_vcard = TRUE;
	g_static_rw_lock_init (&ebsdb->priv->rwlock);
}

static void
e_book_sqlitedb_match_func (sqlite3_context *ctx,
                            gint nArgs,
                            sqlite3_value **values)
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
book_backend_sql_exec (sqlite3 *db,
                       const gchar *stmt,
                       gint (*callback)(gpointer ,gint,gchar **,gchar **),
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
book_backend_sqlitedb_start_transaction (EBookBackendSqliteDB *ebsdb,
                                         GError **error)
{
	book_backend_sql_exec (ebsdb->priv->db, "BEGIN", NULL, NULL, error);
}

static void
book_backend_sqlitedb_end_transaction (EBookBackendSqliteDB *ebsdb,
                                       GError **error)
{
	if (!error || !*error)
		book_backend_sql_exec (ebsdb->priv->db, "COMMIT", NULL, NULL, error);
	else
		book_backend_sql_exec (ebsdb->priv->db, "ROLLBACK", NULL, NULL, NULL);
}

static void
create_folders_table (EBookBackendSqliteDB *ebsdb,
                      GError **error)
{
	GError *err = NULL;
	/* sync_data points to syncronization data, it could be last_modified time
	 * or a sequence number or some text depending on the backend.
	 *
	 * partial_content says whether the contents are partially downloaded for
	 * auto-completion or if it has the complete content.
	 *
	 * Have not included a bdata here since the keys table should suffice any
	 * additional need that arises.
	 */
	const gchar *stmt = "CREATE TABLE IF NOT EXISTS folders"
			     "( folder_id  TEXT PRIMARY KEY,"
			     " folder_name TEXT,"
			     "  sync_data TEXT,"
			     " is_populated INTEGER,"
			     "  partial_content INTEGER,"
			     " version INTEGER)";

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
folder_found_cb (gpointer ref,
                 gint col,
                 gchar **cols,
                 gchar **name)
{
	gboolean *found = ref;

	*found = TRUE;

	return 0;
}

static gboolean
folder_exists (EBookBackendSqliteDB *ebsdb,
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
add_folder_into_db (EBookBackendSqliteDB *ebsdb,
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
		stmt = sqlite3_mprintf ("INSERT OR REPLACE INTO folders VALUES ( %Q, %Q, %Q, %d, %d, %d ) ",
					folderid, folder_name, NULL, 0, 0, FOLDER_VERSION);

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
create_contacts_table (EBookBackendSqliteDB *ebsdb,
                       const gchar *folderid,
                       GError **error)
{
	gint ret, i;
	gchar *stmt, *tmp;
	GError *err = NULL;
	GString *string;

	/* Construct the create statement from the summary fields table */
	string = g_string_new ("CREATE TABLE IF NOT EXISTS %Q ( uid TEXT PRIMARY KEY, ");

	for (i = 1; i < G_N_ELEMENTS (summary_fields); i++) {
		g_string_append   (string, summary_fields[i].dbname);
		g_string_append_c (string, ' ');

		if (summary_fields[i].fundamental_type == G_TYPE_STRING)
			g_string_append (string, "TEXT, ");
		else if (summary_fields[i].fundamental_type == G_TYPE_BOOLEAN)
			g_string_append (string, "INTEGER, ");
		else
			g_assert_not_reached ();
	}
	g_string_append (string, "vcard TEXT, bdata TEXT)");

	stmt = sqlite3_mprintf (string->str, folderid);
	g_string_free (string, TRUE);

	WRITER_LOCK (ebsdb);
	ret = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL , &err);
	sqlite3_free (stmt);

	/* Create indexes on full_name and email_1 as autocompletion queries would mainly
	 * rely on this. Assuming that the frequency of matching on these would be higher than
	 * on the other fields like email_2, surname etc. email_1 should be the primary email */
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
book_backend_sqlitedb_load (EBookBackendSqliteDB *ebsdb,
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
 *
 * Since: 3.2
 **/
EBookBackendSqliteDB *
e_book_backend_sqlitedb_new (const gchar *path,
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

/* Add Contact (free the result with g_free() ) */
static gchar *
insert_stmt_from_contact (EContact *contact,
                          gboolean partial_content,
                          const gchar *folderid,
                          gboolean store_vcard)
{
	GString *string;
	gchar   *str, *vcard_str;
	gint     i;

	str    = sqlite3_mprintf ("INSERT or REPLACE INTO %Q VALUES (", folderid);
	string = g_string_new (str);
	sqlite3_free (str);

	for (i = 0; i < G_N_ELEMENTS (summary_fields); i++) {
		if (i > 0)
			g_string_append (string, ", ");

		if (summary_fields[i].fundamental_type == G_TYPE_STRING) {
			gchar *val;

			val = e_contact_get (contact, summary_fields[i].field);
			str = sqlite3_mprintf ("%Q", val);

			g_string_append (string, str);

			sqlite3_free (str);
			g_free (val);

		} else if (summary_fields[i].fundamental_type == G_TYPE_BOOLEAN) {
			gboolean val;

			val = e_contact_get (contact, summary_fields[i].field) ? TRUE : FALSE;
			g_string_append_printf (string, "%d", val ? 1 : 0);

		} else
			g_assert_not_reached ();
	}

	vcard_str = store_vcard ? e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30) : NULL;
	str       = sqlite3_mprintf (", %Q, %Q)", vcard_str, NULL);

	g_string_append (string, str);

	sqlite3_free (str);
	g_free (vcard_str);

	return g_string_free (string, FALSE);
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
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_add_contact (EBookBackendSqliteDB *ebsdb,
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
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_add_contacts (EBookBackendSqliteDB *ebsdb,
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

		g_free (stmt);
	}

	book_backend_sqlitedb_end_transaction (ebsdb, &err);

	WRITER_UNLOCK (ebsdb);

	if (err)
		g_propagate_error (error, err);

	return ret && !err;
}

/**
 * e_book_backend_sqlitedb_remove_contact:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_remove_contact (EBookBackendSqliteDB *ebsdb,
                                        const gchar *folderid,
                                        const gchar *uid,
                                        GError **error)
{
	GSList l;
	l.data = (gchar *) uid; /* Won't modify it, I promise :) */
	l.next = NULL;
	return e_book_backend_sqlitedb_remove_contacts (ebsdb, folderid, &l,
							error);
}

/**
 * e_book_backend_sqlitedb_remove_contacts:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_remove_contacts (EBookBackendSqliteDB *ebsdb,
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
contact_found_cb (gpointer ref,
                  gint col,
                  gchar **cols,
                  gchar **name)
{
	struct _contact_info *cinfo = ref;

	cinfo->exists = TRUE;
	cinfo->partial_content = cols[0] ? strtoul (cols[0], NULL, 10) : 0;

	return 0;
}

/**
 * e_book_backend_sqlitedb_has_contact:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_has_contact (EBookBackendSqliteDB *ebsdb,
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
get_vcard_cb (gpointer ref,
              gint col,
              gchar **cols,
              gchar **name)
{
	gchar **vcard_str = ref;

	if (cols[0])
		*vcard_str = g_strdup (cols [0]);

	return 0;
}

/**
 * e_book_backend_sqlitedb_get_contact:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
EContact *
e_book_backend_sqlitedb_get_contact (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     const gchar *uid,
                                     GHashTable *fields_of_interest,
                                     gboolean *with_all_required_fields,
                                     GError **error)
{
	GError *err = NULL;
	EContact *contact = NULL;
	gchar *vcard = e_book_backend_sqlitedb_get_vcard_string (ebsdb, folderid, uid,
								 fields_of_interest, with_all_required_fields, &err);
	if (!err) {
		contact = e_contact_new_from_vcard (vcard);
		g_free (vcard);
	} else
		g_propagate_error (error, err);

	return contact;
}

static void
accumulate_fields_select_stmt (const gchar *field_name,
                               gpointer is_present,
                               GString *string)
{
	EContactField field = e_contact_field_id (field_name);
	const gchar *dbname = NULL;

	if (field == E_CONTACT_UID)
		return;

	dbname = summary_dbname_from_field (field);

	/* The field of interest is not in the summary information,
	 * technically we shouldnt reach this case
	 */
	if (!dbname)
		return;

	g_string_append (string, ", ");
	g_string_append (string, dbname);
}

static void
check_field_foreach (const gchar *field_name,
                     gpointer is_present,
                     gboolean *is_summary_query)
{
	EContactField field = e_contact_field_id (field_name);

	if (!summary_dbname_from_field (field)) {
		*is_summary_query = FALSE;
	}
}

/**
 * e_book_backend_sqlitedb_is_summary_fields:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_is_summary_fields (GHashTable *fields_of_interest)
{
	gboolean summary_fields = TRUE;

	if (!fields_of_interest)
		return FALSE;

	g_hash_table_foreach (fields_of_interest, (GHFunc) check_field_foreach, &summary_fields);

	return summary_fields;
}

/* free return value with g_free */
static gchar *
summary_select_stmt (const gchar *folderid,
                     GHashTable *fields_of_interest,
                     gboolean *with_all_required_fields)
{
	GString   *string;
	gchar     *str;

	string = g_string_new ("SELECT uid");

	/* If filtering by fields of interest, only query those and include the 'uid'
	 *
	 */
	if (fields_of_interest && e_book_backend_sqlitedb_is_summary_fields (fields_of_interest)) {
		g_hash_table_foreach (fields_of_interest, (GHFunc) accumulate_fields_select_stmt, string);

		/* The query should return all the required information */
		if (with_all_required_fields)
			*with_all_required_fields = TRUE;
	} else if (with_all_required_fields) {
		/* If the fields of interest is null or contains fields that are not 
		 * part of the summary then only the uids are returned.
		 */
		*with_all_required_fields = FALSE;
	}

	str = sqlite3_mprintf (" FROM %Q", folderid);
	g_string_append (string, str);
	sqlite3_free (str);

	return g_string_free (string, FALSE);
}

/**
 * e_book_backend_sqlitedb_get_vcard_string:
 * @ebsdb: An #EBookBackendSqliteDB
 * @folderid: The folder id
 * @uid: The uid to fetch a vcard for
 * @fields_of_interest: The required fields for this vcard, or %NULL to require all fields.
 * @with_all_required_fields: (allow none) (out): Whether all the required fields are present in the returned vcard.
 * @error: A location to store any error that may have occurred.
 *
 * Searches @ebsdb in the context of @folderid for @uid.
 *
 * If @ebsdb is configured to store the whole vcards, the whole vcard will be returned.
 * Otherwise the summary cache will be searched and the virtual vcard will be built
 * from the summary cache.
 *
 * In either case, @with_all_required_fields if specified, will be updated to reflect whether
 * the returned vcard string satisfies the passed 'fields_of_interest' parameter.
 * 
 * Returns: (transfer full): The vcard string for @uid or %NULL if @uid was not found.
 *
 * Since: 3.2
 */
gchar *
e_book_backend_sqlitedb_get_vcard_string (EBookBackendSqliteDB *ebsdb,
                                          const gchar *folderid,
                                          const gchar *uid,
                                          GHashTable *fields_of_interest,
                                          gboolean *with_all_required_fields,
                                          GError **error)
{
	gchar *stmt, *select_stmt;
	gchar *vcard_str = NULL;
	gboolean local_with_all_required_fields = FALSE;

	READER_LOCK (ebsdb);

	if (!ebsdb->priv->store_vcard) {
		GSList *vcards = NULL;

		select_stmt = summary_select_stmt (folderid, fields_of_interest, &local_with_all_required_fields);
		stmt        = sqlite3_mprintf ("%s WHERE uid = %Q", select_stmt, uid);

		book_backend_sql_exec (ebsdb->priv->db, stmt, store_data_to_vcard, &vcards, error);

		sqlite3_free (stmt);
		g_free (select_stmt);

		if (vcards) {
			EbSdbSearchData *s_data = (EbSdbSearchData *) vcards->data;

			vcard_str     = s_data->vcard;
			s_data->vcard = NULL;

			e_book_backend_sqlitedb_search_data_free (s_data);

			g_slist_free (vcards);
			vcards = NULL;
		}
	} else {
		stmt = sqlite3_mprintf ("SELECT vcard FROM %Q WHERE uid = %Q", folderid, uid);
		book_backend_sql_exec (ebsdb->priv->db, stmt, get_vcard_cb , &vcard_str, error);
		sqlite3_free (stmt);

		local_with_all_required_fields = TRUE;
	}

	READER_UNLOCK (ebsdb);

	if (with_all_required_fields)
		*with_all_required_fields = local_with_all_required_fields;

	return vcard_str;
}

static ESExpResult *
func_check (struct _ESExp *f,
            gint argc,
            struct _ESExpResult **argv,
            gpointer data)
{
	ESExpResult *r;
	gint truth = FALSE;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {

		gchar *query_name = argv[0]->value.string;
		gint   i;

		/* Special case, when testing the special symbolic 'any field' we can
		 * consider it a summary query (it's similar to a 'no query'). */
		if (!strcmp ("x-evolution-any-field", query_name))
			truth = TRUE;

		for (i = 0; truth == FALSE && i < G_N_ELEMENTS (summary_fields); i++) {

			if (!strcmp (e_contact_field_name (summary_fields[i].field), query_name))
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

/**
 * e_book_backend_sqlitedb_is_summary_query:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_is_summary_query (const gchar *query)
{
	ESExp *sexp;
	ESExpResult *r;
	gboolean retval;
	gint i;
	gint esexp_error;

	g_return_val_if_fail (query != NULL, FALSE);
	g_return_val_if_fail (*query, FALSE);

	sexp = e_sexp_new ();

	for (i = 0; i < G_N_ELEMENTS (check_symbols); i++) {
		if (check_symbols[i].type == 1) {
			e_sexp_add_ifunction (sexp, 0, check_symbols[i].name,
					      (ESExpIFunc *) check_symbols[i].func, NULL);
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
func_and (ESExp *f,
          gint argc,
          struct _ESExpTerm **argv,
          gpointer data)
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
		if (r1->value.string && *r1->value.string)
			g_string_append_printf(string, "%s%s", r1->value.string, ((argc>1) && (i != argc-1)) ?  " AND ":"");
		e_sexp_result_free (f, r1);
	}
	g_string_append(string, " )");
	r = e_sexp_result_new (f, ESEXP_RES_STRING);

	if (strlen (string->str) == 4)
		r->value.string = g_strdup("");
	else
		r->value.string = string->str;
	g_string_free (string, FALSE);

	return r;
}

static ESExpResult *
func_or (ESExp *f,
         gint argc,
         struct _ESExpTerm **argv,
         gpointer data)
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
convert_match_exp (struct _ESExp *f,
                   gint argc,
                   struct _ESExpResult **argv,
                   gpointer data,
                   match_type match)
{
	ESExpResult *r;
	gchar *str = NULL;

	/* are we inside a match-all? */
	if (argc > 1 && argv[0]->type == ESEXP_RES_STRING) {
		const gchar *field;

		/* only a subset of headers are supported .. */
		field = argv[0]->value.string;

		if (argv[1]->type == ESEXP_RES_STRING && argv[1]->value.string[0] != 0) {
			gchar *value = NULL;

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
func_contains (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_CONTAINS);
}

static ESExpResult *
func_is (struct _ESExp *f,
         gint argc,
         struct _ESExpResult **argv,
         gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_IS);
}

static ESExpResult *
func_beginswith (struct _ESExp *f,
                 gint argc,
                 struct _ESExpResult **argv,
                 gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_BEGINS_WITH);
}

static ESExpResult *
func_endswith (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	return convert_match_exp (f, argc, argv, data, MATCH_ENDS_WITH);
}

/* 'builtin' functions */
static struct {
	const gchar *name;
	ESExpFunc *func;
	guint immediate :1;
} symbols[] = {
	{ "and", (ESExpFunc *) func_and, 1},
	{ "or", (ESExpFunc *) func_or, 1},

	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
};

static gchar *
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
addto_vcard_list_cb (gpointer ref,
                     gint col,
                     gchar **cols,
                     gchar **name)
{
	GSList **vcard_data = ref;
	EbSdbSearchData *s_data = g_new0 (EbSdbSearchData, 1);

	if (cols[0])
		s_data->uid = g_strdup (cols[0]);

	if (cols[1])
		s_data->vcard = g_strdup (cols[1]);

	if (cols[2])
		s_data->bdata = g_strdup (cols[2]);

	*vcard_data = g_slist_prepend (*vcard_data, s_data);

	return 0;
}

static gint
addto_slist_cb (gpointer ref,
                gint col,
                gchar **cols,
                gchar **name)
{
	GSList **uids = ref;

	if (cols[0])
		*uids = g_slist_prepend (*uids, g_strdup (cols [0]));

	return 0;
}

static gint
store_data_to_vcard (gpointer ref,
                     gint ncol,
                     gchar **cols,
                     gchar **name)
{
	GSList **vcard_data = ref;
	EbSdbSearchData *search_data = g_new0 (EbSdbSearchData, 1);
	EContact *contact = e_contact_new ();
	gchar *vcard;
	gint i, j;

	/* parse through cols, this will be useful if the api starts supporting field restrictions */
	for (i = 0; i < ncol; i++)
	{
		gboolean found = FALSE;

		if (!name[i] || !cols[i])
			continue;

		for (j = 0; j < G_N_ELEMENTS (summary_fields); j++) {

			if (!strcmp (name[i], summary_fields[j].dbname)) {

				if (summary_fields[j].fundamental_type == G_TYPE_STRING)
					e_contact_set (contact, summary_fields[j].field, cols[i]);
				else if (summary_fields[j].fundamental_type == G_TYPE_BOOLEAN) {
					gboolean val = cols[i] ? strtoul (cols[i], NULL, 10) != 0 : FALSE;
					e_contact_set (contact, summary_fields[j].field, GINT_TO_POINTER (val ? TRUE : FALSE));
				} else
					g_assert_not_reached ();

				if (summary_fields[j].field == E_CONTACT_UID)
					search_data->uid = g_strdup (cols[i]);

				found = TRUE;
				break;
			}
		}

		if (found)
			continue;

		if (!strcmp (name [i], "bdata"))
			search_data->bdata = g_strdup (cols[i]);
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	search_data->vcard = vcard;
	*vcard_data = g_slist_prepend (*vcard_data, search_data);

	g_object_unref (contact);
	return 0;
}

static GSList *
book_backend_sqlitedb_search_query (EBookBackendSqliteDB *ebsdb,
                                         const gchar *sql,
                                         const gchar *folderid,
					 /* const */ GHashTable *fields_of_interest,
                                         gboolean *with_all_required_fields,
                                         GError **error)
{
	GError *err = NULL;
	GSList *vcard_data = NULL;
	gchar  *stmt, *select_stmt;
	gboolean local_with_all_required_fields = FALSE;

	READER_LOCK (ebsdb);

	if (!ebsdb->priv->store_vcard) {

		select_stmt = summary_select_stmt (folderid, fields_of_interest, &local_with_all_required_fields);

		if (sql && sql[0]) {
			stmt = sqlite3_mprintf ("%s WHERE %s", select_stmt, sql);

			book_backend_sql_exec (ebsdb->priv->db, stmt, store_data_to_vcard, &vcard_data, &err);
			sqlite3_free (stmt);
		} else
			book_backend_sql_exec (ebsdb->priv->db, select_stmt,
					       store_data_to_vcard, &vcard_data, &err);

		g_free (select_stmt);

	} else {
		if (sql && sql[0]) {
			stmt = sqlite3_mprintf ("SELECT uid, vcard, bdata FROM %Q WHERE %s", folderid, sql);
			book_backend_sql_exec (ebsdb->priv->db, stmt, addto_vcard_list_cb , &vcard_data, &err);
			sqlite3_free (stmt);
		} else {
			stmt = sqlite3_mprintf ("SELECT uid, vcard, bdata FROM %Q", folderid);
			book_backend_sql_exec (ebsdb->priv->db, stmt, addto_vcard_list_cb , &vcard_data, &err);
			sqlite3_free (stmt);
		}
		local_with_all_required_fields = TRUE;
	}

	READER_UNLOCK (ebsdb);

	if (vcard_data)
		vcard_data = g_slist_reverse (vcard_data);

	if (err)
		g_propagate_error (error, err);

	if (with_all_required_fields)
		* with_all_required_fields = local_with_all_required_fields;

	return vcard_data;
}

static GSList *
book_backend_sqlitedb_search_full (EBookBackendSqliteDB *ebsdb,
                                   const gchar *sexp,
                                   const gchar *folderid,
                                   gboolean return_uids,
                                   GError **error)
{
	GError *err = NULL;
	GSList *r_list = NULL, *all = NULL, *l;
	EBookBackendSExp *bsexp = NULL;
	gchar *stmt;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT uid, vcard, bdata FROM %Q", folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, addto_vcard_list_cb , &all, &err);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	if (!err) {
		bsexp = e_book_backend_sexp_new (sexp);

		for (l = all; l != NULL; l = g_slist_next (l)) {
			EbSdbSearchData *s_data = (EbSdbSearchData *) l->data;

			if (e_book_backend_sexp_match_vcard (bsexp, s_data->vcard)) {
				if (!return_uids)
					r_list = g_slist_prepend (r_list, s_data);
				else {
					r_list = g_slist_prepend (r_list, g_strdup (s_data->uid));
					e_book_backend_sqlitedb_search_data_free (s_data);
				}
			} else
				e_book_backend_sqlitedb_search_data_free (s_data);
		}

		g_object_unref (bsexp);
	}

	g_slist_free (all);

	return r_list;
}

/**
 * e_book_backend_sqlitedb_search 
 * @ebsdb: 
 * @folderid: 
 * @sexp: search expression; use NULL or an empty string to get all stored contacts.
 * @fields_of_interest: a #GHashTable containing the names of fields to return, or NULL for all. 
 *  At the moment if this is non-null, the vcard will be populated with summary fields, else it would return the 
 *  whole vcard if its stored in the db. [not implemented fully]
 * @searched: (allow none) (out): Whether @ebsdb was capable of searching for the provided query @sexp.
 * @with_all_required_fields: (allow none) (out): Whether all the required fields are present in the returned vcards.
 * @error: 
 *
 * Searching with summary fields is always supported. Search expressions containing
 * any other field is supported only if backend chooses to store the vcard inside the db.
 *
 * Summary fields - uid, rev, nickname, given_name, family_name, file_as email_1, email_2, email_3, email_4, is_list, 
 * list_show_addresses, wants_html
 *
 * If @ebsdb was incapable of returning vcards with results that satisfy
 * @fields_of_interest, then @with_all_required_fields will be updated to @FALSE
 * and only uid fields will be present in the returned vcards. This can be useful
 * when a summary query succeeds and the returned list can be used to iterate
 * and fetch for full required data from another persistance.
 *
 * Returns: List of EbSdbSearchData.
 *
 * Since: 3.2
 **/
GSList *
e_book_backend_sqlitedb_search (EBookBackendSqliteDB *ebsdb,
                                const gchar *folderid,
                                const gchar *sexp,
                                /* const */ GHashTable *fields_of_interest,
                                gboolean *searched,
                                gboolean *with_all_required_fields,
                                GError **error)
{
	GSList *search_contacts = NULL;
	gboolean local_searched = FALSE;
	gboolean local_with_all_required_fields = FALSE;

	if (sexp && !*sexp)
		sexp = NULL;

	if (!sexp || e_book_backend_sqlitedb_is_summary_query (sexp)) {
		gchar *sql_query;

		sql_query = sexp ? sexp_to_sql_query (sexp) : NULL;
		search_contacts = book_backend_sqlitedb_search_query (ebsdb, sql_query, folderid,
								      fields_of_interest,
								      &local_with_all_required_fields, error);
		g_free (sql_query);

		local_searched = TRUE;

	} else if (ebsdb->priv->store_vcard) {
		search_contacts = book_backend_sqlitedb_search_full (ebsdb, sexp, folderid, FALSE, error);
		local_searched = TRUE;
		local_with_all_required_fields = TRUE;
	} else {
		g_set_error (error, E_BOOK_SDB_ERROR,
				0, "Full search_contacts are not stored in cache. Hence only summary query is supported.");
	}

	if (searched)
		*searched = local_searched;
	if (with_all_required_fields)
		*with_all_required_fields = local_with_all_required_fields;

	return search_contacts;
}

/**
 * e_book_backend_sqlitedb_search_uids:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
GSList *
e_book_backend_sqlitedb_search_uids (EBookBackendSqliteDB *ebsdb,
                                     const gchar *folderid,
                                     const gchar *sexp,
                                     gboolean *searched,
                                     GError **error)
{
	GSList *uids = NULL;
	gboolean local_searched = FALSE;

	if (sexp && !*sexp)
		sexp = NULL;

	if (!sexp || e_book_backend_sqlitedb_is_summary_query (sexp)) {
		gchar *stmt;
		gchar *sql_query = sexp ? sexp_to_sql_query (sexp) : NULL;

		READER_LOCK (ebsdb);

		stmt = sqlite3_mprintf ("SELECT uid FROM %Q%s%s", folderid, sql_query ? " WHERE " : "", sql_query ? sql_query : "");
		book_backend_sql_exec (ebsdb->priv->db, stmt, addto_slist_cb, &uids, error);
		sqlite3_free (stmt);

		READER_UNLOCK (ebsdb);

		local_searched = TRUE;

		g_free (sql_query);
	} else if (ebsdb->priv->store_vcard) {
		uids = book_backend_sqlitedb_search_full (ebsdb, sexp, folderid, TRUE, error);

		local_searched = TRUE;
	} else {
		g_set_error (error, E_BOOK_SDB_ERROR,
				0, "Full vcards are not stored in cache. Hence only summary query is supported.");
	}

	if (searched)
		*searched = local_searched;

	return uids;
}

static gint
get_bool_cb (gpointer ref,
             gint col,
             gchar **cols,
             gchar **name)
{
	gboolean *ret = ref;

	*ret = cols [0] ? strtoul (cols [0], NULL, 10) : 0;

	return 0;
}

/**
 * e_book_backend_sqlitedb_get_is_populated:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_get_is_populated (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_set_is_populated:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_is_populated (EBookBackendSqliteDB *ebsdb,
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
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_get_has_partial_content (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_set_has_partial_content:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_has_partial_content (EBookBackendSqliteDB *ebsdb,
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

static gint
get_string_cb (gpointer ref,
               gint col,
               gchar **cols,
               gchar **name)
{
	gchar **ret = ref;

	*ret = g_strdup (cols [0]);

	return 0;
}

/**
 * e_book_backend_sqlitedb_get_contact_bdata:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gchar *
e_book_backend_sqlitedb_get_contact_bdata (EBookBackendSqliteDB *ebsdb,
                                           const gchar *folderid,
                                           const gchar *uid,
                                           GError **error)
{
	gchar *stmt, *ret = NULL;

	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT bdata FROM %Q WHERE uid = %Q", folderid, uid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, get_string_cb , &ret, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	return ret;
}

/**
 * e_book_backend_sqlitedb_set_contact_bdata:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_contact_bdata (EBookBackendSqliteDB *ebsdb,
                                           const gchar *folderid,
                                           const gchar *uid,
                                           const gchar *value,
                                           GError **error)
{
	gchar *stmt = NULL;
	GError *err = NULL;

	WRITER_LOCK (ebsdb);
	book_backend_sqlitedb_start_transaction (ebsdb, &err);

	if (!err) {
		stmt = sqlite3_mprintf ("UPDATE %Q SET bdata = %Q WHERE uid = %Q", folderid,
					value, uid);
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
 * e_book_backend_sqlitedb_get_sync_data:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gchar *
e_book_backend_sqlitedb_get_sync_data (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_set_sync_data:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_sync_data (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_get_key_value:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gchar *
e_book_backend_sqlitedb_get_key_value (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_set_key_value:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_set_key_value (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_get_partially_cached_ids:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
GSList *
e_book_backend_sqlitedb_get_partially_cached_ids (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_delete_addressbook:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_delete_addressbook (EBookBackendSqliteDB *ebsdb,
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

/**
 * e_book_backend_sqlitedb_search_data_free:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
void
e_book_backend_sqlitedb_search_data_free (EbSdbSearchData *s_data)
{
	if (s_data) {
		g_free (s_data->uid);
		g_free (s_data->vcard);
		g_free (s_data->bdata);
		g_free (s_data);
	}
}

gboolean
e_book_backend_sqlitedb_create_addressbook (EBookBackendSqliteDB *ebsdb,
                                            const gchar *folderid,
                                            const gchar *folder_name,
                                            gboolean store_vcard,
                                            GError **error)
{
	GError *err = NULL;
	gboolean ret = TRUE;

	add_folder_into_db (ebsdb, folderid, folder_name, &err);
	if (!err)
		create_contacts_table (ebsdb, folderid, &err);

	if (err) {
		g_propagate_error (error, err);
		ret = FALSE;
	}

	return ret;
}

/**
 * e_book_backend_sqlitedb_remove:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
gboolean
e_book_backend_sqlitedb_remove (EBookBackendSqliteDB *ebsdb,
                                GError **error)
{
	EBookBackendSqliteDBPrivate *priv;
	gchar *filename;
	gint ret;

	priv = ebsdb->priv;

	WRITER_LOCK (ebsdb);

	sqlite3_close (priv->db);
	filename = g_build_filename (priv->path, DB_FILENAME, NULL);
	ret = g_unlink (filename);

	WRITER_UNLOCK (ebsdb);

	g_free (filename);
	if (ret == -1) {
		g_set_error (error, E_BOOK_SDB_ERROR,
				0, "Unable to remove the db file: errno %d", errno);
		return FALSE;
	}

	return TRUE;
}
