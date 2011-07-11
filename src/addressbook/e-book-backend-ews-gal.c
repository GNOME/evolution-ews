/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews-gal.c - EwsGal contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "libedataserver/e-sexp.h"
#include "libedataserver/e-data-server-util.h"
#include "libedataserver/e-flag.h"
#include "libedataserver/e-url.h"
#include "libebook/e-contact.h"
#include "libebook/e-destination.h"
#include "libedata-book/e-book-backend-sexp.h"
#include "libedata-book/e-data-book.h"
#include "libedata-book/e-data-book-view.h"

#include "e-book-backend-ews-gal.h"
#include "e-book-backend-sqlitedb.h"
#include "lzx/ews-oal-decompress.h"
#include "ews-oab-decoder.h"

#include "e-ews-message.h"
#include "e-ews-connection.h"
#include "e-ews-item.h"

#define d(x) x

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code,_msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)
#define EDB_ERROR_FAILED_STATUS(_code, _status) e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_ ## _code, "Failed with status 0x%x", _status)

G_DEFINE_TYPE (EBookBackendEwsGal, e_book_backend_ews_gal, E_TYPE_BOOK_BACKEND)

GList *supported_fields = NULL;

typedef struct {
	GCond *cond;
	GMutex *mutex;
	gboolean exit;
} SyncDelta;

struct _EBookBackendEwsGalPrivate {
	EEwsConnection *cnc;
	gchar *oal_id;
	gchar *oab_url;
	gchar *folder_name;
	
	gchar *username;
	gchar *password;

	EBookBackendSqliteDB *ebsdb;

	gboolean only_if_exists;
	gboolean marked_for_offline;
	gboolean cache_ready;
	gint mode;

	GHashTable *ops;

	/* used for storing attachments */
	gchar *attachment_dir;

	GStaticRecMutex rec_mutex;
	GThread *dthread;
	SyncDelta *dlock;
};

/* refresh once per day */
#define REFRESH_INTERVAL (24 * 60 * 60)

#define PRIV_LOCK(p)   (g_static_rec_mutex_lock (&(p)->rec_mutex))
#define PRIV_UNLOCK(p) (g_static_rec_mutex_unlock (&(p)->rec_mutex))

static gboolean
ews_remove_attachments (const gchar *attachment_dir);

static void
e_book_backend_ews_gal_create_contact	(EBookBackend *backend,
					 EDataBook *book,
					 guint32 opid,
					 const gchar *vcard )
{
	e_data_book_respond_create (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
}

static void
e_book_backend_ews_gal_remove_contacts	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32 opid,
					 GList *id_list)
{
	e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
}


static void
e_book_backend_ews_gal_modify_contact	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 const gchar   *vcard)
{
	e_data_book_respond_modify (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
}

static void
e_book_backend_ews_gal_get_contact	(EBookBackend *backend,
				 EDataBook    *book,
				 guint32       opid,
				 const gchar   *id)
{
	EBookBackendEwsGal *gwb;

	gwb =  E_BOOK_BACKEND_EWS_GAL (backend);

	switch (gwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), "");
		return;

	case E_DATA_BOOK_MODE_REMOTE :
		if (gwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact (book, opid, e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, "Not connected"), NULL);
			return;
		}
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), "");
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_gal_get_contact_list	(EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 const gchar   *query )
{
	GList *vcard_list;
	EBookBackendEwsGal *egwb;

	egwb = E_BOOK_BACKEND_EWS_GAL (backend);
	vcard_list = NULL;

	switch (egwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :

		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);
		return;

	case E_DATA_BOOK_MODE_REMOTE:

		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}

		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);
		return;
	default :
		break;

	}
}

static gboolean
ews_gal_needs_update (EBookBackendEwsGal *cbews, EwsOALDetails *full, GError **error)
{
	EBookBackendEwsGalPrivate *priv = cbews->priv;
	guint32 seq;
	gboolean ret = FALSE;
	gchar *tmp;

	tmp = e_book_backend_sqlitedb_get_key_value (priv->ebsdb, priv->oal_id, "seq", error);
	if (error)
		goto exit;

	sscanf (tmp, "%"G_GUINT32_FORMAT, &seq);
	if (seq < full->seq)
		ret = TRUE;
	
	d(printf ("Gal needs update: %d \n", ret);)
exit:
	g_free (tmp);
	return ret;	
}

