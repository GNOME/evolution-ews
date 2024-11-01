/*
 * SPDX-FileCopyrightText: (C) 2011 Intel Corporation
 * SPDX-FileContributor: David Woodhouse <dwmw2@infradead.org>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include "e-soap-request.h"
#include "e-soap-response.h"
#include "e-ews-request.h"
#include "e-ews-item-change.h"

void
e_ews_request_start_folder_change (ESoapRequest *request,
				   const gchar *email,
				   const EwsFolderId *folder_id)
{
	g_return_if_fail (request != NULL);
	g_return_if_fail (folder_id != NULL);

	e_soap_request_start_element (request, "FolderChange", NULL, NULL);
	e_ews_folder_id_append_to_request (request, email, folder_id);
	e_soap_request_start_element (request, "Updates", NULL, NULL);
}

void
e_ews_request_start_item_change (ESoapRequest *request,
                                 EEwsItemChangeType type,
                                 const gchar *itemid,
                                 const gchar *changekey,
                                 gint instance_index)
{
	gchar *instance;

	switch (type) {
	case E_EWS_ITEMCHANGE_TYPE_FOLDER:
		e_soap_request_start_element (request, "FolderChange", NULL, NULL);
		e_soap_request_start_element (request, "FolderId", NULL, NULL);
		e_soap_request_add_attribute (request, "Id", itemid, NULL, NULL);
		break;

	case E_EWS_ITEMCHANGE_TYPE_ITEM:
		e_soap_request_start_element (request, "ItemChange", NULL, NULL);
		e_soap_request_start_element (request, "ItemId", NULL, NULL);
		e_soap_request_add_attribute (request, "Id", itemid, NULL, NULL);
		break;

	case E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM:
		e_soap_request_start_element (request, "ItemChange", NULL, NULL);
		e_soap_request_start_element (request, "OccurrenceItemId", NULL, NULL);
		e_soap_request_add_attribute (request, "RecurringMasterId", itemid, NULL, NULL);
		instance = g_strdup_printf ("%d", instance_index);
		e_soap_request_add_attribute (request, "InstanceIndex", instance, NULL, NULL);
		g_free (instance);
		break;

	case E_EWS_ITEMCHANGE_TYPE_RECURRINGMASTER:
		e_soap_request_start_element (request, "ItemChange", NULL, NULL);
		e_soap_request_start_element (request, "RecurringMasterItemId", NULL, NULL);
		e_soap_request_add_attribute (request, "OccurrenceId", itemid, NULL, NULL);
		break;
	}
	if (changekey)
		e_soap_request_add_attribute (request, "ChangeKey", changekey, NULL, NULL);
	e_soap_request_end_element (request);

	e_soap_request_start_element (request, "Updates", NULL, NULL);
}

void
e_ews_request_end_item_change (ESoapRequest *request)
{
	e_soap_request_end_element (request); /* Updates */
	e_soap_request_end_element (request); /* ItemChange */
}

