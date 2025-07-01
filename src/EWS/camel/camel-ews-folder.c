/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* This file is broken and suffers from multiple author syndrome.
This needs to be rewritten with a lot of functions cleaned up.
 *
There are a lot of places where code is unneccesarily duplicated,
which needs to be better organized via functions */

#include "evolution-ews-config.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <libecal/libecal.h>

#include "e-ews-common-utils.h"
#include "common/e-ews-query-to-restriction.h"

#include "common/camel-ews-settings.h"
#include "common/e-ews-camel-common.h"
#include "common/e-ews-connection.h"
#include "common/e-ews-item-change.h"
#include "common/e-ews-request.h"

#include "camel-ews-folder.h"
#include "camel-ews-private.h"
#include "camel-ews-store.h"
#include "camel-ews-summary.h"
#include "camel-ews-utils.h"

#define EWS_MAX_FETCH_COUNT 100

#define MAX_ATTACHMENT_SIZE 1*1024*1024   /*In bytes*/

/* there are written more follow-up flags, but it's read only few of them */
#define SUMMARY_ITEM_FLAGS "item:ResponseObjects item:Sensitivity item:Importance item:Categories"

#define ITEM_PROPS "item:Subject item:DateTimeReceived item:DateTimeSent item:DateTimeCreated item:Size " \
		   "item:HasAttachments item:InReplyTo"
#define SUMMARY_ITEM_PROPS ITEM_PROPS " " SUMMARY_ITEM_FLAGS


#define SUMMARY_MESSAGE_FLAGS SUMMARY_ITEM_FLAGS " message:IsRead"
#define SUMMARY_MESSAGE_PROPS ITEM_PROPS " message:From message:Sender message:ToRecipients message:CcRecipients " \
		   "message:BccRecipients message:IsRead message:References message:InternetMessageId " \
		   SUMMARY_MESSAGE_FLAGS
#define SUMMARY_MESSAGE_PROPS_2013 SUMMARY_MESSAGE_PROPS " item:Preview"

#define SUMMARY_POSTITEM_PROPS ITEM_PROPS " " SUMMARY_ITEM_FLAGS " message:From message:Sender"

struct _CamelEwsFolderPrivate {
	GRecMutex cache_lock;	/* for locking the cache object */

	/* For syncronizing refresh_info/sync_changes */
	gboolean refreshing;
	gboolean fetch_pending;
	GMutex state_lock;
	GCond fetch_cond;
	GHashTable *fetching_uids;

	gboolean apply_filters;
	gboolean check_folder;
};

static gboolean ews_delete_messages (CamelFolder *folder, const GSList *deleted_items, gboolean expunge, GCancellable *cancellable, GError **error);
static gboolean ews_refresh_info_sync (CamelFolder *folder, GCancellable *cancellable, GError **error);

#define d(x)

enum {
	PROP_0,
	PROP_APPLY_FILTERS,
	PROP_CHECK_FOLDER
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelEwsFolder, camel_ews_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static GSList *
ews_folder_get_summary_followup_mapi_flags (void)
{
	GSList *list = NULL;
	EEwsExtendedFieldURI *ext_uri;

	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x1090); /* PidTagFlagStatus */
	ext_uri->prop_type = g_strdup ("Integer");
	list = g_slist_append (list, ext_uri);

	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x1091); /* PidTagFlagCompleteTime */
	ext_uri->prop_type = g_strdup ("SystemTime");
	list = g_slist_append (list, ext_uri);

	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x007D); /*  PidTagTransportMessageHeaders */
	ext_uri->prop_type = g_strdup ("String");
	list = g_slist_append (list, ext_uri);

	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->distinguished_prop_set_id = g_strdup ("Common");
	ext_uri->prop_id = g_strdup_printf ("%d", 0x8530); /* PidLidFlagRequest */
	ext_uri->prop_type = g_strdup ("String");
	list = g_slist_append (list, ext_uri);

	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->distinguished_prop_set_id = g_strdup ("Task");
	ext_uri->prop_id = g_strdup_printf ("%d", 0x8105); /* PidLidTaskDueDate */
	ext_uri->prop_type = g_strdup ("SystemTime");
	list = g_slist_append (list, ext_uri);

	return list;
}

static GSList *
ews_folder_get_summary_message_mapi_flags (void)
{
	GSList *list;
	EEwsExtendedFieldURI *ext_uri;

	list = ews_folder_get_summary_followup_mapi_flags ();

	/* PidTagMessageFlags */
	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x0e07);
	ext_uri->prop_type = g_strdup ("Integer");
	list = g_slist_append (list, ext_uri);

	/* PidTagMessageStatus */
	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x0e17);
	ext_uri->prop_type = g_strdup ("Integer");
	list = g_slist_append (list, ext_uri);

	/* PidTagIconIndex */
	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x1080);
	ext_uri->prop_type = g_strdup ("Integer");
	list = g_slist_append (list, ext_uri);

	/* PidTagLastVerbExecuted */
	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x1081);
	ext_uri->prop_type = g_strdup ("Integer");
	list = g_slist_append (list, ext_uri);

	/* PidTagReadReceiptRequested */
	ext_uri = e_ews_extended_field_uri_new ();
	ext_uri->prop_tag = g_strdup_printf ("%d", 0x0029);
	ext_uri->prop_type = g_strdup ("Boolean");
	list = g_slist_append (list, ext_uri);

	return list;
}

static gchar *
ews_get_filename (CamelFolder *folder,
                  const gchar *uid,
                  GError **error)
{
	CamelEwsFolder *ews_folder = CAMEL_EWS_FOLDER (folder);
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gchar *filename;

	g_checksum_update (sha, (guchar *) uid, strlen (uid));
	filename = camel_data_cache_get_filename (
		ews_folder->cache, "cur", g_checksum_get_string (sha));
	g_checksum_free (sha);

	return filename;
}

static CamelStream *
ews_data_cache_add (CamelDataCache *cdc,
		    const gchar *path,
		    const gchar *key,
		    GError **error)
{
	GIOStream *base_stream;
	CamelStream *stream = NULL;
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);

	g_checksum_update (sha, (guchar *) key, strlen (key));
	base_stream = camel_data_cache_add (
		cdc, path, g_checksum_get_string (sha), error);
	g_checksum_free (sha);

	if (base_stream != NULL) {
		stream = camel_stream_new (base_stream);
		g_object_unref (base_stream);
	}

	return stream;
}

static gint
ews_data_cache_remove (CamelDataCache *cdc,
                       const gchar *path,
                       const gchar *key,
                       GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gint ret;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	ret = camel_data_cache_remove (
		cdc, path, g_checksum_get_string (sha), error);
	g_checksum_free (sha);

	return ret;
}

static CamelStream *
ews_data_cache_get (CamelDataCache *cdc,
                    const gchar *path,
                    const gchar *key,
                    GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	CamelStream *stream = NULL;
	GIOStream *base_stream;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	base_stream = camel_data_cache_get (
		cdc, path, g_checksum_get_string (sha), error);
	if (base_stream != NULL) {
		stream = camel_stream_new (base_stream);
		g_object_unref (base_stream);
	}
	g_checksum_free (sha);

	return stream;
}

static gchar *
ews_data_cache_get_filename (CamelDataCache *cdc,
                             const gchar *path,
                             const gchar *key,
                             GError **error)
{
	GChecksum *sha = g_checksum_new (G_CHECKSUM_SHA256);
	gchar *filename;

	g_checksum_update (sha, (guchar *) key, strlen (key));
	filename = camel_data_cache_get_filename (
		cdc, path, g_checksum_get_string (sha));
	g_checksum_free (sha);

	return filename;
}

static CamelMimeMessage *
camel_ews_folder_get_message_from_cache (CamelEwsFolder *ews_folder,
                                         const gchar *uid,
                                         GCancellable *cancellable,
                                         GError **error)
{
	CamelStream *stream;
	CamelMimeMessage *msg;
	CamelEwsFolderPrivate *priv;

	priv = ews_folder->priv;

	g_rec_mutex_lock (&priv->cache_lock);
	stream = ews_data_cache_get (ews_folder->cache, "cur", uid, error);
	if (!stream) {
		gchar *old_fname = camel_data_cache_get_filename (
			ews_folder->cache, "cur", uid);
		if (!g_access (old_fname, R_OK)) {
			gchar *new_fname = ews_data_cache_get_filename (
				ews_folder->cache,
				"cur", uid, error);
			if (g_rename (old_fname, new_fname) == -1) {
				g_warning ("%s: Failed to rename '%s' to '%s': %s", G_STRFUNC,
					   old_fname, new_fname, g_strerror (errno));
			}
			g_free (new_fname);
			stream = ews_data_cache_get (ews_folder->cache, "cur", uid, error);
		}
		g_free (old_fname);
		if (!stream) {
			g_rec_mutex_unlock (&priv->cache_lock);
			return NULL;
		}
	}

	msg = camel_mime_message_new ();

	if (!camel_data_wrapper_construct_from_stream_sync (
		(CamelDataWrapper *) msg, stream, cancellable, error)) {
		g_object_unref (msg);
		msg = NULL;
	}

	g_rec_mutex_unlock (&priv->cache_lock);
	g_object_unref (stream);

	return msg;
}

static CamelMimePart *
ews_get_calendar_mime_part (CamelMimePart *mimepart)
{
	guint partnumber, i;
	CamelDataWrapper *datawrapper;
	datawrapper = camel_medium_get_content (CAMEL_MEDIUM (mimepart));

	if (CAMEL_IS_MULTIPART (datawrapper)) {
		partnumber = camel_multipart_get_number (CAMEL_MULTIPART (datawrapper));
		for (i = 0; i < partnumber; i++) {
			CamelMimePart *child_mimepart = NULL;
			CamelMimePart *ret_mimepart = NULL;
			child_mimepart = camel_multipart_get_part (CAMEL_MULTIPART (datawrapper), i);

			if (!child_mimepart)
				goto exit;

			ret_mimepart = ews_get_calendar_mime_part (child_mimepart);
			if (ret_mimepart)
				return ret_mimepart;
		}
	}
	else {
		gchar *type;
		type = camel_data_wrapper_get_mime_type (datawrapper);
		if (!g_ascii_strcasecmp (type, "text/calendar")) {
			g_free (type);
			return mimepart;
		}
		g_free (type);
	}
	exit:
	return NULL;
}

