/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: JP Rosevear <jpr@ximian.com>
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_REQUEST_H
#define E_EWS_REQUEST_H

#include "e-soap-request.h"

G_BEGIN_DECLS

typedef enum {
	E_EWS_EXCHANGE_UNKNOWN = -1,
	E_EWS_EXCHANGE_2007,
	E_EWS_EXCHANGE_2007_SP1,
	E_EWS_EXCHANGE_2010,
	E_EWS_EXCHANGE_2010_SP1,
	E_EWS_EXCHANGE_2010_SP2,
	E_EWS_EXCHANGE_2013,
	E_EWS_EXCHANGE_FUTURE
} EEwsServerVersion;

ESoapRequest *	e_ews_request_new_with_header	(const gchar *uri,
						 const gchar *impersonate_user,
						 const gchar *method_name,
						 const gchar *attribute_name,
						 const gchar *attribute_value,
						 EEwsServerVersion server_version,
						 EEwsServerVersion minimum_version,
						 gboolean force_minimum_version,
						 GError **error);
void		e_ews_request_write_string_parameter
						(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prefix,
						 const gchar *value);
void		e_ews_request_write_string_parameter_with_attribute
						(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prefix,
						 const gchar *value,
						 const gchar *attribute_name,
						 const gchar *attribute_value);
void		e_ews_request_write_base64_parameter
						(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prefix,
						 const gchar *value);
void		e_ews_request_write_int_parameter
						(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prefix,
						 glong value);
void		e_ews_request_write_double_parameter
						(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prefix,
						 gdouble value);
void		e_ews_request_write_time_parameter
						(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prefix,
						 time_t value);
void		e_ews_request_write_footer	(ESoapRequest *req);

void		e_ews_request_write_extended_tag
						(ESoapRequest *req,
						 guint32 prop_id,
						 const gchar *prop_type);

void		e_ews_request_write_extended_distinguished_tag
						(ESoapRequest *req,
						 const gchar *set_id,
						 guint32 prop_id,
						 const gchar *prop_type);

void		e_ews_request_write_extended_name
						(ESoapRequest *req,
						 const gchar *name,
						 const gchar *prop_type);

void		e_ews_request_write_extended_distinguished_name
						(ESoapRequest *req,
						 const gchar *set_id,
						 const gchar *name,
						 const gchar *prop_type);

void		e_ews_request_replace_server_version
						(ESoapRequest *req,
						 EEwsServerVersion version);

G_END_DECLS

#endif