void
e_ews_request_start_set_item_field (ESoapRequest *request,
                                    const gchar *name,
                                    const gchar *fielduri_prefix,
                                    const gchar *field_kind)
{
	gchar * fielduri = NULL;
	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	e_soap_request_start_element (request, "SetItemField", NULL, NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", fielduri);
	e_soap_request_start_element (request, field_kind, NULL, NULL);

	g_free (fielduri);
}

void
e_ews_request_start_set_indexed_item_field (ESoapRequest *request,
                                            const gchar *name,
                                            const gchar *fielduri_prefix,
                                            const gchar *field_kind,
                                            const gchar *field_index,
                                            gboolean delete_field)
{
	gchar * fielduri = NULL;
	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	if (delete_field)
		e_soap_request_start_element (request, "DeleteItemField", NULL, NULL);
	else
		e_soap_request_start_element (request, "SetItemField", NULL, NULL);

	e_soap_request_start_element (request, "IndexedFieldURI", NULL, NULL);
	e_soap_request_add_attribute (request, "FieldURI", fielduri, NULL, NULL);
	e_soap_request_add_attribute (request, "FieldIndex", field_index, NULL, NULL);
	e_soap_request_end_element (request);

	if (!delete_field)
		e_soap_request_start_element (request, field_kind, NULL, NULL);

	g_free (fielduri);
}

void
e_ews_request_end_set_indexed_item_field (ESoapRequest *request,
                                          gboolean delete_field)
{
	if (!delete_field)
		e_soap_request_end_element (request); /* CalendarItem */
	e_soap_request_end_element (request); /* SetItemField */
}

void
e_ews_request_end_set_item_field (ESoapRequest *request)
{
	e_soap_request_end_element (request); /* CalendarItem */
	e_soap_request_end_element (request); /* SetItemField */
}

void
e_ews_request_add_delete_item_field (ESoapRequest *request,
                                     const gchar *name,
                                     const gchar *fielduri_prefix)
{
	gchar *fielduri;

	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	e_soap_request_start_element (request, "DeleteItemField", NULL, NULL);
	e_ews_request_write_string_parameter_with_attribute (request, "FieldURI", NULL, NULL, "FieldURI", fielduri);
	e_soap_request_end_element (request); /* DeleteItemField */

	g_free (fielduri);
}

void
e_ews_request_add_delete_item_field_indexed (ESoapRequest *request,
                                             const gchar *name,
                                             const gchar *fielduri_prefix,
                                             const gchar *field_index)
{
	gchar *fielduri;

	fielduri = g_strconcat (fielduri_prefix, ":", name, NULL);

	e_soap_request_start_element (request, "DeleteItemField", NULL, NULL);
	e_soap_request_start_element (request, "IndexedFieldURI", NULL, NULL);
	e_soap_request_add_attribute (request, "FieldURI", fielduri, NULL, NULL);
	e_soap_request_add_attribute (request, "FieldIndex", field_index, NULL, NULL);
	e_soap_request_end_element (request); /* IndexedFieldURI */
	e_soap_request_end_element (request); /* DeleteItemField */

	g_free (fielduri);
}

const gchar *
e_ews_request_data_type_get_xml_name (EEwsMessageDataType data_type)
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
e_ews_request_add_delete_item_field_extended_tag (ESoapRequest *request,
						  guint32 prop_id,
						  EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "DeleteItemField", NULL, NULL);
	e_ews_request_write_extended_tag (request, prop_id, prop_type);
	e_soap_request_end_element (request); /* DeleteItemField */
}

void
e_ews_request_add_delete_item_field_extended_distinguished_tag (ESoapRequest *request,
								const gchar *set_id,
								guint32 prop_id,
								EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "DeleteItemField", NULL, NULL);
	e_ews_request_write_extended_distinguished_tag (request, set_id, prop_id, prop_type);
	e_soap_request_end_element (request); /* DeleteItemField */
}

static void
ews_request_write_data_value (ESoapRequest *request,
			      EEwsMessageDataType data_type,
			      gconstpointer value)
{
	g_return_if_fail (value != NULL);

	switch (data_type) {
	case E_EWS_MESSAGE_DATA_TYPE_BOOLEAN:
		e_ews_request_write_string_parameter (request, "Value", NULL, (*((const gboolean *) value)) ? "true" : "false");
		return;
	case E_EWS_MESSAGE_DATA_TYPE_INT:
		e_ews_request_write_int_parameter (request, "Value", NULL, *((const gint *) value));
		return;
	case E_EWS_MESSAGE_DATA_TYPE_DOUBLE:
		e_ews_request_write_double_parameter (request, "Value", NULL, *((const gdouble *) value));
		return;
	case E_EWS_MESSAGE_DATA_TYPE_STRING:
		e_ews_request_write_string_parameter (request, "Value", NULL, (const gchar *) value);
		return;
	case E_EWS_MESSAGE_DATA_TYPE_TIME:
		e_ews_request_write_time_parameter (request, "Value", NULL, *((const time_t *) value));
		return;
	}

	g_warn_if_reached ();
}

static void
ews_request_add_extended_property_tag (ESoapRequest *request,
				       guint32 prop_id,
				       EEwsMessageDataType data_type,
				       gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "ExtendedProperty", NULL, NULL);

	e_ews_request_write_extended_tag (request, prop_id, prop_type);
	ews_request_write_data_value (request, data_type, value);

	e_soap_request_end_element (request); /* ExtendedProperty */
}

static void
ews_request_add_extended_property_distinguished_tag (ESoapRequest *request,
						     const gchar *set_id,
						     guint32 prop_id,
						     EEwsMessageDataType data_type,
						     gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "ExtendedProperty", NULL, NULL);

	e_ews_request_write_extended_distinguished_tag (request, set_id, prop_id, prop_type);
	ews_request_write_data_value (request, data_type, value);

	e_soap_request_end_element (request); /* ExtendedProperty */
}