static gchar *
ews_update_mtgrequest_mime_calendar_itemid (const gchar *mime_fname,
                                            const EwsId *calendar_item_id,
                                            gboolean is_calendar_UID,
					    const EwsId *mail_item_id,
					    const gchar *html_body,
                                            GError **error)
{
	CamelMimeParser *mimeparser;
	CamelMimeMessage *msg;
	CamelMimePart *mimepart = NULL;
	gint fd_old;
	gchar *mime_fname_new = NULL;

	/* original mime file */
	fd_old = open (mime_fname, O_RDONLY);
	if (fd_old == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to open mimecontent temporary file!"));
		return NULL;
	}

	mimeparser = camel_mime_parser_new ();
	if (camel_mime_parser_init_with_fd (mimeparser, fd_old) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to generate parser from mimecontent!"));
		goto exit_parser;
	}

	msg = camel_mime_message_new ();
	if (camel_mime_part_construct_from_parser_sync (
		CAMEL_MIME_PART (msg), mimeparser, NULL, error) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to parse meeting request mimecontent!"));
		goto exit_msg;
	}

	mimepart = ews_get_calendar_mime_part (CAMEL_MIME_PART (msg));
	if (mimepart) {
		CamelDataWrapper *dw;
		CamelStream *tmpstream = NULL, *newstream = NULL;
		GByteArray *ba;
		ICalComponent *icomp = NULL;
		gchar *dir;
		gint fd;
		gboolean success = FALSE;

		dw = camel_medium_get_content (CAMEL_MEDIUM (mimepart));
		tmpstream = camel_stream_mem_new ();
		if (camel_data_wrapper_decode_to_stream_sync (
			dw, tmpstream, NULL, error) == -1) {
			g_object_unref (tmpstream);
			goto exit_msg;
		}
		ba = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (tmpstream));
		if (ba && ba->len) {
			g_byte_array_append (ba, (guint8 *) "\0", 1);
			icomp = i_cal_parser_parse_string ((gchar *) ba->data);
			if (!icomp) {
				const gchar *content = (const gchar *) ba->data;
				const gchar *begin_vcalendar, *end_vcalendar;

				/* Workaround Office365.com error, which returns invalid iCalendar object (without 'END:VCALENDAR'),
				   in the MimeContent's text/calendar attachments as of 2023-12-12. */
				begin_vcalendar = camel_strstrcase (content, "BEGIN:VCALENDAR");
				end_vcalendar = camel_strstrcase (content, "END:VCALENDAR");

				/* If it exists, then it should be alone on a separate line */
				if (!(begin_vcalendar && (begin_vcalendar == content || begin_vcalendar[-1] == '\n') &&
				    (begin_vcalendar[15 /* strlen ("BEGIN:VCALENDAR") */] == '\r' || begin_vcalendar[15] == '\n')))
					begin_vcalendar = NULL;

				/* If it exists, then it should be alone on a separate line and not at the very beginning of the mime_content */
				if (!(end_vcalendar && end_vcalendar > content && end_vcalendar[-1] == '\n' &&
				    (end_vcalendar[13 /* strlen ("END:VCALENDAR") */] == '\r' || end_vcalendar[13] == '\n' || end_vcalendar[13] == '\0')))
					end_vcalendar = NULL;

				if (begin_vcalendar && !end_vcalendar) {
					g_byte_array_remove_index (ba, ba->len - 1);
					#define add_str(_str) g_byte_array_append (ba, (guint8 *) _str, strlen (_str))
					add_str ("\r\nEND:VCALENDAR\r\n");
					#undef add_str
					g_byte_array_append (ba, (guint8 *) "\0", 1);

					icomp = i_cal_parser_parse_string ((const gchar *) ba->data);
				}
			}
		}
		if (icomp) {
			ICalComponent *subcomp;
			ICalProperty *prop;
			gchar *calstring_new;

			subcomp = i_cal_component_get_first_component (icomp, I_CAL_VEVENT_COMPONENT);
			prop = i_cal_property_new_x (calendar_item_id->change_key);
			i_cal_property_set_x_name (prop, "X-EVOLUTION-CHANGEKEY");
			i_cal_component_take_property (subcomp, prop);

			/* In order to accept items we have to store AssociatedCalendarItemId (X-EVOLUTION-ITEMID)
			 * or mail id (X-EVOLUTION-ACCEPT-ID) when we do not have AssociatedCalendarItemId */
			if (is_calendar_UID) {
				prop = i_cal_property_new_x (calendar_item_id->id);
				i_cal_property_set_x_name (prop, "X-EVOLUTION-ITEMID");
				i_cal_component_take_property (subcomp, prop);
			}

			prop = i_cal_property_new_x (mail_item_id->id);
			i_cal_property_set_x_name (prop, "X-EVOLUTION-ACCEPT-ID");
			i_cal_component_take_property (subcomp, prop);

			if (html_body && *html_body) {
				prop = i_cal_component_get_first_property (subcomp, I_CAL_DESCRIPTION_PROPERTY);

				/* The server can return empty HTML (with "<html><body></body></html>" only),
				   thus add it only if there was any DESCRIPTION provided as well. */
				if (prop) {
					g_clear_object (&prop);

					prop = i_cal_property_new_x (html_body);
					i_cal_property_set_x_name (prop, "X-ALT-DESC");
					i_cal_property_set_parameter_from_string (prop, "FMTTYPE", "text/html");
					i_cal_component_take_property (subcomp, prop);
				}
			}

			calstring_new = i_cal_component_as_ical_string (icomp);
			if (calstring_new) {
				camel_mime_part_set_content (
					mimepart,
					(const gchar *) calstring_new,
					strlen (calstring_new),
					"text/calendar");
				g_free (calstring_new);
			}
			g_object_unref (subcomp);
			g_object_unref (icomp);
		}
		g_object_unref (tmpstream);

		/* Create a new file to store updated mimecontent */
		dir = g_path_get_dirname (mime_fname);
		mime_fname_new = g_build_filename ((const gchar *) dir, "XXXXXX", NULL);
		fd = g_mkstemp (mime_fname_new);
		if (fd == -1) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Unable to create cache file"));
			goto exit_save;
		}
		newstream = camel_stream_fs_new_with_fd (fd);
		if (camel_data_wrapper_write_to_stream_sync (
			CAMEL_DATA_WRAPPER (msg),
			newstream, NULL, error) == -1)
			goto exit_save;
		if (camel_stream_flush (newstream, NULL, error) == -1)
			goto exit_save;
		if (camel_stream_close (newstream, NULL, error) == -1)
			goto exit_save;
		if (g_remove (mime_fname) == -1) {
			g_warning ("%s: Failed to remove file '%s': %s",
				   G_STRFUNC, mime_fname, g_strerror (errno));
		}
		success = TRUE;
 exit_save:
		if (fd != -1)
			close (fd);
		g_free (dir);
		if (newstream)
			g_object_unref (newstream);
		if (!success) {
			g_free (mime_fname_new);
			mime_fname_new = NULL;
		}
	}
 exit_msg:
	g_object_unref (msg);
 exit_parser:
	g_object_unref (mimeparser);
	close (fd_old);

	/* must be freed in the caller */
	return mime_fname_new;
}

static void
ews_fetch_cancellable_cancelled_cb (GCancellable *cancellable,
				    GCond *fetch_cond)
{
	g_return_if_fail (fetch_cond != NULL);

	g_cond_broadcast (fetch_cond);
}

static gboolean
ews_message_from_properties_sync (CamelEwsFolder *ews_folder,
				  EEwsConnection *cnc,
				  gint pri,
				  GSList *ids,
				  const gchar *mime_dir,
				  GSList **out_items, /* EEwsItem * */
				  GCancellable *cancellable,
				  GError **error)
{
	EEwsAdditionalProps *add_props;
	const CamelNameValueArray *headers;
	CamelMessageInfo *mi;
	CamelMimeMessage *msg;
	CamelStream *new_stream;
	EEwsItem *item;
	gboolean bval = FALSE;
	gchar *mime_fname_new;
	gint fd;
	GSList *items = NULL;

	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (ews_folder), FALSE);
	g_return_val_if_fail (E_IS_EWS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (ids != NULL, FALSE);
	g_return_val_if_fail (mime_dir != NULL, FALSE);
	g_return_val_if_fail (out_items != NULL, FALSE);

	add_props = e_ews_additional_props_new ();
	if (e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2013))
		add_props->field_uri = g_strdup (SUMMARY_MESSAGE_PROPS_2013 " item:Body item:Attachments");
	else
		add_props->field_uri = g_strdup (SUMMARY_MESSAGE_PROPS " item:Body item:Attachments");
	add_props->extended_furis = ews_folder_get_summary_message_mapi_flags ();

	if (!e_ews_connection_get_items_sync (cnc, pri, ids, "IdOnly", add_props,
		FALSE, NULL, E_EWS_BODY_TYPE_TEXT, &items, NULL, NULL, cancellable, error) || !items) {
		e_ews_additional_props_free (add_props);
		return FALSE;
	}

	e_ews_additional_props_free (add_props);

	mi = camel_ews_utils_item_to_message_info (ews_folder, cnc, items->data, cancellable);
	if (!mi) {
		g_slist_free_full (items, g_object_unref);
		return FALSE;
	}

	item = items->data;
	msg = camel_mime_message_new ();

	headers = camel_message_info_get_headers (mi);
	if (headers) {
		CamelMedium *medium = CAMEL_MEDIUM (msg);
		guint ii, len;

		len = camel_name_value_array_get_length (headers);

		for (ii = 0; ii < len; ii++) {
			const gchar *name = NULL, *value = NULL;

			/* Skip any content-describing headers */
			if (camel_name_value_array_get (headers, ii, &name, &value) &&
			    name && g_ascii_strncasecmp (name, "Content-", 8) != 0) {
				camel_medium_add_header (medium, name, value);
			}
		}
	} else {
		CamelMedium *medium = CAMEL_MEDIUM (msg);

		camel_mime_message_set_date (msg, e_ews_item_get_date_sent (item), 0);
		camel_mime_message_set_message_id (msg, e_ews_item_get_msg_id (item));
		if (e_ews_item_get_in_replyto (item))
			camel_medium_set_header (medium, "In-Reply-To", e_ews_item_get_in_replyto (item));
		if (e_ews_item_get_references (item))
			camel_medium_set_header (medium, "References", e_ews_item_get_references (item));
		camel_medium_set_header (medium, "From", camel_message_info_get_from (mi));
		camel_medium_set_header (medium, "To", camel_message_info_get_to (mi));
		camel_medium_set_header (medium, "Cc", camel_message_info_get_from (mi));
		camel_mime_message_set_subject (msg, camel_message_info_get_subject (mi));
	}

	if (e_ews_item_has_attachments (item, &bval) && bval &&
	    e_ews_item_get_attachments_ids (item)) {
		CamelMimePart *part;
		CamelMultipart *m_mixed;
		const gchar *body = e_ews_item_get_body (item);
		GSList *attach_ids, *attachments = NULL, *link;

		m_mixed = camel_multipart_new ();
		camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (m_mixed), "multipart/mixed");
		camel_multipart_set_boundary (m_mixed, NULL);

		if (!body || !*body)
			body = " ";

		part = camel_mime_part_new ();
		camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_8BIT);
		camel_mime_part_set_content (part, body, strlen (body), "text/plain");
		camel_multipart_add_part (m_mixed, part);
		g_object_unref (part);

		attach_ids = e_ews_item_get_attachments_ids (item);
		if (e_ews_connection_get_attachments_sync (cnc, EWS_PRIORITY_MEDIUM, NULL, attach_ids, NULL, FALSE, &attachments,
		    NULL, NULL, cancellable, error)) {
			for (link = attachments; link; link = g_slist_next (link)) {
				EEwsAttachmentInfo *ainfo = link->data;

				if (ainfo && e_ews_attachment_info_get_type (ainfo) == E_EWS_ATTACHMENT_INFO_TYPE_INLINED) {
					const gchar *mime_type;
					const gchar *filename;
					const gchar *content;
					gsize content_len = 0;

					mime_type = e_ews_attachment_info_get_mime_type (ainfo);
					if (!mime_type)
						mime_type = "application/octet-stream";

					filename = e_ews_attachment_info_get_prefer_filename (ainfo);
					if (!filename)
						filename = e_ews_attachment_info_get_filename (ainfo);

					content = e_ews_attachment_info_get_inlined_data (ainfo, &content_len);
					if (!content) {
						content = " ";
						content_len = 1;
					}

					part = camel_mime_part_new ();
					camel_mime_part_set_disposition (part, "attachment");
					camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_BASE64);
					camel_mime_part_set_content (part, content, content_len, mime_type);

					if (filename)
						camel_mime_part_set_filename (part, filename);

					camel_multipart_add_part (m_mixed, part);
					g_object_unref (part);
				}
			}

			g_slist_free_full (attachments, (GDestroyNotify) e_ews_attachment_info_free);
		}

		camel_medium_set_content (CAMEL_MEDIUM (msg), CAMEL_DATA_WRAPPER (m_mixed));

		g_object_unref (m_mixed);
	} else {
		const gchar *body = e_ews_item_get_body (item);

		if (!body || !*body)
			body = " ";

		camel_mime_part_set_encoding (CAMEL_MIME_PART (msg), CAMEL_TRANSFER_ENCODING_8BIT);
		camel_mime_part_set_content (CAMEL_MIME_PART (msg), body, strlen (body), "text/plain");
	}

	mime_fname_new = g_build_filename (mime_dir, "XXXXXX", NULL);
	fd = g_mkstemp (mime_fname_new);
	if (fd != -1)
		e_ews_item_set_mime_content (item, mime_fname_new);
	g_free (mime_fname_new);

	if (fd == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to create cache file"));

		g_slist_free_full (items, g_object_unref);
		g_clear_object (&msg);
		g_clear_object (&mi);

		return FALSE;
	}

	new_stream = camel_stream_fs_new_with_fd (fd);
	if (camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (msg), new_stream, cancellable, error) == -1 ||
	   camel_stream_flush (new_stream, cancellable, error) == -1 ||
	   camel_stream_close (new_stream, cancellable, error) == -1) {
		g_slist_free_full (items, g_object_unref);
		g_clear_object (&new_stream);
		g_clear_object (&msg);
		g_clear_object (&mi);

		return FALSE;
	}

	g_clear_object (&new_stream);
	g_clear_object (&msg);
	g_clear_object (&mi);

	*out_items = items;

	return TRUE;
}

