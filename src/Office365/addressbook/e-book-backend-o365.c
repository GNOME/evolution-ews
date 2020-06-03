/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "evolution-ews-config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>

#define LIBICAL_GLIB_UNSTABLE_API
#include <libical-glib/libical-glib.h>
#undef LIBICAL_GLIB_UNSTABLE_API

#include <libedata-book/libedata-book.h>

#include "common/camel-o365-settings.h"

#include "e-book-backend-o365.h"

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define d(x)

#define EC_ERROR_EX(_code,_msg) e_client_error_create (_code, _msg)
#define EBC_ERROR_EX(_code,_msg) e_book_client_error_create (_code, _msg)

#define EBB_O365_DATA_VERSION 1
#define EBB_O365_DATA_VERSION_KEY "o365-data-version"

struct _EBookBackendO365Private {
	GRecMutex property_lock;
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendO365, e_book_backend_o365, E_TYPE_BOOK_META_BACKEND)

static void
ebb_o365_convert_error_to_client_error (GError **perror)
{
	GError *error = NULL;

	if (!perror || !*perror || (*perror)->domain == E_CLIENT_ERROR || (*perror)->domain == E_BOOK_CLIENT_ERROR)
		return;

	/*if ((*perror)->domain == EWS_CONNECTION_ERROR) {
		switch ((*perror)->code) {
		case EWS_CONNECTION_ERROR_AUTHENTICATION_FAILED:
			error = EC_ERROR_EX (E_CLIENT_ERROR_AUTHENTICATION_FAILED, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_FOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_MANAGEDFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PARENTFOLDERNOTFOUND:
		case EWS_CONNECTION_ERROR_PUBLICFOLDERSERVERNOTFOUND:
			error = EBC_ERROR_EX (E_BOOK_CLIENT_ERROR_NO_SUCH_BOOK, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_EVENTNOTFOUND:
		case EWS_CONNECTION_ERROR_ITEMNOTFOUND:
			error = EBC_ERROR_EX (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND, (*perror)->message);
			break;
		case EWS_CONNECTION_ERROR_UNAVAILABLE:
			g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND, (*perror)->message);
			break;
		}

		if (!error)
			error = EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, (*perror)->message);
	}*/

	if (error) {
		g_error_free (*perror);
		*perror = error;
	}
}

static void
ebb_o365_maybe_disconnect_sync (EBookBackendO365 *bbo365,
			        GError **in_perror,
			        GCancellable *cancellable)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_O365 (bbo365));

	if (in_perror && g_error_matches (*in_perror, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		e_book_meta_backend_disconnect_sync (E_BOOK_META_BACKEND (bbo365), cancellable, NULL);
		e_backend_schedule_credentials_required (E_BACKEND (bbo365), E_SOURCE_CREDENTIALS_REASON_REJECTED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static void
ebb_o365_unset_connection (EBookBackendO365 *bbo365,
			   gboolean is_disconnect)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_O365 (bbo365));

	g_rec_mutex_lock (&bbo365->priv->property_lock);

	/*if (bbo365->priv->cnc) {
		if (is_disconnect)
			e_o365_connection_set_disconnected_flag (bbo365->priv->cnc, TRUE);
	}

	g_clear_object (&bbo365->priv->cnc);*/

	g_rec_mutex_unlock (&bbo365->priv->property_lock);
}

static gboolean
ebb_o365_connect_sync (EBookMetaBackend *meta_backend,
		       const ENamedParameters *credentials,
		       ESourceAuthenticationResult *out_auth_result,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
		       GCancellable *cancellable,
		       GError **error)
{
	EBookBackendO365 *bbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	g_rec_mutex_lock (&bbo365->priv->property_lock);

	/*if (bbo365->priv->cnc)*/ {
		g_rec_mutex_unlock (&bbo365->priv->property_lock);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		return TRUE;
	}

	g_rec_mutex_unlock (&bbo365->priv->property_lock);

	return success;
}

static gboolean
ebb_o365_disconnect_sync (EBookMetaBackend *meta_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	ebb_o365_unset_connection (E_BOOK_BACKEND_O365 (meta_backend), TRUE);

	return TRUE;
}

static gboolean
ebb_o365_get_changes_sync (EBookMetaBackend *meta_backend,
			   const gchar *last_sync_tag,
			   gboolean is_repeat,
			   gchar **out_new_sync_tag,
			   gboolean *out_repeat,
			   GSList **out_created_objects,
			   GSList **out_modified_objects,
			   GSList **out_removed_objects,
			   GCancellable *cancellable,
			   GError **error)
{
	EBookBackendO365 *bbo365;
	EBookCache *book_cache;
	gboolean success = TRUE;
	/*GError *local_error = NULL;*/

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (E_IS_BOOK_CACHE (book_cache), FALSE);

	g_rec_mutex_lock (&bbo365->priv->property_lock);

	g_rec_mutex_unlock (&bbo365->priv->property_lock);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	g_clear_object (&book_cache);

	return success;
}

static gboolean
ebb_o365_load_contact_sync (EBookMetaBackend *meta_backend,
			    const gchar *uid,
			    const gchar *extra,
			    EContact **out_contact,
			    gchar **out_extra,
			    GCancellable *cancellable,
			    GError **error)
{
	EBookBackendO365 *bbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact, FALSE);

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	g_rec_mutex_lock (&bbo365->priv->property_lock);

	g_rec_mutex_unlock (&bbo365->priv->property_lock);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	return success;
}

