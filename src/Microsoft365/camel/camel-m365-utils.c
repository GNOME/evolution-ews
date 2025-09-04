/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include "camel-m365-utils.h"

/* Unref with g_object_unref() when done with it */
static ESource *
camel_m365_utils_ref_corresponding_source (CamelService *service,
					   GCancellable *cancellable)
{
	ESourceRegistry *registry;
	ESource *source = NULL;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	registry = e_source_registry_new_sync (cancellable, NULL);

	if (registry) {
		source = e_source_registry_ref_source (registry, camel_service_get_uid (service));

		if (source) {
			ESource *parent;

			parent = e_source_registry_find_extension (registry, source, E_SOURCE_EXTENSION_COLLECTION);

			g_clear_object (&source);
			source = parent;
		}
	}

	g_clear_object (&registry);

	return source;
}

EM365Connection *
camel_m365_utils_new_connection (CamelService *service,
				 GCancellable *cancellable)
{
	CamelSettings *settings;
	EM365Connection *cnc;
	ESource *source;

	g_return_val_if_fail (CAMEL_IS_SERVICE (service), NULL);

	source = camel_m365_utils_ref_corresponding_source (service, cancellable);

	if (!source)
		return NULL;

	settings = camel_service_ref_settings (service);

	cnc = e_m365_connection_new (source, CAMEL_M365_SETTINGS (settings));

	e_binding_bind_property (
		service, "proxy-resolver",
		cnc, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	g_clear_object (&settings);
	g_clear_object (&source);

	return cnc;
}

/* From Outlook name (which allows spaces) to Evolution name */
gchar *
camel_m365_utils_encode_category_name (const gchar *name)
{
	if (name && strchr (name, ' ')) {
		GString *str;

		str = g_string_sized_new (strlen (name) + 16);

		while (*name) {
			if (*name == '_')
				g_string_append_c (str, '_');

			g_string_append_c (str, *name == ' ' ? '_' : *name);

			name++;
		}

		return g_string_free (str, FALSE);
	}

	return g_strdup (name);
}

/* From Evolution name to Outlook name (which allows spaces) */
gchar *
camel_m365_utils_decode_category_name (const gchar *flag)
{
	if (flag && strchr (flag, '_')) {
		GString *str = g_string_sized_new (strlen (flag));

		while (*flag) {
			if (*flag == '_') {
				if (flag[1] == '_') {
					g_string_append_c (str, '_');
					flag++;
				} else {
					g_string_append_c (str, ' ');
				}
			} else {
				g_string_append_c (str, *flag);
			}

			flag++;
		}

		return g_string_free (str, FALSE);
	}

	return g_strdup (flag);
}

gboolean
camel_m365_utils_is_system_user_flag (const gchar *name)
{
	if (!name)
		return FALSE;

	return g_str_equal (name, "receipt-handled") ||
		g_str_equal (name, "$has-cal");
}

const gchar *
camel_m365_utils_rename_label (const gchar *cat,
			       gboolean from_cat)
{
	gint ii;

	/* This is a mapping from Outlook categories to
	 * Evolution labels based on the standard colors */
	const gchar *labels[] = {
		"Red category", "$Labelimportant",
		"Orange category", "$Labelwork",
		"Green category", "$Labelpersonal",
		"Blue category", "$Labeltodo",
		"Purple category", "$Labellater",
		NULL, NULL
	};

	if (!cat || !*cat)
		return "";

	for (ii = 0; labels[ii]; ii += 2) {
		if (from_cat) {
			if (!g_ascii_strcasecmp (cat, labels[ii]))
				return labels[ii + 1];
		} else {
			if (!g_ascii_strcasecmp (cat, labels[ii + 1]))
				return labels[ii];
		}
	}

	return cat;
}

static void
m365_utils_add_address (JsonBuilder *builder,
			CamelInternetAddress *addr,
			void (* add_func) (JsonBuilder *builder,
					   const gchar *name,
					   const gchar *address))
{
	const gchar *name = NULL, *address = NULL;

	g_return_if_fail (add_func != NULL);

	if (!addr || camel_address_length (CAMEL_ADDRESS (addr)) < 1)
		return;

	if (camel_internet_address_get (addr, 0, &name, &address))
		add_func (builder, name, address);
}

static void
m365_utils_add_address_array (JsonBuilder *builder,
			      CamelInternetAddress *addr,
			      void (* begin_func) (JsonBuilder *builder),
			      void (* end_func) (JsonBuilder *builder),
			      GHashTable *known_recipients,
			      CamelAddress *expected_recipients)
{
	gint ii, len;
	gboolean did_add = FALSE;

	g_return_if_fail (begin_func != NULL);
	g_return_if_fail (end_func != NULL);

	if (!addr)
		return;

	len = camel_address_length (CAMEL_ADDRESS (addr));

	for (ii = 0; ii < len; ii++) {
		const gchar *name = NULL, *address = NULL;

		if (camel_internet_address_get (addr, ii, &name, &address)) {
			if (!did_add) {
				did_add = TRUE;
				begin_func (builder);
			}

			if (known_recipients && address && *address)
				g_hash_table_add (known_recipients, (gpointer) address);

			e_m365_add_recipient (builder, NULL, name, address);
		}
	}

	if (known_recipients && expected_recipients && CAMEL_IS_INTERNET_ADDRESS (expected_recipients)) {
		CamelInternetAddress *iaddr = CAMEL_INTERNET_ADDRESS (expected_recipients);

		len = camel_address_length (expected_recipients);

		for (ii = 0; ii < len; ii++) {
			const gchar *name = NULL, *address = NULL;

			if (camel_internet_address_get (iaddr, ii, &name, &address) && address && *address &&
			    !g_hash_table_contains (known_recipients, address)) {
				if (!did_add) {
					did_add = TRUE;
					begin_func (builder);
				}

				if (known_recipients && address && *address)
					g_hash_table_add (known_recipients, (gpointer) address);

				e_m365_add_recipient (builder, NULL, name, address);
			}
		}
	}

	if (did_add)
		end_func (builder);
}

static void
m365_utils_add_headers (JsonBuilder *builder,
			const CamelNameValueArray *headers,
			CamelInternetAddress **out_sender,
			gboolean *out_request_read_receipt)
{
	guint ii, len;
	gint did_add = 0;

	if (!headers)
		return;

	len = camel_name_value_array_get_length (headers);

	for (ii = 0; ii < len; ii++) {
		const gchar *name = NULL, *value = NULL;

		if (camel_name_value_array_get (headers, ii, &name, &value) && name && *name && value &&
		    g_ascii_strcasecmp (name, "X-Evolution-Source") != 0) {
			/* The Graph API allows only X- headers to be saved */
			if (g_ascii_strncasecmp (name, "X-", 2) == 0) {
				if (!did_add)
					e_m365_mail_message_begin_internet_message_headers (builder);

				did_add++;

				/* Preserve only the first five... (see the comment at m365_folder_append_message_sync()) */
				if (did_add < 5)
					e_m365_add_internet_message_header (builder, name, value);
			}

			if (out_sender && g_ascii_strcasecmp (name, "Sender") == 0) {
				CamelInternetAddress *addr;

				addr = camel_internet_address_new ();

				if (camel_address_decode (CAMEL_ADDRESS (addr), value) != -1) {
					*out_sender = addr;
					addr = NULL;
				}

				g_clear_object (&addr);

				/* To not compare the header name again */
				out_sender = NULL;
			} else if (out_request_read_receipt && g_ascii_strcasecmp (name, "Disposition-Notification-To") == 0) {
				*out_request_read_receipt = TRUE;

				/* To not compare the header name again */
				out_request_read_receipt = NULL;
			}
		}
	}

	if (did_add)
		e_m365_mail_message_end_internet_message_headers (builder);
}

static CamelStream *
m365_utils_get_content_stream (CamelMimePart *part,
			       gssize *out_wrote_bytes,
			       GCancellable *cancellable)
{
	CamelStream *content_stream;
	CamelStream *filter_stream = NULL;
	CamelMimeFilterWindows *windows = NULL;
	CamelDataWrapper *dw;
	gssize wrote_bytes;

	g_return_val_if_fail (part != NULL, NULL);

	dw = camel_medium_get_content (CAMEL_MEDIUM (part));
	g_return_val_if_fail (dw != NULL, NULL);

	content_stream = camel_stream_mem_new ();

	if (camel_mime_part_get_content_type (part)) {
		const gchar *charset = camel_content_type_param (camel_mime_part_get_content_type (part), "charset");

		if (charset && *charset && g_ascii_strcasecmp (charset, "utf8") != 0 && g_ascii_strcasecmp (charset, "utf-8") != 0) {
			if (g_ascii_strncasecmp (charset, "iso-8859-", 9) == 0) {
				CamelStream *null;

				/* Since a few Windows mailers like to claim they sent
				 * out iso-8859-# encoded text when they really sent
				 * out windows-cp125#, do some simple sanity checking
				 * before we move on... */

				null = camel_stream_null_new ();
				filter_stream = camel_stream_filter_new (null);
				g_object_unref (null);

				windows = (CamelMimeFilterWindows *)camel_mime_filter_windows_new (charset);
				camel_stream_filter_add (CAMEL_STREAM_FILTER (filter_stream), CAMEL_MIME_FILTER (windows));

				camel_data_wrapper_decode_to_stream_sync (dw, CAMEL_STREAM (filter_stream), cancellable, NULL);
				camel_stream_flush (CAMEL_STREAM (filter_stream), cancellable, NULL);
				g_object_unref (filter_stream);

				charset = camel_mime_filter_windows_real_charset (windows);
			}

			if (charset && *charset) {
				CamelMimeFilter *filter;

				filter_stream = camel_stream_filter_new (content_stream);

				if ((filter = camel_mime_filter_charset_new (charset, "UTF-8"))) {
					camel_stream_filter_add (CAMEL_STREAM_FILTER (filter_stream), CAMEL_MIME_FILTER (filter));
					g_object_unref (filter);
				} else {
					g_object_unref (filter_stream);
					filter_stream = NULL;
				}
			}
		}
	}

	if (filter_stream) {
		wrote_bytes = camel_data_wrapper_decode_to_stream_sync (dw, CAMEL_STREAM (filter_stream), cancellable, NULL);
		camel_stream_flush (filter_stream, cancellable, NULL);
		g_object_unref (filter_stream);
	} else {
		wrote_bytes = camel_data_wrapper_decode_to_stream_sync (dw, CAMEL_STREAM (content_stream), cancellable, NULL);
	}

	if (windows)
		g_object_unref (windows);

	g_seekable_seek (G_SEEKABLE (content_stream), 0, G_SEEK_SET, NULL, NULL);

	if (out_wrote_bytes)
		*out_wrote_bytes = wrote_bytes;

	return content_stream;
}

static gboolean
m365_utils_part_is_attachment (CamelMimePart *part,
			       gboolean *out_is_inline)
{
	const CamelContentDisposition *content_disposition;

	g_return_val_if_fail (CAMEL_IS_MIME_PART (part), FALSE);

	content_disposition = camel_mime_part_get_content_disposition (part);

	if (!content_disposition)
		return FALSE;

	if (out_is_inline) {
		*out_is_inline = content_disposition && content_disposition->disposition &&
				g_ascii_strcasecmp (content_disposition->disposition, "inline") == 0;
	}

	return content_disposition &&
		content_disposition->disposition && (
		g_ascii_strcasecmp (content_disposition->disposition, "attachment") == 0 ||
		g_ascii_strcasecmp (content_disposition->disposition, "inline") == 0);
}

enum {
	ADD_ATTACHMENT_WITH_CONTENT_TYPE = 1 << 0,
	ADD_ATTACHMENT_PREFIX_CONTENT_TYPE_HEADER = 1 << 1,
	ADD_ATTACHMENT_DECODE_CONTENT = 1 << 2
};

static void
m365_utils_add_file_attachment_content (JsonBuilder *builder,
					CamelDataWrapper *dw,
					guint32 add_flags,
					GCancellable *cancellable)
{
	CamelMimeFilter *filter;
	CamelStream *content_stream, *filter_stream;
	GByteArray *data;
	CamelContentType *ct;
	gchar *content_type_str;
	const gchar *content_id;
	gboolean is_inline = FALSE;

	ct = camel_data_wrapper_get_mime_type_field (dw);
	content_type_str = camel_content_type_format (ct);

	if ((add_flags & ADD_ATTACHMENT_WITH_CONTENT_TYPE) != 0)
		e_m365_attachment_add_content_type (builder, content_type_str);

	content_stream = camel_stream_mem_new ();
	filter_stream = camel_stream_filter_new (content_stream);

	filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_ENC);
	camel_stream_filter_add (CAMEL_STREAM_FILTER (filter_stream), filter);
	g_object_unref (filter);

	if ((add_flags & ADD_ATTACHMENT_PREFIX_CONTENT_TYPE_HEADER) != 0) {
		gchar *content_type_unfolded;

		content_type_unfolded = camel_header_unfold (content_type_str);

		#define wstr(str) camel_stream_write (filter_stream, str, strlen (str), cancellable, NULL)
		wstr ("Content-Type: ");
		wstr (content_type_unfolded);
		wstr ("\r\n\r\n");
		#undef wstr

		g_free (content_type_unfolded);
	}

	g_free (content_type_str);

	if (CAMEL_IS_MIME_PART (dw)) {
		CamelMimePart *part = CAMEL_MIME_PART (dw);

		content_id = camel_mime_part_get_content_id (part);
		if (content_id)
			e_m365_file_attachment_add_content_id (builder, content_id);

		if (m365_utils_part_is_attachment (part, &is_inline) && is_inline)
			e_m365_attachment_add_is_inline (builder, TRUE);

		dw = camel_medium_get_content (CAMEL_MEDIUM (part));
	}

	if ((add_flags & ADD_ATTACHMENT_DECODE_CONTENT) != 0)
		camel_data_wrapper_decode_to_stream_sync (dw, filter_stream, cancellable, NULL);
	else
		camel_data_wrapper_write_to_stream_sync (dw, filter_stream, cancellable, NULL);
	camel_stream_flush (filter_stream, cancellable, NULL);
	g_object_unref (filter_stream);

	camel_stream_flush (content_stream, cancellable, NULL);

	data = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (content_stream));

	/* Ensure the string is NUL-terminated */
	g_byte_array_append (data, (const guchar *) "\0", 1);

	e_m365_file_attachment_add_content_bytes (builder, (const gchar *) data->data);

	g_object_unref (content_stream);
}

