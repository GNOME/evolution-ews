/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* ews-book-backend-sqlitedb.h
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

#ifndef EWS_BOOK_BACKEND_SQLITEDB_H
#define EWS_BOOK_BACKEND_SQLITEDB_H

#include <libebook/libebook.h>

/* Standard GObject macros */
#define EWS_TYPE_BOOK_BACKEND_SQLITEDB \
	(ews_book_backend_sqlitedb_get_type ())
#define EWS_BOOK_BACKEND_SQLITEDB(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EWS_TYPE_BOOK_BACKEND_SQLITEDB, EwsBookBackendSqliteDB))
#define EWS_BOOK_BACKEND_SQLITEDB_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EWS_TYPE_BOOK_BACKEND_SQLITEDB, EwsBookBackendSqliteDBClass))
#define EWS_IS_BOOK_BACKEND_SQLITEDB(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EWS_TYPE_BOOK_BACKEND_SQLITEDB))
#define EWS_IS_BOOK_BACKEND_SQLITEDB_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EWS_TYPE_BOOK_BACKEND_SQLITEDB))
#define EWS_BOOK_BACKEND_SQLITEDB_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EWS_TYPE_BOOK_BACKEND_SQLITEDB, EwsBookBackendSqliteDBClass))

G_BEGIN_DECLS

typedef struct _EwsBookBackendSqliteDB EwsBookBackendSqliteDB;
typedef struct _EwsBookBackendSqliteDBClass EwsBookBackendSqliteDBClass;
typedef struct _EwsBookBackendSqliteDBPrivate EwsBookBackendSqliteDBPrivate;

/**
 * EwsBookBackendSqliteDB:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.2
 **/
struct _EwsBookBackendSqliteDB {
	GObject parent;
	EwsBookBackendSqliteDBPrivate *priv;
};

struct _EwsBookBackendSqliteDBClass {
	GObjectClass parent_class;

	/* virtual methods */
};

/**
 * EwsSdbSearchData:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
typedef struct {
	gchar *vcard;
	gchar *uid;
	gchar *bdata;
} EwsSdbSearchData;

GType		ews_book_backend_sqlitedb_get_type	(void);

EwsBookBackendSqliteDB *
		ews_book_backend_sqlitedb_new		(const gchar *path,
							 const gchar *email_id,
							 const gchar *folderid,
							 const gchar *folder_name,
							 gboolean store_vcard,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_add_contact	(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 EContact *contact,
							 gboolean partial_content,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_add_contacts	(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GSList *contacts,
							 gboolean partial_content,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_remove_contact
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *uid,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_remove_contacts
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GSList *uids,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_has_contact	(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *uid,
							 gboolean *partial_content,
							 GError **error);
EContact *	ews_book_backend_sqlitedb_get_contact	(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *uid,
							 GHashTable  *fields_of_interest,
							 gboolean    *with_all_required_fields,
							 GError     **error);
gchar *         ews_book_backend_sqlitedb_get_vcard_string
                                                        (EwsBookBackendSqliteDB *ebsdb,
							 const gchar          *folderid,
							 const gchar          *uid,
							 GHashTable           *fields_of_interest,
							 gboolean             *with_all_required_fields,
							 GError              **error);

GSList *	ews_book_backend_sqlitedb_search	(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *sexp,
							 /* const */ GHashTable *fields_of_interest,
							 gboolean    *searched,
							 gboolean    *with_all_required_fields,
							 GError     **error);

GSList *	ews_book_backend_sqlitedb_search_uids	(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *sexp,
							 gboolean *searched,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_get_is_populated
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GError **error);

gboolean	ews_book_backend_sqlitedb_set_is_populated
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 gboolean populated,
							 GError **error);
gchar *		ews_book_backend_sqlitedb_get_sync_data
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_set_sync_data
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *sync_data,
							 GError **error);
gchar *		ews_book_backend_sqlitedb_get_key_value
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *key,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_set_key_value
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *key,
							 const gchar *value,
							 GError **error);

gchar *		ews_book_backend_sqlitedb_get_contact_bdata
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *uid,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_set_contact_bdata
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 const gchar *uid,
							 const gchar *value,
							 GError **error);

gboolean	ews_book_backend_sqlitedb_get_has_partial_content
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_set_has_partial_content
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 gboolean partial_content,
							 GError **error);
GSList *	ews_book_backend_sqlitedb_get_partially_cached_ids
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_delete_addressbook
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folderid,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_create_addressbook
							(EwsBookBackendSqliteDB *ebsdb,
							 const gchar *folder_id,
							 const gchar *folder_name,
							 gboolean store_vcard,
							 GError **error);
gboolean	ews_book_backend_sqlitedb_remove	(EwsBookBackendSqliteDB *ebsdb,
							 GError **error);
void		ews_book_backend_sqlitedb_search_data_free
							(EwsSdbSearchData *s_data);

gboolean	ews_book_backend_sqlitedb_is_summary_query
							(const gchar *query);
gboolean	ews_book_backend_sqlitedb_is_summary_fields
							(GHashTable *fields_of_interest);

G_END_DECLS

#endif /* EWS_BOOK_BACKEND_SQLITEDB_H */