static CamelMimeMessage *
camel_ews_folder_get_message (CamelFolder *folder,
                              const gchar *uid,
                              gint pri,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	EEwsConnection *cnc = NULL;
	EEwsAdditionalProps *add_props = NULL;
	CamelEwsStore *ews_store;
	const gchar *mime_content;
	CamelMimeMessage *message = NULL;
	GSList *ids = NULL, *items = NULL;
	gchar *mime_dir;
	gchar *cache_file;
	gchar *dir;
	gboolean res;
	gchar *mime_fname_new = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (folder), NULL);

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);
	ews_folder = (CamelEwsFolder *) folder;
	priv = ews_folder->priv;

	g_mutex_lock (&priv->state_lock);

	message = camel_ews_folder_get_message_from_cache (ews_folder, uid, cancellable, NULL);
	if (message) {
		g_mutex_unlock (&priv->state_lock);

		return message;
	}

	/* If another thread is already fetching this message, wait for it */
	if (g_hash_table_lookup (priv->fetching_uids, uid)) {
		gulong cancelled_handler_id = 0;

		if (G_IS_CANCELLABLE (cancellable)) {
			cancelled_handler_id = g_cancellable_connect (cancellable,
				G_CALLBACK (ews_fetch_cancellable_cancelled_cb),
				&priv->fetch_cond, NULL);
		}

		do {
			g_cond_wait (&priv->fetch_cond, &priv->state_lock);
		} while (g_hash_table_lookup (priv->fetching_uids, uid) &&
			 !g_cancellable_is_cancelled (cancellable));

		if (cancelled_handler_id && G_IS_CANCELLABLE (cancellable))
			g_cancellable_disconnect (cancellable, cancelled_handler_id);

		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			g_mutex_unlock (&priv->state_lock);
			return NULL;
		}

		message = camel_ews_folder_get_message_from_cache (ews_folder, uid, cancellable, NULL);
		if (message || g_cancellable_set_error_if_cancelled (cancellable, error)) {
			g_mutex_unlock (&priv->state_lock);
			return message;
		}
	}

	/* Because we're using this as a form of mutex, we *know* that
	 * we won't be inserting where an entry already exists. So it's
	 * OK to insert uid itself, not g_strdup (uid) */
	g_hash_table_insert (priv->fetching_uids, (gchar *) uid, (gchar *) uid);
	g_mutex_unlock (&priv->state_lock);

	if (!camel_ews_store_connected (ews_store, cancellable, error))
		goto exit;

	cnc = camel_ews_store_ref_connection (ews_store);
	ids = g_slist_append (ids, (gchar *) uid);

	mime_dir = g_build_filename (
		camel_data_cache_get_path (ews_folder->cache),
		"mimecontent", NULL);

	if (g_access (mime_dir, F_OK) == -1 &&
	    g_mkdir_with_parents (mime_dir, 0700) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to create cache path “%s”: %s"),
			mime_dir, g_strerror (errno));
		g_free (mime_dir);
		goto exit;
	}

	add_props = e_ews_additional_props_new ();
	add_props->field_uri = g_strdup ("item:MimeContent message:From message:Sender");
	add_props->indexed_furis = g_slist_prepend (NULL, e_ews_indexed_field_uri_new ("item:InternetMessageHeader", "Date"));

	res = e_ews_connection_get_items_sync (
		cnc, pri, ids, "IdOnly", add_props,
		TRUE, mime_dir, E_EWS_BODY_TYPE_ANY,
		&items,
		(ESoapResponseProgressFn) camel_operation_progress,
		(gpointer) cancellable,
		cancellable, &local_error);
	e_ews_additional_props_free (add_props);

	if (!res || !items) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		g_free (mime_dir);
		goto exit;
	}

	if (e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_ERROR) {
		if (g_error_matches (e_ews_item_get_error (items->data), EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_MIMECONTENTCONVERSIONFAILED)) {
			g_slist_free_full (items, g_object_unref);
			items = NULL;
			/* The server failed to convert message into the MimeContent;
			   construct it from the properties. */
			if (!ews_message_from_properties_sync (ews_folder, cnc, pri, ids, mime_dir, &items, cancellable, &local_error) || !items) {
				camel_ews_store_maybe_disconnect (ews_store, local_error);
				g_propagate_error (error, local_error);
				g_free (mime_dir);
				goto exit;
			}
		}

		if (e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_ERROR) {
			local_error = g_error_copy (e_ews_item_get_error (items->data));
			camel_ews_store_maybe_disconnect (ews_store, local_error);
			if (local_error)
				g_propagate_error (error, local_error);
			g_free (mime_dir);
			goto exit;
		}
	}

	g_free (mime_dir);

	/* The mime_content actually contains the *filename*, due to the
	 * streaming hack in ESoapResponse */
	mime_content = e_ews_item_get_mime_content (items->data);
	if (!mime_content)
		goto exit;

	/* Exchange returns random UID for associated calendar item, which has no way
	 * to match with calendar components saved in calendar cache. So manually get
	 * AssociatedCalendarItemId, replace the random UID with this ItemId,
	 * And save updated message data to a new temp file */
	if (e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_REQUEST ||
		e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_CANCELLATION ||
		e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_MESSAGE ||
		e_ews_item_get_item_type (items->data) == E_EWS_ITEM_TYPE_MEETING_RESPONSE) {
		GSList *items_req = NULL, *html_body_resp = NULL;
		GSList *html_body_ids;
		const EwsId *calendar_item_accept_id = NULL;
		const gchar *html_body = NULL;
		gboolean is_calendar_UID = TRUE;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup ("meeting:AssociatedCalendarItemId");

		/* Get AssociatedCalendarItemId with second get_items call */
		res = e_ews_connection_get_items_sync (
			cnc, pri, ids, "IdOnly", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_ANY,
			&items_req,
			(ESoapResponseProgressFn) camel_operation_progress,
			(gpointer) cancellable,
			cancellable, &local_error);

		e_ews_additional_props_free (add_props);

		if (!res || (items_req && e_ews_item_get_item_type (items_req->data) == E_EWS_ITEM_TYPE_ERROR)) {
			if (items_req) {
				g_object_unref (items_req->data);
				g_slist_free (items_req);
			}
			if (local_error) {
				camel_ews_store_maybe_disconnect (ews_store, local_error);
				g_propagate_error (error, local_error);
			}
			goto exit;
		}

		if (items_req != NULL)
			calendar_item_accept_id = e_ews_item_get_calendar_item_accept_id (items_req->data);

		/*In case of non-exchange based meetings invites the calendar backend have to create the meeting*/
		if (calendar_item_accept_id == NULL) {
			calendar_item_accept_id = e_ews_item_get_id (items->data);
			is_calendar_UID = FALSE;
		}

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup ("item:Body");

		html_body_ids = g_slist_prepend (NULL, calendar_item_accept_id->id);

		if (e_ews_connection_get_items_sync (
			cnc, pri, html_body_ids, "IdOnly", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_BEST,
			&html_body_resp,
			(ESoapResponseProgressFn) camel_operation_progress,
			(gpointer) cancellable,
			cancellable, NULL) && html_body_resp && e_ews_item_get_item_type (html_body_resp->data) != E_EWS_ITEM_TYPE_ERROR) {
			EEwsItem *item = html_body_resp->data;

			if (e_ews_item_get_body_type (item) == E_EWS_BODY_TYPE_HTML) {
				html_body = e_ews_item_get_body (item);
			}
		}

		e_ews_additional_props_free (add_props);
		g_slist_free (html_body_ids);

		mime_fname_new = ews_update_mtgrequest_mime_calendar_itemid (mime_content, calendar_item_accept_id, is_calendar_UID, e_ews_item_get_id (items->data), html_body, error);
		if (mime_fname_new)
			mime_content = (const gchar *) mime_fname_new;

		g_slist_free_full (html_body_resp, g_object_unref);
		g_slist_free_full (items_req, g_object_unref);
	}

	cache_file = ews_data_cache_get_filename (
		ews_folder->cache, "cur", uid, error);
	dir = g_path_get_dirname (cache_file);

	if (g_mkdir_with_parents (dir, 0700) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to create cache path “%s”: %s"),
			dir, g_strerror (errno));
		g_free (dir);
		g_free (cache_file);
		goto exit;
	}
	g_free (dir);

	if (g_rename (mime_content, cache_file) != 0) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			/* Translators: The first %s consists of the source file name,
			   the second %s of the destination file name and
			   the third %s of the error message. */
			_("Failed to move message cache file from “%s” to “%s”: %s"),
			mime_content, cache_file, g_strerror (errno));
		g_free (cache_file);
		goto exit;
	}
	g_free (cache_file);

	message = camel_ews_folder_get_message_from_cache (ews_folder, uid, cancellable, error);
	if (message) {
		CamelInternetAddress *from;
		CamelMessageInfo *mi;
		const gchar *email = NULL, *date_header;
		gboolean resave = FALSE;

		from = camel_mime_message_get_from (message);

		if (!from || !camel_internet_address_get (from, 0, NULL, &email) || !email || !*email) {
			const EwsMailbox *mailbox;

			mailbox = e_ews_item_get_from (items->data);
			if (!mailbox)
				mailbox = e_ews_item_get_sender (items->data);
			if (mailbox) {
				email = NULL;

				if (g_strcmp0 (mailbox->routing_type, "EX") == 0)
					email = e_ews_item_util_strip_ex_address (mailbox->email);

				from = camel_internet_address_new ();
				camel_internet_address_add (from, mailbox->name, email ? email : mailbox->email);
				camel_mime_message_set_from (message, from);
				g_object_unref (from);

				resave = TRUE;
			}
		}

		date_header = e_ews_item_get_date_header (items->data);
		if (date_header && *date_header) {
			time_t tt;
			gint tz_offset;

			tt = camel_header_decode_date (date_header, &tz_offset);
			if (tt > 0) {
				camel_mime_message_set_date (message, tt, tz_offset);
				resave = TRUE;
			}
		}

		if (resave) {
			CamelStream *cache_stream;

			g_rec_mutex_lock (&priv->cache_lock);
			/* Ignore errors here, it's nothing fatal in this case */
			cache_stream = ews_data_cache_get (ews_folder->cache, "cur", uid, NULL);
			if (cache_stream) {
				GIOStream *iostream;

				/* Truncate the stream first, in case the message will be shorter
				   than the one received from the server */
				iostream = camel_stream_ref_base_stream (cache_stream);
				if (iostream) {
					GOutputStream *output_stream;

					output_stream = g_io_stream_get_output_stream (iostream);
					if (G_IS_SEEKABLE (output_stream)) {
						GSeekable *seekable = G_SEEKABLE (output_stream);

						if (g_seekable_can_truncate (seekable)) {
							g_seekable_truncate (seekable, 0, NULL, NULL);
						}
					}

					g_object_unref (iostream);
				}

				camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (message), cache_stream, cancellable, NULL);
				g_object_unref (cache_stream);
			}
			g_rec_mutex_unlock (&priv->cache_lock);
		}

		mi = camel_folder_summary_get (camel_folder_get_folder_summary (folder), uid);
		if (mi) {
			CamelMessageFlags flags;
			gboolean has_attachment;

			flags = camel_message_info_get_flags (mi);
			has_attachment = camel_mime_message_has_attachment (message);
			if (((flags & CAMEL_MESSAGE_ATTACHMENTS) && !has_attachment) ||
			    ((flags & CAMEL_MESSAGE_ATTACHMENTS) == 0 && has_attachment)) {
				camel_message_info_set_flags (
					mi, CAMEL_MESSAGE_ATTACHMENTS,
					has_attachment ? CAMEL_MESSAGE_ATTACHMENTS : 0);
			}

			g_clear_object (&mi);
		}
	}

exit:
	g_mutex_lock (&priv->state_lock);
	g_hash_table_remove (priv->fetching_uids, uid);
	g_cond_broadcast (&priv->fetch_cond);
	g_mutex_unlock (&priv->state_lock);

	if (!message && error && !*error)
		g_set_error (
			error, CAMEL_ERROR, 1,
			"Could not retrieve the message");
	if (ids)
		g_slist_free (ids);
	if (items) {
		g_object_unref (items->data);
		g_slist_free (items);
	}

	if (mime_fname_new)
		g_free (mime_fname_new);
	g_clear_object (&cnc);

	return message;
}

static void
ews_folder_maybe_update_mlist (CamelFolder *folder,
			       const gchar *uid,
			       CamelMimeMessage *message)
{
	CamelFolderSummary *summary;
	CamelMessageInfo *mi;
	const gchar *set_mlist;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (message != NULL);

	mi = camel_folder_summary_get (camel_folder_get_folder_summary (folder), uid);
	if (!mi)
		return;

	summary = camel_message_info_ref_summary (mi);
	if (summary)
		camel_folder_summary_lock (summary);
	camel_message_info_property_lock (mi);
	set_mlist = camel_message_info_get_mlist (mi);

	if (!set_mlist || !*set_mlist) {
		/* update mailing list information, if necessary */
		gchar *mlist = camel_headers_dup_mailing_list (camel_medium_get_headers (CAMEL_MEDIUM (message)));

		if (mlist) {
			camel_message_info_set_mlist (mi, mlist);
			g_free (mlist);
		}
	}

	camel_message_info_property_unlock (mi);
	if (summary)
		camel_folder_summary_unlock (summary);

	g_clear_object (&summary);
	g_clear_object (&mi);
}

static guint32
ews_folder_get_permanent_flags (CamelFolder *folder)
{
	return CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN |
		CAMEL_MESSAGE_FORWARDED |
		CAMEL_MESSAGE_USER;
}

/* Get the message from cache if available otherwise get it from server */
static CamelMimeMessage *
ews_folder_get_message_sync (CamelFolder *folder,
                             const gchar *uid,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelMimeMessage *message;

	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (folder), NULL);

	message = camel_ews_folder_get_message (folder, uid, EWS_ITEM_HIGH, cancellable, error);
	if (message)
		ews_folder_maybe_update_mlist (folder, uid, message);

	return message;
}

static CamelMimeMessage *
ews_folder_get_message_cached (CamelFolder *folder,
                               const gchar *message_uid,
                               GCancellable *cancellable)
{
	return camel_ews_folder_get_message_from_cache ((CamelEwsFolder *) folder, message_uid, cancellable, NULL);
}

/********************* folder functions*************************/