static void
m365_utils_add_file_attachment (JsonBuilder *builder,
				CamelDataWrapper *dw,
				GCancellable *cancellable)
{
	const gchar *filename = NULL;

	g_return_if_fail (builder != NULL);
	g_return_if_fail (dw != NULL);

	m365_utils_add_file_attachment_content (builder, dw, ADD_ATTACHMENT_WITH_CONTENT_TYPE | ADD_ATTACHMENT_DECODE_CONTENT, cancellable);

	if (CAMEL_IS_MIME_PART (dw))
		filename = camel_mime_part_get_filename (CAMEL_MIME_PART (dw));

	if (filename)
		e_m365_attachment_add_name (builder, filename);
	else
		e_m365_attachment_add_name (builder, "attachment.dat");
}

static void
m365_utils_add_smime_encrypted_attachment (JsonBuilder *builder,
					   CamelDataWrapper *dw,
					   GCancellable *cancellable)
{
	g_return_if_fail (builder != NULL);
	g_return_if_fail (dw != NULL);

	e_m365_attachment_add_name (builder, "smime.p7m");

	m365_utils_add_file_attachment_content (builder, dw, ADD_ATTACHMENT_WITH_CONTENT_TYPE | ADD_ATTACHMENT_DECODE_CONTENT, cancellable);
}