static void
ews_request_add_set_item_field_extended_tag (ESoapRequest *request,
					     const gchar *elem_prefix,
					     const gchar *elem_name,
					     guint32 prop_id,
					     EEwsMessageDataType data_type,
					     gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "SetItemField", NULL, NULL);
	e_ews_request_write_extended_tag (request, prop_id, prop_type);

	e_soap_request_start_element (request, elem_name, elem_prefix, NULL);
	ews_request_add_extended_property_tag (request, prop_id, data_type, value);
	e_soap_request_end_element (request); /* elem_name */

	e_soap_request_end_element (request); /* SetItemField */
}

static void
ews_request_add_set_item_field_extended_distinguished_tag (ESoapRequest *request,
							   const gchar *elem_prefix,
							   const gchar *elem_name,
							   const gchar *set_id,
							   guint32 prop_id,
							   EEwsMessageDataType data_type,
							   gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "SetItemField", NULL, NULL);

	e_ews_request_write_extended_distinguished_tag (request, set_id, prop_id, prop_type);

	e_soap_request_start_element (request, elem_name, elem_prefix, NULL);
	ews_request_add_extended_property_distinguished_tag (request, set_id, prop_id, data_type, value);
	e_soap_request_end_element (request); /* elem_name */

	e_soap_request_end_element (request); /* SetItemField */
}

