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

#include <glib/gi18n.h>
#include <libebook/e-contact.h>
#include <libedataserver/e-sexp.h>
#include <libedata-book/e-book-backend-sexp.h>

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
	gchar *filename;
	gchar *hash_key;
	
	gboolean ready;
	GStaticRWLock rwlock;
};

G_DEFINE_TYPE (EBookBackendSqliteDB, e_book_backend_sqlitedb, G_TYPE_OBJECT)

#define E_BOOK_SDB_ERROR \
	(e_book_backend_sqlitedb_error_quark ())

static GHashTable *db_connections = NULL;
static GStaticMutex dbcon_lock = G_STATIC_MUTEX_INIT;

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

	g_free (priv->filename);
	priv->filename = NULL;

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

static gint
create_folders_table	(EBookBackendSqliteDB *ebsdb,
			 GError **error)
{
	gint ret;
	const gchar *stmt = "CREATE TABLE IF NOT EXISTS folders		\
			     ( folder_id  TEXT PRIMARY KEY,		\
			       folder_name TEXT,			\
			       sync_data TEXT,		 		\
			       bdata1 TEXT, bdata2 TEXT,		\
			       bdata3 TEXT)";
	
	WRITER_LOCK (ebsdb);
	ret = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL , NULL);
	WRITER_UNLOCK (ebsdb);
	
	return ret;
}

/* The column names match the fields used in book-backend-sexp */
static gint
create_contacts_table	(EBookBackendSqliteDB *ebsdb,
			 const gchar *folderid,
			 GError **error)
{
	gint ret;
	gchar *stmt;
		
	stmt = sqlite3_mprintf ("CREATE TABLE IF NOT EXISTS %Q	 		\
			     ( uid  TEXT PRIMARY KEY,				\
			       nickname TEXT, full_name TEXT,			\
			       given_name TEXT, family_name TEXT,		\
			       email_1 TEXT, email_2 TEXT,			\
			       email_3 TEXT, email_4 TEXT,			\
			       vcard TEXT)", folderid);

	WRITER_LOCK (ebsdb);
	ret = book_backend_sql_exec (ebsdb->priv->db, stmt, NULL, NULL , NULL);
	WRITER_UNLOCK (ebsdb);

	sqlite3_free (stmt);

	/* TODO create indexes */
	
	return ret;
}

static gboolean
book_backend_sqlitedb_load	(EBookBackendSqliteDB *ebsdb, 
				 GError **error)
{
	EBookBackendSqliteDBPrivate *priv;
	gint ret;
	
	priv = ebsdb->priv;

	e_sqlite3_vfs_init ();

	ret = sqlite3_open (priv->filename, &priv->db);
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
	WRITER_UNLOCK (ebsdb);

	create_folders_table (ebsdb, error);

	return TRUE;
}

EBookBackendSqliteDB *
e_book_backend_sqlitedb_new	(const gchar *path,
				 const gchar *emailid,
				 const gchar *folderid,
				 GError **error)
{
	EBookBackendSqliteDB *ebsdb;
	gchar *hash_key;

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
	ebsdb->priv->filename = g_build_filename (path, DB_FILENAME, NULL);

	book_backend_sqlitedb_load (ebsdb, error);
	
	if (db_connections == NULL)
		db_connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_hash_table_insert (db_connections, hash_key, ebsdb);
	ebsdb->priv->hash_key = g_strdup (hash_key);
	
	g_static_mutex_unlock (&dbcon_lock);

exit:	
	create_contacts_table (ebsdb, folderid, error);
	return ebsdb;
}

