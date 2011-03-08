/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-ews.c - Ews contact backend.
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
#include "db.h"

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
#include "libedata-book/e-book-backend-db-cache.h"
#include "libedata-book/e-book-backend-summary.h"
#include "e-book-backend-ews.h"

#include "e-ews-connection.h"
#include "e-ews-item.h"

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code,_msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)
#define EDB_ERROR_FAILED_STATUS(_code, _status) e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_ ## _code, "Failed with status 0x%x", _status)

G_DEFINE_TYPE (EBookBackendEws, e_book_backend_ews, E_TYPE_BOOK_BACKEND)

struct _EBookBackendEwsPrivate {
	EEwsConnection *cnc;
	gchar *folder_id;
	gchar *summary_file_name;
	gboolean only_if_exists;
	gboolean is_writable;
	gboolean is_cache_ready;
	gboolean is_summary_ready;
	gint mode;
	EBookBackendSummary *summary;
	
	guint cache_timeout;
};

#define CACHE_REFRESH_INTERVAL 600000
#define SUMMARY_FLUSH_TIMEOUT 5000

static void
e_book_backend_ews_create_contact (EBookBackend *backend,
					 EDataBook *book,
					 guint32 opid,
					 const gchar *vcard )
{
	EContact *contact = NULL;
	EBookBackendEws *ebews;

	ebews = E_BOOK_BACKEND_EWS (backend);

	switch (ebews->priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_create (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;

	case  E_DATA_BOOK_MODE_REMOTE :

		if (ebews->priv->cnc == NULL) {
			e_data_book_respond_create (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}
		
		if (!ebews->priv->is_writable) {
			e_data_book_respond_create (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}
		
		e_data_book_respond_create (book, opid, EDB_ERROR (SUCCESS), contact);

		return;
	default:
		break;
	}
}

static void
e_book_backend_ews_remove_contacts (EBookBackend *backend,
					  EDataBook    *book,
					  guint32 opid,
					  GList *id_list)
{
	EBookBackendEws *ebgw;
	GList *deleted_ids = NULL;

	ebgw = E_BOOK_BACKEND_EWS (backend);

	switch (ebgw->priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;

	case E_DATA_BOOK_MODE_REMOTE :
		if (ebgw->priv->cnc == NULL) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}

		if (!ebgw->priv->is_writable) {
			e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}

		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (SUCCESS),  deleted_ids);
		return;
	default :
		break;
	}
}


static void
e_book_backend_ews_modify_contact (EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 const gchar   *vcard)
{
	EContact *contact = NULL;
	EBookBackendEws *egwb;

	egwb = E_BOOK_BACKEND_EWS (backend);

	switch (egwb->priv->mode) {

	case E_DATA_BOOK_MODE_LOCAL :
		e_data_book_respond_modify (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	case E_DATA_BOOK_MODE_REMOTE :

		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (AUTHENTICATION_REQUIRED), NULL);
			return;
		}
		if (!egwb->priv->is_writable) {
			e_data_book_respond_modify (book, opid, EDB_ERROR (PERMISSION_DENIED), NULL);
			return;
		}
	
		e_data_book_respond_modify (book, opid, EDB_ERROR (SUCCESS), contact);
		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_contact (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const gchar   *id)
{
	EBookBackendEws *gwb;

	gwb =  E_BOOK_BACKEND_EWS (backend);

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
e_book_backend_ews_get_contact_list (EBookBackend *backend,
					   EDataBook    *book,
					   guint32       opid,
					   const gchar   *query )
{
	GList *vcard_list;
	EBookBackendEws *egwb;
	gboolean match_needed;

	egwb = E_BOOK_BACKEND_EWS (backend);
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

		match_needed = TRUE;
		e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);
		return;
	default :
		break;

	}
}

static void
e_book_backend_ews_start_book_view (EBookBackend  *backend,
				     EDataBookView *book_view)
{
}

static void
e_book_backend_ews_stop_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
}

static void
e_book_backend_ews_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const gchar *change_id)
{
}

static void
e_book_backend_ews_authenticate_user (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid,
					    const gchar *user,
					    const gchar *passwd,
					    const gchar *auth_method)
{
	EBookBackendEws *ebgw;
	EBookBackendEwsPrivate *priv;

	ebgw = E_BOOK_BACKEND_EWS (backend);
	priv = ebgw->priv;

	switch (ebgw->priv->mode) {
	case E_DATA_BOOK_MODE_LOCAL:
		/* load summary file for offline use */
		g_mkdir_with_parents (g_path_get_dirname (priv->summary_file_name), 0700);
		priv->summary = e_book_backend_summary_new (priv->summary_file_name,
						    SUMMARY_FLUSH_TIMEOUT);
		e_book_backend_summary_load (priv->summary);

		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);
		e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
		return;

	case E_DATA_BOOK_MODE_REMOTE:
		if (priv->cnc) { /*we have already authenticated to server */
			printf("already authenticated\n");
			e_data_book_respond_authenticate_user (book, opid, EDB_ERROR (SUCCESS));
			return;
		}

		return;
	default :
		break;
	}
}