static void
m365_utils_add_smime_signed_attachment (JsonBuilder *builder,
					CamelDataWrapper *dw,
					GCancellable *cancellable)
{
	e_m365_attachment_add_content_type (builder, "multipart/signed");
	e_m365_attachment_add_name (builder, "smime.txt");

	m365_utils_add_file_attachment_content (builder, dw, ADD_ATTACHMENT_PREFIX_CONTENT_TYPE_HEADER, cancellable);
}

static gboolean
m365_utils_do_smime_signed (CamelMultipart *multipart,
			    CamelMimePart **out_body_part,
			    GSList **out_attachments,
			    GCancellable *cancellable)
{
	CamelMimePart *content, *signature;
	/*CamelContentType *ct;*/

	content = camel_multipart_get_part (multipart, CAMEL_MULTIPART_SIGNED_CONTENT);
	signature = camel_multipart_get_part (multipart, CAMEL_MULTIPART_SIGNED_SIGNATURE);

	g_return_val_if_fail (content != NULL, FALSE);
	g_return_val_if_fail (signature != NULL, FALSE);

	/*ct = camel_mime_part_get_content_type (content);

	if (camel_content_type_is (ct, "text", "plain")) {
		g_clear_object (out_body_part);
		*out_body_part = g_object_ref (content);
	} else if (camel_content_type_is (ct, "text", "html")) {
		g_clear_object (out_body_part);
		*out_body_part = g_object_ref (content);
	} else {
		*out_attachments = g_slist_prepend (*out_attachments, g_object_ref (content));
	}*/

	*out_attachments = g_slist_prepend (*out_attachments, g_object_ref (multipart));

	return TRUE;
}

