/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors:
 *    Fabiano Fidêncio <fidencio@redhat.com>
 */

#ifndef EWS_TEST_COMMON_H
#define EWS_TEST_COMMON_H

#include <uhttpmock/uhm.h>

#include "server/camel-ews-settings.h"
#include "server/e-ews-connection.h"

G_BEGIN_DECLS

typedef struct _EwsTestData {
	EEwsConnection *connection;
	gchar *hostname;
	gchar *version;
	gchar *server_uri;
} EwsTestData;

gint			ews_test_init					(gint argc,
									 gchar **argv);
UhmServer *		ews_test_get_mock_server			(void);
GList *			ews_test_get_test_data_list			(void);
void			ews_test_cleanup				(void);
void			ews_test_server_start_trace			(UhmServer *server,
									 EwsTestData *etd,
									 const gchar *trace_filename,
									 GError **error);
void			ews_test_server_set_trace_directory		(UhmServer *server,
									 const gchar *version,
									 const gchar *tests);

G_END_DECLS

#endif /* EWS_TEST_COMMON_H */
