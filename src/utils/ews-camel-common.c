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
#include "server/e-ews-item-change.h"

struct _create_mime_msg_data {
	EEwsConnection *cnc;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelAddress *from;
	CamelAddress *recipients;
	gboolean is_send;
};

static void
filter_recipients (CamelMimeMessage *message,
		   CamelAddress *recipients,
		   GHashTable *recip_to,
		   GHashTable *recip_cc,
		   GHashTable *recip_bcc)
{
	CamelInternetAddress *addresses, *mime_cc, *mime_bcc;
	gint ii, len;

	g_return_if_fail (message != NULL);
	g_return_if_fail (recipients != NULL);
	g_return_if_fail (CAMEL_IS_INTERNET_ADDRESS (recipients));
	g_return_if_fail (recip_to != NULL);
	g_return_if_fail (recip_cc != NULL);
	g_return_if_fail (recip_bcc != NULL);

	mime_cc = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	mime_bcc = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_BCC);

	addresses = CAMEL_INTERNET_ADDRESS (recipients);
	len = camel_address_length (recipients);
	for (ii = 0; ii < len; ii++) {
		const gchar *name = NULL, *email = NULL;

		if (!camel_internet_address_get (addresses, ii, &name, &email) ||
		    !email)
			continue;

		if (mime_bcc && camel_internet_address_find_address (mime_bcc, email, NULL) != -1) {
			g_hash_table_insert (recip_bcc, (gpointer) email, GINT_TO_POINTER (1));
		} else if (mime_cc && camel_internet_address_find_address (mime_cc, email, NULL) != -1) {
			g_hash_table_insert (recip_cc, (gpointer) email, GINT_TO_POINTER (1));
		} else {
			g_hash_table_insert (recip_to, (gpointer) email, GINT_TO_POINTER (1));
		}
	}
}

