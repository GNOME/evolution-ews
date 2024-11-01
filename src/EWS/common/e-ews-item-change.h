/*
 * SPDX-FileCopyrightText: (C) 2011 Intel Corporation
 * SPDX-FileContributor: David Woodhouse <dwmw2@infradead.org>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_EWS_ITEM_CHANGE_H
#define E_EWS_ITEM_CHANGE_H

#include "e-ews-folder.h"
#include "e-soap-request.h"
#include "e-soap-response.h"

G_BEGIN_DECLS

typedef enum {
	E_EWS_ITEMCHANGE_TYPE_FOLDER,
	E_EWS_ITEMCHANGE_TYPE_ITEM,
	E_EWS_ITEMCHANGE_TYPE_OCCURRENCEITEM,
	E_EWS_ITEMCHANGE_TYPE_RECURRINGMASTER,
} EEwsItemChangeType;

void	e_ews_request_start_folder_change			(ESoapRequest *request,
								 const gchar *email,
								 const EwsFolderId *folder_id);
void	e_ews_request_start_item_change				(ESoapRequest *request,
								 EEwsItemChangeType type,
								 const gchar *itemid,
								 const gchar *changekey,
								 gint instance_index);
void	e_ews_request_end_item_change				(ESoapRequest *request);
void	e_ews_request_start_set_item_field			(ESoapRequest *request,
								 const gchar *name,
								 const gchar *fielduri_prefix,
								 const gchar *field_kind);
void	e_ews_request_start_set_indexed_item_field		(ESoapRequest *request,
								 const gchar *name,
								 const gchar *fielduri_prefix,
								 const gchar *field_kind,
								 const gchar *field_index,
								 gboolean delete_field);
void	e_ews_request_end_set_indexed_item_field		(ESoapRequest *request,
								 gboolean delete_field);
void	e_ews_request_end_set_item_field			(ESoapRequest *request);
void	e_ews_request_add_delete_item_field			(ESoapRequest *request,
								 const gchar *name,
								 const gchar *fielduri_prefix);
void	e_ews_request_add_delete_item_field_indexed		(ESoapRequest *request,
								 const gchar *name,
								 const gchar *fielduri_prefix,
								 const gchar *field_index);

typedef enum {
	E_EWS_MESSAGE_DATA_TYPE_BOOLEAN,
	E_EWS_MESSAGE_DATA_TYPE_INT,
	E_EWS_MESSAGE_DATA_TYPE_DOUBLE,
	E_EWS_MESSAGE_DATA_TYPE_STRING,
	E_EWS_MESSAGE_DATA_TYPE_TIME
} EEwsMessageDataType;

const gchar *
	e_ews_request_data_type_get_xml_name			(EEwsMessageDataType data_type);

void	e_ews_request_add_delete_item_field_extended_tag	(ESoapRequest *request,
								 guint32 prop_id,
								 EEwsMessageDataType data_type);

void	e_ews_request_add_delete_item_field_extended_distinguished_tag
								(ESoapRequest *request,
								 const gchar *set_id,
								 guint32 prop_id,
								 EEwsMessageDataType data_type);

void	e_ews_request_add_set_item_field_extended_tag_boolean	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 guint32 prop_id,
								 gboolean value);
void	e_ews_request_add_set_item_field_extended_tag_int	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 guint32 prop_id,
								 gint value);
void	e_ews_request_add_set_item_field_extended_tag_double	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 guint32 prop_id,
								 gdouble value);
void	e_ews_request_add_set_item_field_extended_tag_string	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 guint32 prop_id,
								 const gchar *value);
void	e_ews_request_add_set_item_field_extended_tag_time	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 guint32 prop_id,
								 time_t value);
void	e_ews_request_add_set_item_field_extended_distinguished_tag_boolean
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 guint32 prop_id,
								 gboolean value);
void	e_ews_request_add_set_item_field_extended_distinguished_tag_int
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 guint32 prop_id,
								 gint value);
void	e_ews_request_add_set_item_field_extended_distinguished_tag_double
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 guint32 prop_id,
								 gdouble value);
void	e_ews_request_add_set_item_field_extended_distinguished_tag_string
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 guint32 prop_id,
								 const gchar *value);
void	e_ews_request_add_set_item_field_extended_distinguished_tag_time
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 guint32 prop_id,
								 time_t value);
void	e_ews_request_add_extended_property_tag_boolean		(ESoapRequest *request,
								 guint32 prop_id,
								 gboolean value);
void	e_ews_request_add_extended_property_tag_int		(ESoapRequest *request,
								 guint32 prop_id,
								 gint value);
void	e_ews_request_add_extended_property_tag_double		(ESoapRequest *request,
								 guint32 prop_id,
								 gdouble value);
void	e_ews_request_add_extended_property_tag_string		(ESoapRequest *request,
								 guint32 prop_id,
								 const gchar *value);
void	e_ews_request_add_extended_property_tag_time		(ESoapRequest *request,
								 guint32 prop_id,
								 time_t value);
void	e_ews_request_add_extended_property_distinguished_tag_boolean
								(ESoapRequest *request,
								 const gchar *set_id,
								 guint32 prop_id,
								 gboolean value);
void	e_ews_request_add_extended_property_distinguished_tag_int
								(ESoapRequest *request,
								 const gchar *set_id,
								 guint32 prop_id,
								 gint value);
void	e_ews_request_add_extended_property_distinguished_tag_double
								(ESoapRequest *request,
								 const gchar *set_id,
								 guint32 prop_id,
								 gdouble value);
void	e_ews_request_add_extended_property_distinguished_tag_string
								(ESoapRequest *request,
								 const gchar *set_id,
								 guint32 prop_id,
								 const gchar *value);
void	e_ews_request_add_extended_property_distinguished_tag_time
								(ESoapRequest *request,
								 const gchar *set_id,
								 guint32 prop_id,
								 time_t value);

void	e_ews_request_add_delete_item_field_extended_name	(ESoapRequest *request,
								 const gchar *name,
								 EEwsMessageDataType data_type);

void	e_ews_request_add_delete_item_field_extended_distinguished_name
								(ESoapRequest *request,
								 const gchar *set_id,
								 const gchar *name,
								 EEwsMessageDataType data_type);

void	e_ews_request_add_set_item_field_extended_name_boolean	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *name,
								 gboolean value);
void	e_ews_request_add_set_item_field_extended_name_int	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *name,
								 gint value);
void	e_ews_request_add_set_item_field_extended_name_double	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *name,
								 gdouble value);
void	e_ews_request_add_set_item_field_extended_name_string	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *name,
								 const gchar *value);
void	e_ews_request_add_set_item_field_extended_name_time	(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *name,
								 time_t value);
void	e_ews_request_add_set_item_field_extended_distinguished_name_boolean
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 const gchar *name,
								 gboolean value);
void	e_ews_request_add_set_item_field_extended_distinguished_name_int
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 const gchar *name,
								 gint value);
void	e_ews_request_add_set_item_field_extended_distinguished_name_double
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 const gchar *name,
								 gdouble value);
void	e_ews_request_add_set_item_field_extended_distinguished_name_string
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 const gchar *name,
								 const gchar *value);
void	e_ews_request_add_set_item_field_extended_distinguished_name_time
								(ESoapRequest *request,
								 const gchar *elem_prefix,
								 const gchar *elem_name,
								 const gchar *set_id,
								 const gchar *name,
								 time_t value);
void	e_ews_request_add_extended_property_name_boolean	(ESoapRequest *request,
								 const gchar *name,
								 gboolean value);
void	e_ews_request_add_extended_property_name_int		(ESoapRequest *request,
								 const gchar *name,
								 gint value);
void	e_ews_request_add_extended_property_name_double		(ESoapRequest *request,
								 const gchar *name,
								 gdouble value);
void	e_ews_request_add_extended_property_name_string		(ESoapRequest *request,
								 const gchar *name,
								 const gchar *value);
void	e_ews_request_add_extended_property_name_time		(ESoapRequest *request,
								 const gchar *name,
								 time_t value);
void	e_ews_request_add_extended_property_distinguished_name_boolean
								(ESoapRequest *request,
								 const gchar *set_id,
								 const gchar *name,
								 gboolean value);
void	e_ews_request_add_extended_property_distinguished_name_int
								(ESoapRequest *request,
								 const gchar *set_id,
								 const gchar *name,
								 gint value);
void	e_ews_request_add_extended_property_distinguished_name_double
								(ESoapRequest *request,
								 const gchar *set_id,
								 const gchar *name,
								 gdouble value);
void	e_ews_request_add_extended_property_distinguished_name_string
								(ESoapRequest *request,
								 const gchar *set_id,
								 const gchar *name,
								 const gchar *value);
void	e_ews_request_add_extended_property_distinguished_name_time
								(ESoapRequest *request,
								 const gchar *set_id,
								 const gchar *name,
								 time_t value);

G_END_DECLS

#endif
