/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileContributor: Fabiano Fidêncio <fidencio@redhat.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef EWS_TEST_COMMON_H
#define EWS_TEST_COMMON_H

#include <uhttpmock/uhm.h>

#include "common/camel-ews-settings.h"
#include "common/e-ews-connection.h"

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