/* Add Contact */
static gchar *
insert_stmt_from_contact	(EContact *contact, const gchar *folderid)
{
	gchar *stmt = NULL;
	gchar *id, *nickname, *full_name;
	gchar *given_name, *surname, *file_as;
	gchar *email_1, *email_2, *email_3, *email_4;
	gchar *vcard_str;

	id		= e_contact_get (contact, E_CONTACT_UID);
	nickname   	= e_contact_get (contact, E_CONTACT_NICKNAME);
	full_name  	= e_contact_get (contact, E_CONTACT_FULL_NAME);
	given_name 	= e_contact_get (contact, E_CONTACT_GIVEN_NAME);
	surname    	= e_contact_get (contact, E_CONTACT_FAMILY_NAME);
	file_as    	= e_contact_get (contact, E_CONTACT_FILE_AS);
	email_1    	= e_contact_get (contact, E_CONTACT_EMAIL_1);
	email_2    	= e_contact_get (contact, E_CONTACT_EMAIL_2);
	email_3    	= e_contact_get (contact, E_CONTACT_EMAIL_3);
	email_4    	= e_contact_get (contact, E_CONTACT_EMAIL_4);
	vcard_str	= e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);


	stmt = sqlite3_mprintf ("INSERT or REPLACE INTO %Q VALUES (%Q, %s, %s, \
	       				%s, %s, %s, %s, %s, %s, %s, %s))", folderid, id, nickname,
					full_name, given_name, surname, file_as, email_1,
					email_2, email_3, email_4, vcard_str
					);

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

gboolean
e_book_backend_sqlitedb_add_contact	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 GSList *contacts,
					 GError **error)
{
	GSList *l;
	GError *err = NULL;
	gboolean ret = TRUE;
	EBookBackendSqliteDBPrivate *priv;
	
	priv = ebsdb->priv;

	WRITER_LOCK (ebsdb);
	book_backend_sql_exec (priv->db, "BEGIN", NULL, NULL, &err);

	for (l = contacts; l != NULL; l = g_slist_next (l)) {
		gchar *stmt;
		EContact *contact = (EContact *) l->data;
		
		stmt = insert_stmt_from_contact (contact, folderid);
		book_backend_sql_exec (priv->db, stmt, NULL, NULL, &err);
		
		sqlite3_free (stmt);

		if (err)
			break;
	}
	
	if (!err)
		book_backend_sql_exec (priv->db, "COMMIT", NULL, NULL, &err);
	else
		book_backend_sql_exec (priv->db, "ROLLBACK", NULL, NULL, NULL);

	WRITER_UNLOCK (ebsdb);

	if (err)
		ret = FALSE;

	g_propagate_error (error, err);

	return ret;
}

gboolean
e_book_backend_sqlitedb_remove_contact	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 GSList *uids,
					 GError **error)
{	
	GSList *l;
	GError *err = NULL;
	gboolean ret = TRUE;
	GString *str;
	gchar *tmp;
	EBookBackendSqliteDBPrivate *priv;

	priv = ebsdb->priv;
	str = g_string_new ("DELETE FROM ");
       
	tmp = sqlite3_mprintf ("%Q WHERE uid IN (", folderid);
	g_string_append (str, tmp);
	sqlite3_free (tmp);

	WRITER_LOCK (ebsdb);
	book_backend_sql_exec (priv->db, "BEGIN", NULL, NULL, &err);

	for (l = uids; l != NULL; l = g_slist_next (l)) {
		gchar *uid = (gchar *) uids->data;
		
		tmp = sqlite3_mprintf ("%Q", uid);
		g_string_append_printf (str, " %s ,", tmp);
		sqlite3_free (tmp);

		if (err)
			break;
	}

	/* remove the last comma */
	g_string_truncate (str, 1);

	g_string_append (str, ")");

	book_backend_sql_exec (priv->db, str->str, NULL, NULL, &err);

	if (!err)
		book_backend_sql_exec (priv->db, "COMMIT", NULL, NULL, &err);
	else
		book_backend_sql_exec (priv->db, "ROLLBACK", NULL, NULL, NULL);

	WRITER_UNLOCK (ebsdb);

	g_string_free (str, TRUE);
	if (err)
		ret = FALSE;

	g_propagate_error (error, err);

	return ret;
}

static gint
contact_found_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	gboolean *ret = ref;

	*ret = TRUE;

	return 0;
}

