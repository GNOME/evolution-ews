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

#include "evolution-ews-config.h"

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
		e_soap_message_start_element (
			msg, "FolderId",
			NULL, NULL);
		e_soap_message_add_attribute (
			msg, "Id",
			itemid, NULL, NULL);
		break;

	case E_EWS_ITEMCHANGE_TYPE_ITEM:
		e_soap_message_start_element (msg, "ItemChange", NULL, NULL);
		e_soap_message_start_element (
			msg, "ItemId",
			NULL, NULL);
		e_soap_message_add_attribute (
			msg, "Id",
			itemid, NULL, NULL);
		break;

	case E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM:
		e_soap_message_start_element (msg, "ItemChange", NULL, NULL);
		e_soap_message_start_element (
			msg, "OccurrenceItemId",
			NULL, NULL);
		e_soap_message_add_attribute (
			msg, "RecurringMasterId",
			itemid, NULL, NULL);
		instance = g_strdup_printf ("%d", instance_index);
		e_soap_message_add_attribute (
			msg, "InstanceIndex", instance,
			NULL, NULL);
		g_free (instance);
		break;

	case E_EWS_ITEMCHANGE_TYPE_RECURRINGMASTER:
		e_soap_message_start_element (msg, "ItemChange", NULL, NULL);
		e_soap_message_start_element (
			msg, "RecurringMasterItemId",
			NULL, NULL);
		e_soap_message_add_attribute (
			msg, "OccurrenceId",
			itemid, NULL, NULL);
		break;
	}
	if (changekey)
		e_soap_message_add_attribute (
			msg, "ChangeKey",
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
	e_ews_message_write_string_parameter_with_attribute (
		msg, "FieldURI", NULL, NULL, "FieldURI", fielduri);
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

void
e_ews_message_add_delete_item_field (ESoapMessage *msg,
                                     const gchar *name,
                                     const gchar *fielduri_prefix)
{
	gchar *fielduri;

	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	e_soap_message_start_element (msg, "DeleteItemField", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (
		msg, "FieldURI", NULL, NULL, "FieldURI", fielduri);
	e_soap_message_end_element (msg); /* DeleteItemField */

	g_free (fielduri);
}

void
e_ews_message_add_delete_item_field_indexed (ESoapMessage *msg,
                                             const gchar *name,
                                             const gchar *fielduri_prefix,
                                             const gchar *field_index)
{
	gchar *fielduri;

	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	e_soap_message_start_element (msg, "DeleteItemField", NULL, NULL);
	e_soap_message_start_element (msg, "IndexedFieldURI", NULL, NULL);
	e_soap_message_add_attribute (msg, "FieldURI", fielduri, NULL, NULL);
	e_soap_message_add_attribute (msg, "FieldIndex", field_index, NULL, NULL);
	e_soap_message_end_element (msg); /* IndexedFieldURI */
	e_soap_message_end_element (msg); /* DeleteItemField */

	g_free (fielduri);
}

const gchar *
e_ews_message_data_type_get_xml_name (EEwsMessageDataType data_type)
{
	switch (data_type) {
	case E_EWS_MESSAGE_DATA_TYPE_BOOLEAN:
		return "Boolean";
	case E_EWS_MESSAGE_DATA_TYPE_INT:
		return "Integer";
	case E_EWS_MESSAGE_DATA_TYPE_DOUBLE:
		return "Double";
	case E_EWS_MESSAGE_DATA_TYPE_STRING:
		return "String";
	case E_EWS_MESSAGE_DATA_TYPE_TIME:
		return "SystemTime";
	}

	g_warn_if_reached ();

	return NULL;
}

void
e_ews_message_add_delete_item_field_extended_tag (ESoapMessage *msg,
						  guint32 prop_id,
						  EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "DeleteItemField", NULL, NULL);
	e_ews_message_write_extended_tag (msg, prop_id, prop_type);
	e_soap_message_end_element (msg); /* DeleteItemField */
}

void
e_ews_message_add_delete_item_field_extended_distinguished_tag (ESoapMessage *msg,
								const gchar *set_id,
								guint32 prop_id,
								EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "DeleteItemField", NULL, NULL);
	e_ews_message_write_extended_distinguished_tag (msg, set_id, prop_id, prop_type);
	e_soap_message_end_element (msg); /* DeleteItemField */
}

