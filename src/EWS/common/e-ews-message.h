/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: JP Rosevear <jpr@ximian.com>
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_MESSAGE_H
#define E_EWS_MESSAGE_H

#include "e-soap-message.h"
#include "camel-ews-settings.h"

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

void		e_ews_message_attach_chunk_allocator
						(SoupMessage *message);
void		e_ews_message_set_user_agent_header
						(SoupMessage *message,
						 CamelEwsSettings *settings);
ESoapMessage *	e_ews_message_new_with_header	(CamelEwsSettings *settings,
						 const gchar *uri,
						 const gchar *impersonate_user,
						 const gchar *method_name,
						 const gchar *attribute_name,
						 const gchar *attribute_value,
						 EEwsServerVersion server_version,
						 EEwsServerVersion minimum_version,
						 gboolean force_minimum_version,
						 gboolean standard_handlers);
void		e_ews_message_write_string_parameter
						(ESoapMessage *msg,
						 const gchar *name,
						 const gchar *prefix,
						 const gchar *value);
void		e_ews_message_write_string_parameter_with_attribute
						(ESoapMessage *msg,
						 const gchar *name,
						 const gchar *prefix,
						 const gchar *value,
						 const gchar *attribute_name,
						 const gchar *attribute_value);
void		e_ews_message_write_base64_parameter
						(ESoapMessage *msg,
						 const gchar *name,
						 const gchar *prefix,
						 const gchar *value);
void		e_ews_message_write_int_parameter
						(ESoapMessage *msg,
						 const gchar *name,
						 const gchar *prefix,
						 glong value);
void		e_ews_message_write_double_parameter
						(ESoapMessage *msg,
						 const gchar *name,
						 const gchar *prefix,
						 gdouble value);
void		e_ews_message_write_time_parameter
						(ESoapMessage *msg,
						 const gchar *name,
						 const gchar *prefix,
						 time_t value);
void		e_ews_message_write_footer	(ESoapMessage *msg);

void		e_ews_message_write_extended_tag
						(ESoapMessage *msg,
						 guint32 prop_id,
						 const gchar *prop_type);

void		e_ews_message_write_extended_distinguished_tag
						(ESoapMessage *msg,
						 const gchar *set_id,
						 guint32 prop_id,
						 const gchar *prop_type);

void		e_ews_message_write_extended_name
						(ESoapMessage *msg,
						 const gchar *name,
						 const gchar *prop_type);

void		e_ews_message_write_extended_distinguished_name
						(ESoapMessage *msg,
						 const gchar *set_id,
						 const gchar *name,
						 const gchar *prop_type);

void		e_ews_message_replace_server_version (ESoapMessage *msg,
						      EEwsServerVersion version);

G_END_DECLS

#endif