gboolean
e_book_backend_sqlitedb_has_contact	(EBookBackendSqliteDB *ebsdb,
					 const gchar *folderid,
					 const gchar *uid,
					 GError **error)
{
	gchar *stmt;
	gboolean ret = FALSE;
	
	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT uid FROM %Q WHERE uid = %Q", folderid, uid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, contact_found_cb , &ret, error);
	sqlite3_free (stmt);
	
	READER_UNLOCK (ebsdb);

	return ret;
}

static gint
get_vcard_cb (gpointer ref, gint col, gchar **cols, gchar **name)
{
	gchar **vcard_str = ref;

	if (cols [0])
		*vcard_str = g_strdup (cols [0]);

	return 0;
}

const gchar *
e_book_backend_sqlitedb_get_vcard_string	(EBookBackendSqliteDB *ebsdb,
						 const gchar *folderid,
						 const gchar *uid,
						 GError **error)
{
	gchar *stmt;
	gchar *vcard_str = NULL;
	
	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT vcard FROM %Q WHERE uid = %Q", folderid, uid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, get_vcard_cb , &vcard_str, error);
	sqlite3_free (stmt);

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
				value = g_strdup_printf ("%%%s%%", argv[1]->value.string);
			} else if (match == MATCH_ENDS_WITH) {
				value = g_strdup_printf ("%%%s", argv[1]->value.string);
			} else if (match == MATCH_BEGINS_WITH) {
				value = g_strdup_printf ("%s%%", argv[1]->value.string);
			} else if (match == MATCH_IS) {
				value = g_strdup_printf ("%%%s%%", argv[1]->value.string);
			}
			
			if (!strcmp (value, "full_name")) {
				gchar *full, *sur, *given, *nick;

				full = g_strdup_printf("(full_name IS NOT NULL AND full_name LIKE %s)",value);
				sur = g_strdup_printf("(family_name IS NOT NULL AND family_name LIKE %s)",value);
				given = g_strdup_printf("(given_name IS NOT NULL AND given_name LIKE %s)",value);
				nick = g_strdup_printf("(nick_name IS NOT NULL AND nick_name LIKE %s)",value);
			
				str = g_strdup_printf (" %s OR %s OR %s OR %s ", full, sur, given, nick);

				g_free (full);
				g_free (sur);
				g_free (given);
				g_free (nick);
			} else if (!strcmp (value, "email")) {
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
		*vcards = g_list_prepend (*vcards, cols [0]);

	return 0;
}

static GList *
book_backend_sqlitedb_search_query (EBookBackendSqliteDB *ebsdb, const gchar *sql, const gchar *folderid, GError **error)
{
	GList *vcards = NULL;
	gchar *stmt;
	
	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT vcard FROM %Q WHERE %s", folderid, sql);
	book_backend_sql_exec (ebsdb->priv->db, stmt, addto_vcard_list_cb , &vcards, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	vcards = g_list_reverse (vcards);
	
	return vcards;
}

static GList *
book_backend_sqlitedb_search_full (EBookBackendSqliteDB *ebsdb, const gchar *sexp, const gchar *folderid, GError **error)
{
	GList *vcards = NULL, *all = NULL, *l;
	EBookBackendSExp *bsexp = NULL;
	gchar *stmt;
	
	READER_LOCK (ebsdb);

	stmt = sqlite3_mprintf ("SELECT vcard FROM %Q", folderid);
	book_backend_sql_exec (ebsdb->priv->db, stmt, addto_vcard_list_cb , &all, error);
	sqlite3_free (stmt);

	READER_UNLOCK (ebsdb);

	bsexp = e_book_backend_sexp_new (sexp);
	
	for (l = all; l != NULL; l = g_list_next (l)) {
		if (e_book_backend_sexp_match_vcard (bsexp, l->data))
			vcards = g_list_prepend (vcards, g_strdup (l->data));
	}

	g_object_unref (bsexp);
	g_list_foreach (all, (GFunc) g_free, NULL);
	g_list_free (all);

	return vcards;

}

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
	} else
		vcards = book_backend_sqlitedb_search_full (ebsdb, sexp, folderid, error);

	return vcards;
}