static void
ews_message_write_data_value (ESoapMessage *msg,
			      EEwsMessageDataType data_type,
			      gconstpointer value)
{
	g_return_if_fail (value != NULL);

	switch (data_type) {
	case E_EWS_MESSAGE_DATA_TYPE_BOOLEAN:
		e_ews_message_write_string_parameter (msg, "Value", NULL, (*((const gboolean *) value)) ? "true" : "false");
		return;
	case E_EWS_MESSAGE_DATA_TYPE_INT:
		e_ews_message_write_int_parameter (msg, "Value", NULL, *((const gint *) value));
		return;
	case E_EWS_MESSAGE_DATA_TYPE_DOUBLE:
		e_ews_message_write_double_parameter (msg, "Value", NULL, *((const gdouble *) value));
		return;
	case E_EWS_MESSAGE_DATA_TYPE_STRING:
		e_ews_message_write_string_parameter (msg, "Value", NULL, (const gchar *) value);
		return;
	case E_EWS_MESSAGE_DATA_TYPE_TIME:
		e_ews_message_write_time_parameter (msg, "Value", NULL, *((const time_t *) value));
		return;
	}

	g_warn_if_reached ();
}

static void
ews_message_add_extended_property_tag (ESoapMessage *msg,
				       guint32 prop_id,
				       EEwsMessageDataType data_type,
				       gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "ExtendedProperty", NULL, NULL);

	e_ews_message_write_extended_tag (msg, prop_id, prop_type);
	ews_message_write_data_value (msg, data_type, value);

	e_soap_message_end_element (msg); /* ExtendedProperty */
}

static void
ews_message_add_extended_property_distinguished_tag (ESoapMessage *msg,
						     const gchar *set_id,
						     guint32 prop_id,
						     EEwsMessageDataType data_type,
						     gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "ExtendedProperty", NULL, NULL);

	e_ews_message_write_extended_distinguished_tag (msg, set_id, prop_id, prop_type);
	ews_message_write_data_value (msg, data_type, value);

	e_soap_message_end_element (msg); /* ExtendedProperty */
}

static void
ews_message_add_set_item_field_extended_tag (ESoapMessage *msg,
					     const gchar *elem_prefix,
					     const gchar *elem_name,
					     guint32 prop_id,
					     EEwsMessageDataType data_type,
					     gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "SetItemField", NULL, NULL);
	e_ews_message_write_extended_tag (msg, prop_id, prop_type);

	e_soap_message_start_element (msg, elem_name, elem_prefix, NULL);
	ews_message_add_extended_property_tag (msg, prop_id, data_type, value);
	e_soap_message_end_element (msg); /* elem_name */

	e_soap_message_end_element (msg); /* SetItemField */
}

static void
ews_message_add_set_item_field_extended_distinguished_tag (ESoapMessage *msg,
							   const gchar *elem_prefix,
							   const gchar *elem_name,
							   const gchar *set_id,
							   guint32 prop_id,
							   EEwsMessageDataType data_type,
							   gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "SetItemField", NULL, NULL);

	e_ews_message_write_extended_distinguished_tag (msg, set_id, prop_id, prop_type);

	e_soap_message_start_element (msg, elem_name, elem_prefix, NULL);
	ews_message_add_extended_property_distinguished_tag (msg, set_id, prop_id, data_type, value);
	e_soap_message_end_element (msg); /* elem_name */

	e_soap_message_end_element (msg); /* SetItemField */
}

