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

#ifndef CAMEL_O365_UTILS_H
#define CAMEL_O365_UTILS_H

#include <camel/camel.h>

#include "common/e-o365-connection.h"

EO365Connection *
		camel_o365_utils_new_connection	(CamelService *service,
						 GCancellable *cancellable);
gchar *		camel_o365_utils_encode_category_name
						(const gchar *name);
gchar *		camel_o365_utils_decode_category_name
						(const gchar *flag);
gboolean	camel_o365_utils_is_system_user_flag
						(const gchar *name);
const gchar *	camel_o365_utils_rename_label	(const gchar *cat,
						 gboolean from_cat);
void		camel_o365_utils_add_message_flags
						(JsonBuilder *builder,
						 CamelMessageInfo *info,
						 CamelMimeMessage *message);
gboolean	camel_o365_utils_fill_message_object_sync
						(JsonBuilder *builder,
						 CamelMimeMessage *message,
						 CamelMessageInfo *info,
						 CamelAddress *override_from,
						 CamelAddress *override_recipients, /* it merges them, not really override */
						 gboolean is_send,
						 GSList **out_attachments,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_o365_utils_create_message_sync
						(EO365Connection *cnc,
						 const gchar *folder_id,
						 CamelMimeMessage *message,
						 CamelMessageInfo *info,
						 gchar **out_appended_id,
						 GCancellable *cancellable,
						 GError **error);

#endif /* CAMEL_O365_UTILS_H */