static void
write_recipients (ESoapMessage *msg,
		  const gchar *elem_name,
		  GHashTable *recips)
{
	GHashTableIter iter;
	gpointer key, value;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (elem_name != NULL);
	g_return_if_fail (recips != NULL);

	if (!g_hash_table_size (recips))
		return;

	e_soap_message_start_element (msg, elem_name, NULL, NULL);

	g_hash_table_iter_init (&iter, recips);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		e_soap_message_start_element (msg, "Mailbox", NULL, NULL);
		e_ews_message_write_string_parameter_with_attribute (msg, "EmailAddress", NULL, key, NULL, NULL);
		e_soap_message_end_element (msg); /* Mailbox */
	}

	e_soap_message_end_element (msg); /* elem_name */
}

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
	guint32 message_camel_flags = 0;

	if (create_data->info)
		message_camel_flags = camel_message_info_get_flags (create_data->info);
		
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
			(message_camel_flags & CAMEL_MESSAGE_FLAGGED) != 0 ? "High" : "Normal",
			NULL,
			NULL);

	/* more MAPI crap.  You can't just set the IsDraft property
	 * here you have to use the MAPI MSGFLAG_UNSENT extended
	 * property Further crap is that Exchange 2007 assumes when it
	 * sees this property that you're setting the value to 0
	 * ... it never checks */
	msgflag = MAPI_MSGFLAG_READ; /* draft or sent is always read */
	if ((message_camel_flags & CAMEL_MESSAGE_DRAFT) != 0)
		msgflag |= MAPI_MSGFLAG_UNSENT;

	e_ews_message_add_extended_property_tag_int (msg, 0x0e07, msgflag);

	if ((message_camel_flags & (CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_ANSWERED)) != 0) {
		gint icon;

		icon = (message_camel_flags & CAMEL_MESSAGE_ANSWERED) != 0 ? 0x105 : 0x106;

		e_ews_message_add_extended_property_tag_int (msg, 0x1080, icon);
	}

	if (create_data->info) {
		const gchar *followup, *completed, *dueby;
		time_t completed_tt = (time_t) 0 , dueby_tt = (time_t) 0;

		/* follow-up flags */
		followup = camel_message_info_get_user_tag (create_data->info, "follow-up");
		completed = camel_message_info_get_user_tag (create_data->info, "completed-on");
		dueby = camel_message_info_get_user_tag (create_data->info, "due-by");

		if (followup && !*followup)
			followup = NULL;

		if (completed && *completed)
			completed_tt = camel_header_decode_date (completed, NULL);

		if (dueby && *dueby)
			dueby_tt = camel_header_decode_date (dueby, NULL);

		/* PidTagFlagStatus */
		e_ews_message_add_extended_property_tag_int (msg, 0x1090,
			followup ? (completed_tt != (time_t) 0 ? 0x01 /* followupComplete */: 0x02 /* followupFlagged */) : 0x0);

		if (followup) {
			/* PidLidFlagRequest */
			e_ews_message_add_extended_property_distinguished_tag_string (msg, "Common", 0x8530, followup);

			/* PidTagToDoItemFlags */
			e_ews_message_add_extended_property_tag_int (msg, 0x0e2b, 1);
		}

		if (followup && completed_tt != (time_t) 0) {
			/* minute precision */
			completed_tt = completed_tt - (completed_tt % 60);

			/* PidTagFlagCompleteTime */
			e_ews_message_add_extended_property_tag_time (msg, 0x1091, completed_tt);

			/* PidLidTaskDateCompleted */
			e_ews_message_add_extended_property_distinguished_tag_time (msg, "Task", 0x810f, completed_tt);

			/* PidLidTaskStatus */
			e_ews_message_add_extended_property_distinguished_tag_int (msg, "Task", 0x8101, 2);

			/* PidLidPercentComplete */
			e_ews_message_add_extended_property_distinguished_tag_double (msg, "Task", 0x8102, 1.0);

			/* PidLidTaskComplete */
			e_ews_message_add_extended_property_distinguished_tag_boolean (msg, "Task", 0x811c, TRUE);
		}

		if (followup && dueby_tt != (time_t) 0 && completed_tt == (time_t) 0) {
			/* PidLidTaskStatus */
			e_ews_message_add_extended_property_distinguished_tag_int (msg, "Task", 0x8101, 0);

			/* PidLidPercentComplete */
			e_ews_message_add_extended_property_distinguished_tag_double (msg, "Task", 0x8102, 0.0);

			/* PidLidTaskDueDate */
			e_ews_message_add_extended_property_distinguished_tag_time (msg, "Task", 0x8105, dueby_tt);

			/* PidLidTaskComplete */
			e_ews_message_add_extended_property_distinguished_tag_boolean (msg, "Task", 0x811c, FALSE);
		}
	}

	if (create_data->cnc && create_data->is_send) {
		CamelEwsSettings *settings;

		settings = e_ews_connection_ref_settings (create_data->cnc);
		if (settings) {
			e_soap_message_start_element (msg, "Sender", NULL, NULL);

			e_soap_message_start_element (msg, "Mailbox", NULL, NULL);
			e_ews_message_write_string_parameter_with_attribute (msg, "EmailAddress", NULL, camel_ews_settings_get_email (settings), NULL, NULL);
			e_soap_message_end_element (msg); /* Mailbox */

			e_soap_message_end_element (msg); /* Sender */
		}
		g_clear_object (&settings);
	}

	if (create_data->recipients) {
		GHashTable *recip_to, *recip_cc, *recip_bcc;

		recip_to = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);
		recip_cc = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);
		recip_bcc = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);
	
		filter_recipients (create_data->message, create_data->recipients, recip_to, recip_cc, recip_bcc);

		write_recipients (msg, "ToRecipients", recip_to);
		write_recipients (msg, "CcRecipients", recip_cc);
		write_recipients (msg, "BccRecipients", recip_bcc);

		g_hash_table_destroy (recip_to);
		g_hash_table_destroy (recip_cc);
		g_hash_table_destroy (recip_bcc);
	}

	if (create_data->is_send && create_data->from && CAMEL_IS_INTERNET_ADDRESS (create_data->from)) {
		const gchar *from_name = NULL, *from_email = NULL;

		if (camel_internet_address_get (CAMEL_INTERNET_ADDRESS (create_data->from), 0, &from_name, &from_email) && from_email) {
			e_soap_message_start_element (msg, "From", NULL, NULL);

			e_soap_message_start_element (msg, "Mailbox", NULL, NULL);
			if (from_name && *from_name)
				e_ews_message_write_string_parameter_with_attribute (msg, "Name", NULL, from_name, NULL, NULL);
			e_ews_message_write_string_parameter_with_attribute (msg, "EmailAddress", NULL, from_email, NULL, NULL);
			e_soap_message_end_element (msg); /* Mailbox */

			e_soap_message_end_element (msg); /* From */
		}
	}

	e_ews_message_write_string_parameter_with_attribute (
			msg,
			"IsRead",
			NULL,
			(message_camel_flags & CAMEL_MESSAGE_SEEN) != 0 ? "true" : "false",
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
                                     CamelMessageInfo *info,
                                     CamelAddress *from,
				     CamelAddress *recipients,
                                     gchar **itemid,
                                     gchar **changekey,
                                     GCancellable *cancellable,
                                     GError **error)
{
	struct _create_mime_msg_data *create_data;
	GSList *ids;
	EEwsItem *item;
	const EwsId *ewsid;
	gboolean res;

	create_data = g_new0 (struct _create_mime_msg_data, 1);

	create_data->cnc = cnc;
	create_data->message = message;
	create_data->info = info;
	create_data->from = from;
	create_data->recipients = recipients;
	create_data->is_send = g_strcmp0 (disposition, "SendOnly") == 0 || g_strcmp0 (disposition, "SendAndSaveCopy") == 0;

	if (create_data->is_send && !create_data->from) {
		CamelInternetAddress *address = camel_mime_message_get_from (message);

		if (address)
			create_data->from = CAMEL_ADDRESS (address);
	}

	res = e_ews_connection_create_items_sync (
		cnc, EWS_PRIORITY_MEDIUM,
		disposition, NULL, fid,
		create_mime_message_cb, create_data,
		&ids, cancellable, error);

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