static gboolean
msg_update_flags (ESoapRequest *request,
                  gpointer user_data,
		  GError **error)
{
	/* the mi_list is owned by the caller */
	const GSList *mi_list = user_data, *iter;
	CamelMessageInfo *mi;
	CamelEwsMessageInfo *emi;

	for (iter = mi_list; iter; iter = g_slist_next (iter)) {
		CamelFolderSummary *summary;
		guint32 flags_changed, mi_flags;
		GSList *user_flags;

		mi = iter->data;
		emi = CAMEL_EWS_MESSAGE_INFO (mi);

		if (!mi || !emi)
			continue;

		summary = camel_message_info_ref_summary (mi);
		if (summary)
			camel_folder_summary_lock (summary);
		camel_message_info_property_lock (mi);

		mi_flags = camel_message_info_get_flags (mi);
		flags_changed = camel_ews_message_info_get_server_flags (emi) ^ mi_flags;

		e_ews_request_start_item_change (
			request, E_EWS_ITEMCHANGE_TYPE_ITEM,
			camel_message_info_get_uid (mi), camel_ews_message_info_get_change_key (emi), 0);
		if (flags_changed & CAMEL_MESSAGE_FLAGGED) {
			const gchar *flag;

			if ((mi_flags & CAMEL_MESSAGE_FLAGGED) != 0)
				flag = "High";
			else
				flag = "Normal";

			e_soap_request_start_element (request, "SetItemField", NULL, NULL);

			e_soap_request_start_element (request, "FieldURI", NULL, NULL);
			e_soap_request_add_attribute (request, "FieldURI", "item:Importance", NULL, NULL);
			e_soap_request_end_element (request);

			e_soap_request_start_element (request, "Message", NULL, NULL);

			e_ews_request_write_string_parameter (request, "Importance", NULL, flag);

			e_soap_request_end_element (request); /* Message */
			e_soap_request_end_element (request); /* SetItemField */
		}

		if (flags_changed & CAMEL_MESSAGE_SEEN) {
			e_soap_request_start_element (request, "SetItemField", NULL, NULL);

			e_soap_request_start_element (request, "FieldURI", NULL, NULL);
			e_soap_request_add_attribute (request, "FieldURI", "message:IsRead", NULL, NULL);
			e_soap_request_end_element (request);

			e_soap_request_start_element (request, "Message", NULL, NULL);
			e_ews_request_write_string_parameter (request, "IsRead", NULL,
				(mi_flags & CAMEL_MESSAGE_SEEN) ? "true" : "false");

			e_soap_request_end_element (request); /* Message */
			e_soap_request_end_element (request); /* SetItemField */
		}
		/* Ick Ick Ick. Why in hell is there a field in the database for the Icon
		 * *anyway*? Why isn't there a better place for forwarded/answered status? */
		if (flags_changed & (CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_ANSWERED)) {
			gint icon = (mi_flags & CAMEL_MESSAGE_SEEN) ? 0x100 : 0x101;

			if (mi_flags & CAMEL_MESSAGE_ANSWERED)
				icon = 0x105;
			if (mi_flags & CAMEL_MESSAGE_FORWARDED)
				icon = 0x106;

			e_ews_request_add_set_item_field_extended_tag_int (request, NULL, "Message", 0x1080, icon);
		}

		/* now update the Categories */
		user_flags = ews_utils_gather_server_user_flags (request, mi);
		if (user_flags) {
			GSList *link;

			e_soap_request_start_element (request, "SetItemField", NULL, NULL);

			e_soap_request_start_element (request, "FieldURI", NULL, NULL);
			e_soap_request_add_attribute (request, "FieldURI", "item:Categories", NULL, NULL);
			e_soap_request_end_element (request);

			e_soap_request_start_element (request, "Message", NULL, NULL);
			e_soap_request_start_element (request, "Categories", NULL, NULL);

			for (link = user_flags; link; link = g_slist_next (link)) {
				const gchar *user_flag = link->data;

				e_ews_request_write_string_parameter (request, "String", NULL, user_flag);
			}

			e_soap_request_end_element (request); /* Categories */
			e_soap_request_end_element (request); /* Message */
			e_soap_request_end_element (request); /* SetItemField */
		} else {
			e_ews_request_add_delete_item_field (request, "Categories", "item");
		}

		g_slist_free_full (user_flags, g_free);

		ews_utils_update_followup_flags (request, mi);

		e_ews_request_end_item_change (request);

		camel_message_info_set_folder_flagged (mi, FALSE);

		camel_message_info_property_unlock (mi);
		if (summary)
			camel_folder_summary_unlock (summary);
		g_clear_object (&summary);
	}

	return TRUE;
}

static gboolean
ews_suppress_read_receipt (ESoapRequest *request,
			   gpointer user_data,
			   GError **error)
{
	/* the mi_list is owned by the caller */
	const GSList *mi_list = user_data, *iter;
	CamelMessageInfo *mi;

	for (iter = mi_list; iter; iter = g_slist_next (iter)) {
		CamelFolderSummary *summary;

		mi = iter->data;
		if (!mi || (camel_message_info_get_flags (mi) & CAMEL_EWS_MESSAGE_MSGFLAG_RN_PENDING) == 0)
			continue;

		summary = camel_message_info_ref_summary (mi);
		if (summary)
			camel_folder_summary_lock (summary);
		camel_message_info_property_lock (mi);
		camel_message_info_freeze_notifications (mi);

		/* There was requested a read-receipt, but it is handled by evolution-ews,
		   thus prevent an automatic send of it by the server */
		e_soap_request_start_element (request, "SuppressReadReceipt", NULL, NULL);
		e_soap_request_start_element (request, "ReferenceItemId", NULL, NULL);
		e_soap_request_add_attribute (request, "Id", camel_message_info_get_uid (mi), NULL, NULL);
		e_soap_request_add_attribute (request, "ChangeKey", camel_ews_message_info_get_change_key (CAMEL_EWS_MESSAGE_INFO (mi)), NULL, NULL);
		e_soap_request_end_element (request); /* "ReferenceItemId" */
		e_soap_request_end_element (request); /* SuppressReadReceipt */

		camel_message_info_set_flags (mi, CAMEL_EWS_MESSAGE_MSGFLAG_RN_PENDING, 0);

		if (!camel_message_info_get_user_flag (mi, "receipt-handled"))
			camel_message_info_set_user_flag (mi, "receipt-handled", TRUE);

		camel_message_info_thaw_notifications (mi);
		camel_message_info_property_unlock (mi);
		if (summary)
			camel_folder_summary_unlock (summary);
		g_clear_object (&summary);
	}

	return TRUE;
}

static gboolean
ews_sync_mi_flags (CamelFolder *folder,
                   const GSList *mi_list,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelEwsStore *ews_store;
	EEwsConnection *cnc;
	const GSList *iter;
	GError *local_error = NULL;
	gboolean res = TRUE;

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	if (!camel_ews_store_connected (ews_store, cancellable, error)) {
		return FALSE;
	}

	cnc = camel_ews_store_ref_connection (ews_store);

	for (iter = mi_list; iter; iter = g_slist_next (iter)) {
		CamelMessageInfo *mi = iter->data;

		if (mi && (camel_message_info_get_flags (mi) & CAMEL_EWS_MESSAGE_MSGFLAG_RN_PENDING) != 0)
			break;
	}

	/* NULL means all had been checked and none has the flag set */
	if (iter) {
		GSList *ids = NULL;

		res = e_ews_connection_create_items_sync (
			cnc, EWS_PRIORITY_LOW,
			"SaveOnly", NULL, NULL,
			ews_suppress_read_receipt, (gpointer) mi_list,
			&ids, cancellable, &local_error);

		g_slist_free_full (ids, g_object_unref);

		/* ignore this error, it's not a big problem */
		if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_READRECEIPTNOTPENDING)) {
			g_clear_error (&local_error);
			res = TRUE;
		}
	}

	if (res) {
		res = e_ews_connection_update_items_sync (
			cnc, EWS_PRIORITY_LOW,
			"AlwaysOverwrite", "SaveOnly",
			NULL, NULL,
			msg_update_flags, (gpointer) mi_list, NULL,
			cancellable, &local_error);
	}

	camel_folder_summary_save (camel_folder_get_folder_summary (folder), NULL);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
	}

	g_object_unref (cnc);

	return res;
}

static gboolean
ews_save_flags (CamelFolder *folder,
		const GSList *mi_list,
		GCancellable *cancellable,
		GError **error)
{
	gboolean ret;
	GError *local_error = NULL;

	ret = ews_sync_mi_flags (folder, mi_list, cancellable, &local_error);

	if (local_error != NULL) {
		if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ACCESSDENIED)) {
			/*
			 * If cannot save flags, then it can be a public
			 * or a foreign folder with no write access;
			 * the flags will be saved locally, at least
			 */
			g_clear_error (&local_error);
			ret = TRUE;
		} else {
			g_propagate_error (error, local_error);
		}
	}

	return ret;
}

static gboolean
ews_folder_is_of_type (CamelFolder *folder,
                       guint32 folder_type)
{
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	gboolean is_of_type;
	gchar *folder_id;

	g_return_val_if_fail (folder != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	g_return_val_if_fail (ews_store != NULL, FALSE);

	folder_type = folder_type & CAMEL_FOLDER_TYPE_MASK;
	folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, camel_folder_get_full_name (folder));
	is_of_type = folder_id &&
		(camel_ews_store_summary_get_folder_flags (ews_store->summary, folder_id, NULL) & CAMEL_FOLDER_TYPE_MASK) == folder_type;
	g_free (folder_id);

	return is_of_type;
}

static gboolean
ews_folder_is_public_or_foreign (CamelFolder *folder)
{
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	gboolean res;
	gchar *folder_id;

	g_return_val_if_fail (folder != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	g_return_val_if_fail (ews_store != NULL, FALSE);

	folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, camel_folder_get_full_name (folder));
	res = folder_id && (camel_ews_store_summary_get_public (ews_store->summary, folder_id, NULL) ||
		camel_ews_store_summary_get_foreign (ews_store->summary, folder_id, NULL));
	g_free (folder_id);

	return res;
}

