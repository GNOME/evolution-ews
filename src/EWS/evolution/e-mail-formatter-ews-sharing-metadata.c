/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>
#include <em-format/e-mail-formatter-extension.h>
#include <em-format/e-mail-formatter.h>
#include <em-format/e-mail-part-utils.h>

#include "e-mail-part-ews-sharing-metadata.h"
#include "e-mail-formatter-ews-sharing-metadata.h"

typedef EMailFormatterExtension EMailFormatterEwsSharingMetadata;
typedef EMailFormatterExtensionClass EMailFormatterEwsSharingMetadataClass;

GType e_mail_formatter_ews_sharing_metadata_get_type (void);

G_DEFINE_DYNAMIC_TYPE (EMailFormatterEwsSharingMetadata, e_mail_formatter_ews_sharing_metadata, E_TYPE_MAIL_FORMATTER_EXTENSION)

static const gchar *formatter_mime_types[] = {
	"application/x-sharing-metadata-xml",
	NULL
};

static gboolean
emf_ews_sharing_metadata (const gchar *xml_str,
			  gchar **out_datatype,
			  gchar **out_initiator_name,
			  gchar **out_initiator_email,
			  gchar **out_folder_entry_id)
{
	xmlDoc *xml;
	xmlXPathContext *xpath;

	if (!xml_str || !*xml_str)
		return FALSE;

	xml = e_xml_parse_data (xml_str, strlen (xml_str));

	if (!xml)
		return FALSE;

	xpath = e_xml_new_xpath_context_with_namespaces (xml,
		"s", "http://schemas.microsoft.com/sharing/2008",
		"e", "http://schemas.microsoft.com/exchange/sharing/2008",
		NULL);

	*out_datatype = e_xml_xpath_eval_as_string (xpath, "/s:SharingMessage/s:DataType");
	*out_initiator_name = e_xml_xpath_eval_as_string (xpath, "/s:SharingMessage/s:Initiator/s:Name");
	*out_initiator_email = e_xml_xpath_eval_as_string (xpath, "/s:SharingMessage/s:Initiator/s:SmtpAddress");
	*out_folder_entry_id = e_xml_xpath_eval_as_string (xpath, "/s:SharingMessage/s:Invitation/s:Providers/s:Provider/e:FolderId");

	xmlXPathFreeContext (xpath);
	xmlFreeDoc (xml);

	return *out_datatype && **out_datatype &&
		*out_initiator_name && **out_initiator_name &&
		*out_initiator_email && **out_initiator_email &&
		*out_folder_entry_id && **out_folder_entry_id;
}

