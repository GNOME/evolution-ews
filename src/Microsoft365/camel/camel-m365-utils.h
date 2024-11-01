/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef CAMEL_M365_UTILS_H
#define CAMEL_M365_UTILS_H

#include <camel/camel.h>

#include "common/e-m365-connection.h"

EM365Connection *
		camel_m365_utils_new_connection	(CamelService *service,
						 GCancellable *cancellable);
gchar *		camel_m365_utils_encode_category_name
						(const gchar *name);
gchar *		camel_m365_utils_decode_category_name
						(const gchar *flag);
gboolean	camel_m365_utils_is_system_user_flag
						(const gchar *name);
const gchar *	camel_m365_utils_rename_label	(const gchar *cat,
						 gboolean from_cat);
void		camel_m365_utils_add_message_flags
						(JsonBuilder *builder,
						 CamelMessageInfo *info,
						 CamelMimeMessage *message);
gboolean	camel_m365_utils_fill_message_object_sync
						(JsonBuilder *builder,
						 CamelMimeMessage *message,
						 CamelMessageInfo *info,
						 CamelAddress *override_from,
						 CamelAddress *override_recipients, /* it merges them, not really override */
						 gboolean is_send,
						 GSList **out_attachments,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_m365_utils_create_message_sync
						(EM365Connection *cnc,
						 const gchar *folder_id,
						 CamelMimeMessage *message,
						 CamelMessageInfo *info,
						 gchar **out_appended_id,
						 GCancellable *cancellable,
						 GError **error);

#endif /* CAMEL_M365_UTILS_H */
