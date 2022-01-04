/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_DEBUG_H
#define E_EWS_DEBUG_H

#include <glib.h>
#include <common/e-ews-connection.h>

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
const gchar *	e_ews_debug_redact_headers		(gchar direction,
							 const gchar *data);
void		e_ews_debug_soup_log_printer_stdout	(SoupLogger *logger,
							 SoupLoggerLogLevel level,
							 char direction,
							 const gchar *data,
							 gpointer user_data);
G_END_DECLS

#endif /* E_EWS_DEBUG_H */


