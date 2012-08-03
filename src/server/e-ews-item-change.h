/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * Copyright © 2011 Intel Corporation
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

#ifndef E_EWS_ITEM_CHANGE_H
#define E_EWS_ITEM_CHANGE_H

#include "e-soap-message.h"
#include "e-soap-response.h"

G_BEGIN_DECLS

typedef enum {
	E_EWS_ITEMCHANGE_TYPE_FOLDER,
	E_EWS_ITEMCHANGE_TYPE_ITEM,
	E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM,
	E_EWS_ITEMCHANGE_TYPE_RECURRINGMASTER,
} EEwsItemChangeType;

void e_ews_message_start_item_change (ESoapMessage *msg, EEwsItemChangeType type,
				     const gchar *itemid, const gchar *changekey,
				     gint instance_index);
void e_ews_message_end_item_change (ESoapMessage *msg);

void e_ews_message_start_set_item_field (ESoapMessage *msg, const gchar *name, const gchar * fielduri_prefix, const gchar *field_kind);

void e_ews_message_start_set_indexed_item_field (ESoapMessage *msg, const gchar *name, const gchar * fielduri_prefix, const gchar *field_kind, const gchar *field_index, gboolean delete_field);

void e_ews_message_end_set_indexed_item_field (ESoapMessage *msg, gboolean delete_field);

void e_ews_message_end_set_item_field (ESoapMessage *msg);

void e_ews_message_add_delete_item_field (ESoapMessage *msg, const gchar *name, const gchar *fielduri_prefix);

void e_ews_message_add_delete_item_field_indexed (ESoapMessage *msg, const gchar *name, const gchar *fielduri_prefix, const gchar *field_index);

G_END_DECLS

#endif