static gchar *
ews_download_full_gal (EBookBackendEwsGal *cbews, EwsOALDetails *full, GCancellable *cancellable, GError **error)
{
	EBookBackendEwsGalPrivate *priv = cbews->priv;
	EEwsConnection *oab_cnc;
	gchar *full_url, *oab_url, *cache_file = NULL;
	const gchar *cache_dir;
	gchar *comp_cache_file = NULL, *uncompress_file = NULL;

	/* oab url with oab.xml removed from the suffix */
	oab_url = g_strndup (priv->oab_url, strlen (priv->oab_url) - 7);
	full_url = g_strconcat (oab_url, full->filename, NULL);
	cache_dir = e_book_backend_get_cache_dir (E_BOOK_BACKEND (cbews));
	comp_cache_file = g_build_filename (cache_dir, full->filename, NULL);

	oab_cnc = e_ews_connection_new (full_url, priv->username, priv->password, NULL, NULL, NULL);
	if (!e_ews_connection_download_oal_file (oab_cnc, comp_cache_file, NULL, NULL, cancellable, error))
		goto exit;

	cache_file = g_strdup_printf ("%s-%d.oab", priv->folder_name, full->seq);
	uncompress_file = g_build_filename (cache_dir, cache_file, NULL);
	if (!oal_decompress_v4_full_detail_file (comp_cache_file, uncompress_file, error)) {
		g_free (uncompress_file);
		uncompress_file = NULL;
		goto exit;
	}

	d(g_print ("OAL file decompressed %s \n", uncompress_file);)

exit:	
	if (comp_cache_file)
		g_unlink (comp_cache_file);
	g_object_unref (oab_cnc);
	g_free (oab_url);
	g_free (full_url);
	g_free (comp_cache_file);
	g_free (cache_file);

	return uncompress_file;
}

static gboolean
ews_remove_old_gal_file (EBookBackendEwsGal *cbews, GError **error)
{
	EBookBackendEwsGalPrivate *priv = cbews->priv;
	gchar *filename;

	filename = e_book_backend_sqlitedb_get_key_value (priv->ebsdb, priv->oal_id, "oab-filename", error);
	if (*error)
		return FALSE;

	g_unlink (filename);
	
	return TRUE;
}

struct _db_data {
	GSList *contact_collector;
	guint collected_length;
	EBookBackendEwsGal *cbews;
};

static void
ews_gal_store_contact (EContact *contact, goffset offset, guint percent, gpointer user_data, GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;
	EBookBackendEwsGalPrivate *priv = data->cbews->priv;
	
	data->contact_collector = g_slist_prepend (data->contact_collector, g_object_ref (contact));
	data->collected_length += 1;

	if (data->collected_length == 1000 || percent >= 100) {
		GSList *l;

		d(g_print ("GAL adding contacts, percent complete : %d \n", percent);)

		data->contact_collector = g_slist_reverse (data->contact_collector);
		e_book_backend_sqlitedb_add_contacts (priv->ebsdb, priv->oal_id, data->contact_collector, FALSE, error);

		for (l = data->contact_collector; l != NULL; l = g_slist_next (l))
			e_book_backend_notify_update (E_BOOK_BACKEND (data->cbews), E_CONTACT (l->data));

		/* reset data */
		g_slist_foreach (data->contact_collector, (GFunc) g_object_unref, NULL);
		g_slist_free (data->contact_collector);
		data->contact_collector = NULL;
		data->collected_length = 0;
	}

	if (percent == 100)
		e_book_backend_notify_complete (E_BOOK_BACKEND (data->cbews));
}

