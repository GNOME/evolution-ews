/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CAMEL_EWS_UTILS_H
#define CAMEL_EWS_UTILS_H

#include <e-ews-folder.h>

G_BEGIN_DECLS

gboolean
ews_esource_utils_add_esource	(EEwsFolder *folder, 
				 const gchar *account_name, 
				 const gchar *username, 
				 const gchar *email_id,
				 const gchar *hosturl,
				 gint refresh_timeout);
gboolean
ews_esource_utils_remove_esource	
				(const gchar *fid, 
				 const gchar *account_name, 
				 const gchar *folder_id,
				 EwsFolderType ftype);

gboolean
ews_esource_utils_remove_groups	(const gchar *account_name);

gboolean
ews_source_utils_remove_group (const gchar *account_name, EwsFolderType ftype);

G_END_DECLS

#endif