void
e_ews_message_add_set_item_field_extended_tag_boolean (ESoapMessage *msg,
						       const gchar *elem_prefix,
						       const gchar *elem_name,
						       guint32 prop_id,
						       gboolean value)
{
	ews_message_add_set_item_field_extended_tag (msg, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_set_item_field_extended_tag_int (ESoapMessage *msg,
						   const gchar *elem_prefix,
						   const gchar *elem_name,
						   guint32 prop_id,
						   gint value)
{
	ews_message_add_set_item_field_extended_tag (msg, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_set_item_field_extended_tag_double (ESoapMessage *msg,
						      const gchar *elem_prefix,
						      const gchar *elem_name,
						      guint32 prop_id,
						      gdouble value)
{
	ews_message_add_set_item_field_extended_tag (msg, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_set_item_field_extended_tag_string (ESoapMessage *msg,
						      const gchar *elem_prefix,
						      const gchar *elem_name,
						      guint32 prop_id,
						      const gchar *value)
{
	ews_message_add_set_item_field_extended_tag (msg, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_set_item_field_extended_tag_time (ESoapMessage *msg,
						    const gchar *elem_prefix,
						    const gchar *elem_name,
						    guint32 prop_id,
						    time_t value)
{
	ews_message_add_set_item_field_extended_tag (msg, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_tag_boolean (ESoapMessage *msg,
								     const gchar *elem_prefix,
								     const gchar *elem_name,
								     const gchar *set_id,
								     guint32 prop_id,
								     gboolean value)
{
	ews_message_add_set_item_field_extended_distinguished_tag (msg, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_tag_int (ESoapMessage *msg,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 guint32 prop_id,
								 gint value)
{
	ews_message_add_set_item_field_extended_distinguished_tag (msg, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_tag_double (ESoapMessage *msg,
								    const gchar *elem_prefix,
								    const gchar *elem_name,
								    const gchar *set_id,
								    guint32 prop_id,
								    gdouble value)
{
	ews_message_add_set_item_field_extended_distinguished_tag (msg, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_tag_string (ESoapMessage *msg,
								    const gchar *elem_prefix,
								    const gchar *elem_name,
								    const gchar *set_id,
								    guint32 prop_id,
								    const gchar *value)
{
	ews_message_add_set_item_field_extended_distinguished_tag (msg, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_tag_time (ESoapMessage *msg,
								  const gchar *elem_prefix,
								  const gchar *elem_name,
								  const gchar *set_id,
								  guint32 prop_id,
								  time_t value)
{
	ews_message_add_set_item_field_extended_distinguished_tag (msg, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_message_add_extended_property_tag_boolean (ESoapMessage *msg,
					         guint32 prop_id,
					         gboolean value)
{
	ews_message_add_extended_property_tag (msg, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_extended_property_tag_int (ESoapMessage *msg,
					     guint32 prop_id,
					     gint value)
{
	ews_message_add_extended_property_tag (msg, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_extended_property_tag_double (ESoapMessage *msg,
						guint32 prop_id,
						gdouble value)
{
	ews_message_add_extended_property_tag (msg, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_extended_property_tag_string (ESoapMessage *msg,
						guint32 prop_id,
						const gchar *value)
{
	ews_message_add_extended_property_tag (msg, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_extended_property_tag_time (ESoapMessage *msg,
					      guint32 prop_id,
					      time_t value)
{
	ews_message_add_extended_property_tag (msg, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_message_add_extended_property_distinguished_tag_boolean (ESoapMessage *msg,
							       const gchar *set_id,
							       guint32 prop_id,
							       gboolean value)
{
	ews_message_add_extended_property_distinguished_tag (msg, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_extended_property_distinguished_tag_int (ESoapMessage *msg,
							   const gchar *set_id,
							   guint32 prop_id,
							   gint value)
{
	ews_message_add_extended_property_distinguished_tag (msg, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_extended_property_distinguished_tag_double (ESoapMessage *msg,
							      const gchar *set_id,
							      guint32 prop_id,
							      gdouble value)
{
	ews_message_add_extended_property_distinguished_tag (msg, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_extended_property_distinguished_tag_string (ESoapMessage *msg,
							      const gchar *set_id,
							      guint32 prop_id,
							      const gchar *value)
{
	ews_message_add_extended_property_distinguished_tag (msg, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_extended_property_distinguished_tag_time (ESoapMessage *msg,
							    const gchar *set_id,
							    guint32 prop_id,
							    time_t value)
{
	ews_message_add_extended_property_distinguished_tag (msg, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_message_add_delete_item_field_extended_name (ESoapMessage *msg,
						   const gchar *name,
						   EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "DeleteItemField", NULL, NULL);
	e_ews_message_write_extended_name (msg, name, prop_type);
	e_soap_message_end_element (msg); /* DeleteItemField */
}

void
e_ews_message_add_delete_item_field_extended_distinguished_name (ESoapMessage *msg,
								 const gchar *set_id,
								 const gchar *name,
								 EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "DeleteItemField", NULL, NULL);
	e_ews_message_write_extended_distinguished_name (msg, set_id, name, prop_type);
	e_soap_message_end_element (msg); /* DeleteItemField */
}

static void
ews_message_add_extended_property_name (ESoapMessage *msg,
					const gchar *name,
					EEwsMessageDataType data_type,
					gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "ExtendedProperty", NULL, NULL);

	e_ews_message_write_extended_name (msg, name, prop_type);
	ews_message_write_data_value (msg, data_type, value);

	e_soap_message_end_element (msg); /* ExtendedProperty */
}

static void
ews_message_add_extended_property_distinguished_name (ESoapMessage *msg,
						      const gchar *set_id,
						      const gchar *name,
						      EEwsMessageDataType data_type,
						      gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "ExtendedProperty", NULL, NULL);

	e_ews_message_write_extended_distinguished_name (msg, set_id, name, prop_type);
	ews_message_write_data_value (msg, data_type, value);

	e_soap_message_end_element (msg); /* ExtendedProperty */
}

static void
ews_message_add_set_item_field_extended_name (ESoapMessage *msg,
					      const gchar *elem_prefix,
					      const gchar *elem_name,
					      const gchar *name,
					      EEwsMessageDataType data_type,
					      gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "SetItemField", NULL, NULL);
	e_ews_message_write_extended_name (msg, name, prop_type);

	e_soap_message_start_element (msg, elem_name, elem_prefix, NULL);
	ews_message_add_extended_property_name (msg, name, data_type, value);
	e_soap_message_end_element (msg); /* elem_name */

	e_soap_message_end_element (msg); /* SetItemField */
}

static void
ews_message_add_set_item_field_extended_distinguished_name (ESoapMessage *msg,
							    const gchar *elem_prefix,
							    const gchar *elem_name,
							    const gchar *set_id,
							    const gchar *name,
							    EEwsMessageDataType data_type,
							    gconstpointer value)
{
	const gchar *prop_type = e_ews_message_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_message_start_element (msg, "SetItemField", NULL, NULL);

	e_ews_message_write_extended_distinguished_name (msg, set_id, name, prop_type);

	e_soap_message_start_element (msg, elem_name, elem_prefix, NULL);
	ews_message_add_extended_property_distinguished_name (msg, set_id, name, data_type, value);
	e_soap_message_end_element (msg); /* elem_name */

	e_soap_message_end_element (msg); /* SetItemField */
}

void
e_ews_message_add_set_item_field_extended_name_boolean (ESoapMessage *msg,
							const gchar *elem_prefix,
							const gchar *elem_name,
							const gchar *name,
							gboolean value)
{
	ews_message_add_set_item_field_extended_name (msg, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_set_item_field_extended_name_int (ESoapMessage *msg,
						    const gchar *elem_prefix,
						    const gchar *elem_name,
						    const gchar *name,
						    gint value)
{
	ews_message_add_set_item_field_extended_name (msg, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_set_item_field_extended_name_double (ESoapMessage *msg,
						       const gchar *elem_prefix,
						       const gchar *elem_name,
						       const gchar *name,
						       gdouble value)
{
	ews_message_add_set_item_field_extended_name (msg, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_set_item_field_extended_name_string (ESoapMessage *msg,
						       const gchar *elem_prefix,
						       const gchar *elem_name,
						       const gchar *name,
						       const gchar *value)
{
	ews_message_add_set_item_field_extended_name (msg, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_set_item_field_extended_name_time (ESoapMessage *msg,
						     const gchar *elem_prefix,
						     const gchar *elem_name,
						     const gchar *name,
						     time_t value)
{
	ews_message_add_set_item_field_extended_name (msg, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_name_boolean (ESoapMessage *msg,
								      const gchar *elem_prefix,
								      const gchar *elem_name,
								      const gchar *set_id,
								      const gchar *name,
								      gboolean value)
{
	ews_message_add_set_item_field_extended_distinguished_name (msg, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_name_int (ESoapMessage *msg,
								  const gchar *elem_prefix,
								  const gchar *elem_name,
								  const gchar *set_id,
								  const gchar *name,
								  gint value)
{
	ews_message_add_set_item_field_extended_distinguished_name (msg, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_name_double (ESoapMessage *msg,
								     const gchar *elem_prefix,
								     const gchar *elem_name,
								     const gchar *set_id,
								     const gchar *name,
								     gdouble value)
{
	ews_message_add_set_item_field_extended_distinguished_name (msg, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_name_string (ESoapMessage *msg,
								     const gchar *elem_prefix,
								     const gchar *elem_name,
								     const gchar *set_id,
								     const gchar *name,
								     const gchar *value)
{
	ews_message_add_set_item_field_extended_distinguished_name (msg, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_set_item_field_extended_distinguished_name_time (ESoapMessage *msg,
								   const gchar *elem_prefix,
								   const gchar *elem_name,
								   const gchar *set_id,
								   const gchar *name,
								   time_t value)
{
	ews_message_add_set_item_field_extended_distinguished_name (msg, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_message_add_extended_property_name_boolean (ESoapMessage *msg,
						  const gchar *name,
						  gboolean value)
{
	ews_message_add_extended_property_name (msg, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_extended_property_name_int (ESoapMessage *msg,
					      const gchar *name,
					      gint value)
{
	ews_message_add_extended_property_name (msg, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_extended_property_name_double (ESoapMessage *msg,
						 const gchar *name,
						 gdouble value)
{
	ews_message_add_extended_property_name (msg, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_extended_property_name_string (ESoapMessage *msg,
						 const gchar *name,
						 const gchar *value)
{
	ews_message_add_extended_property_name (msg, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_extended_property_name_time (ESoapMessage *msg,
					       const gchar *name,
					       time_t value)
{
	ews_message_add_extended_property_name (msg, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_message_add_extended_property_distinguished_name_boolean (ESoapMessage *msg,
							        const gchar *set_id,
							        const gchar *name,
							        gboolean value)
{
	ews_message_add_extended_property_distinguished_name (msg, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_message_add_extended_property_distinguished_name_int (ESoapMessage *msg,
							    const gchar *set_id,
							    const gchar *name,
							    gint value)
{
	ews_message_add_extended_property_distinguished_name (msg, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_message_add_extended_property_distinguished_name_double (ESoapMessage *msg,
							       const gchar *set_id,
							       const gchar *name,
							       gdouble value)
{
	ews_message_add_extended_property_distinguished_name (msg, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_message_add_extended_property_distinguished_name_string (ESoapMessage *msg,
							       const gchar *set_id,
							       const gchar *name,
							       const gchar *value)
{
	ews_message_add_extended_property_distinguished_name (msg, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_message_add_extended_property_distinguished_name_time (ESoapMessage *msg,
							     const gchar *set_id,
							     const gchar *name,
							     time_t value)
{
	ews_message_add_extended_property_distinguished_name (msg, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}