static gboolean
ews_move_to_special_folder (CamelFolder *folder,
			    const GSList *uids,
			    guint32 folder_type,
			    GCancellable *cancellable,
			    GError **error)
{
	CamelEwsFolder *ews_folder;
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	EEwsConnection *cnc;
	gboolean status = TRUE;

	if (ews_folder_is_of_type (folder, folder_type)) {
		/* cannot move to itself, but treat it as success */
		return TRUE;
	}

	parent_store = camel_folder_get_parent_store (folder);
	ews_folder = CAMEL_EWS_FOLDER (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	if (!camel_ews_store_connected (ews_store, cancellable, error))
		return FALSE;

	cnc = camel_ews_store_ref_connection (ews_store);

	if (uids) {
		GSList *moved_items = NULL;
		GError *local_error = NULL;
		gchar *folder_id;

		folder_id = camel_ews_store_summary_get_folder_id_from_folder_type (ews_store->summary, folder_type);

		status = e_ews_connection_move_items_in_chunks_sync (
			cnc, EWS_PRIORITY_MEDIUM, folder_id, FALSE,
			uids, &moved_items, cancellable, &local_error);

		if (!status && local_error && local_error->code == EWS_CONNECTION_ERROR_ITEMNOTFOUND) {
			/* If move failed due to the item not found, ignore the error,
			 * trigger folder info refresh and then go on to clear the
			 * cache of the remove items anyway. */
			g_clear_error (&local_error);
			status = ews_refresh_info_sync (folder, cancellable, &local_error);
		}

		/* Messages could be moved partially only, like when
		   the user cancels the operation in the middle */
		if (status || moved_items) {
			CamelFolderChangeInfo *changes;
			const GSList *iter, *items_iter;

			changes = camel_folder_change_info_new ();

			for (iter = uids, items_iter = moved_items; iter && items_iter; iter = g_slist_next (iter), items_iter = g_slist_next (items_iter)) {
				const gchar *uid = iter->data;
				EEwsItem *item = items_iter->data;

				if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
					continue;

				camel_folder_summary_lock (camel_folder_get_folder_summary (folder));

				camel_folder_change_info_remove_uid (changes, uid);
				camel_folder_summary_remove_uid (camel_folder_get_folder_summary (folder), uid);
				ews_data_cache_remove (ews_folder->cache, "cur", uid, NULL);

				camel_folder_summary_unlock (camel_folder_get_folder_summary (folder));
			}

			if (camel_folder_change_info_changed (changes)) {
				camel_folder_summary_touch (camel_folder_get_folder_summary (folder));
				camel_folder_changed (folder, changes);
			}
			camel_folder_change_info_free (changes);
		}

		g_slist_free_full (moved_items, g_object_unref);
		g_free (folder_id);

		if (local_error) {
			camel_ews_store_maybe_disconnect (ews_store, local_error);
			g_propagate_error (error, local_error);
		}
	}

	g_object_unref (cnc);

	return status;
}

static gboolean
ews_synchronize_sync (CamelFolder *folder,
                      gboolean expunge,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelEwsStore *ews_store;
	CamelFolderSummary *folder_summary;
	GPtrArray *uids;
	GSList *mi_list = NULL, *deleted_uids = NULL, *junk_uids = NULL, *inbox_uids = NULL;
	gchar *fid;
	gint mi_list_len = 0;
	gboolean is_junk_folder;
	gboolean success = TRUE;
	gint i;
	GError *local_error = NULL;

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	fid = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, camel_folder_get_full_name (folder));

	if (g_strcmp0 (fid, EWS_FOREIGN_FOLDER_ROOT_ID) == 0 ||
	    g_strcmp0 (fid, EWS_PUBLIC_FOLDER_ROOT_ID) == 0) {
		g_free (fid);
		return TRUE;
	} else if (fid) {
		gchar *parent_fid;

		parent_fid = camel_ews_store_summary_get_parent_folder_id (ews_store->summary, fid, NULL);

		if (g_strcmp0 (parent_fid, EWS_FOREIGN_FOLDER_ROOT_ID) == 0) {
			g_free (parent_fid);
			g_free (fid);
			return TRUE;
		}

		g_free (parent_fid);
	}

	g_free (fid);

	if (!camel_ews_store_connected (ews_store, cancellable, error))
		return FALSE;

	folder_summary = camel_folder_get_folder_summary (folder);
	if (camel_folder_summary_get_deleted_count (folder_summary) > 0 ||
	    camel_folder_summary_get_junk_count (folder_summary) > 0) {
		camel_folder_summary_prepare_fetch_all (folder_summary, NULL);
		uids = camel_folder_summary_dup_uids (folder_summary);
	} else {
		uids = camel_folder_summary_dup_changed (folder_summary);
	}
	if (!uids || !uids->len) {
		g_ptr_array_unref (uids);
		return TRUE;
	}

	is_junk_folder = ews_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_JUNK);

	for (i = 0; success && i < uids->len; i++) {
		guint32 flags_changed, flags_set;
		CamelMessageInfo *mi = camel_folder_summary_get (folder_summary, uids->pdata[i]);

		if (!mi)
			continue;

		flags_set = camel_message_info_get_flags (mi);
		flags_changed = camel_ews_message_info_get_server_flags (CAMEL_EWS_MESSAGE_INFO (mi)) ^ flags_set;

		/* Exchange doesn't seem to have a sane representation
		 * for most flags — not even replied/forwarded. */
		if ((flags_set & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0 &&
		    (flags_changed & (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_FORWARDED | CAMEL_MESSAGE_FLAGGED)) != 0) {
			mi_list = g_slist_prepend (mi_list, mi);
			mi_list_len++;

			if (flags_set & CAMEL_MESSAGE_DELETED)
				deleted_uids = g_slist_prepend (deleted_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			else if (flags_set & CAMEL_MESSAGE_JUNK)
				junk_uids = g_slist_prepend (junk_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			else if (is_junk_folder && (flags_set & CAMEL_MESSAGE_NOTJUNK) != 0)
				inbox_uids = g_slist_prepend (inbox_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
		} else if (flags_set & CAMEL_MESSAGE_DELETED) {
			deleted_uids = g_slist_prepend (deleted_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			g_clear_object (&mi);
		} else if (flags_set & CAMEL_MESSAGE_JUNK) {
			junk_uids = g_slist_prepend (junk_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			g_clear_object (&mi);
		} else if (is_junk_folder && (flags_set & CAMEL_MESSAGE_NOTJUNK) != 0) {
			inbox_uids = g_slist_prepend (inbox_uids, (gpointer) camel_pstring_strdup (uids->pdata[i]));
			g_clear_object (&mi);
		} else if ((flags_set & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0) {
			/* OK, the change must have been the labels */
			mi_list = g_slist_prepend (mi_list, mi);
			mi_list_len++;
		} else {
			g_clear_object (&mi);
		}

		if (mi_list_len == EWS_MAX_FETCH_COUNT) {
			success = ews_save_flags (folder, mi_list, cancellable, &local_error);
			g_slist_free_full (mi_list, g_object_unref);
			mi_list = NULL;
			mi_list_len = 0;
		}
	}

	if (mi_list != NULL && success)
		success = ews_save_flags (folder, mi_list, cancellable, &local_error);
	g_slist_free_full (mi_list, g_object_unref);

	if (deleted_uids && success)
		success = ews_delete_messages (folder, deleted_uids, ews_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_TRASH), cancellable, &local_error);
	g_slist_free_full (deleted_uids, (GDestroyNotify) camel_pstring_free);

	if (junk_uids && success)
		success = ews_move_to_special_folder (folder, junk_uids, CAMEL_FOLDER_TYPE_JUNK, cancellable, &local_error);
	g_slist_free_full (junk_uids, (GDestroyNotify) camel_pstring_free);

	if (inbox_uids && success)
		success = ews_move_to_special_folder (folder, inbox_uids, CAMEL_FOLDER_TYPE_INBOX, cancellable, &local_error);
	g_slist_free_full (inbox_uids, (GDestroyNotify) camel_pstring_free);

	camel_folder_summary_save (folder_summary, NULL);
	g_ptr_array_unref (uids);

	if (local_error)
		g_propagate_error (error, local_error);

	return success;
}

static void
ews_folder_count_notify_cb (CamelFolderSummary *folder_summary,
                            GParamSpec *param,
                            CamelFolder *folder)
{
	gint count;
	CamelEwsStore *ews_store;
	CamelEwsStoreSummary *store_summary;
	gchar *folder_id;

	g_return_if_fail (folder_summary != NULL);
	g_return_if_fail (param != NULL);
	g_return_if_fail (folder != NULL);
	g_return_if_fail (camel_folder_get_folder_summary (folder) == folder_summary);

	ews_store = CAMEL_EWS_STORE (camel_folder_get_parent_store (folder));
	g_return_if_fail (ews_store != NULL);

	store_summary = ews_store->summary;
	folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, camel_folder_get_full_name (folder));

	/* this can happen on folder delete/unsubscribe, after folder summary clear */
	if (!folder_id)
		return;

	if (g_strcmp0 (g_param_spec_get_name (param), "saved-count") == 0) {
		count = camel_folder_summary_get_saved_count (folder_summary);
		camel_ews_store_summary_set_folder_total (store_summary, folder_id, count);
	} else if (g_strcmp0 (g_param_spec_get_name (param), "unread-count") == 0) {
		count = camel_folder_summary_get_unread_count (folder_summary);
		camel_ews_store_summary_set_folder_unread (store_summary, folder_id, count);
	} else {
		g_warn_if_reached ();
	}

	g_free (folder_id);
}

static gboolean
folder_has_inbox_type (CamelEwsStore *ews_store,
                       const gchar *folder_name)
{
	gchar *folder_id;
	guint64 flags;

	g_return_val_if_fail (CAMEL_IS_EWS_STORE (ews_store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, folder_name);
	if (!folder_id)
		return FALSE;

	flags = camel_ews_store_summary_get_folder_flags (ews_store->summary, folder_id, NULL);

	g_free (folder_id);

	return (flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX;
}

CamelFolder *
camel_ews_folder_new (CamelStore *store,
                      const gchar *folder_name,
                      const gchar *folder_dir,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelFolder *folder;
	CamelFolderSummary *folder_summary;
	CamelEwsFolder *ews_folder;
	CamelSettings *settings;
	gboolean filter_inbox = FALSE;
	gboolean filter_junk = FALSE;
	gboolean filter_junk_inbox = FALSE;
	gboolean offline_limit_by_age = FALSE;
	CamelTimeUnit offline_limit_unit;
	gint offline_limit_value = 0;
	guint32 add_folder_flags = 0;
	gchar *state_file;
	const gchar *short_name;

	short_name = strrchr (folder_name, '/');
	if (!short_name)
		short_name = folder_name;
	else
		short_name++;

	folder = g_object_new (
		CAMEL_TYPE_EWS_FOLDER,
		"display_name", short_name, "full-name", folder_name,
		"parent_store", store, NULL);

	ews_folder = CAMEL_EWS_FOLDER (folder);

	folder_summary = camel_ews_summary_new (folder);

	if (!folder_summary) {
		g_object_unref (folder);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load summary for %s"), folder_name);
		return NULL;
	}

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	g_object_get (
		settings,
		"filter-inbox", &filter_inbox,
		"filter-junk", &filter_junk,
		"filter-junk-inbox", &filter_junk_inbox,
		"limit-by-age", &offline_limit_by_age,
		"limit-unit", &offline_limit_unit,
		"limit-value", &offline_limit_value,
		NULL);

	g_clear_object (&settings);

	camel_folder_take_folder_summary (folder, folder_summary);

	/* set/load persistent state */
	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_folder_take_state_filename (folder, g_steal_pointer (&state_file));
	camel_folder_load_state (folder);

	ews_folder->cache = camel_data_cache_new (folder_dir, error);
	if (!ews_folder->cache) {
		g_object_unref (folder);
		return NULL;
	}

	if (camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder))) {
		time_t when = (time_t) 0;

		if (offline_limit_by_age)
			when = camel_time_value_apply (when, offline_limit_unit, offline_limit_value);

		if (when <= (time_t) 0)
			when = (time_t) -1;

		/* Ensure cache will expire when set up, otherwise
		 * it causes redownload of messages too soon. */
		camel_data_cache_set_expire_age (ews_folder->cache, when);
		camel_data_cache_set_expire_access (ews_folder->cache, when);
	} else {
		/* Set cache expiration for one week. */
		camel_data_cache_set_expire_age (ews_folder->cache, 60 * 60 * 24 * 7);
		camel_data_cache_set_expire_access (ews_folder->cache, 60 * 60 * 24 * 7);
	}

	camel_binding_bind_property (store, "online",
		ews_folder->cache, "expire-enabled",
		G_BINDING_SYNC_CREATE);

	if (!g_ascii_strcasecmp (folder_name, "Inbox") ||
	    folder_has_inbox_type (CAMEL_EWS_STORE (store), folder_name)) {
		if (filter_inbox)
			add_folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

		if (filter_junk)
			add_folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else {
		if (camel_ews_folder_get_apply_filters (ews_folder))
			add_folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

		if (filter_junk && !filter_junk_inbox)
			add_folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	}

	if (add_folder_flags)
		camel_folder_set_flags (folder, camel_folder_get_flags (folder) | add_folder_flags);

	g_signal_connect (folder_summary, "notify::saved-count", G_CALLBACK (ews_folder_count_notify_cb), folder);
	g_signal_connect (folder_summary, "notify::unread-count", G_CALLBACK (ews_folder_count_notify_cb), folder);

	return folder;
}

static void
sync_updated_items (CamelEwsFolder *ews_folder,
                    EEwsConnection *cnc,
		    gboolean is_drafts_folder,
                    GSList *updated_items,
		    CamelFolderChangeInfo *change_info,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelEwsStore *ews_store;
	CamelFolder *folder = (CamelFolder *) ews_folder;
	GSList *items = NULL, *l;
	GSList *generic_item_ids = NULL, *msg_ids = NULL;
	GError *local_error = NULL;

	ews_store = CAMEL_EWS_STORE (camel_folder_get_parent_store (folder));

	for (l = updated_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id;
		EEwsItemType item_type;
		CamelMessageInfo *mi;

		if (!item)
			continue;

		id = e_ews_item_get_id (item);
		item_type = e_ews_item_get_item_type (item);

		if (!id) {
			g_warning ("%s: Missing ItemId for item type %d (subject:%s)", G_STRFUNC, item_type,
				e_ews_item_get_subject (item) ? e_ews_item_get_subject (item) : "???");
			g_object_unref (item);
			continue;
		}

		/* Compare the item_type from summary as the updated items seems to
		 * arrive as generic types while it's not the case */
		mi = camel_folder_summary_get (camel_folder_get_folder_summary (folder), id->id);
		if (!mi) {
			g_object_unref (item);
			continue;
		}

		/* Check if the item has really changed */
		if (!g_strcmp0 (camel_ews_message_info_get_change_key (CAMEL_EWS_MESSAGE_INFO (mi)), id->change_key)) {
			g_clear_object (&mi);
			g_object_unref (item);
			continue;
		}

		if (item_type == E_EWS_ITEM_TYPE_GENERIC_ITEM)
			generic_item_ids = g_slist_append (generic_item_ids, g_strdup (id->id));
		else if (item_type == E_EWS_ITEM_TYPE_MESSAGE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_REQUEST ||
			item_type == E_EWS_ITEM_TYPE_MEETING_MESSAGE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_RESPONSE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_CANCELLATION ||
			item_type == E_EWS_ITEM_TYPE_POST_ITEM ||
			/* Unknown for items received through the server notifications;
		           it's part of the summary, thus it is a message anyway */
			item_type == E_EWS_ITEM_TYPE_UNKNOWN)
			msg_ids = g_slist_append (msg_ids, g_strdup (id->id));

		g_clear_object (&mi);
		g_object_unref (item);
	}
	g_slist_free (updated_items);


	if (msg_ids) {
		EEwsAdditionalProps *add_props;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup (is_drafts_folder ?
			(e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2013) ? SUMMARY_MESSAGE_PROPS_2013 : SUMMARY_MESSAGE_PROPS) :
			SUMMARY_MESSAGE_FLAGS);
		add_props->extended_furis = ews_folder_get_summary_message_mapi_flags ();

		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			msg_ids, "IdOnly", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_ANY, &items, NULL, NULL,
			cancellable, &local_error);

		e_ews_additional_props_free (add_props);
	}

	camel_ews_utils_sync_updated_items (ews_folder, cnc, is_drafts_folder, items, change_info, cancellable);
	items = NULL;
	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		goto exit;
	}

	if (generic_item_ids) {
		EEwsAdditionalProps *add_props;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup (is_drafts_folder ? SUMMARY_ITEM_PROPS : SUMMARY_ITEM_FLAGS);
		add_props->extended_furis = ews_folder_get_summary_followup_mapi_flags ();

		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			generic_item_ids, "IdOnly", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_ANY, &items, NULL, NULL,
			cancellable, &local_error);

		e_ews_additional_props_free (add_props);
	}
	camel_ews_utils_sync_updated_items (ews_folder, cnc, is_drafts_folder, items, change_info, cancellable);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
	}

exit:
	if (msg_ids) {
		g_slist_foreach (msg_ids, (GFunc) g_free, NULL);
		g_slist_free (msg_ids);
	}

	if (generic_item_ids) {
		g_slist_foreach (generic_item_ids, (GFunc) g_free, NULL);
		g_slist_free (generic_item_ids);
	}
}

void
camel_ews_folder_remove_cached_message (CamelEwsFolder *ews_folder,
					const gchar *uid)
{
	g_return_if_fail (CAMEL_IS_EWS_FOLDER (ews_folder));
	g_return_if_fail (uid != NULL);

	ews_data_cache_remove (ews_folder->cache, "cur", uid, NULL);
}