static gboolean
m365_utils_do_multipart (CamelMultipart *mp,
			 gboolean *is_first,
			 CamelMimePart **out_body_part,
			 GSList **out_attachments,
			 GCancellable *cancellable)
{
	CamelDataWrapper *dw;
	CamelContentType *type;
	CamelMimePart *part;
	gboolean parent_is_alternative;
	gint nn, ii;

	g_return_val_if_fail (is_first != NULL, FALSE);

	type = camel_data_wrapper_get_mime_type_field (CAMEL_DATA_WRAPPER (mp));
	parent_is_alternative = type && camel_content_type_is (type, "multipart", "alternative");

	nn = camel_multipart_get_number (mp);

	for (ii = 0; ii < nn; ii++) {
		part = camel_multipart_get_part (mp, ii);

		if (!part)
			continue;

		dw = camel_medium_get_content (CAMEL_MEDIUM (part));

		if (CAMEL_IS_MULTIPART (dw)) {
			if (!m365_utils_do_multipart (CAMEL_MULTIPART (dw), is_first, out_body_part, out_attachments, cancellable))
				return FALSE;
			continue;
		}

		type = camel_mime_part_get_content_type (part);

		if (ii == 0 && (*is_first) && camel_content_type_is (type, "text", "plain")) {
			g_clear_object (out_body_part);
			*out_body_part = g_object_ref (part);

			*is_first = FALSE;
		} else if ((ii == 0 || parent_is_alternative) &&
			   camel_content_type_is (type, "text", "html") &&
			   !m365_utils_part_is_attachment (part, NULL)) {
			g_clear_object (out_body_part);
			*out_body_part = g_object_ref (part);
		} else {
			*out_attachments = g_slist_prepend (*out_attachments, g_object_ref (part));
		}
	}

	return TRUE;
}