static gboolean
ebb_o365_save_contact_sync (EBookMetaBackend *meta_backend,
			    gboolean overwrite_existing,
			    EConflictResolution conflict_resolution,
			    /* const */ EContact *contact,
			    const gchar *extra,
			    guint32 opflags,
			    gchar **out_new_uid,
			    gchar **out_new_extra,
			    GCancellable *cancellable,
			    GError **error)
{
	EBookBackendO365 *bbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	g_rec_mutex_lock (&bbo365->priv->property_lock);

	g_rec_mutex_unlock (&bbo365->priv->property_lock);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	return success;
}

static gboolean
ebb_o365_remove_contact_sync (EBookMetaBackend *meta_backend,
			      EConflictResolution conflict_resolution,
			      const gchar *uid,
			      const gchar *extra,
			      const gchar *object,
			      guint32 opflags,
			      GCancellable *cancellable,
			      GError **error)
{
	EBookBackendO365 *bbo365;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	bbo365 = E_BOOK_BACKEND_O365 (meta_backend);

	g_rec_mutex_lock (&bbo365->priv->property_lock);

	g_rec_mutex_unlock (&bbo365->priv->property_lock);

	ebb_o365_convert_error_to_client_error (error);
	ebb_o365_maybe_disconnect_sync (bbo365, error, cancellable);

	return success;
}

static gboolean
ebb_o365_search_sync (EBookMetaBackend *meta_backend,
		      const gchar *expr,
		      gboolean meta_contact,
		      GSList **out_contacts,
		      GCancellable *cancellable,
		      GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	/*ebb_o365_update_cache_for_expression (E_BOOK_BACKEND_O365 (meta_backend), expr, cancellable, NULL);*/

	/* Chain up to parent's method */
	if (!E_BOOK_META_BACKEND_CLASS (e_book_backend_o365_parent_class)->search_sync (meta_backend, expr, meta_contact,
		out_contacts, cancellable, error))
		return FALSE;

	return TRUE;
}

static gboolean
ebb_o365_search_uids_sync (EBookMetaBackend *meta_backend,
			   const gchar *expr,
			   GSList **out_uids,
			   GCancellable *cancellable,
			   GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (meta_backend), FALSE);

	/* Ignore errors, just try its best */
	/*ebb_o365_update_cache_for_expression (E_BOOK_BACKEND_O365 (meta_backend), expr, cancellable, NULL);*/

	/* Chain up to parent's method */
	return E_BOOK_META_BACKEND_CLASS (e_book_backend_o365_parent_class)->search_uids_sync (meta_backend, expr,
		out_uids, cancellable, error);
}