static void
e_book_backend_ews_get_required_fields (EBookBackend *backend,
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
e_book_backend_ews_get_supported_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;

	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_ICQ)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_YAHOO)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_GADUGADU)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_MSN)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_SKYPE)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_JABBER)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_WORK)));
	e_data_book_respond_get_supported_fields (book, opid,
						  EDB_ERROR (SUCCESS),
						  fields);
	g_list_free (fields);
}

static void
e_book_backend_ews_cancel_operation (EBookBackend *backend, EDataBook *book, GError **perror)
{
}

static void
e_book_backend_ews_load_source (EBookBackend           *backend,
				      ESource                *source,
				      gboolean                only_if_exists,
				      GError                **perror)
{
}

static void
e_book_backend_ews_remove (EBookBackend *backend,
				 EDataBook        *book,
				 guint32           opid)
{
}

static gchar *
e_book_backend_ews_get_static_capabilities (EBookBackend *backend)
{
	EBookBackendEws *ebgw;

	ebgw = E_BOOK_BACKEND_EWS (backend);

	/* do-initialy-query is enabled for system address book also, so that we get the
	 * book_view, which is needed for displaying cache update progress.
	 * and null query is handled for system address book.
	 */
	return g_strdup ("net,bulk-removes,do-initial-query,contact-lists");
}

static void
e_book_backend_ews_get_supported_auth_methods (EBookBackend *backend, EDataBook *book, guint32 opid)
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
e_book_backend_ews_set_mode (EBookBackend *backend,
                                   EDataBookMode mode)
{
	EBookBackendEws *bg;

	bg = E_BOOK_BACKEND_EWS (backend);
	bg->priv->mode = mode;
	if (e_book_backend_is_loaded (backend)) {
		if (mode == E_DATA_BOOK_MODE_LOCAL) {
			e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, FALSE);
			if (bg->priv->cnc) {
				g_object_unref (bg->priv->cnc);
				bg->priv->cnc=NULL;
			}
		}
		else if (mode == E_DATA_BOOK_MODE_REMOTE) {
			if (bg->priv->is_writable)
				e_book_backend_notify_writable (backend, TRUE);
			else
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
e_book_backend_ews_new (void)
{
	EBookBackendEws *backend;

	backend = g_object_new (E_TYPE_BOOK_BACKEND_EWS, NULL);

	return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_ews_dispose (GObject *object)
{
	EBookBackendEws *bgw;
        EBookBackendEwsPrivate *priv;

	bgw = E_BOOK_BACKEND_EWS (object);
        priv = bgw->priv;

	G_OBJECT_CLASS (e_book_backend_ews_parent_class)->dispose (object);
}

static void
e_book_backend_ews_class_init (EBookBackendEwsClass *klass)
{

	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *parent_class;

	parent_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	parent_class->load_source             = e_book_backend_ews_load_source;
	parent_class->get_static_capabilities = e_book_backend_ews_get_static_capabilities;

	parent_class->create_contact          = e_book_backend_ews_create_contact;
	parent_class->remove_contacts         = e_book_backend_ews_remove_contacts;
	parent_class->modify_contact          = e_book_backend_ews_modify_contact;
	parent_class->get_contact             = e_book_backend_ews_get_contact;
	parent_class->get_contact_list        = e_book_backend_ews_get_contact_list;
	parent_class->start_book_view         = e_book_backend_ews_start_book_view;
	parent_class->stop_book_view          = e_book_backend_ews_stop_book_view;
	parent_class->get_changes             = e_book_backend_ews_get_changes;
	parent_class->authenticate_user       = e_book_backend_ews_authenticate_user;
	parent_class->get_required_fields     = e_book_backend_ews_get_required_fields;
	parent_class->get_supported_fields    = e_book_backend_ews_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_ews_get_supported_auth_methods;
	parent_class->cancel_operation        = e_book_backend_ews_cancel_operation;
	parent_class->remove                  = e_book_backend_ews_remove;
	parent_class->set_mode                = e_book_backend_ews_set_mode;
	object_class->dispose                 = e_book_backend_ews_dispose;
}

static void
e_book_backend_ews_init (EBookBackendEws *backend)
{
	EBookBackendEwsPrivate *priv;

	priv= g_new0 (EBookBackendEwsPrivate, 1);
}