static void
sync_created_items (CamelEwsFolder *ews_folder,
                    EEwsConnection *cnc,
		    gboolean is_drafts_folder,
                    GSList *created_items,
		    GHashTable *updating_summary_uids,
		    CamelFolderChangeInfo *change_info,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelEwsStore *ews_store;
	GSList *items = NULL, *l;
	GSList *generic_item_ids = NULL, *msg_ids = NULL, *post_item_ids = NULL;
	GError *local_error = NULL;

	ews_store = CAMEL_EWS_STORE (camel_folder_get_parent_store (CAMEL_FOLDER (ews_folder)));

	for (l = created_items; l != NULL; l = g_slist_next (l)) {
		EEwsItem *item = (EEwsItem *) l->data;
		const EwsId *id;
		EEwsItemType item_type;

		if (!item)
			continue;

		id = e_ews_item_get_id (item);
		item_type = e_ews_item_get_item_type (item);

		if (!id) {
			g_warning ("%s: Missing ItemId for item type %d (subject:%s)", G_STRFUNC, item_type,
				e_ews_item_get_subject (item) ? e_ews_item_get_subject (item) : "???");
			g_object_unref (item);
			continue;
		}

		if (updating_summary_uids) {
			const gchar *pooled_uid = camel_pstring_strdup (id->id);
			gboolean known;

			known = g_hash_table_remove (updating_summary_uids, pooled_uid);

			camel_pstring_free (pooled_uid);

			if (known) {
				g_object_unref (item);
				continue;
			}
		}

		/* created_msg_ids are items other than generic item. We fetch them
		 * separately since the property sets vary */
		/* FIXME: Do we need to handle any other item types
		 * "specially"? */
		if (item_type == E_EWS_ITEM_TYPE_MESSAGE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_REQUEST ||
			item_type == E_EWS_ITEM_TYPE_MEETING_MESSAGE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_RESPONSE ||
			item_type == E_EWS_ITEM_TYPE_MEETING_CANCELLATION)
			msg_ids = g_slist_append (msg_ids, g_strdup (id->id));
		else if (item_type == E_EWS_ITEM_TYPE_POST_ITEM)
			post_item_ids = g_slist_append (post_item_ids, g_strdup (id->id));
		else if (item_type == E_EWS_ITEM_TYPE_GENERIC_ITEM)
			generic_item_ids = g_slist_append (generic_item_ids, g_strdup (id->id));

		g_object_unref (item);
	}
	g_slist_free (created_items);


	if (msg_ids) {
		EEwsAdditionalProps *add_props;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup (e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2013) ? SUMMARY_MESSAGE_PROPS_2013 : SUMMARY_MESSAGE_PROPS);
		add_props->extended_furis = ews_folder_get_summary_message_mapi_flags ();

		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			msg_ids, "IdOnly", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_ANY, &items, NULL, NULL,
			cancellable, &local_error);

		e_ews_additional_props_free (add_props);
	}

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		goto exit;
	}

	camel_ews_utils_sync_created_items (ews_folder, cnc, is_drafts_folder, items, change_info, cancellable);
	items = NULL;


	if (post_item_ids) {
		EEwsAdditionalProps *add_props;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup (SUMMARY_POSTITEM_PROPS);
		add_props->extended_furis = ews_folder_get_summary_followup_mapi_flags ();

		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			post_item_ids, "IdOnly", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_ANY, &items, NULL, NULL,
			cancellable, &local_error);

		e_ews_additional_props_free (add_props);
	}

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		goto exit;
	}

	camel_ews_utils_sync_created_items (ews_folder, cnc, is_drafts_folder, items, change_info, cancellable);
	items = NULL;

	if (generic_item_ids) {
		EEwsAdditionalProps *add_props;

		add_props = e_ews_additional_props_new ();
		add_props->field_uri = g_strdup (SUMMARY_ITEM_PROPS);
		add_props->extended_furis = ews_folder_get_summary_followup_mapi_flags ();

		e_ews_connection_get_items_sync (
			cnc, EWS_PRIORITY_MEDIUM,
			generic_item_ids, "IdOnly", add_props,
			FALSE, NULL, E_EWS_BODY_TYPE_ANY, &items, NULL, NULL,
			cancellable, &local_error);

		e_ews_additional_props_free (add_props);
	}

	camel_ews_utils_sync_created_items (ews_folder, cnc, is_drafts_folder, items, change_info, cancellable);

	if (local_error) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
	}

exit:
	if (msg_ids) {
		g_slist_foreach (msg_ids, (GFunc) g_free, NULL);
		g_slist_free (msg_ids);
	}

	if (post_item_ids) {
		g_slist_foreach (post_item_ids, (GFunc) g_free, NULL);
		g_slist_free (post_item_ids);
	}

	if (generic_item_ids) {
		g_slist_foreach (generic_item_ids, (GFunc) g_free, NULL);
		g_slist_free (generic_item_ids);
	}
}

static void
ews_folder_forget_all_mails (CamelEwsFolder *ews_folder)
{
	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
	CamelFolderSummary *folder_summary;
	GPtrArray *known_uids;
	gint ii;

	g_return_if_fail (CAMEL_IS_EWS_FOLDER (ews_folder));

	folder = CAMEL_FOLDER (ews_folder);
	g_return_if_fail (folder != NULL);

	known_uids = camel_folder_summary_dup_uids (camel_folder_get_folder_summary (folder));
	if (!known_uids)
		return;

	changes = camel_folder_change_info_new ();
	folder_summary = camel_folder_get_folder_summary (folder);

	camel_folder_summary_lock (folder_summary);
	for (ii = 0; ii < known_uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (known_uids, ii);

		camel_folder_change_info_remove_uid (changes, uid);
		camel_folder_summary_remove_uid (folder_summary, uid);
		ews_data_cache_remove (ews_folder->cache, "cur", uid, NULL);
	}
	camel_folder_summary_unlock (folder_summary);

	if (camel_folder_change_info_changed (changes)) {
		camel_folder_summary_touch (folder_summary);
		camel_folder_summary_save (folder_summary, NULL);
		camel_folder_changed (folder, changes);
	}

	camel_folder_change_info_free (changes);
	g_ptr_array_unref (known_uids);
}

static gboolean
ews_refresh_info_sync (CamelFolder *folder,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelFolderChangeInfo *change_info;
	CamelFolderSummary *folder_summary;
	CamelEwsFolder *ews_folder;
	CamelEwsFolderPrivate *priv;
	GHashTable *updating_summary_uids = NULL;
	EEwsConnection *cnc;
	CamelEwsStore *ews_store;
	CamelEwsSettings *settings;
	const gchar *full_name;
	gchar *id;
	gchar *sync_state;
	gboolean includes_last_item = FALSE;
	gboolean is_drafts_folder;
	gint64 last_folder_update_time;
	GError *local_error = NULL;

	full_name = camel_folder_get_full_name (folder);
	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	ews_folder = (CamelEwsFolder *) folder;
	priv = ews_folder->priv;

	id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary, full_name);

	if (g_strcmp0 (id, EWS_FOREIGN_FOLDER_ROOT_ID) == 0 ||
	    g_strcmp0 (id, EWS_PUBLIC_FOLDER_ROOT_ID) == 0) {
		g_free (id);
		return TRUE;
	} else {
		gchar *parent_fid;

		parent_fid = camel_ews_store_summary_get_parent_folder_id (ews_store->summary, id, NULL);

		if (g_strcmp0 (parent_fid, EWS_FOREIGN_FOLDER_ROOT_ID) == 0) {
			g_free (parent_fid);
			g_free (id);
			return TRUE;
		}

		g_free (parent_fid);
	}

	if (!camel_ews_store_connected (ews_store, cancellable, error)) {
		g_free (id);
		return FALSE;
	}

	g_mutex_lock (&priv->state_lock);

	if (priv->refreshing) {
		g_free (id);
		g_mutex_unlock (&priv->state_lock);
		return TRUE;
	}

	priv->refreshing = TRUE;
	g_mutex_unlock (&priv->state_lock);

	cnc = camel_ews_store_ref_connection (ews_store);
	if (!cnc) {
		g_free (id);
		g_warn_if_reached ();
		return FALSE;
	}

	folder_summary = camel_folder_get_folder_summary (folder);
	camel_folder_summary_prepare_fetch_all (folder_summary, NULL);

	change_info = camel_folder_change_info_new ();
	last_folder_update_time = g_get_monotonic_time ();

	camel_operation_push_message (cancellable, _("Refreshing folder “%s”"), camel_folder_get_display_name (folder));

	is_drafts_folder = camel_ews_utils_folder_is_drafts_folder (ews_folder);

	/* Sync folder items does not return the fields ToRecipients,
	 * CCRecipients. With the item_type unknown, it's not possible
	 * to fetch the right properties which are valid for an item type.
	 * Due to these reasons we just get the item ids and its type in
	 * SyncFolderItem request and fetch the item using the
	 * GetItem request. */

	settings = e_ews_connection_ref_settings (cnc);

	if (settings && camel_ews_summary_get_sync_tag_stamp (CAMEL_EWS_SUMMARY (folder_summary)) != camel_ews_settings_get_sync_tag_stamp (settings))
		sync_state = NULL;
	else
		sync_state = camel_ews_summary_dup_sync_state (CAMEL_EWS_SUMMARY (folder_summary));

	if (!sync_state ||
	    camel_ews_summary_get_version (CAMEL_EWS_SUMMARY (folder_summary)) < CAMEL_EWS_SUMMARY_VERSION) {
		updating_summary_uids = camel_folder_summary_get_hash (folder_summary);
	}

	do {
		GSList *items_created = NULL, *items_updated = NULL;
		GSList *items_deleted = NULL;
		gchar *new_sync_state = NULL;
		guint32 total, unread;

		e_ews_connection_sync_folder_items_sync (cnc, EWS_PRIORITY_MEDIUM, sync_state, id, "IdOnly", NULL, EWS_MAX_FETCH_COUNT,
			&new_sync_state, &includes_last_item, &items_created, &items_updated, &items_deleted,
			cancellable, &local_error);

		g_free (sync_state);
		sync_state = new_sync_state;

		if (g_error_matches (local_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_INVALIDSYNCSTATEDATA)) {
			g_clear_error (&local_error);
			camel_ews_summary_set_sync_state (CAMEL_EWS_SUMMARY (folder_summary), NULL);
			g_free (sync_state);
			sync_state = NULL;
			ews_folder_forget_all_mails (ews_folder);
			if (updating_summary_uids) {
				g_hash_table_destroy (updating_summary_uids);
				updating_summary_uids = NULL;
			}

			e_ews_connection_sync_folder_items_sync (cnc, EWS_PRIORITY_MEDIUM, NULL, id, "IdOnly", NULL, EWS_MAX_FETCH_COUNT,
				&sync_state, &includes_last_item, &items_created, &items_updated, &items_deleted,
				cancellable, &local_error);
		}

		if (local_error) {
			camel_ews_store_maybe_disconnect (ews_store, local_error);
			break;
		}

		if (items_deleted)
			camel_ews_utils_sync_deleted_items (ews_folder, items_deleted, change_info);

		if (items_created)
			sync_created_items (ews_folder, cnc, is_drafts_folder, items_created, updating_summary_uids, change_info, cancellable, &local_error);

		if (local_error) {
			if (items_updated) {
				g_slist_foreach (items_updated, (GFunc) g_object_unref, NULL);
				g_slist_free (items_updated);
			}

			break;
		}

		if (items_updated)
			sync_updated_items (ews_folder, cnc, is_drafts_folder, items_updated, change_info, cancellable, &local_error);

		if (local_error)
			break;

		total = camel_folder_summary_count (folder_summary);
		unread = camel_folder_summary_get_unread_count (folder_summary);

		camel_ews_store_summary_set_folder_total (ews_store->summary, id, total);
		camel_ews_store_summary_set_folder_unread (ews_store->summary, id, unread);
		camel_ews_store_summary_save (ews_store->summary, NULL);

		camel_ews_summary_set_sync_state (CAMEL_EWS_SUMMARY (folder_summary), sync_state);
		if (settings)
			camel_ews_summary_set_sync_tag_stamp (CAMEL_EWS_SUMMARY (folder_summary), camel_ews_settings_get_sync_tag_stamp (settings));

		camel_folder_summary_touch (folder_summary);

		if (camel_folder_change_info_changed (change_info)) {
			camel_folder_summary_save (folder_summary, NULL);
			/* Notify any listeners only once per 10 seconds, as such notify can cause UI update */
			if (g_get_monotonic_time () - last_folder_update_time >= 10 * G_USEC_PER_SEC) {
				last_folder_update_time = g_get_monotonic_time ();
				camel_folder_changed (folder, change_info);
				camel_folder_change_info_clear (change_info);
			}
		}
	} while (!local_error && !includes_last_item && !g_cancellable_is_cancelled (cancellable));

	if (updating_summary_uids) {
		if (!local_error && !g_cancellable_is_cancelled (cancellable) &&
		    g_hash_table_size (updating_summary_uids) > 0) {
			GHashTableIter iter;
			gpointer key;
			GPtrArray *removed_uids = g_ptr_array_sized_new (g_hash_table_size (updating_summary_uids));

			g_hash_table_iter_init (&iter, updating_summary_uids);
			while (g_hash_table_iter_next (&iter, &key, NULL)) {
				const gchar *uid = key;

				camel_folder_change_info_remove_uid (change_info, uid);
				ews_data_cache_remove (ews_folder->cache, "cur", uid, NULL);

				g_ptr_array_add (removed_uids, (gpointer) uid);
			}

			camel_folder_summary_remove_uids (folder_summary, removed_uids);

			g_ptr_array_unref (removed_uids);
		}

		g_hash_table_destroy (updating_summary_uids);
		updating_summary_uids = NULL;
	}

	camel_operation_pop_message (cancellable);

	if (camel_folder_change_info_changed (change_info)) {
		camel_folder_summary_touch (folder_summary);
		camel_folder_summary_save (folder_summary, NULL);
		camel_folder_changed (folder, change_info);
	} else {
		camel_folder_summary_save (folder_summary, NULL);
	}

	camel_folder_change_info_free (change_info);

	if (local_error)
		g_propagate_error (error, local_error);

	g_mutex_lock (&priv->state_lock);
	priv->refreshing = FALSE;
	g_mutex_unlock (&priv->state_lock);

	g_clear_object (&settings);
	g_object_unref (cnc);
	g_free (sync_state);
	g_free (id);

	return !local_error;
}

