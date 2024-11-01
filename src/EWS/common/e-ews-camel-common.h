/*
 * SPDX-FileCopyrightText: (C) 1999-2011 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef EWS_CAMEL_COMMON_H
#define	EWS_CAMEL_COMMON_H

#include <camel/camel.h>

#include "common/e-ews-connection.h"
#include "common/e-ews-folder.h"

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
