/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors : Or Goshen, Pavel Ocheretny
 *
 * Copyright (C) 1999-2011 Intel, Inc. (www.intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef E_EWS_PARSER_H
#define E_EWS_PARSER_H

#include <glib-object.h>
#include "e-ews-message.h"

G_BEGIN_DECLS

typedef struct {
	gchar *id;
	gchar *change_key;
} EwsId;

void	e_ews_free_id (EwsId *id);

EwsId	*e_ews_parse_item_id (ESoapParameter *item_node, GError **error);

//GObject *e_ews_parse_calendar_item (ESoapParameter *item_node);

//GObject *e_ews_parse_camel_item (ESoapParameter *item_node);



G_END_DECLS

#endif