static gboolean
ews_append_message_sync (CamelFolder *folder,
                         CamelMimeMessage *message,
                         CamelMessageInfo *info,
                         gchar **appended_uid,
                         GCancellable *cancellable,
                         GError **error)
{
	gchar *itemid, *changekey;
	const gchar *folder_name;
	gchar *folder_id;
	EwsFolderId *fid;
	CamelAddress *from;
	CamelEwsStore *ews_store;
	EEwsConnection *cnc;
	GError *local_error = NULL;

	ews_store = (CamelEwsStore *) camel_folder_get_parent_store (folder);

	if (!camel_ews_store_connected (ews_store, cancellable, error)) {
		return FALSE;
	}

	folder_name = camel_folder_get_full_name (folder);
	folder_id = camel_ews_store_summary_get_folder_id_from_name (
		ews_store->summary,
		folder_name);
	if (!folder_id)
		return FALSE;

	from = CAMEL_ADDRESS (camel_mime_message_get_from (message));

	cnc = camel_ews_store_ref_connection (ews_store);

	if (!cnc) {
		g_free (folder_id);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot perform actions on the folder while in offline mode"));
		return FALSE;
	}

	fid = e_ews_folder_id_new (folder_id, NULL, FALSE);
	if (!camel_ews_utils_create_mime_message (
		cnc, "SaveOnly", fid, message,
		info, from, NULL, &itemid, &changekey,
		cancellable, &local_error)) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);
		e_ews_folder_id_free (fid);
		g_free (folder_id);
		g_object_unref (cnc);
		return FALSE;
	}
	e_ews_folder_id_free (fid);
	g_free (folder_id);

	if (camel_ews_summary_add_message (camel_folder_get_folder_summary (folder), itemid, changekey, info, message)) {
		CamelFolderChangeInfo *changes;

		changes = camel_folder_change_info_new ();

		camel_folder_change_info_add_uid (changes, itemid);

		if (camel_folder_change_info_changed (changes))
			camel_folder_changed (folder, changes);

		camel_folder_change_info_free (changes);
	}

	if (appended_uid)
		*appended_uid = itemid;
	else
		g_free (itemid);
	g_free (changekey);

	g_object_unref (cnc);

	return TRUE;
}

/* move messages */
static gboolean
ews_transfer_messages_to_sync (CamelFolder *source,
                               GPtrArray *uids,
                               CamelFolder *destination,
                               gboolean delete_originals,
                               GPtrArray **transferred_uids,
                               GCancellable *cancellable,
                               GError **error)
{
	EEwsConnection *cnc;
	CamelEwsStore *dst_ews_store;
	const gchar *dst_full_name;
	gchar *dst_id;
	GError *local_error = NULL;
	GSList *ids = NULL, *ret_items = NULL, *mi_list = NULL;
	gint i = 0, mi_list_len = 0;
	gboolean success = TRUE;

	dst_full_name = camel_folder_get_full_name (destination);
	dst_ews_store = (CamelEwsStore *) camel_folder_get_parent_store (destination);

	if (!camel_ews_store_connected (dst_ews_store, cancellable, error))
		return FALSE;

	cnc = camel_ews_store_ref_connection (dst_ews_store);
	dst_id = camel_ews_store_summary_get_folder_id_from_name (
		dst_ews_store->summary, dst_full_name);

	for (i = 0; success && i < uids->len; i++) {
		guint32 flags_set;
		CamelMessageInfo *mi;

		ids = g_slist_prepend (ids, (gchar *) uids->pdata[i]);

		mi = camel_folder_summary_get (camel_folder_get_folder_summary (source), uids->pdata[i]);
		if (!mi)
			continue;

		flags_set = camel_message_info_get_flags (mi);

		/* Exchange doesn't seem to have a sane representation
		 * for most flags — not even replied/forwarded. */
		if ((flags_set & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0) {
			mi_list = g_slist_prepend (mi_list, mi);
			mi_list_len++;
		} else {
			g_clear_object (&mi);
		}

		if (mi_list_len == EWS_MAX_FETCH_COUNT) {
			success = ews_save_flags (source, mi_list, cancellable, &local_error);
			g_slist_free_full (mi_list, g_object_unref);
			mi_list = NULL;
			mi_list_len = 0;
		}
	}

	if (mi_list != NULL && success)
		success = ews_save_flags (source, mi_list, cancellable, &local_error);
	g_slist_free_full (mi_list, g_object_unref);

	ids = g_slist_reverse (ids);

	success = success && e_ews_connection_move_items_in_chunks_sync (
		cnc, EWS_PRIORITY_MEDIUM,
		dst_id, !delete_originals,
		ids, &ret_items,
		cancellable, &local_error);

	/* Messages could be copied/moved partially only, like when
	   the user cancels the operation in the middle */
	if (success || ret_items) {
		CamelFolderChangeInfo *changes;
		GSList *l, *processed_items = NULL;

		changes = camel_folder_change_info_new ();

		for (l = ret_items, i = 0; l != NULL; l = l->next, i++) {
			CamelMimeMessage *message;
			CamelStream *stream;
			CamelMessageInfo *info;
			CamelMessageInfo *clone;
			const EwsId *id;

			if (e_ews_item_get_item_type (l->data) == E_EWS_ITEM_TYPE_ERROR) {
				if (!local_error)
					local_error = g_error_copy (e_ews_item_get_error (l->data));
				continue;
			}

			id = e_ews_item_get_id (l->data);
			processed_items = g_slist_prepend (processed_items, uids->pdata[i]);

			message = ews_folder_get_message_cached (source, uids->pdata[i], cancellable);
			if (message == NULL)
				continue;

			stream = ews_data_cache_add (
				CAMEL_EWS_FOLDER (destination)->cache, "cur", id->id, NULL);
			if (stream == NULL) {
				g_object_unref (message);

				continue;
			}

			camel_data_wrapper_write_to_stream_sync (
				CAMEL_DATA_WRAPPER (message), stream, cancellable, NULL);

			info = camel_folder_summary_get (camel_folder_get_folder_summary (source), uids->pdata[i]);
			if (info == NULL) {
				g_object_unref (stream);
				g_object_unref (message);

				continue;
			}

			clone = camel_message_info_clone (info, NULL);

			if (camel_ews_summary_add_message (camel_folder_get_folder_summary (destination), id->id, id->change_key, clone, message))
				camel_folder_change_info_add_uid (changes, id->id);

			g_clear_object (&clone);
			g_clear_object (&info);
			g_object_unref (stream);
			g_object_unref (message);
		}

		if (camel_folder_change_info_changed (changes))
			camel_folder_changed (destination, changes);

		camel_folder_change_info_free (changes);

		if (delete_originals) {
			changes = camel_folder_change_info_new ();

			for (l = processed_items; l; l = g_slist_next (l)) {
				const gchar *uid = l->data;

				camel_folder_summary_remove_uid (camel_folder_get_folder_summary (source), uid);
				camel_folder_change_info_remove_uid (changes, uid);
				ews_data_cache_remove (CAMEL_EWS_FOLDER (source)->cache, "cur", uid, NULL);
			}
			if (camel_folder_change_info_changed (changes)) {
				camel_folder_summary_touch (camel_folder_get_folder_summary (source));
				camel_folder_changed (source, changes);
			}

			camel_folder_change_info_free (changes);
		}

		/* update destination folder only if not frozen, to not update
		   for each single message transfer during filtering
		 */
		if (!camel_folder_is_frozen (destination)) {
			camel_operation_progress (cancellable, -1);

			ews_refresh_info_sync (destination, cancellable, NULL);
		}

		g_slist_free (processed_items);
	}
	g_free (dst_id);

	if (local_error) {
		camel_ews_store_maybe_disconnect (dst_ews_store, local_error);
		g_propagate_error (error, local_error);
	}

	g_object_unref (cnc);
	g_slist_free (ids);
	g_slist_free_full (ret_items, g_object_unref);

	return !local_error;
}

static void
ews_prepare_content_refresh (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_EWS_FOLDER (folder));

	camel_ews_summary_set_sync_state (CAMEL_EWS_SUMMARY (camel_folder_get_folder_summary (folder)), NULL);
}

static gboolean
ews_delete_messages_from_server (CamelEwsStore *ews_store,
				 const GSList *deleted_items,
				 EwsDeleteType delete_type,
				 GCancellable *cancellable,
				 GError **error)
{
	EEwsConnection *cnc;
	gboolean ret;

	cnc = camel_ews_store_ref_connection (ews_store);

	ret = e_ews_connection_delete_items_in_chunks_sync (
		cnc, EWS_PRIORITY_MEDIUM, deleted_items, delete_type,
		EWS_SEND_TO_NONE, EWS_NONE_OCCURRENCES, cancellable, error);

	g_object_unref (cnc);

	return ret;
}

static void
ews_delete_messages_from_folder (CamelFolder *folder,
				 const GSList *deleted_items)
{
	CamelFolderChangeInfo *changes;
	CamelFolderSummary *folder_summary;
	const GSList *iter;

	changes = camel_folder_change_info_new ();
	folder_summary = camel_folder_get_folder_summary (folder);

	for (iter = deleted_items; iter != NULL; iter = iter->next) {
		const gchar *uid = iter->data;

		camel_folder_summary_lock (folder_summary);
		camel_folder_change_info_remove_uid (changes, uid);
		camel_folder_summary_remove_uid (folder_summary, uid);
		ews_data_cache_remove (CAMEL_EWS_FOLDER (folder)->cache, "cur", uid, NULL);
		camel_folder_summary_unlock (folder_summary);
	}

	if (camel_folder_change_info_changed (changes)) {
		camel_folder_summary_touch (folder_summary);
		camel_folder_changed (folder, changes);
	}

	camel_folder_change_info_free (changes);
}

static gboolean
ews_delete_messages (CamelFolder *folder,
                     const GSList *deleted_items,
                     gboolean expunge,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelStore *parent_store;
	CamelEwsStore *ews_store;
	GError *local_error = NULL;

	if (deleted_items == NULL)
		return TRUE;

	parent_store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	if (!camel_ews_store_connected (ews_store, cancellable, error))
		return FALSE;

	if (!expunge)
		expunge = ews_folder_is_public_or_foreign (folder);

	ews_delete_messages_from_server (
		ews_store,
		deleted_items,
		expunge ? EWS_HARD_DELETE : EWS_MOVE_TO_DELETED_ITEMS,
		cancellable,
		&local_error);

	if (local_error != NULL && local_error->code == EWS_CONNECTION_ERROR_ITEMNOTFOUND) {
			/* If delete failed due to the item not found, ignore the error,
			 * trigger folder info refresh and then go on to clear the
			 * cache of the deleted items anyway. */
			g_clear_error (&local_error);
			ews_refresh_info_sync (folder, cancellable, &local_error);
	}

	if (local_error != NULL) {
		camel_ews_store_maybe_disconnect (ews_store, local_error);
		g_propagate_error (error, local_error);

		return FALSE;
	}

	ews_delete_messages_from_folder (folder, deleted_items);

	return TRUE;
}

static gboolean
ews_expunge_deleted_items_sync (CamelFolder *folder,
				CamelEwsStore *ews_store,
				gboolean *delete_items,
				GCancellable *cancellable,
				GError **error)
{
	EEwsConnection *cnc;
	CamelFolderInfo *folder_info = NULL;
	CamelFolderInfo *to_delete;
	gchar *trash_id;
	gchar *folder_name;
	gboolean ret = FALSE;
	gboolean expunge_deleted_items_at_once = FALSE;

	cnc = camel_ews_store_ref_connection (ews_store);
	trash_id = camel_ews_store_summary_get_folder_id_from_folder_type (ews_store->summary, CAMEL_FOLDER_TYPE_TRASH);

	folder_name = camel_ews_store_summary_get_folder_full_name (ews_store->summary, trash_id, error);
	if (folder_name == NULL)
		goto exit;

	folder_info = camel_store_get_folder_info_sync (
		CAMEL_STORE (ews_store), folder_name,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE |
		CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
		cancellable, error);


	if (folder_info == NULL)
		goto exit;

	to_delete = folder_info->child;

	expunge_deleted_items_at_once = e_ews_connection_satisfies_server_version (cnc, E_EWS_EXCHANGE_2010);

	if (expunge_deleted_items_at_once) {
		ret = e_ews_connection_empty_folder_sync (
			cnc,
			EWS_PRIORITY_MEDIUM,
			trash_id,
			FALSE,
			"HardDelete",
			TRUE,
			cancellable,
			error);

		if (!ret)
			goto exit;
	} else {
		/*
		 * As we cannot delete the "Deleted Items" folder itself, we have to walk throught its first
		 * level subfolders and delete each folder one by one from the server,
		 */
		while (to_delete != NULL) {
			gchar *fid;

			fid = camel_ews_store_summary_get_folder_id_from_name (
				ews_store->summary, to_delete->full_name);

			ret = e_ews_connection_delete_folder_sync (
				cnc,
				EWS_PRIORITY_MEDIUM,
				fid,
				FALSE,
				"HardDelete",
				cancellable,
				error);

			g_free (fid);

			if (!ret)
				goto exit;

			to_delete = to_delete->next;
		}
	}

	to_delete = folder_info->child;
	ret = camel_ews_utils_delete_folders_from_summary_recursive (ews_store, to_delete, TRUE, error);

exit:
	camel_folder_info_free (folder_info);
	g_free (folder_name);
	g_free (trash_id);
	g_object_unref (cnc);

	*delete_items = !expunge_deleted_items_at_once;

	return ret;
}