void
e_ews_request_add_set_item_field_extended_tag_boolean (ESoapRequest *request,
						       const gchar *elem_prefix,
						       const gchar *elem_name,
						       guint32 prop_id,
						       gboolean value)
{
	ews_request_add_set_item_field_extended_tag (request, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_set_item_field_extended_tag_int (ESoapRequest *request,
						   const gchar *elem_prefix,
						   const gchar *elem_name,
						   guint32 prop_id,
						   gint value)
{
	ews_request_add_set_item_field_extended_tag (request, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_set_item_field_extended_tag_double (ESoapRequest *request,
						      const gchar *elem_prefix,
						      const gchar *elem_name,
						      guint32 prop_id,
						      gdouble value)
{
	ews_request_add_set_item_field_extended_tag (request, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_set_item_field_extended_tag_string (ESoapRequest *request,
						      const gchar *elem_prefix,
						      const gchar *elem_name,
						      guint32 prop_id,
						      const gchar *value)
{
	ews_request_add_set_item_field_extended_tag (request, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_set_item_field_extended_tag_time (ESoapRequest *request,
						    const gchar *elem_prefix,
						    const gchar *elem_name,
						    guint32 prop_id,
						    time_t value)
{
	ews_request_add_set_item_field_extended_tag (request, elem_prefix, elem_name, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_tag_boolean (ESoapRequest *request,
								     const gchar *elem_prefix,
								     const gchar *elem_name,
								     const gchar *set_id,
								     guint32 prop_id,
								     gboolean value)
{
	ews_request_add_set_item_field_extended_distinguished_tag (request, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_tag_int (ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 guint32 prop_id,
								 gint value)
{
	ews_request_add_set_item_field_extended_distinguished_tag (request, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_tag_double (ESoapRequest *request,
								    const gchar *elem_prefix,
								    const gchar *elem_name,
								    const gchar *set_id,
								    guint32 prop_id,
								    gdouble value)
{
	ews_request_add_set_item_field_extended_distinguished_tag (request, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_tag_string (ESoapRequest *request,
								    const gchar *elem_prefix,
								    const gchar *elem_name,
								    const gchar *set_id,
								    guint32 prop_id,
								    const gchar *value)
{
	ews_request_add_set_item_field_extended_distinguished_tag (request, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_tag_time (ESoapRequest *request,
								  const gchar *elem_prefix,
								  const gchar *elem_name,
								  const gchar *set_id,
								  guint32 prop_id,
								  time_t value)
{
	ews_request_add_set_item_field_extended_distinguished_tag (request, elem_prefix, elem_name, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_request_add_extended_property_tag_boolean (ESoapRequest *request,
					         guint32 prop_id,
					         gboolean value)
{
	ews_request_add_extended_property_tag (request, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_extended_property_tag_int (ESoapRequest *request,
					     guint32 prop_id,
					     gint value)
{
	ews_request_add_extended_property_tag (request, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_extended_property_tag_double (ESoapRequest *request,
						guint32 prop_id,
						gdouble value)
{
	ews_request_add_extended_property_tag (request, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_extended_property_tag_string (ESoapRequest *request,
						guint32 prop_id,
						const gchar *value)
{
	ews_request_add_extended_property_tag (request, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_extended_property_tag_time (ESoapRequest *request,
					      guint32 prop_id,
					      time_t value)
{
	ews_request_add_extended_property_tag (request, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_request_add_extended_property_distinguished_tag_boolean (ESoapRequest *request,
							       const gchar *set_id,
							       guint32 prop_id,
							       gboolean value)
{
	ews_request_add_extended_property_distinguished_tag (request, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_extended_property_distinguished_tag_int (ESoapRequest *request,
							   const gchar *set_id,
							   guint32 prop_id,
							   gint value)
{
	ews_request_add_extended_property_distinguished_tag (request, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_extended_property_distinguished_tag_double (ESoapRequest *request,
							      const gchar *set_id,
							      guint32 prop_id,
							      gdouble value)
{
	ews_request_add_extended_property_distinguished_tag (request, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_extended_property_distinguished_tag_string (ESoapRequest *request,
							      const gchar *set_id,
							      guint32 prop_id,
							      const gchar *value)
{
	ews_request_add_extended_property_distinguished_tag (request, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_extended_property_distinguished_tag_time (ESoapRequest *request,
							    const gchar *set_id,
							    guint32 prop_id,
							    time_t value)
{
	ews_request_add_extended_property_distinguished_tag (request, set_id, prop_id,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_request_add_delete_item_field_extended_name (ESoapRequest *request,
						   const gchar *name,
						   EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "DeleteItemField", NULL, NULL);
	e_ews_request_write_extended_name (request, name, prop_type);
	e_soap_request_end_element (request); /* DeleteItemField */
}

void
e_ews_request_add_delete_item_field_extended_distinguished_name (ESoapRequest *request,
								 const gchar *set_id,
								 const gchar *name,
								 EEwsMessageDataType data_type)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "DeleteItemField", NULL, NULL);
	e_ews_request_write_extended_distinguished_name (request, set_id, name, prop_type);
	e_soap_request_end_element (request); /* DeleteItemField */
}

static void
ews_request_add_extended_property_name (ESoapRequest *request,
					const gchar *name,
					EEwsMessageDataType data_type,
					gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "ExtendedProperty", NULL, NULL);

	e_ews_request_write_extended_name (request, name, prop_type);
	ews_request_write_data_value (request, data_type, value);

	e_soap_request_end_element (request); /* ExtendedProperty */
}

static void
ews_request_add_extended_property_distinguished_name (ESoapRequest *request,
						      const gchar *set_id,
						      const gchar *name,
						      EEwsMessageDataType data_type,
						      gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "ExtendedProperty", NULL, NULL);

	e_ews_request_write_extended_distinguished_name (request, set_id, name, prop_type);
	ews_request_write_data_value (request, data_type, value);

	e_soap_request_end_element (request); /* ExtendedProperty */
}

static void
ews_request_add_set_item_field_extended_name (ESoapRequest *request,
					      const gchar *elem_prefix,
					      const gchar *elem_name,
					      const gchar *name,
					      EEwsMessageDataType data_type,
					      gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "SetItemField", NULL, NULL);
	e_ews_request_write_extended_name (request, name, prop_type);

	e_soap_request_start_element (request, elem_name, elem_prefix, NULL);
	ews_request_add_extended_property_name (request, name, data_type, value);
	e_soap_request_end_element (request); /* elem_name */

	e_soap_request_end_element (request); /* SetItemField */
}

static void
ews_request_add_set_item_field_extended_distinguished_name (ESoapRequest *request,
							    const gchar *elem_prefix,
							    const gchar *elem_name,
							    const gchar *set_id,
							    const gchar *name,
							    EEwsMessageDataType data_type,
							    gconstpointer value)
{
	const gchar *prop_type = e_ews_request_data_type_get_xml_name (data_type);

	g_return_if_fail (prop_type != NULL);

	e_soap_request_start_element (request, "SetItemField", NULL, NULL);

	e_ews_request_write_extended_distinguished_name (request, set_id, name, prop_type);

	e_soap_request_start_element (request, elem_name, elem_prefix, NULL);
	ews_request_add_extended_property_distinguished_name (request, set_id, name, data_type, value);
	e_soap_request_end_element (request); /* elem_name */

	e_soap_request_end_element (request); /* SetItemField */
}

void
e_ews_request_add_set_item_field_extended_name_boolean (ESoapRequest *request,
							const gchar *elem_prefix,
							const gchar *elem_name,
							const gchar *name,
							gboolean value)
{
	ews_request_add_set_item_field_extended_name (request, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_set_item_field_extended_name_int (ESoapRequest *request,
						    const gchar *elem_prefix,
						    const gchar *elem_name,
						    const gchar *name,
						    gint value)
{
	ews_request_add_set_item_field_extended_name (request, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_set_item_field_extended_name_double (ESoapRequest *request,
						       const gchar *elem_prefix,
						       const gchar *elem_name,
						       const gchar *name,
						       gdouble value)
{
	ews_request_add_set_item_field_extended_name (request, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_set_item_field_extended_name_string (ESoapRequest *request,
						       const gchar *elem_prefix,
						       const gchar *elem_name,
						       const gchar *name,
						       const gchar *value)
{
	ews_request_add_set_item_field_extended_name (request, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_set_item_field_extended_name_time (ESoapRequest *request,
						     const gchar *elem_prefix,
						     const gchar *elem_name,
						     const gchar *name,
						     time_t value)
{
	ews_request_add_set_item_field_extended_name (request, elem_prefix, elem_name, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_name_boolean (ESoapRequest *request,
								      const gchar *elem_prefix,
								      const gchar *elem_name,
								      const gchar *set_id,
								      const gchar *name,
								      gboolean value)
{
	ews_request_add_set_item_field_extended_distinguished_name (request, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_name_int (ESoapRequest *request,
								  const gchar *elem_prefix,
								  const gchar *elem_name,
								  const gchar *set_id,
								  const gchar *name,
								  gint value)
{
	ews_request_add_set_item_field_extended_distinguished_name (request, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_name_double (ESoapRequest *request,
								     const gchar *elem_prefix,
								     const gchar *elem_name,
								     const gchar *set_id,
								     const gchar *name,
								     gdouble value)
{
	ews_request_add_set_item_field_extended_distinguished_name (request, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_name_string (ESoapRequest *request,
								     const gchar *elem_prefix,
								     const gchar *elem_name,
								     const gchar *set_id,
								     const gchar *name,
								     const gchar *value)
{
	ews_request_add_set_item_field_extended_distinguished_name (request, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_set_item_field_extended_distinguished_name_time (ESoapRequest *request,
								   const gchar *elem_prefix,
								   const gchar *elem_name,
								   const gchar *set_id,
								   const gchar *name,
								   time_t value)
{
	ews_request_add_set_item_field_extended_distinguished_name (request, elem_prefix, elem_name, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_request_add_extended_property_name_boolean (ESoapRequest *request,
						  const gchar *name,
						  gboolean value)
{
	ews_request_add_extended_property_name (request, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_extended_property_name_int (ESoapRequest *request,
					      const gchar *name,
					      gint value)
{
	ews_request_add_extended_property_name (request, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_extended_property_name_double (ESoapRequest *request,
						 const gchar *name,
						 gdouble value)
{
	ews_request_add_extended_property_name (request, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_extended_property_name_string (ESoapRequest *request,
						 const gchar *name,
						 const gchar *value)
{
	ews_request_add_extended_property_name (request, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_extended_property_name_time (ESoapRequest *request,
					       const gchar *name,
					       time_t value)
{
	ews_request_add_extended_property_name (request, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}

void
e_ews_request_add_extended_property_distinguished_name_boolean (ESoapRequest *request,
							        const gchar *set_id,
							        const gchar *name,
							        gboolean value)
{
	ews_request_add_extended_property_distinguished_name (request, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_BOOLEAN, &value);
}

void
e_ews_request_add_extended_property_distinguished_name_int (ESoapRequest *request,
							    const gchar *set_id,
							    const gchar *name,
							    gint value)
{
	ews_request_add_extended_property_distinguished_name (request, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_INT, &value);
}

void
e_ews_request_add_extended_property_distinguished_name_double (ESoapRequest *request,
							       const gchar *set_id,
							       const gchar *name,
							       gdouble value)
{
	ews_request_add_extended_property_distinguished_name (request, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_DOUBLE, &value);
}

void
e_ews_request_add_extended_property_distinguished_name_string (ESoapRequest *request,
							       const gchar *set_id,
							       const gchar *name,
							       const gchar *value)
{
	ews_request_add_extended_property_distinguished_name (request, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_STRING, value);
}

void
e_ews_request_add_extended_property_distinguished_name_time (ESoapRequest *request,
							     const gchar *set_id,
							     const gchar *name,
							     time_t value)
{
	ews_request_add_extended_property_distinguished_name (request, set_id, name,
		E_EWS_MESSAGE_DATA_TYPE_TIME, &value);
}