static gboolean
emf_ews_sharing_metadata_format (EMailFormatterExtension *extension,
				 EMailFormatter *formatter,
				 EMailFormatterContext *context,
				 EMailPart *part,
				 GOutputStream *stream,
				 GCancellable *cancellable)
{
	EMailPartEwsSharingMetadata *sharing_part;
	gboolean handled = TRUE;
	GString *buffer = NULL;

	if (!E_IS_MAIL_PART_EWS_SHARING_METADATA (part))
		return FALSE;

	sharing_part = E_MAIL_PART_EWS_SHARING_METADATA (part);

	if (context->mode == E_MAIL_FORMATTER_MODE_PRINTING ||
	    context->mode == E_MAIL_FORMATTER_MODE_RAW) {
		gchar *datatype = NULL, *initiator_name = NULL, *initiator_email = NULL, *folder_entry_id = NULL;

		if (emf_ews_sharing_metadata (sharing_part->xml, &datatype, &initiator_name, &initiator_email, &folder_entry_id)) {
			ENamedParameters *params;
			gchar *params_str, *info;

			params = e_named_parameters_new ();

			e_named_parameters_set (params, "email", initiator_email);
			e_named_parameters_set (params, "folder_id", folder_entry_id);

			params_str = e_named_parameters_to_string (params);

			e_named_parameters_free (params);

			buffer = g_string_sized_new (2048);

			g_string_append (buffer, e_mail_formatter_get_sub_html_header (formatter));
			/* No need for body margins within <iframe> */
			g_string_append (buffer, "<style>body{ margin: 0; }</style>");

			if (g_strcmp0 (datatype, "calendar") == 0) {
				/* Translators: the first %s is replaced with a user name, the second %s is replaced with an email address of that user */
				info = g_strdup_printf (_("%s (%s) has invited you to view his or her Microsoft Exchange calendar."), initiator_name, initiator_email);
			} else {
				/* Translators: the first %s is replaced with a user name, the second %s is replaced with an email address of that user */
				info = g_strdup_printf (_("%s (%s) has invited you to view his or her Microsoft Exchange folder."), initiator_name, initiator_email);
			}

			e_util_markup_append_escaped (buffer,
				"<div class=\"part-container -e-web-view-background-color -e-web-view-text-color\" style=\"border: none; padding: 8px; margin: 0;\">%s<br>"
				"<br>"
				"%s<br>"
				"<br>"
				"<button type=\"button\" class=\"ews-sharing-metadata-btn\" id=\"ews-sharing-metadata-btn\" value=\"%s\">%s</button></div></body></html>",
				info,
				_("Click the Subscribe button to add it to Evolution."),
				params_str,
				_("Subscribe"));

			g_free (params_str);
			g_free (info);
		} else {
			CamelMimePart *err_part;
			EMailPart *mail_part;
			const gchar *msg, *mime_type = "application/vnd.evolution.error";

			msg = _("Failed to extract sharing information from provided data.");

			err_part = camel_mime_part_new ();
			camel_mime_part_set_content (err_part, msg, strlen (msg), mime_type);

			mail_part = e_mail_part_new (err_part, e_mail_part_get_id (part));

			handled = e_mail_formatter_format_as (formatter, context, mail_part, stream, mime_type, cancellable);

			g_object_unref (mail_part);
			g_object_unref (err_part);
		}

		g_free (datatype);
		g_free (initiator_name);
		g_free (initiator_email);
		g_free (folder_entry_id);
	} else {
		const gchar *default_charset, *charset;
		gchar *uri;

		default_charset = e_mail_formatter_get_default_charset (formatter);
		charset = e_mail_formatter_get_charset (formatter);

		if (!default_charset)
			default_charset = "";
		if (!charset)
			charset = "";

		uri = e_mail_part_build_uri (
			e_mail_part_list_get_folder (context->part_list),
			e_mail_part_list_get_message_uid (context->part_list),
			"part_id", G_TYPE_STRING, e_mail_part_get_id (part),
			"mode", G_TYPE_INT, E_MAIL_FORMATTER_MODE_RAW,
			"formatter_default_charset", G_TYPE_STRING, default_charset,
			"formatter_charset", G_TYPE_STRING, charset,
			NULL);

		buffer = g_string_sized_new (256);

		g_string_append_printf (buffer,
			"<div class=\"part-container-nostyle\" >"
			"<iframe width=\"100%%\" height=\"10\""
			" id=\"%s\" name=\"%s\" "
			" frameborder=\"0\" src=\"%s\" "
			" class=\"-e-mail-formatter-frame-color %s"
			" -e-web-view-text-color\" >"
			"</iframe>"
			"</div>",
			e_mail_part_get_id (part),
			e_mail_part_get_id (part),
			uri,
			e_mail_part_get_frame_security_style (part));

		g_free (uri);
	}

	if (buffer) {
		g_output_stream_write_all (stream, buffer->str, buffer->len, NULL, cancellable, NULL);

		g_string_free (buffer, TRUE);
	}

	return handled;
}

static void
e_mail_formatter_ews_sharing_metadata_class_init (EMailFormatterExtensionClass *klass)
{
	klass->display_name = _("EWS Sharing Metadata");
	klass->description = _("Display part as EWS sharing metadata");
	klass->mime_types = formatter_mime_types;
	klass->format = emf_ews_sharing_metadata_format;
}

static void
e_mail_formatter_ews_sharing_metadata_class_finalize (EMailFormatterExtensionClass *klass)
{
}

static void
e_mail_formatter_ews_sharing_metadata_init (EMailFormatterExtension *extension)
{
}

void
e_mail_formatter_ews_sharing_metadata_type_register (GTypeModule *type_module)
{
	e_mail_formatter_ews_sharing_metadata_register_type (type_module);
}