static gboolean
ews_replace_gal_in_db (EBookBackendEwsGal *cbews, const gchar *filename, GCancellable *cancellable, GError **error)
{
	EBookBackendEwsGalPrivate *priv = cbews->priv;
	EwsOabDecoder *eod;
	gboolean ret = TRUE;
	struct _db_data data;

	/* remove the old address-book and create a new one in db */
	if (e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->oal_id, NULL)) {
		ret = e_book_backend_sqlitedb_delete_addressbook (priv->ebsdb, priv->oal_id, error);
		ews_remove_attachments (priv->attachment_dir);
		if (ret)
			ret = e_book_backend_sqlitedb_create_addressbook (priv->ebsdb, priv->oal_id, priv->folder_name, FALSE, error);
	}

	if (!ret)
		return FALSE;

	eod = ews_oab_decoder_new (filename, priv->attachment_dir, error);
	if (*error)
		return FALSE;

	data.contact_collector = NULL;
	data.collected_length = 0;
	data.cbews = cbews;

	ret = ews_oab_decoder_decode (eod, ews_gal_store_contact, &data, cancellable, error);
	if (!ret)
	       return ret;

	/* mark the db as populated */
	ret = e_book_backend_sqlitedb_set_is_populated (priv->ebsdb, priv->oal_id, TRUE, error);

	return ret;
}

static gboolean
ebews_start_sync	(gpointer data)
{
	EBookBackendEwsGal *cbews;
	EBookBackendEwsGalPrivate *priv;
	EwsOALDetails *full = NULL;
	GError *error = NULL;
	EEwsConnection *oab_cnc;
	GSList *full_l = NULL;
	gboolean ret = TRUE;
	gchar *uncompressed_filename = NULL;
	GCancellable *cancellable;

	cbews = (EBookBackendEwsGal *) data;
	priv = cbews->priv;

	cancellable = g_cancellable_new ();
	oab_cnc = e_ews_connection_new (priv->oab_url, priv->username, priv->password, NULL, NULL, NULL);

	d(printf ("Ewsgal: Fetching oal full details file \n");)

	if (!e_ews_connection_get_oal_detail (oab_cnc, priv->oal_id, "Full", &full_l, cancellable, &error)) {
		ret = FALSE;
		goto exit;
	}

	full = (EwsOALDetails *) full_l->data; 
	/* TODO fetch differential updates if available instead of downloading the whole GAL */
	if (!e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->oal_id, NULL) || ews_gal_needs_update (cbews, full, &error)) {
		gchar *seq;
		
		d(printf ("Ewsgal: Downloading full gal \n");)
		uncompressed_filename = ews_download_full_gal (cbews, full, cancellable, &error);
		if (error) {
			ret = FALSE;
			goto exit;
		}

		d(printf ("Ewsgal: Removing old gal \n");)
		/* remove old_gal_file */
		ret = ews_remove_old_gal_file (cbews, &error);
		if (!ret) {
			goto exit;
		}

		d(printf ("Ewsgal: Replacing old gal with new gal contents in db \n");)
		ret = ews_replace_gal_in_db (cbews, uncompressed_filename, cancellable, &error);
		if (!ret)
			goto exit;
	
		seq = g_strdup_printf ("%"G_GUINT32_FORMAT, full->seq);
		ret = e_book_backend_sqlitedb_set_key_value (priv->ebsdb, priv->oal_id, "seq", seq, &error);
		g_free (seq);
		
		if (!ret) {
			e_book_backend_sqlitedb_delete_addressbook (priv->ebsdb, priv->oal_id, &error);
			goto exit;
		}
	}
	
	d(printf ("Ews gal: sync successfull complete \n");)
	
exit:
	if (error) {
		g_warning ("Unable to update gal : %s \n", error->message);
		g_clear_error (&error);
	}

	/* preserve  the oab file once we are able to decode the differential updates */
	if (uncompressed_filename) {
		g_unlink (uncompressed_filename);
		g_free (uncompressed_filename);
	}

	if (full_l) {
		g_free (full->sha);
		g_free (full->filename);
		g_free (full);
		g_slist_free (full_l);
	}

	g_object_unref (oab_cnc);
	return ret;
}

