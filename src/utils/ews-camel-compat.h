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
#ifndef EWS_CAMEL_COMPAT_H
#define EWS_CAMEL_COMPAT_H

#include <camel/camel.h>
#include <libedataserver/eds-version.h>
#include <glib.h>
#include <gio/gio.h>

/* APIs which have the same symbol name across versions but with arguments changed are suffixed with _compat.
   Rest of the apis are maintained with the latest camel apis in master */
gint		camel_stream_flush_compat 	(CamelStream *stream, 
						 GCancellable *cancellable, 
						 GError **error);
gint		camel_stream_close_compat 	(CamelStream *stream, 
						 GCancellable *cancellable, 
						 GError **error);
gchar *		camel_data_cache_get_filename_compat    
						(CamelDataCache *cdc,
	                                         const gchar *path,
        	                                 const gchar *key,
                	                         GError **error);
CamelDataCache *camel_data_cache_new_compat     (const gchar *path,
                		                 GError **error);
CamelStream *	camel_data_cache_get_compat     (CamelDataCache *cdc,
                		                 const gchar *path,
                                		 const gchar *key,
		                                 GError **error);
GPtrArray *	camel_folder_search_search_compat       
						(CamelFolderSearch *search,
	                                         const gchar *expr,
        	                                 GPtrArray *uids,
                	                         GError **error);
guint32		camel_folder_search_count_compat        
						(CamelFolderSearch *search,
	                                         const gchar *expr,
        	                                 GError **error);
gchar *		camel_session_get_storage_path_compat
						(CamelSession *session,
						 CamelService *service,
						 GError **error);
gchar *		camel_session_get_password_compat	
						(CamelSession *session,
						 CamelService *service,
				 		 const gchar *domain,
						 const gchar *prompt,
						 const gchar *item,
						 guint32 flags,
						 GError **error);
gboolean	camel_service_connect_compat	(CamelService *service,
						 GError **error);


/* Missing apis or renamed apis beyond 3.0 */
#if ! EDS_CHECK_VERSION(2,33,0)
gssize		camel_data_wrapper_write_to_stream_sync 
						(CamelDataWrapper *data_wrapper, 
						 CamelStream *stream, GCancellable *cancellable,
						 GError **error);
gssize		camel_data_wrapper_decode_to_stream_sync 
						(CamelDataWrapper *data_wrapper,
	                                         CamelStream *stream,
        	                                 GCancellable *cancellable,
                	                         GError **error);
gboolean	camel_data_wrapper_construct_from_stream_sync   
						(CamelDataWrapper *data_wrapper,
                                                 CamelStream *stream,
                                                 GCancellable *cancellable,
                                                 GError **error);
gboolean	camel_mime_part_construct_from_parser_sync 
						(CamelMimePart *mime_part,
						 CamelMimeParser *parser,
						 GCancellable *cancellable,
						 GError **error);
#endif

/* Missing api's and abi's in Evolution 2.28 */
#if EDS_CHECK_VERSION(2,29,0)
#else
GQuark
camel_error_quark_compat (void);

/* Since 2.32 */
#define CAMEL_ERROR \
	(camel_error_quark_compat ())
#define CAMEL_STORE_ERROR CAMEL_ERROR /* it doesn matter */
#define CAMEL_SERVICE_ERROR CAMEL_ERROR /* it doesn matter */

typedef enum {
	CAMEL_ERROR_GENERIC		/* lazy fallback error */
} CamelError;

typedef enum {
	CAMEL_STORE_ERROR_INVALID,
	CAMEL_STORE_ERROR_NO_FOLDER
} CamelStoreError;


typedef enum {
	CAMEL_SERVICE_ERROR_INVALID,
	CAMEL_SERVICE_ERROR_URL_INVALID,
	CAMEL_SERVICE_ERROR_UNAVAILABLE,
	CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
	CAMEL_SERVICE_ERROR_NOT_CONNECTED
} CamelServiceError;

typedef enum _CamelFolderSummaryLock {
	CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK,
	CAMEL_FOLDER_SUMMARY_IO_LOCK,
	CAMEL_FOLDER_SUMMARY_FILTER_LOCK,
	CAMEL_FOLDER_SUMMARY_ALLOC_LOCK,
	CAMEL_FOLDER_SUMMARY_REF_LOCK
} CamelFolderSummaryLock;

typedef enum {
	CAMEL_SERVICE_REC_CONNECT_LOCK,
	CAMEL_SERVICE_CONNECT_OP_LOCK
} CamelServiceLock;

GByteArray *		camel_stream_mem_get_byte_array (CamelStreamMem *mem);
CamelDataWrapper *	camel_medium_get_content	(CamelMedium *medium);
const gchar *		camel_data_cache_get_path	(CamelDataCache *cdc);
void			camel_object_set_state_filename (CamelObject *object,
                                 			 const gchar *state_filename);
void			camel_folder_changed		(CamelFolder *folder,
                      					 CamelFolderChangeInfo *changes);
void			camel_service_lock		(CamelService *service,
					                 CamelServiceLock lock);
void			camel_service_unlock		(CamelService *service,
							 CamelServiceLock lock);
void			camel_folder_summary_lock	(CamelFolderSummary *summary,
							 CamelFolderSummaryLock lock);
void			camel_folder_summary_unlock	(CamelFolderSummary *summary,
							 CamelFolderSummaryLock lock);
void			camel_folder_set_description	(CamelFolder *folder,
							 const gchar *description);
void			camel_folder_set_lock_async	(CamelFolder *folder,
							 gboolean skip_folder_lock);
CamelStream *		camel_stream_filter_new 	(CamelStream *stream);
void			ews_compat_propagate_exception_to_gerror 
							(CamelException *ex, GError **error);
void			ews_compat_propagate_gerror_to_exception 
							(GError *error, 
							 CamelException *ex);


#endif /* 2.28 api adds */


#endif /* EWS_CAMEL_COMPAT_H */