static CamelMimePart *
m365_utils_get_body_part (CamelMimeMessage *message,
			  GSList **out_attachments,
			  GCancellable *cancellable)
{
	CamelContentType *ct;
	CamelMimePart *body_part = NULL;

	ct = camel_data_wrapper_get_mime_type_field (CAMEL_DATA_WRAPPER (message));
	g_return_val_if_fail (ct != NULL, NULL);

	if (camel_content_type_is (ct, "application", "x-pkcs7-mime") ||
	    camel_content_type_is (ct, "application", "pkcs7-mime")) {
		*out_attachments = g_slist_prepend (*out_attachments, g_object_ref (message));
	} else {
		CamelDataWrapper *dw = NULL;
		CamelMultipart *multipart;

		/* contents body */
		dw = camel_medium_get_content (CAMEL_MEDIUM (message));

		if (CAMEL_IS_MULTIPART (dw)) {
			gboolean is_first = TRUE;

			multipart = CAMEL_MULTIPART (dw);
			if (CAMEL_IS_MULTIPART_SIGNED (multipart) && camel_multipart_get_number (multipart) == 2) {
				m365_utils_do_smime_signed (multipart, &body_part, out_attachments, cancellable);
			} else {
				m365_utils_do_multipart (multipart, &is_first, &body_part, out_attachments, cancellable);
			}
		} else if (dw) {
			CamelContentType *type;
			CamelMimePart *part = CAMEL_MIME_PART (message);

			type = camel_data_wrapper_get_mime_type_field (dw);

			if (camel_content_type_is (type, "text", "plain")) {
				body_part = g_object_ref (part);
			} else if (camel_content_type_is (type, "text", "html")) {
				body_part = g_object_ref (part);
			} else {
				*out_attachments = g_slist_prepend (*out_attachments, g_object_ref (part));
			}
		}
	}

	*out_attachments = g_slist_reverse (*out_attachments);

	return body_part;
}