static gpointer
delta_thread (gpointer data)
{
	EBookBackendEwsGal *ebews = data;
	EBookBackendEwsGalPrivate *priv = ebews->priv;
	GTimeVal timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	while (TRUE)	{
		gboolean succeeded = ebews_start_sync (ebews);

		g_mutex_lock (priv->dlock->mutex);

		if (!succeeded || priv->dlock->exit)
			break;

		g_get_current_time (&timeout);
		timeout.tv_sec += REFRESH_INTERVAL;
		g_cond_timed_wait (priv->dlock->cond, priv->dlock->mutex, &timeout);

		if (priv->dlock->exit)
			break;

		g_mutex_unlock (priv->dlock->mutex);
	}

	g_mutex_unlock (priv->dlock->mutex);
	priv->dthread = NULL;
	return NULL;
}

static gboolean
fetch_deltas (EBookBackendEwsGal *ebews)
{
	EBookBackendEwsGalPrivate *priv = ebews->priv;
	GError *error = NULL;

	/* If the thread is already running just return back */
	if (priv->dthread)
		return FALSE;

	if (!priv->dlock) {
		priv->dlock = g_new0 (SyncDelta, 1);
		priv->dlock->mutex = g_mutex_new ();
		priv->dlock->cond = g_cond_new ();
	}

	priv->dlock->exit = FALSE;
	priv->dthread = g_thread_create ((GThreadFunc) delta_thread, ebews, TRUE, &error);
	if (!priv->dthread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);
	}

	return TRUE;
}

static void
ebews_start_refreshing (EBookBackendEwsGal *ebews)
{
	EBookBackendEwsGalPrivate *priv;

	priv = ebews->priv;

	PRIV_LOCK (priv);

	if	(priv->mode == E_DATA_BOOK_MODE_REMOTE &&
		 priv->cnc && priv->marked_for_offline)
				fetch_deltas (ebews);

	PRIV_UNLOCK (priv);
}

static void
fetch_from_offline (EBookBackendEwsGal *ews, EDataBookView *book_view, const gchar *query, GError *error)
{
	GSList *contacts, *l;
	EBookBackendEwsGalPrivate *priv;

	priv = ews->priv;

	contacts = e_book_backend_sqlitedb_search (priv->ebsdb, priv->oal_id, query, NULL, &error);
	for (l = contacts; l != NULL; l = g_slist_next (l)) {
		EbSdbSearchData *s_data = (EbSdbSearchData *) l->data;

		/* reset vcard to NULL as it would be free'ed in prefiltered_vcard function */
		e_data_book_view_notify_update_prefiltered_vcard (book_view, s_data->uid, s_data->vcard);
		s_data->vcard = NULL;

		e_book_backend_sqlitedb_search_data_free (s_data);
	}

	if (contacts)
		g_slist_free (contacts);
	e_data_book_view_notify_complete (book_view, error);
	e_data_book_view_unref (book_view);
}

typedef struct {
	/* For future use */
	gpointer restriction;

	gboolean is_query_handled;
	gboolean is_autocompletion;
	gchar *auto_comp_str;
} EBookBackendEwsSExpData;