static gchar *
ebb_o365_get_backend_property (EBookBackend *book_backend,
			       const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_O365 (book_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			"net",
			"contact-lists",
			"do-initial-query",
			e_book_meta_backend_get_capabilities (E_BOOK_META_BACKEND (book_backend)),
			NULL);
	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		return g_strdup (e_contact_field_name (E_CONTACT_FILE_AS));
	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GString *buffer;
		gchar *fields;
		/*gint ii;*/

		buffer = g_string_sized_new (1024);

		/*for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
			if (mappings[ii].element_type != ELEMENT_TYPE_SIMPLE)
				continue;

			if (buffer->len > 0)
				g_string_append_c (buffer, ',');
			g_string_append (buffer, e_contact_field_name (mappings[ii].field_id));
		}

		for (ii = 0; ii < G_N_ELEMENTS (phone_field_map); ii++) {
			if (buffer->len > 0)
				g_string_append_c (buffer, ',');
			g_string_append (buffer, e_contact_field_name (phone_field_map[ii].field));
		}*/

		fields = g_strjoin (
			",",
			buffer->str,
			e_contact_field_name (E_CONTACT_FULL_NAME),
			e_contact_field_name (E_CONTACT_NICKNAME),
			e_contact_field_name (E_CONTACT_FAMILY_NAME),
			e_contact_field_name (E_CONTACT_EMAIL_1),
			e_contact_field_name (E_CONTACT_EMAIL_2),
			e_contact_field_name (E_CONTACT_EMAIL_3),
			e_contact_field_name (E_CONTACT_ADDRESS_WORK),
			e_contact_field_name (E_CONTACT_ADDRESS_HOME),
			e_contact_field_name (E_CONTACT_ADDRESS_OTHER),
			e_contact_field_name (E_CONTACT_ANNIVERSARY),
			e_contact_field_name (E_CONTACT_BIRTH_DATE),
			e_contact_field_name (E_CONTACT_NOTE),
			e_contact_field_name (E_CONTACT_PHOTO),
			NULL);

		g_string_free (buffer, TRUE);

		return fields;
	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_o365_parent_class)->impl_get_backend_property (book_backend, prop_name);
}

static gboolean
ebb_o365_get_destination_address (EBackend *backend,
				  gchar **host,
				  guint16 *port)
{
	g_return_val_if_fail (port != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	/* Sanity checking */
	if (!e_book_backend_get_registry (E_BOOK_BACKEND (backend)) ||
	    !e_backend_get_source (backend))
		return FALSE;

	*host = g_strdup ("graph.microsoft.com");
	*port = 443;

	return TRUE;
}

static void
e_book_backend_o365_dispose (GObject *object)
{
	EBookBackendO365 *bbo365 = E_BOOK_BACKEND_O365 (object);

	ebb_o365_unset_connection (bbo365, FALSE);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_o365_parent_class)->dispose (object);
}

static void
e_book_backend_o365_finalize (GObject *object)
{
	EBookBackendO365 *bbo365 = E_BOOK_BACKEND_O365 (object);

	g_rec_mutex_clear (&bbo365->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_o365_parent_class)->finalize (object);
}

static void
e_book_backend_o365_init (EBookBackendO365 *bbo365)
{
	bbo365->priv = e_book_backend_o365_get_instance_private (bbo365);

	g_rec_mutex_init (&bbo365->priv->property_lock);
}

static void
e_book_backend_o365_class_init (EBookBackendO365Class *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	EBookBackendClass *book_backend_class;
	EBookMetaBackendClass *book_meta_backend_class;

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->backend_module_filename = "libebookbackendoffice365.so";
	book_meta_backend_class->backend_factory_type_name = "EBookBackendO365Factory";
	book_meta_backend_class->connect_sync = ebb_o365_connect_sync;
	book_meta_backend_class->disconnect_sync = ebb_o365_disconnect_sync;
	book_meta_backend_class->get_changes_sync = ebb_o365_get_changes_sync;
	book_meta_backend_class->load_contact_sync = ebb_o365_load_contact_sync;
	book_meta_backend_class->save_contact_sync = ebb_o365_save_contact_sync;
	book_meta_backend_class->remove_contact_sync = ebb_o365_remove_contact_sync;
	book_meta_backend_class->search_sync = ebb_o365_search_sync;
	book_meta_backend_class->search_uids_sync = ebb_o365_search_uids_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->impl_get_backend_property = ebb_o365_get_backend_property;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->get_destination_address = ebb_o365_get_destination_address;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = e_book_backend_o365_dispose;
	object_class->finalize = e_book_backend_o365_finalize;
}