void
camel_m365_utils_add_message_flags (JsonBuilder *builder,
				    CamelMessageInfo *info,
				    CamelMimeMessage *message)
{
	const gchar *follow_up;
	guint32 flags = 0;

	if (info) {
		const CamelNamedFlags *user_flags;
		gboolean did_add = FALSE;
		guint ii, len;

		flags = camel_message_info_get_flags (info);

		user_flags = camel_message_info_get_user_flags (info);
		len = camel_named_flags_get_length (user_flags);

		for (ii = 0; ii < len; ii++) {
			const gchar *name = camel_named_flags_get (user_flags, ii);

			if (!camel_m365_utils_is_system_user_flag (name)) {
				const gchar *renamed;

				renamed = camel_m365_utils_rename_label (name, FALSE);

				if (renamed && *renamed && renamed != name) {
					if (!did_add) {
						did_add = TRUE;
						e_m365_mail_message_begin_categories (builder);
					}

					e_m365_mail_message_add_category (builder, renamed);
				} else if (renamed == name && name && *name) {
					gchar *cat;

					cat = camel_m365_utils_decode_category_name (name);

					if (cat && *cat) {
						if (!did_add) {
							did_add = TRUE;
							e_m365_mail_message_begin_categories (builder);
						}

						e_m365_mail_message_add_category (builder, cat);
					}

					g_free (cat);
				}
			}
		}

		/* unset it when no category/label found, in case there had been any set before */
		if (!did_add)
			e_m365_mail_message_begin_categories (builder);
		e_m365_mail_message_end_categories (builder);
	}

	if (message && !(flags & CAMEL_MESSAGE_FLAGGED)) {
		CamelMedium *medium = CAMEL_MEDIUM (message);
		const gchar *value;

		value = camel_medium_get_header (medium, "X-Priority");

		if (g_strcmp0 (value, "1") == 0) {
			flags |= CAMEL_MESSAGE_FLAGGED;
		} else {
			value = camel_medium_get_header (medium, "Importance");

			if (value && g_ascii_strcasecmp (value, "High") == 0)
				flags |= CAMEL_MESSAGE_FLAGGED;
		}
	}

	e_m365_mail_message_add_importance (builder,
		(flags & CAMEL_MESSAGE_FLAGGED) != 0 ? E_M365_IMPORTANCE_HIGH : E_M365_IMPORTANCE_NORMAL);

	e_m365_mail_message_add_is_read (builder, (flags & CAMEL_MESSAGE_SEEN) != 0);

	follow_up = camel_message_info_get_user_tag (info, "follow-up");
	if (follow_up && !*follow_up)
		follow_up = NULL;

	e_m365_mail_message_begin_flag (builder);

	if (follow_up) {
		const gchar *completed, *due_by, *follow_up_start;
		time_t completed_tt = (time_t) 0 , due_by_tt = (time_t) 0, start_tt = (time_t) 0;

		completed = camel_message_info_get_user_tag (info, "completed-on");
		due_by = camel_message_info_get_user_tag (info, "due-by");
		follow_up_start = camel_message_info_get_user_tag (info, "follow-up-start");

		if (completed && *completed)
			completed_tt = camel_header_decode_date (completed, NULL);

		if (due_by && *due_by)
			due_by_tt = camel_header_decode_date (due_by, NULL);

		if (follow_up_start && *follow_up_start)
			start_tt = camel_header_decode_date (follow_up_start, NULL);

		if (!start_tt)
			start_tt = time (NULL);

		e_m365_followup_flag_add_flag_status (builder, completed_tt != (time_t) 0 ?
			E_M365_FOLLOWUP_FLAG_STATUS_COMPLETE : E_M365_FOLLOWUP_FLAG_STATUS_FLAGGED);

		if (completed_tt != (time_t) 0) {
			e_m365_followup_flag_add_completed_date_time (builder, completed_tt, NULL);
		} else if (due_by_tt != (time_t) 0) {
			e_m365_followup_flag_add_start_date_time (builder, start_tt, NULL);
				e_m365_followup_flag_add_due_date_time (builder, due_by_tt, NULL);
		}

		if (!follow_up_start || !*follow_up_start) {
			gchar *text = camel_header_format_date (start_tt, 0);
			camel_message_info_set_user_tag (info, "follow-up-start", text);
			g_free (text);
		}
	} else {
		e_m365_followup_flag_add_flag_status (builder, E_M365_FOLLOWUP_FLAG_STATUS_NOT_FLAGGED);
	}

	e_m365_mail_message_end_flag (builder);
}