static gboolean
ews_expunge_sync (CamelFolder *folder,
                  GCancellable *cancellable,
                  GError **error)
{
	CamelStore *parent_store;
	GSList *deleted_items = NULL;
	gint i;
	gboolean is_trash;
	gboolean delete_items_from_server = TRUE;
	gboolean ret;
	GPtrArray *known_uids;
	GError *local_error = NULL;

	parent_store = camel_folder_get_parent_store (folder);

	if (!camel_ews_store_connected (CAMEL_EWS_STORE (parent_store), cancellable, error))
		return FALSE;

	/*
	 * If the user is expunging the trash folder, we should delete
	 * all messages, subfolders and subfolders' messages inside the
	 * trash folder.
	 */
	is_trash = ews_folder_is_of_type (folder, CAMEL_FOLDER_TYPE_TRASH);

	camel_folder_summary_prepare_fetch_all (camel_folder_get_folder_summary (folder), NULL);
	known_uids = camel_folder_summary_dup_uids (camel_folder_get_folder_summary (folder));

	if (known_uids == NULL)
		return TRUE;

	if (is_trash) {
		ews_expunge_deleted_items_sync (
			folder,
			CAMEL_EWS_STORE (parent_store),
			&delete_items_from_server,
			cancellable,
			&local_error);

		if (local_error != NULL) {
			camel_ews_store_maybe_disconnect (CAMEL_EWS_STORE (parent_store), local_error);
			g_propagate_error (error, local_error);

			g_ptr_array_unref (known_uids);

			return FALSE;
		}
	}

	for (i = 0; i < known_uids->len; i++) {
		CamelMessageInfo *info;
		const gchar *uid = g_ptr_array_index (known_uids, i);

		info = camel_folder_summary_get (camel_folder_get_folder_summary (folder), uid);

		if (info && (is_trash || (camel_message_info_get_flags (info) & CAMEL_MESSAGE_DELETED) != 0))
			deleted_items = g_slist_prepend (deleted_items, (gpointer) camel_pstring_strdup (uid));

		g_clear_object (&info);
	}

	if (is_trash && !delete_items_from_server) {
		ews_delete_messages_from_folder (folder, deleted_items);
		ret = TRUE;
	} else {
		ret = ews_delete_messages (folder, deleted_items, TRUE, cancellable, error);
	}

	g_slist_free_full (deleted_items, (GDestroyNotify) camel_pstring_free);
	g_ptr_array_unref (known_uids);

	return ret;
}

static gint
ews_cmp_uids (CamelFolder *folder,
              const gchar *uid1,
              const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return strcmp (uid1, uid2);
}

static gboolean
ews_search_body_sync (CamelFolder *folder,
		      /* const */ GPtrArray *words, /* gchar * */
		      GPtrArray **out_uids, /* gchar * */
		      GCancellable *cancellable,
		      GError **error)
{
	CamelStore *parent_store;
	CamelEwsFolder *ews_folder;
	CamelEwsStore *ews_store;
	gboolean success = FALSE;

	ews_folder = CAMEL_EWS_FOLDER (folder);

	/* Sanity check. */
	g_return_val_if_fail (ews_folder != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (folder);
	ews_store = CAMEL_EWS_STORE (parent_store);

	if (!camel_ews_store_connected (ews_store, cancellable, error))
		return FALSE;

	if (ews_folder != NULL) {
		EEwsConnection *connection = NULL;
		gchar *folder_id = NULL;
		gboolean can_search;

		/* there should always be one, held by one of the callers of this function */
		g_warn_if_fail (ews_store != NULL);

		can_search = ews_store != NULL && words != NULL;

		if (can_search) {
			folder_id = camel_ews_store_summary_get_folder_id_from_name (ews_store->summary,
				camel_folder_get_full_name (CAMEL_FOLDER (ews_folder)));
			if (!folder_id)
				can_search = FALSE;
		}

		if (can_search) {
			connection = camel_ews_store_ref_connection (ews_store);
			if (!connection)
				can_search = FALSE;
		}

		if (can_search) {
			EwsFolderId *fid;
			GSList *found_items = NULL;
			gboolean includes_last_item = FALSE;
			GString *expression;
			guint ii;

			fid = e_ews_folder_id_new (folder_id, NULL, FALSE);
			expression = g_string_new ("");

			if (words->len >= 2)
				g_string_append (expression, "(and ");

			for (ii = 0; ii < words->len; ii++) {
				GString *word;

				word = e_ews_common_utils_str_replace_string (g_ptr_array_index (words, ii), "\"", "\\\"");

				g_string_append (expression, "(body-contains \"");
				g_string_append (expression, word->str);
				g_string_append (expression, "\")");

				g_string_free (word, TRUE);
			}

			/* Close the 'and' */
			if (words->len >= 2)
				g_string_append_c (expression, ')');

			success = e_ews_connection_find_folder_items_sync (
				connection, EWS_PRIORITY_MEDIUM,
				fid, "IdOnly", NULL, NULL, expression->str, NULL,
				E_EWS_FOLDER_TYPE_MAILBOX, &includes_last_item, &found_items,
				e_ews_query_to_restriction,
				cancellable, error);
			if (success) {
				GPtrArray *matches = NULL;
				const GSList *link;

				for (link = found_items; link; link = g_slist_next (link)) {
					EEwsItem *item = link->data;
					const EwsId *id;

					if (!item || e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR)
						continue;

					id = e_ews_item_get_id (item);
					if (!id || !id->id)
						continue;

					if (!matches)
						matches = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);

					g_ptr_array_add (matches, (gpointer) camel_pstring_strdup (id->id));
				}

				*out_uids = matches;
			}

			g_slist_free_full (found_items, g_object_unref);
			g_string_free (expression, TRUE);
			e_ews_folder_id_free (fid);
		}

		g_clear_object (&connection);
		g_free (folder_id);
	}

	return success;
}

static void
ews_folder_set_property (GObject *object,
			 guint property_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_APPLY_FILTERS:
			camel_ews_folder_set_apply_filters (
				CAMEL_EWS_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_CHECK_FOLDER:
			camel_ews_folder_set_check_folder (
				CAMEL_EWS_FOLDER (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_folder_get_property (GObject *object,
			 guint property_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_APPLY_FILTERS:
			g_value_set_boolean (
				value,
				camel_ews_folder_get_apply_filters (
				CAMEL_EWS_FOLDER (object)));
			return;

		case PROP_CHECK_FOLDER:
			g_value_set_boolean (
				value,
				camel_ews_folder_get_check_folder (
				CAMEL_EWS_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ews_folder_dispose (GObject *object)
{
	CamelEwsFolder *ews_folder = CAMEL_EWS_FOLDER (object);
	CamelFolderSummary *summary;

	summary = camel_folder_get_folder_summary (CAMEL_FOLDER (ews_folder));
	if (summary) {
		g_signal_handlers_disconnect_by_func (summary, G_CALLBACK (ews_folder_count_notify_cb), ews_folder);

		/* save changes, if there are any unsaved */
		camel_folder_summary_save (summary, NULL);
	}

	if (ews_folder->cache != NULL) {
		g_object_unref (ews_folder->cache);
		ews_folder->cache = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_ews_folder_parent_class)->dispose (object);
}

static void
ews_folder_finalize (GObject *object)
{
	CamelEwsFolder *ews_folder;

	ews_folder = CAMEL_EWS_FOLDER (object);

	g_mutex_clear (&ews_folder->priv->state_lock);
	g_rec_mutex_clear (&ews_folder->priv->cache_lock);
	g_hash_table_destroy (ews_folder->priv->fetching_uids);
	g_cond_clear (&ews_folder->priv->fetch_cond);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_ews_folder_parent_class)->finalize (object);
}

static void
ews_folder_constructed (GObject *object)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelService *service;
	CamelFolder *folder;
	const gchar *full_name;
	gchar *description;
	gchar *host;
	gchar *user;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_ews_folder_parent_class)->constructed (object);

	folder = CAMEL_FOLDER (object);
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_ews_utils_get_host_name (settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	description = g_strdup_printf (
		"%s@%s:%s", user, host, full_name);
	camel_folder_set_description (folder, description);
	g_free (description);

	g_free (host);
	g_free (user);
}

static void
camel_ews_folder_class_init (CamelEwsFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = ews_folder_set_property;
	object_class->get_property = ews_folder_get_property;
	object_class->dispose = ews_folder_dispose;
	object_class->finalize = ews_folder_finalize;
	object_class->constructed = ews_folder_constructed;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->get_permanent_flags = ews_folder_get_permanent_flags;
	folder_class->get_message_sync = ews_folder_get_message_sync;
	folder_class->get_message_cached = ews_folder_get_message_cached;
	folder_class->cmp_uids = ews_cmp_uids;
	folder_class->append_message_sync = ews_append_message_sync;
	folder_class->refresh_info_sync = ews_refresh_info_sync;
	folder_class->synchronize_sync = ews_synchronize_sync;
	folder_class->expunge_sync = ews_expunge_sync;
	folder_class->transfer_messages_to_sync = ews_transfer_messages_to_sync;
	folder_class->prepare_content_refresh = ews_prepare_content_refresh;
	folder_class->get_filename = ews_get_filename;
	folder_class->search_body_sync = ews_search_body_sync;

	camel_folder_class_map_legacy_property (folder_class, "apply-filters", 0x2501);
	camel_folder_class_map_legacy_property (folder_class, "check-folder", 0x2502);

	g_object_class_install_property (
		object_class,
		PROP_APPLY_FILTERS,
		g_param_spec_boolean (
			"apply-filters",
			"Apply Filters",
			_("Apply message _filters to this folder"),
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			CAMEL_FOLDER_PARAM_PERSISTENT));

	g_object_class_install_property (
		object_class,
		PROP_CHECK_FOLDER,
		g_param_spec_boolean (
			"check-folder",
			"Check Folder",
			_("Always check for _new mail in this folder"),
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			CAMEL_FOLDER_PARAM_PERSISTENT));
}

static void
camel_ews_folder_init (CamelEwsFolder *ews_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (ews_folder);

	ews_folder->priv = camel_ews_folder_get_instance_private (ews_folder);

	camel_folder_set_flags (folder, CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY);

	g_mutex_init (&ews_folder->priv->state_lock);
	g_rec_mutex_init (&ews_folder->priv->cache_lock);

	ews_folder->priv->refreshing = FALSE;

	g_cond_init (&ews_folder->priv->fetch_cond);
	ews_folder->priv->fetching_uids = g_hash_table_new (g_str_hash, g_str_equal);
	camel_folder_set_lock_async (folder, TRUE);
}

static void
camel_ews_folder_update_flags (CamelEwsFolder *self)
{
	CamelFolder *folder = CAMEL_FOLDER (self);
	CamelSettings *settings;
	CamelStore *store;
	gboolean filter_inbox = FALSE, filter_junk = FALSE, filter_junk_inbox = FALSE;
	guint32 flags;

	store = camel_folder_get_parent_store (folder);
	if (!store)
		return;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	g_object_get (
		settings,
		"filter-inbox", &filter_inbox,
		"filter-junk", &filter_junk,
		"filter-junk-inbox", &filter_junk_inbox,
		NULL);

	g_clear_object (&settings);

	flags = camel_folder_get_flags (folder) & (~(CAMEL_FOLDER_FILTER_RECENT | CAMEL_FOLDER_FILTER_JUNK));

	if (!g_ascii_strcasecmp (camel_folder_get_full_name (folder), "Inbox") ||
	    folder_has_inbox_type (CAMEL_EWS_STORE (store), camel_folder_get_full_name (folder))) {
		if (filter_inbox)
			flags |= CAMEL_FOLDER_FILTER_RECENT;

		if (filter_junk)
			flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else {
		if (camel_ews_folder_get_apply_filters (self))
			flags |= CAMEL_FOLDER_FILTER_RECENT;

		if (filter_junk && !filter_junk_inbox)
			flags |= CAMEL_FOLDER_FILTER_JUNK;
	}

	camel_folder_set_flags (folder, flags);
}

gboolean
camel_ews_folder_get_apply_filters (CamelEwsFolder *self)
{
	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (self), FALSE);

	return self->priv->apply_filters;
}

void
camel_ews_folder_set_apply_filters (CamelEwsFolder *self,
				    gboolean apply_filters)
{
	g_return_if_fail (CAMEL_IS_EWS_FOLDER (self));

	if ((self->priv->apply_filters ? 1 : 0) == (apply_filters ? 1 : 0))
		return;

	self->priv->apply_filters = apply_filters;

	g_object_notify (G_OBJECT (self), "apply-filters");
	camel_ews_folder_update_flags (self);
}

gboolean
camel_ews_folder_get_check_folder (CamelEwsFolder *self)
{
	g_return_val_if_fail (CAMEL_IS_EWS_FOLDER (self), FALSE);

	return self->priv->check_folder;
}

void
camel_ews_folder_set_check_folder (CamelEwsFolder *self,
				   gboolean check_folder)
{
	g_return_if_fail (CAMEL_IS_EWS_FOLDER (self));

	if ((self->priv->check_folder ? 1 : 0) == (check_folder ? 1 : 0))
		return;

	self->priv->check_folder = check_folder;

	g_object_notify (G_OBJECT (self), "check-folder");
	camel_ews_folder_update_flags (self);
}
