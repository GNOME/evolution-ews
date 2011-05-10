/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-book-backend-sqlitedb.h
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

#ifndef E_BOOK_BACKEND_SQLITEDB_H
#define E_BOOK_BACKEND_SQLITEDB_H

#include <glib.h>
#include <glib-object.h>
#include <sqlite3.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_SQLITEDB \
	(e_book_backend_sqlitedb_get_type ())
#define E_BOOK_BACKEND_SQLITEDB(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_SQLITEDB, EBookBackendSqliteDB))
#define E_BOOK_BACKEND_SQLITEDB_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_SQLITEDB, EBookBackendSqliteDBClass))
#define E_IS_BOOK_BACKEND_SQLITEDB(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_SQLITEDB))
#define E_IS_BOOK_BACKEND_SQLITEDB_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_SQLITEDB))
#define E_BOOK_BACKEND_SQLITEDB_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_SQLITEDB, EBookBackendSqliteDBClass))

G_BEGIN_DECLS

typedef struct _EBookBackendSqliteDB EBookBackendSqliteDB;
typedef struct _EBookBackendSqliteDBClass EBookBackendSqliteDBClass;
typedef struct _EBookBackendSqliteDBPrivate EBookBackendSqliteDBPrivate;

struct _EBookBackendSqliteDB {
	GObject parent;
	EBookBackendSqliteDBPrivate *priv;
};

struct _EBookBackendSqliteDBClass {
	GObjectClass parent_class;

	/* virtual methods */
};

GType		e_book_backend_sqlitedb_get_type	(void);

EBookBackendSqliteDB *
		e_book_backend_sqlitedb_new		(const gchar *path,
							 const gchar *email_id,
							 const gchar *folderid,
							 GError **error);
gboolean	e_book_backend_sqlitedb_add_contact	(EBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GSList *contacts,
							 gboolean partial_content,
							 GError **error);
gboolean	e_book_backend_sqlitedb_remove_contact	(EBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GSList *uids,
							 GError **error);
gboolean	e_book_backend_sqlitedb_has_contact	(EBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
					 		 const gchar *uid,
							 gboolean *partial_content,
							 GError **error);
const gchar *	e_book_backend_sqlitedb_get_vcard_string
							(EBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *uid,
							 GError **error);
GList *		e_book_backend_sqlitedb_search		(EBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *sexp,
							 GError **error);

G_END_DECLS

#endif /* E_BOOK_BACKEND_SQLITEDB_H */