static void
m365_utils_add_attachment_object (JsonBuilder *builder,
				  CamelDataWrapper *dw,
				  GCancellable *cancellable)
{
	CamelContentType *ct;

	ct = camel_data_wrapper_get_mime_type_field (dw);

	e_m365_attachment_begin_attachment (builder, E_M365_ATTACHMENT_DATA_TYPE_FILE);

	if (camel_content_type_is (ct, "application", "x-pkcs7-mime") ||
	    camel_content_type_is (ct, "application", "pkcs7-mime")) {
		m365_utils_add_smime_encrypted_attachment (builder, dw, cancellable);
	} else if (CAMEL_IS_MULTIPART_SIGNED (dw)) {
		m365_utils_add_smime_signed_attachment (builder, dw, cancellable);
	} else {
		m365_utils_add_file_attachment (builder, dw, cancellable);
	}

	e_m365_json_end_object_member (builder);
}

gboolean
camel_m365_utils_fill_message_object_sync (JsonBuilder *builder,
					   CamelMimeMessage *message,
					   CamelMessageInfo *info,
					   CamelAddress *override_from,
					   CamelAddress *override_recipients, /* it merges them, not really override */
					   gboolean is_send,
					   GSList **out_attachments,
					   GCancellable *cancellable,
					   GError **error)
{
	CamelInternetAddress *addr, *sender = NULL;
	CamelMimePart *body_part;
	GHashTable *known_recipients = NULL;
	GSList *attachments = NULL;
	time_t tt;
	gint offset = 0;
	const gchar *tmp;
	gboolean success = TRUE, request_read_receipt = FALSE;

	g_return_val_if_fail (builder != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);

	tmp = camel_mime_message_get_message_id (message);
	if (tmp && *tmp)
		e_m365_mail_message_add_internet_message_id (builder, tmp);

	tmp = camel_mime_message_get_subject (message);
	e_m365_mail_message_add_subject (builder, tmp ? tmp : "");

	tt = camel_mime_message_get_date (message, &offset);

	if (tt > (time_t) 0) {
		/* Convert to UTC */
		tt += (offset / 100) * 60 * 60;
		tt += (offset % 100) * 60;

		e_m365_mail_message_add_sent_date_time (builder, tt);
	}

	offset = 0;
	tt = camel_mime_message_get_date_received (message, &offset);

	if (tt > (time_t) 0) {
		/* Convert to UTC */
		tt += (offset / 100) * 60 * 60;
		tt += (offset % 100) * 60;

		e_m365_mail_message_add_received_date_time (builder, tt);
	}

	if (override_recipients)
		known_recipients = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);

	if (override_from && CAMEL_IS_INTERNET_ADDRESS (override_from))
		addr = CAMEL_INTERNET_ADDRESS (override_from);
	else
		addr = camel_mime_message_get_from (message);
	m365_utils_add_address (builder, addr, e_m365_mail_message_add_from);

	addr = camel_mime_message_get_reply_to (message);
	m365_utils_add_address_array (builder, addr, e_m365_mail_message_begin_reply_to, e_m365_mail_message_end_reply_to, NULL, NULL);

	addr = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	m365_utils_add_address_array (builder, addr, e_m365_mail_message_begin_to_recipients, e_m365_mail_message_end_to_recipients, known_recipients, NULL);

	addr = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	m365_utils_add_address_array (builder, addr, e_m365_mail_message_begin_cc_recipients, e_m365_mail_message_end_cc_recipients, known_recipients, NULL);

	addr = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_BCC);
	m365_utils_add_address_array (builder, addr, e_m365_mail_message_begin_bcc_recipients, e_m365_mail_message_end_bcc_recipients, known_recipients, override_recipients);

	if (known_recipients) {
		g_hash_table_destroy (known_recipients);
		known_recipients = NULL;
	}

	m365_utils_add_headers (builder, camel_medium_get_headers (CAMEL_MEDIUM (message)), &sender, &request_read_receipt);

	if (sender) {
		const gchar *name = NULL, *address = NULL;

		if (camel_internet_address_get (sender, 0, &name, &address) && ((name && *name) || (address && *address)))
			e_m365_mail_message_add_sender (builder, name, address);

		g_clear_object (&sender);
	} else if (override_from) {
		/* Possibly force the Sender when the passed-in From doesn't match the account user address */
	}

	if (request_read_receipt)
		e_m365_mail_message_add_is_read_receipt_requested (builder, TRUE);

	body_part = m365_utils_get_body_part (message, &attachments, cancellable);

	if (body_part) {
		CamelContentType *ct;
		EM365ItemBodyContentTypeType m365_content_type = E_M365_ITEM_BODY_CONTENT_TYPE_UNKNOWN;

		ct = camel_mime_part_get_content_type (body_part);

		if (ct && camel_content_type_is (ct, "text", "html"))
			m365_content_type = E_M365_ITEM_BODY_CONTENT_TYPE_HTML;
		else if (ct && camel_content_type_is (ct, "text", "plain"))
			m365_content_type = E_M365_ITEM_BODY_CONTENT_TYPE_TEXT;

		if (m365_content_type != E_M365_ITEM_BODY_CONTENT_TYPE_UNKNOWN) {
			CamelStream *mem;
			gssize wrote = -1;

			mem = m365_utils_get_content_stream (body_part, &wrote, cancellable);

			if (mem && wrote >= 0) {
				GByteArray *byte_array;

				camel_stream_flush (mem, cancellable, NULL);

				byte_array = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (mem));

				/* Ensure the string is NUL-terminated */
				g_byte_array_append (byte_array, (const guchar *) "\0", 1);

				e_m365_mail_message_add_body (builder, m365_content_type, (const gchar *) byte_array->data);
			}

			g_clear_object (&mem);
		}

		g_object_unref (body_part);
	} else {
		e_m365_json_add_null_member (builder, "body");
	}

	if (info || is_send)
		camel_m365_utils_add_message_flags (builder, info, is_send ? message : NULL);

	if (out_attachments) {
		*out_attachments = attachments;
	} else if (attachments) {
		GSList *link;

		e_m365_json_begin_array_member (builder, "attachments");

		for (link = attachments; link && success; link = g_slist_next (link)) {
			CamelDataWrapper *dw = link->data;

			m365_utils_add_attachment_object (builder, dw, cancellable);
		}

		e_m365_json_end_array_member (builder);

		g_slist_free_full (attachments, g_object_unref);
	}

	return success;
}

