/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_CONNECTION_UTILS_H
#define E_EWS_CONNECTION_UTILS_H

#include <glib.h>
#include <common/e-ews-connection.h>

G_BEGIN_DECLS

#define E_EWS_CONNECTION_UTILS_CHECK_ELEMENT(element_name, expected_name) \
	(e_ews_connection_utils_check_element (G_STRFUNC, (element_name), (expected_name)))

void		e_ews_connection_utils_set_user_agent_header
							(SoupMessage *message,
							 CamelEwsSettings *settings);
void		e_ews_connection_utils_unref_in_thread	(gpointer object);
gboolean	e_ews_connection_utils_check_element	(const gchar *function_name,
							 const gchar *element_name,
							 const gchar *expected_name);

void		e_ews_connection_utils_force_off_ntlm_auth_check
							(void);
gboolean	e_ews_connection_utils_get_without_password
							(CamelEwsSettings *ews_settings);
void		e_ews_connection_utils_expired_password_to_error
							(const gchar *service_url,
							 GError **error);
gboolean	e_ews_connection_utils_check_x_ms_credential_headers
							(SoupMessage *message,
							 gint *out_expire_in_days,
							 gboolean *out_expired,
							 gchar **out_service_url);

G_END_DECLS

#endif /* E_EWS_CONNECTION_UTILS_H */


