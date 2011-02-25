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
#include "e-ews-item-change.h"

void
e_ews_message_start_item_change(ESoapMessage *msg, EEwsItemChangeType type,
				const gchar *itemid, const gchar *changekey,
				gint instance_index)
{
	gchar *instance;

	e_soap_message_start_element (msg, "ItemChange", "types", NULL);

	switch (type) {
	case E_EWS_ITEMCHANGE_TYPE_ITEM:
		e_soap_message_start_element (msg, "ItemId",
					      "types", NULL);
		e_soap_message_add_attribute (msg, "Id",
					      itemid, NULL, NULL);
		break;

	case E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM:
		e_soap_message_start_element (msg, "OccurrenceItemId",
					      "types", NULL);
		e_soap_message_add_attribute (msg, "RecurringMasterId",
					      itemid, NULL, NULL);
		instance = g_strdup_printf("%d", instance_index);
		e_soap_message_add_attribute (msg, "InstanceIndex", instance,
					      NULL, NULL);
		g_free(instance);
		break;

	case E_EWS_ITEMCHANGE_TYPE_RECURRINGMASTER:
		e_soap_message_start_element (msg, "RecurringMasterItemId",
					      "types", NULL);
		e_soap_message_add_attribute (msg, "OccurrenceId",
					      itemid, NULL, NULL);
		break;
	}
	if (changekey)
		e_soap_message_add_attribute (msg, "ChangeKey",
					      changekey, NULL, NULL);
	e_soap_message_end_element (msg);

	e_soap_message_start_element (msg, "Updates", "types", NULL);
}

void
e_ews_message_end_item_change (ESoapMessage *msg)
{
	e_soap_message_end_element (msg); /* Updates */
	e_soap_message_end_element (msg); /* ItemChange */
}