gboolean
camel_m365_utils_create_message_sync (EM365Connection *cnc,
				      const gchar *folder_id,
				      CamelMimeMessage *message,
				      CamelMessageInfo *info,
				      gchar **out_appended_id,
				      GCancellable *cancellable,
				      GError **error)
{
	EM365MailMessage *appended_message = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_M365_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);

	if (!e_m365_connection_util_reencode_parts_to_base64_sync (CAMEL_MIME_PART (message), cancellable, error)) {
		g_prefix_error (error, "%s", _("Failed to re-encode parts to base64: "));
		return FALSE;
	}

	/* As of 2024-09-26, cannot upload message directly to the folder_id, because the server returns:
	   {"error":{"code":"UnableToDeserializePostBody","message":"were unable to deserialize "}}
	   thus upload to the Drafts folder and then move the message to the right place. */
	success = e_m365_connection_upload_mail_message_sync (cnc, NULL, NULL, message, &appended_message, cancellable, error);

	g_warn_if_fail ((success && appended_message) || (!success && !appended_message));

	if (success && appended_message) {
		GSList src_ids = { 0, }, *des_ids = NULL;
		const gchar *id;

		id = e_m365_mail_message_get_id (appended_message);
		g_warn_if_fail (id != NULL);

		src_ids.next = NULL;
		src_ids.data = (gpointer) id;

		/* Sadly, the isDraft flag cannot be unset, thus every uploaded message
		   is a draft for the server, which is quite bad */
		if (e_m365_connection_copy_move_mail_messages_sync (cnc, NULL, &src_ids, folder_id, FALSE, &des_ids, cancellable, error)) {
			if (des_ids) {
				if (out_appended_id)
					*out_appended_id = g_strdup ((const gchar *) des_ids->data);

				g_slist_free_full (des_ids, (GDestroyNotify) camel_pstring_free);
			} else {
				g_warning ("Moved message to '%s', but did not return new message id", folder_id);
			}
		}
	}

	if (appended_message)
		json_object_unref (appended_message);

	return success;
}
