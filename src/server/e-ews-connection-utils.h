/*
 * e-ews-connection-utils.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_EWS_CONNECTION_UTILS_H
#define E_EWS_CONNECTION_UTILS_H

#include <glib.h>
#include <server/e-ews-connection.h>

G_BEGIN_DECLS

#define E_EWS_CONNECTION_UTILS_CHECK_ELEMENT(element_name, expected_name) \
	(e_ews_connection_utils_check_element (G_STRFUNC, (element_name), (expected_name)))

void		e_ews_connection_utils_unref_in_thread	(gpointer object);
gboolean	e_ews_connection_utils_check_element	(const gchar *function_name,
							 const gchar *element_name,
							 const gchar *expected_name);

gboolean	e_ews_connection_utils_get_without_password
							(CamelEwsSettings *ews_settings);

G_END_DECLS

#endif /* E_EWS_CONNECTION_UTILS_H */