static ESExpResult *
func_not (ESExp *f, gint argc, ESExpResult **argv, gpointer data)
{
	ESExpResult *r;

	if (argc != 1 || argv[0]->type != ESEXP_RES_UNDEFINED) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

static ESExpResult *
func_and_or (ESExp *f, gint argc, ESExpResult **argv, gpointer and)
{
	ESExpResult *r;

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	return r;
}

/* TODO implement */
static ESExpResult *
func_is (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	sdata->is_query_handled = FALSE;
	return r;
}

/* TODO implement */
static ESExpResult *
func_endswith (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	sdata->is_query_handled = FALSE;
	return r;

}

/* TODO implement */
static ESExpResult *
func_contains (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2
	    && argv[0]->type != ESEXP_RES_STRING
	    && argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;

	sdata->is_query_handled = FALSE;
	return r;

}

/* We are just handling for autocompletion now. We need to support other fields after implementing
   Restrictions and find_items request */
static ESExpResult *
func_beginswith (struct _ESExp *f, gint argc, struct _ESExpResult **argv, gpointer data)
{
	ESExpResult *r;
	gchar *propname, *str;
	EBookBackendEwsSExpData *sdata = data;

	if (argc != 2 ||
	    argv[0]->type != ESEXP_RES_STRING ||
	    argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!strcmp (propname, "full_name") || !strcmp (propname, "email")) {
		if (!sdata->auto_comp_str) {
			sdata->auto_comp_str = g_strdup (str);
			sdata->is_autocompletion = TRUE;
		}
	}

	r = e_sexp_result_new (f, ESEXP_RES_BOOL);
	r->value.boolean = FALSE;
	return r;
}

static struct {
	const gchar *name;
	ESExpFunc *func;
	guint flags;
} symbols[] = {
	{ "and", func_and_or, 0 },
	{ "or", func_and_or, 0},
	{ "not", func_not, 0 },
	{ "contains", func_contains, 0},
	{ "is", func_is, 0},
	{ "beginswith", func_beginswith, 0},
	{ "endswith", func_endswith, 0},
};

static gpointer
ews_gal_get_autocompletion_str_from_query (const gchar *query, gboolean *autocompletion, gchar **auto_comp_str)
{
	ESExpResult *r;
	ESExp *sexp;
	EBookBackendEwsSExpData *sdata;
	gint i;

	sexp = e_sexp_new ();
	sdata = g_new0 (EBookBackendEwsSExpData, 1);

	sdata->is_query_handled = TRUE;

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		e_sexp_add_function (sexp, 0, (gchar *) symbols[i].name,
				     symbols[i].func,
				     sdata);
	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);
	if (r) {
		*autocompletion = sdata->is_autocompletion;
		*auto_comp_str = sdata->auto_comp_str;
	}

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);
	g_free (sdata);

	return NULL;
}


static void
e_book_backend_ews_gal_start_book_view (EBookBackend  *backend,
				     EDataBookView *book_view)
{
	EBookBackendEwsGal *ebews;
	EBookBackendEwsGalPrivate *priv;
	const gchar *query;
	gboolean is_autocompletion = FALSE;
	gchar *auto_comp_str = NULL;
	GCancellable *cancellable;
	GSList *mailboxes = NULL, *l;
	GError *error = NULL;
	gboolean includes_last_item;

	ebews = E_BOOK_BACKEND_EWS_GAL (backend);
	priv = ebews->priv;
	query = e_data_book_view_get_card_query (book_view);

	e_data_book_view_ref (book_view);
	e_data_book_view_notify_status_message (book_view, _("Searching..."));

	switch (priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL:
		if (priv->marked_for_offline && e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->oal_id, NULL)) {
			fetch_from_offline (ebews, book_view, query, error);
			return;
		}

		error = EDB_ERROR (OFFLINE_UNAVAILABLE);
		e_data_book_view_notify_complete (book_view, error);
		g_error_free (error);
		return;
	case E_DATA_BOOK_MODE_REMOTE:
		if (!priv->cnc) {
			error = EDB_ERROR (AUTHENTICATION_REQUIRED);
			e_book_backend_notify_auth_required (backend);
			e_data_book_view_notify_complete (book_view, error);
			e_data_book_view_unref (book_view);
			g_error_free (error);
			return;
		}

		/* TODO update the progress messages */
		ebews_start_refreshing (ebews);

		if (priv->marked_for_offline && e_book_backend_sqlitedb_get_is_populated (priv->ebsdb, priv->oal_id, NULL)) {
			fetch_from_offline (ebews, book_view, query, error);
			return;
		}
		
		/* Only autocompletion query is supported in online query. */
		ews_gal_get_autocompletion_str_from_query (query, &is_autocompletion, &auto_comp_str);
		if (!is_autocompletion || !auto_comp_str) {
			/* TODO Indicate that the contacts will be available in BookView only after they are cached.
			   Until then only auto-completion works */

			g_free (auto_comp_str);
			e_data_book_view_notify_complete (book_view, error);
			e_data_book_view_unref (book_view);
			return;
		}

		cancellable = g_cancellable_new ();

		/* We do not scan until we reach the last_item as it might be good enough to show first 100
		   items during auto-completion. Change it if needed */
		g_hash_table_insert (priv->ops, book_view, cancellable);
		e_ews_connection_resolve_names	(priv->cnc, EWS_PRIORITY_MEDIUM, auto_comp_str,
						 EWS_SEARCH_AD, NULL, FALSE, &mailboxes, NULL,
						 &includes_last_item, cancellable, &error);
		g_free (auto_comp_str);
		g_hash_table_remove (priv->ops, book_view);
		if (error != NULL) {
			e_data_book_view_notify_complete (book_view, error);
			e_data_book_view_unref (book_view);
			g_clear_error (&error);
			return;
		}

		for (l = mailboxes; l != NULL; l = g_slist_next (l)) {
			EwsMailbox *mb = l->data;
			EContact *contact;

			contact = e_contact_new ();

			/* We do not get an id from the server, so just using email_id as uid for now */
			e_contact_set (contact, E_CONTACT_UID, mb->email);
			e_contact_set (contact, E_CONTACT_FULL_NAME, mb->name);
			e_contact_set (contact, E_CONTACT_EMAIL_1, mb->email);

			e_data_book_view_notify_update (book_view, contact);

			g_free (mb->email);
			g_free (mb->name);
			g_free (mb);
			g_object_unref (contact);
		}

		g_slist_free (mailboxes);
		e_data_book_view_notify_complete (book_view, error);
		e_data_book_view_unref (book_view);
	default:
		break;
	}
}

