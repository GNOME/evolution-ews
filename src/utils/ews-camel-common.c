/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* ews-camel-common.c
 *
 * Copyright (C) 1999-2011 Intel, Inc. (www.intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "ews-camel-common.h"

#include "server/e-ews-message.h"

struct _create_mime_msg_data {
	CamelMimeMessage *message;
	gint32 message_camel_flags;
	CamelAddress *from;
};

/* MAPI flags gleaned from windows header files */
#define MAPI_MSGFLAG_READ	0x01
#define MAPI_MSGFLAG_UNSENT	0x08

static void
create_mime_message_cb (ESoapMessage *msg,
                        gpointer user_data)
{
	struct _create_mime_msg_data *create_data = user_data;
	CamelStream *mem, *filtered;
	CamelMimeFilter *filter;
	CamelContentType *content_type;
	GByteArray *bytes;
	gchar *base64;
	gint msgflag;

	e_soap_message_start_element (msg, "Message", NULL, NULL);
	e_soap_message_start_element (msg, "MimeContent", NULL, NULL);

	/* This is horrid. We really need to extend ESoapMessage to allow us
	 * to stream this directly rather than storing it in RAM. Which right
	 * now we are doing about four times: the GByteArray in the mem stream,
	 * then the base64 version, then the xmlDoc, then the soup request. */
	camel_mime_message_set_best_encoding (
		create_data->message,
		CAMEL_BESTENC_GET_ENCODING,
		CAMEL_BESTENC_8BIT);

	mem = camel_stream_mem_new ();
	filtered = camel_stream_filter_new (mem);

	filter = camel_mime_filter_crlf_new (
		CAMEL_MIME_FILTER_CRLF_ENCODE,
		CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	camel_stream_filter_add (CAMEL_STREAM_FILTER (filtered), filter);
	g_object_unref (filter);

	camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (create_data->message),
		filtered, NULL, NULL);
	camel_stream_flush (filtered, NULL, NULL);
	camel_stream_flush (mem, NULL, NULL);
	bytes = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (mem));

	base64 = g_base64_encode (bytes->data, bytes->len);
	g_object_unref (mem);
	g_object_unref (filtered);

	e_soap_message_write_string (msg, base64);
	g_free (base64);

	e_soap_message_end_element (msg); /* MimeContent */

	content_type = camel_mime_part_get_content_type (CAMEL_MIME_PART (create_data->message));
	if (content_type && camel_content_type_is (content_type, "multipart", "report") &&
	    camel_content_type_param (content_type, "report-type") &&
	    g_ascii_strcasecmp (camel_content_type_param (content_type, "report-type"), "disposition-notification") == 0) {
		/* it's a disposition notification reply, set ItemClass too */
		e_soap_message_start_element (msg, "ItemClass", NULL, NULL);
		e_soap_message_write_string (msg, "REPORT.IPM.NOTE.IPNRN");
		e_soap_message_end_element (msg); /* ItemClass */
	}

	e_ews_message_write_string_parameter_with_attribute (
			msg,
			"Importance",
			NULL,
			(create_data->message_camel_flags & CAMEL_MESSAGE_FLAGGED) != 0 ? "High" : "Normal",
			NULL,
			NULL);

	/* more MAPI crap.  You can't just set the IsDraft property
	 * here you have to use the MAPI MSGFLAG_UNSENT extended
	 * property Further crap is that Exchange 2007 assumes when it
	 * sees this property that you're setting the value to 0
	 * ... it never checks */
	msgflag  = MAPI_MSGFLAG_READ; /* draft or sent is always read */
	if ((create_data->message_camel_flags & CAMEL_MESSAGE_DRAFT) != 0)
		msgflag |= MAPI_MSGFLAG_UNSENT;

	e_soap_message_start_element (msg, "ExtendedProperty", NULL, NULL);
	e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyTag", "0x0E07", NULL, NULL);
	e_soap_message_add_attribute (msg, "PropertyType", "Integer", NULL, NULL);
	e_soap_message_end_element (msg); /* ExtendedFieldURI */

	e_ews_message_write_int_parameter (msg, "Value", NULL, msgflag);

	e_soap_message_end_element (msg); /* ExtendedProperty */

	if ((create_data->message_camel_flags & (CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_ANSWERED)) != 0) {
		gint icon;

		icon = (create_data->message_camel_flags & CAMEL_MESSAGE_ANSWERED) != 0 ? 0x105 : 0x106;

		e_soap_message_start_element (msg, "ExtendedProperty", NULL, NULL);

		e_soap_message_start_element (msg, "ExtendedFieldURI", NULL, NULL);
		e_soap_message_add_attribute (msg, "PropertyTag", "0x1080", NULL, NULL);
		e_soap_message_add_attribute (msg, "PropertyType", "Integer", NULL, NULL);
		e_soap_message_end_element (msg); /* ExtendedFieldURI */

		e_ews_message_write_int_parameter (msg, "Value", NULL, icon);

		e_soap_message_end_element (msg); /* ExtendedProperty */
	}

	e_ews_message_write_string_parameter_with_attribute (
			msg,
			"IsRead",
			NULL,
			(create_data->message_camel_flags & CAMEL_MESSAGE_SEEN) != 0 ? "true" : "false",
			NULL,
			NULL);

	e_soap_message_end_element (msg); /* Message */

	g_free (create_data);
}

gboolean
camel_ews_utils_create_mime_message (EEwsConnection *cnc,
                                     const gchar *disposition,
                                     const EwsFolderId *fid,
                                     CamelMimeMessage *message,
                                     gint32 message_camel_flags,
                                     CamelAddress *from,
                                     gchar **itemid,
                                     gchar **changekey,
                                     GCancellable *cancellable,
                                     GError **error)
{
	struct _create_mime_msg_data *create_data;
	GSList *ids;
	EEwsItem *item;
	const EwsId *ewsid;
	gchar *restore_from = NULL;
	gboolean res;

	create_data = g_new0 (struct _create_mime_msg_data, 1);

	create_data->message = message;
	create_data->message_camel_flags = message_camel_flags;
	create_data->from = from;

	if (g_strcmp0 (disposition, "SendOnly") == 0 ||
	    g_strcmp0 (disposition, "SendAndSaveCopy") == 0) {
		struct _camel_header_raw *header;

		for (header = CAMEL_MIME_PART (message)->headers; header; header = header->next) {
			if (header->name && g_ascii_strcasecmp (header->name, "From") == 0) {
				restore_from = header->value;
				header->value = g_strdup ("");
				break;
			}
		}
	}

	res = e_ews_connection_create_items_sync (
		cnc, EWS_PRIORITY_MEDIUM,
		disposition, NULL, fid,
		create_mime_message_cb, create_data,
		&ids, cancellable, error);

	if (restore_from) {
		struct _camel_header_raw *header;

		for (header = CAMEL_MIME_PART (message)->headers; header; header = header->next) {
			if (header->name && g_ascii_strcasecmp (header->name, "From") == 0) {
				g_free (header->value);
				header->value = restore_from;
				break;
			}
		}
	}

	if (!res || (!itemid && !changekey))
		return res;

	item = (EEwsItem *) ids->data;
	if (!item || !(ewsid = e_ews_item_get_id (item))) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("CreateItem call failed to return ID for new message"));
		return FALSE;
	}

	if (itemid)
		*itemid = g_strdup (ewsid->id);
	if (changekey)
		*changekey = g_strdup (ewsid->change_key);

	g_object_unref (item);
	g_slist_free (ids);
	return TRUE;
}
