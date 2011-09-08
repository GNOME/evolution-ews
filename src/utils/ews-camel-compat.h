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

gchar *
camel_session_get_password_compat (CamelSession *session,
	                            CamelService *service,
				    const gchar *domain,
                	            const gchar *prompt,
                        	    const gchar *item,
	                            guint32 flags,
        	                    GError **error);

CamelService * camel_session_get_service_compat (CamelSession *session, const gchar *url, CamelProviderType type);
#if ! EDS_CHECK_VERSION(3,1,0)
CamelURL * camel_service_get_camel_url (CamelService *service);
CamelServiceConnectionStatus camel_service_get_connection_status (CamelService *service);

#else
gchar * camel_session_get_storage_path (CamelSession *session, CamelService *service, GError **error);
#endif

#endif /* EWS_CAMEL_COMPAT_H */