static void
e_book_backend_ews_gal_stop_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
	EBookBackendEwsGal *bews = E_BOOK_BACKEND_EWS_GAL (backend);
	EBookBackendEwsGalPrivate *priv = bews->priv;
	GCancellable *cancellable;

	cancellable = g_hash_table_lookup (priv->ops, book_view);
	if (cancellable) {
		g_cancellable_cancel (cancellable);
		g_hash_table_remove (priv->ops, book_view);
	}
}

static void
e_book_backend_ews_gal_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const gchar *change_id)
{
}

static void
e_book_backend_ews_gal_authenticate_user (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid,
					    const gchar *user,
					    const gchar *passwd,
					    const gchar *auth_method)
{
	EBookBackendEwsGal *ebgw;
	EBookBackendEwsGalPrivate *priv;
	ESource *esource;
	GError *error = NULL;
	const gchar *host_url;

	ebgw = E_BOOK_BACKEND_EWS_GAL (backend);
	priv = ebgw->priv;

	switch (ebgw->priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL:
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
		return;

	case E_DATA_BOOK_MODE_REMOTE:
		if (priv->cnc) {
			e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
			return;
		}

		esource = e_book_backend_get_source (backend);
		host_url = e_source_get_property (esource, "hosturl");

		priv->cnc = e_ews_connection_new (host_url, user, passwd,
						  NULL, NULL, &error);
		priv->username = e_source_get_duped_property (esource, "username");
		priv->password = g_strdup (passwd);

		/* FIXME: Do some dummy request to ensure that the password is actually
		   correct; don't just blindly return success */
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
		e_book_backend_notify_writable (backend, FALSE);
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_gal_get_required_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;

	fields = g_list_append (fields, (gchar *)e_contact_field_name (E_CONTACT_FILE_AS));
	e_data_book_respond_get_supported_fields (book, opid,
						  EDB_ERROR (SUCCESS),
						  fields);
	g_list_free (fields);

}

static void
e_book_backend_ews_gal_get_supported_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	e_data_book_respond_get_supported_fields (book,
						  opid,
						  NULL,
						  supported_fields);
}

static void
e_book_backend_ews_gal_cancel_operation (EBookBackend *backend, EDataBook *book, GError **perror)
{

}

static gboolean
ews_remove_attachments (const gchar *attachment_dir)
{
	GDir *dir;
	
	dir = g_dir_open (attachment_dir, 0, NULL);
	if (dir) {
		const gchar *fname;
		gchar *full_path;

		while ((fname = g_dir_read_name (dir))) {
			full_path = g_build_filename (attachment_dir, fname, NULL);
			if (g_unlink (full_path) != 0) {
				g_free (full_path);
				g_dir_close (dir);

				return FALSE;
			}

			g_free (full_path);
		}

		g_dir_close (dir);
	}

	return TRUE;
}

