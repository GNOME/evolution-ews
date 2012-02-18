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

#include "e-soap-message.h"
#include "e-soap-response.h"
#include "e-ews-message.h"
#include "e-ews-item-change.h"

void
e_ews_message_start_item_change (ESoapMessage *msg,
                                 EEwsItemChangeType type,
                                 const gchar *itemid,
                                 const gchar *changekey,
                                 gint instance_index)
{
	gchar *instance;

	switch (type) {
	case E_EWS_ITEMCHANGE_TYPE_FOLDER:
		e_soap_message_start_element (msg, "FolderChange", NULL, NULL);
		e_soap_message_start_element (msg, "FolderId",
					      NULL, NULL);
		e_soap_message_add_attribute (msg, "Id",
					      itemid, NULL, NULL);
		break;

	case E_EWS_ITEMCHANGE_TYPE_ITEM:
		e_soap_message_start_element (msg, "ItemChange", NULL, NULL);
		e_soap_message_start_element (msg, "ItemId",
					      NULL, NULL);
		e_soap_message_add_attribute (msg, "Id",
					      itemid, NULL, NULL);
		break;

	case E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM:
		e_soap_message_start_element (msg, "ItemChange", NULL, NULL);
		e_soap_message_start_element (msg, "OccurrenceItemId",
					      NULL, NULL);
		e_soap_message_add_attribute (msg, "RecurringMasterId",
					      itemid, NULL, NULL);
		instance = g_strdup_printf("%d", instance_index);
		e_soap_message_add_attribute (msg, "InstanceIndex", instance,
					      NULL, NULL);
		g_free (instance);
		break;

	case E_EWS_ITEMCHANGE_TYPE_RECURRINGMASTER:
		e_soap_message_start_element (msg, "ItemChange", NULL, NULL);
		e_soap_message_start_element (msg, "RecurringMasterItemId",
					      NULL, NULL);
		e_soap_message_add_attribute (msg, "OccurrenceId",
					      itemid, NULL, NULL);
		break;
	}
	if (changekey)
		e_soap_message_add_attribute (msg, "ChangeKey",
					      changekey, NULL, NULL);
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "Updates", NULL, NULL);
}

void
e_ews_message_end_item_change (ESoapMessage *msg)
{
	e_soap_message_end_element (msg); /* Updates */
	e_soap_message_end_element (msg); /* ItemChange */
}

void
e_ews_message_start_set_item_field (ESoapMessage *msg,
                                    const gchar *name,
                                    const gchar *fielduri_prefix,
                                    const gchar *field_kind)
{
	gchar * fielduri = NULL;
	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	e_soap_message_start_element (msg, "SetItemField", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "FieldURI", NULL, NULL, "FieldURI", fielduri);
	e_soap_message_start_element (msg, field_kind, NULL, NULL);

	g_free (fielduri);
}

void
e_ews_message_start_set_indexed_item_field (ESoapMessage *msg,
                                            const gchar *name,
                                            const gchar *fielduri_prefix,
                                            const gchar *field_kind,
                                            const gchar *field_index,
                                            gboolean delete_field)
{
	gchar * fielduri = NULL;
	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	if (delete_field)
		e_soap_message_start_element (msg, "DeleteItemField", NULL, NULL);
	else
		e_soap_message_start_element (msg, "SetItemField", NULL, NULL);

	e_soap_message_start_element (msg, "IndexedFieldURI", NULL, NULL);
	e_soap_message_add_attribute (msg, "FieldURI", fielduri, NULL, NULL);
	e_soap_message_add_attribute (msg, "FieldIndex", field_index, NULL, NULL);
	e_soap_message_end_element (msg);

	if (!delete_field)
		e_soap_message_start_element (msg, field_kind, NULL, NULL);

	g_free (fielduri);
}

void
e_ews_message_end_set_indexed_item_field (ESoapMessage *msg,
                                          gboolean delete_field)
{
	if (!delete_field)
		e_soap_message_end_element (msg); /* CalendarItem */
	e_soap_message_end_element (msg); /* SetItemField */
}

void
e_ews_message_end_set_item_field (ESoapMessage *msg)
{
	e_soap_message_end_element (msg); /* CalendarItem */
	e_soap_message_end_element (msg); /* SetItemField */
}
