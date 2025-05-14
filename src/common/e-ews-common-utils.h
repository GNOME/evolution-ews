/*
 * SPDX-FileCopyrightText: (C) 2024 Red Hat (www.redhat.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_COMMON_UTILS_H
#define E_EWS_COMMON_UTILS_H

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>
#include <libedata-cal/libedata-cal.h>

G_BEGIN_DECLS

gboolean	e_ews_common_utils_mail_folder_uri_parse	(CamelSession *session,
								 const gchar *folder_uri,
								 CamelStore **out_store,
								 gchar **out_folder_name,
								 GError **error);
GString *	e_ews_common_utils_str_replace_string		(const gchar *text,
								 const gchar *before,
								 const gchar *after);
ICalTimezone *	e_ews_common_utils_get_configured_icaltimezone	(void);
ICalProperty *	e_ews_common_utils_find_attendee		(ECalBackend *cal_backend,
								 ICalComponent *icomp,
								 GHashTable *aliases);
gboolean	e_ews_common_utils_gsettings_schema_exists	(const gchar *schema_id);
GHashTable *	e_ews_common_utils_dup_mail_addresses		(ESourceRegistry *registry,
								 ESource *child_source,
								 gchar **inout_user_email);

G_END_DECLS

#endif /* E_EWS_COMMON_UTILS_H */