static void
e_book_backend_ews_gal_load_source 	(EBookBackend *backend,
				 	 ESource *source,
					 gboolean only_if_exists,
					 GError **perror)
{
	EBookBackendEwsGal *cbews;
	EBookBackendEwsGalPrivate *priv;
	GError *err = NULL;

	cbews = E_BOOK_BACKEND_EWS_GAL (backend);
	priv = cbews->priv;

	priv->oal_id = e_source_get_duped_property (source, "oal_id");

	/* If oal_id is present it means the GAL is marked for offline usage, we do not check for offline_sync property */
	if (priv->oal_id) {
		const gchar *cache_dir, *email;
		
		cache_dir = e_book_backend_get_cache_dir (backend);
		email = e_source_get_property (source, "email");

		priv->folder_name = g_strdup (e_source_peek_name (source));
		priv->oab_url = e_source_get_duped_property (source, "oab_url");
		
		/* setup stagging dir, remove any old files from there */
		priv->attachment_dir = g_build_filename (cache_dir, "attachments", NULL);
		g_mkdir_with_parents (priv->attachment_dir, 0777);

		priv->ebsdb = e_book_backend_sqlitedb_new (cache_dir, email, priv->oal_id, priv->folder_name, FALSE, &err);
		if (err) {
			g_propagate_error (perror, err);
			return;
		}
		priv->marked_for_offline = TRUE;
	}

	e_book_backend_set_is_loaded (backend, TRUE);
}

static void
e_book_backend_ews_gal_remove	(EBookBackend *backend,
				 EDataBook        *book,
				 guint32           opid)
{
	e_data_book_respond_remove (book,  opid, EDB_ERROR (SUCCESS));
}

static gchar *
e_book_backend_ews_gal_get_static_capabilities (EBookBackend *backend)
{
	/* do-initial-query is enabled for system address book also, so that we get the
	 * book_view, which is needed for displaying cache update progress.
	 * and null query is handled for system address book.
	 */
	return g_strdup ("net,bulk-removes,do-initial-query,contact-lists");
}

static void
e_book_backend_ews_gal_get_supported_auth_methods (EBookBackend *backend, EDataBook *book, guint32 opid)
{
	GList *auth_methods = NULL;
	gchar *auth_method;

	auth_method =  g_strdup_printf ("plain/password");
	auth_methods = g_list_append (auth_methods, auth_method);
	e_data_book_respond_get_supported_auth_methods (book,
							opid,
							EDB_ERROR (SUCCESS),
							auth_methods);
	g_free (auth_method);
	g_list_free (auth_methods);
}

static void
e_book_backend_ews_gal_set_mode (EBookBackend *backend,
                                   EDataBookMode mode)
{
	EBookBackendEwsGal *ebews;
	EBookBackendEwsGalPrivate *priv;

	ebews = E_BOOK_BACKEND_EWS_GAL (backend);
	priv = ebews->priv;
	priv->mode = mode;

	if (e_book_backend_is_loaded (backend)) {
		if (mode == E_DATA_BOOK_MODE_LOCAL) {
			e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, FALSE);
			
			if (priv->dlock) {
				g_mutex_lock (priv->dlock->mutex);
				priv->dlock->exit = TRUE;
				g_mutex_unlock (priv->dlock->mutex);

				g_cond_signal (priv->dlock->cond);
			}
					
			if (priv->cnc) {
				g_object_unref (priv->cnc);
				priv->cnc=NULL;
			}
		}
		else if (mode == E_DATA_BOOK_MODE_REMOTE) {
			e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, TRUE);
			e_book_backend_notify_auth_required (backend);
		}
	}
}

/**
 * e_book_backend_ews_new:
 */
