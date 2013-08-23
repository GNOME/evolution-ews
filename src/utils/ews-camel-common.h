/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2011 Intel, Inc. (www.intel.com)
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

#ifndef EWS_CAMEL_COMMON_H
#define	EWS_CAMEL_COMMON_H

#include <camel/camel.h>

#include "server/e-ews-connection.h"
#include "server/e-ews-folder.h"

G_BEGIN_DECLS

gboolean
camel_ews_utils_create_mime_message (EEwsConnection *cnc,
				     const gchar *disposition,
				     const EwsFolderId *fid,
				     CamelMimeMessage *message,
				     CamelMessageInfo *info,
				     CamelAddress *from,
				     CamelAddress *recipients,
				     gchar **itemid,
				     gchar **changekey,
				     GCancellable *cancellable,
				     GError **error);

G_END_DECLS

#endif	/* EWS_CAMEL_COMMON_H */

