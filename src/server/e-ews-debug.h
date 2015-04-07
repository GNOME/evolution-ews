/*
 * e-ews-debug.h
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

#ifndef E_EWS_DEBUG_H
#define E_EWS_DEBUG_H

#include <glib.h>
#include <server/e-ews-connection.h>

G_BEGIN_DECLS

gint		e_ews_debug_get_log_level		(void);
const gchar *	e_ews_connection_get_server_version_string
							(EEwsConnection *cnc);
EEwsServerVersion
		e_ews_debug_get_server_version_from_string
							(const gchar *version);
void		e_ews_debug_dump_raw_soup_request
							(SoupMessage *msg);
void		e_ews_debug_dump_raw_soup_response
							(SoupMessage *msg);
G_END_DECLS

#endif /* E_EWS_DEBUG_H */