EBookBackend *
e_book_backend_ews_gal_new (void)
{
	EBookBackendEwsGal *backend;

	backend = g_object_new (E_TYPE_BOOK_BACKEND_EWS_GAL, NULL);

	return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_ews_gal_dispose (GObject *object)
{
	EBookBackendEwsGal *bgw;
        EBookBackendEwsGalPrivate *priv;

	bgw = E_BOOK_BACKEND_EWS_GAL (object);
        priv = bgw->priv;

	if (priv->cnc) {
		g_object_unref (priv->cnc);
		priv->cnc = NULL;
	}

	if (priv->oal_id) {
		g_free (priv->oal_id);
		priv->oal_id = NULL;
	}
	
	if (priv->oab_url) {
		g_free (priv->oab_url);
		priv->oab_url = NULL;
	}
	
	if (priv->folder_name) {
		g_free (priv->folder_name);
		priv->folder_name = NULL;
	}

	if (priv->attachment_dir) {
		g_free (priv->attachment_dir);
		priv->attachment_dir = NULL;
	}

	if (priv->dlock) {
		g_mutex_lock (priv->dlock->mutex);
		priv->dlock->exit = TRUE;
		g_mutex_unlock (priv->dlock->mutex);

		g_cond_signal (priv->dlock->cond);

		if (priv->dthread)
			g_thread_join (priv->dthread);

		g_mutex_free (priv->dlock->mutex);
		g_cond_free (priv->dlock->cond);
		g_free (priv->dlock);
		priv->dthread = NULL;
	}

	if (priv->ebsdb) {
		g_object_unref (priv->ebsdb);
		priv->ebsdb = NULL;
	}

	g_static_rec_mutex_free (&priv->rec_mutex);

	g_free (priv);
	priv = NULL;

	G_OBJECT_CLASS (e_book_backend_ews_gal_parent_class)->dispose (object);
}

static void
e_book_backend_ews_gal_class_init (EBookBackendEwsGalClass *klass)
{

	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *parent_class;
	gint i;

	parent_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	parent_class->load_source             = e_book_backend_ews_gal_load_source;
	parent_class->get_static_capabilities = e_book_backend_ews_gal_get_static_capabilities;
	parent_class->remove                  = e_book_backend_ews_gal_remove;

	parent_class->set_mode                = e_book_backend_ews_gal_set_mode;
	parent_class->get_required_fields     = e_book_backend_ews_gal_get_required_fields;
	parent_class->get_supported_fields    = e_book_backend_ews_gal_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_ews_gal_get_supported_auth_methods;

	parent_class->authenticate_user       = e_book_backend_ews_gal_authenticate_user;

	parent_class->start_book_view         = e_book_backend_ews_gal_start_book_view;
	parent_class->stop_book_view          = e_book_backend_ews_gal_stop_book_view;
	parent_class->cancel_operation        = e_book_backend_ews_gal_cancel_operation;

	parent_class->create_contact          = e_book_backend_ews_gal_create_contact;
	parent_class->remove_contacts         = e_book_backend_ews_gal_remove_contacts;
	parent_class->modify_contact          = e_book_backend_ews_gal_modify_contact;
	parent_class->get_contact             = e_book_backend_ews_gal_get_contact;
	parent_class->get_contact_list        = e_book_backend_ews_gal_get_contact_list;

	parent_class->get_changes             = e_book_backend_ews_gal_get_changes;

	object_class->dispose                 = e_book_backend_ews_gal_dispose;

	/* TODO add only supported fields. For now, we add all the fields */
	supported_fields = NULL;
	for (i = 1; i < E_CONTACT_FIELD_LAST; i++) {
		supported_fields = g_list_append (supported_fields,
				(gchar *)e_contact_field_name (i));
	}
}

static void
e_book_backend_ews_gal_init (EBookBackendEwsGal *backend)
{
	EBookBackendEwsGal *bewsgal;
	EBookBackendEwsGalPrivate *priv;

	bewsgal = E_BOOK_BACKEND_EWS_GAL (backend);

	priv = g_new0 (EBookBackendEwsGalPrivate, 1);
	priv->ops = g_hash_table_new (NULL, NULL);

	bewsgal->priv = priv;
	g_static_rec_mutex_init (&priv->rec_mutex);
}
