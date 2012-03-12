/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 1999-2011 Novell, Inc. (www.novell.com)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include "ews-camel-compat.h"

gint
camel_stream_flush_compat (CamelStream *stream, GCancellable *cancellable, GError **error)
{
#if EDS_CHECK_VERSION(2,33,0)
	return camel_stream_flush (stream, cancellable, error);
#elif EDS_CHECK_VERSION(2,29,0)
	return camel_stream_flush (stream, error);
#else
	return camel_stream_flush (stream);
#endif
}

gint
camel_stream_close_compat (CamelStream *stream, GCancellable *cancellable, GError **error)
{
#if EDS_CHECK_VERSION(2,33,0)
	return camel_stream_close (stream, cancellable, error);
#elif EDS_CHECK_VERSION(2,29,0)
	return camel_stream_close (stream, error);
#else
	return camel_stream_close (stream);
#endif
}


gchar *
camel_data_cache_get_filename_compat	(CamelDataCache *cdc,
	                               	 const gchar *path,
	                       	         const gchar *key,
        	                	 GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_data_cache_get_filename (cdc, path, key, error);
#else
	CamelException ex;
	gchar *ret;
	
	camel_exception_init (&ex);
	ret = camel_data_cache_get_filename (cdc, path, key, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	
	return ret;
#endif
}

gint
camel_data_cache_remove_compat (CamelDataCache *cdc,
				const gchar *path,
				const gchar *key,
				GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_data_cache_remove (cdc, path, key, error);
#else
	CamelException ex;
	gint ret;
	
	camel_exception_init (&ex);
	ret = camel_data_cache_remove (cdc, path, key, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);

	return ret;
#endif
}

CamelDataCache *
camel_data_cache_new_compat	(const gchar *path,
		                 GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_data_cache_new (path, error);
#else
	CamelException ex;
	CamelDataCache *ret;

	camel_exception_init (&ex);
	ret = camel_data_cache_new (path, 0, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	
	return ret;
#endif
}

CamelStream *
camel_data_cache_get_compat	(CamelDataCache *cdc,
				 const gchar *path,
				 const gchar *key,
				 GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_data_cache_get (cdc, path, key, error);
#else
	CamelException ex;
	CamelStream *ret;

	camel_exception_init (&ex);
	ret = camel_data_cache_get (cdc, path, key, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	
	return ret;
#endif
}

GPtrArray *
camel_folder_search_search_compat	(CamelFolderSearch *search,
					 const gchar *expr,
					 GPtrArray *uids,
			 		 GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_folder_search_search (search, expr, uids, error);
#else
	CamelException ex;
	GPtrArray *ret;

	camel_exception_init (&ex);
	ret = camel_folder_search_search (search, expr, uids, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	
	return ret;
#endif
}

guint32
camel_folder_search_count_compat	(CamelFolderSearch *search,
					 const gchar *expr,
					 GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_folder_search_count (search, expr, error);
#else
	CamelException ex;
	guint32 ret;

	camel_exception_init (&ex);
	ret = camel_folder_search_count (search, expr, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	
	return ret;
#endif
}

gchar *		
camel_session_get_storage_path_compat	(CamelSession *session,
					 CamelService *service,
					 GError **error)

{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_session_get_storage_path (session, service, error);
#else	
	CamelException ex;
	gchar *ret;

	camel_exception_init (&ex);
	ret = camel_session_get_storage_path (session, service, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	return ret;
#endif	
}

gchar *             
camel_session_get_password_compat	(CamelSession *session,
					 CamelService *service,
			 		 const gchar *domain,
					 const gchar *prompt,
					 const gchar *item,
					 guint32 flags,
					 GError **error)
{

#if EDS_CHECK_VERSION(3,1,0)
	return camel_session_get_password (session, service, prompt, item, flags, error);
#elif EDS_CHECK_VERSION(2,29,0)
	return camel_session_get_password (session, service, domain, prompt, item, flags, error);
#else	
	CamelException ex;
	gchar *ret;

	camel_exception_init (&ex);
	ret = camel_session_get_password (session, service, domain, prompt, item, flags, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	return ret;
#endif	
}

gboolean            
camel_service_connect_compat	(CamelService *service,
				 GError **error)
{
#if EDS_CHECK_VERSION(2,33,0)
	return camel_service_connect_sync (service, error);
#elif EDS_CHECK_VERSION(2,29,0)
	return camel_service_connect (service, error);
#else	
	CamelException ex;
	gboolean ret;
	
	camel_exception_init (&ex);
	ret = camel_service_connect (service, &ex);
	ews_compat_propagate_exception_to_gerror (&ex, error);
	return ret;
#endif	
}

#if ! EDS_CHECK_VERSION(2,33,0)
gssize
camel_data_wrapper_write_to_stream_sync (CamelDataWrapper *data_wrapper,
                                         CamelStream *stream, 
					 GCancellable *cancellable,
                                         GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_data_wrapper_write_to_stream (data_wrapper, stream, error);
#else
	return camel_data_wrapper_write_to_stream (data_wrapper, stream);
#endif	
}

gssize
camel_data_wrapper_decode_to_stream_sync (CamelDataWrapper *data_wrapper,
                                          CamelStream *stream,
                                          GCancellable *cancellable,
                                          GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_data_wrapper_decode_to_stream (data_wrapper, stream, error);
#else
	return camel_data_wrapper_decode_to_stream (data_wrapper, stream);
#endif
}

gboolean 
camel_data_wrapper_construct_from_stream_sync	(CamelDataWrapper *data_wrapper,
						 CamelStream *stream,
						 GCancellable *cancellable,
						 GError **error)
{
	/* this returns an int, which is zero for success in earlier versions */
#if EDS_CHECK_VERSION(2,29,0)
	return !camel_data_wrapper_construct_from_stream(data_wrapper, stream, error);
#else
	return !camel_data_wrapper_construct_from_stream(data_wrapper, stream);
#endif
}

gboolean
camel_mime_part_construct_from_parser_sync (CamelMimePart *mime_part,
                                            CamelMimeParser *parser,
                                            GCancellable *cancellable,
                                            GError **error)
{
#if EDS_CHECK_VERSION(2,29,0)
	return camel_mime_part_construct_from_parser_sync (mime_part, parser, error);
#else
	return camel_mime_part_construct_from_parser (mime_part, parser);
#endif
}

#endif /* missing apis in version < 3.0 */

/* Missing apis in Evolution 2.28 */
#if ! EDS_CHECK_VERSION(2,29,0)
GQuark
camel_error_quark_compat (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

GByteArray *
camel_stream_mem_get_byte_array (CamelStreamMem *mem)
{
	return mem->buffer;
}

CamelDataWrapper *
camel_medium_get_content (CamelMedium *medium)
{
	return medium->content;
}

const gchar *
camel_data_cache_get_path (CamelDataCache *cdc)
{
	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), NULL);

	return cdc->path;
}

void
camel_object_set_state_filename (CamelObject *object,
                                 const gchar *state_filename)
{
	camel_object_set(object, NULL, CAMEL_OBJECT_STATE_FILE, state_filename, NULL);
}


void
camel_folder_changed (CamelFolder *folder,
                      CamelFolderChangeInfo *changes)
{
	camel_object_trigger_event (folder, "folder_changed", changes);
}

void
camel_service_lock (CamelService *service,
                    CamelServiceLock lock)
{
	/* dummy function. As this is present in camel-private.h we cannot implement a backward compatible
	   API from an external backend. I think its not necessary to use a service lock while deleting messages,
	   (ews _delete_messages)but since I have not investigated much on it, not removing it. */
}

void
camel_service_unlock (CamelService *service,
                    CamelServiceLock lock)
{
	/* dummy function */
}

void
camel_folder_summary_lock (CamelFolderSummary *summary,
                             CamelFolderSummaryLock lock)
{
	/* dummy function, same as service lock */
}

void
camel_folder_summary_unlock (CamelFolderSummary *summary,
                             CamelFolderSummaryLock lock)
{
	/* dummy function, same as service lock */
}

void
camel_folder_set_description (CamelFolder *folder,
                              const gchar *description)
{
	folder->description = g_strdup (description);
}

/* requires complete patch to be backported. isn't worth to backport it as 
   cancellation is not possible with 2.28 */
void
camel_folder_set_lock_async (CamelFolder *folder,
                             gboolean skip_folder_lock)
{
	/* dummy function */
}

CamelStream *
camel_stream_filter_new (CamelStream *stream)
{
	return (CamelStream *) camel_stream_filter_new_with_stream (stream);
}

void
ews_compat_propagate_exception_to_gerror (CamelException *ex, GError **error)
{
	if (error && camel_exception_is_set (ex)) {
		g_set_error (error, CAMEL_ERROR, ex->id, "%s", ex->desc);
	}
	camel_exception_clear (ex);
}

void
ews_compat_propagate_gerror_to_exception (GError *error, CamelException *ex)
{
	if (error && ex) {
		camel_exception_set (ex, error->code, error->message);
	}
	g_clear_error (&error);
}
#endif /* 2.28 */

CamelService *
camel_session_get_service_compat (CamelSession *session, const gchar *url, CamelProviderType type)
{
#if ! EDS_CHECK_VERSION(3,1,0)
	return camel_session_get_service (session, url, type, NULL);
#else
	CamelURL *curl = camel_url_new (url, NULL);
	CamelService *service;

	service = camel_session_get_service_by_url (session, curl, type);
	camel_url_free (curl);
	return service;
#endif	
}

#if ! EDS_CHECK_VERSION(3,1,0)

CamelURL *
camel_service_get_camel_url (CamelService *service)
{
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	return service->url;
}

CamelServiceConnectionStatus
camel_service_get_connection_status (CamelService *service)
{
	return service->status;
}

#else

gchar *
camel_session_get_storage_path (CamelSession *session, CamelService *service, GError **error)
{
	return g_strdup (camel_service_get_user_data_dir (service));
}

#endif
