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

gchar *
camel_session_get_password_compat (CamelSession *session,
	                            CamelService *service,
        	                    const gchar *domain,
				    const gchar *prompt,
                        	    const gchar *item,
	                            guint32 flags,
        	                    GError **error)
{
	
#if ! EDS_CHECK_VERSION(3,1,0)
	return camel_session_get_password (session, service, domain, prompt, item, flags, error);
#else
	return camel_session_get_password (session, service, prompt, item, flags, error);
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

#endif
