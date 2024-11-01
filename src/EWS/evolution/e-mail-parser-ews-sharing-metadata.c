/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <em-format/e-mail-parser-extension.h>
#include <em-format/e-mail-part.h>

#include "camel/camel-ews-folder.h"
#include "e-mail-part-ews-sharing-metadata.h"

#include "e-mail-parser-ews-sharing-metadata.h"

typedef EMailParserExtension EMailParserEwsSharingMetadata;
typedef EMailParserExtensionClass EMailParserEwsSharingMetadataClass;

GType e_mail_parser_ews_sharing_metadata_get_type (void);

G_DEFINE_DYNAMIC_TYPE (EMailParserEwsSharingMetadata, e_mail_parser_ews_sharing_metadata, E_TYPE_MAIL_PARSER_EXTENSION)

static const gchar *parser_mime_types[] = {
	"application/x-sharing-metadata-xml",
	NULL
};

static gboolean
emp_ews_sharing_metadata_parse (EMailParserExtension *extension,
				EMailParser *parser,
				CamelMimePart *part,
				GString *part_id,
				GCancellable *cancellable,
				GQueue *out_mail_parts)
{
	EMailPart *mail_part;
	CamelDataWrapper *content;
	CamelStream *stream;
	GByteArray *byte_array;
	gchar *xml;
	gint len;

	/* Allow this only in an EWS folder, because the EWS connection is needed */
	if (!e_mail_parser_ews_sharing_metadata_is_ews_folder (parser, cancellable))
		return FALSE;

	/* This is non-gui thread. Download the part for using in the main thread */
	content = camel_medium_get_content ((CamelMedium *) part);

	byte_array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (byte_array);
	camel_data_wrapper_decode_to_stream_sync (content, stream, NULL, NULL);

	if (byte_array->len == 0)
		xml = NULL;
	else
		xml = g_strndup ((const gchar *) byte_array->data, byte_array->len);

	g_object_unref (stream);

	if (!xml)
		return FALSE;

	len = part_id->len;
	g_string_append_printf (part_id, ".ews-sharing-xml");

	mail_part = e_mail_part_ews_sharing_metadata_new (part, part_id->str);
	mail_part->force_inline = TRUE;
	e_mail_part_set_mime_type (mail_part, parser_mime_types[0]);
	E_MAIL_PART_EWS_SHARING_METADATA (mail_part)->xml = xml;

	g_queue_push_tail (out_mail_parts, mail_part);

	g_string_truncate (part_id, len);

	return TRUE;
}

static void
e_mail_parser_ews_sharing_metadata_class_init (EMailParserExtensionClass *klass)
{
	klass->mime_types = parser_mime_types;
	klass->flags = E_MAIL_PARSER_EXTENSION_INLINE_DISPOSITION;
	klass->parse = emp_ews_sharing_metadata_parse;
}

static void
e_mail_parser_ews_sharing_metadata_class_finalize (EMailParserExtensionClass *klass)
{
}

static void
e_mail_parser_ews_sharing_metadata_init (EMailParserExtension *sharind_metadata)
{
}

void
e_mail_parser_ews_sharing_metadata_type_register (GTypeModule *type_module)
{
	e_mail_parser_ews_sharing_metadata_register_type (type_module);
}

gboolean
e_mail_parser_ews_sharing_metadata_is_ews_folder (EMailParser *parser,
						  GCancellable *operation)
{
	EMailPartList *part_list;
	CamelFolder *folder;
	gboolean is_ews_folder;

	part_list = e_mail_parser_ref_part_list_for_operation (parser, operation);

	if (!part_list)
		return FALSE;

	folder = e_mail_part_list_get_folder (part_list);
	is_ews_folder = CAMEL_IS_EWS_FOLDER (folder);

	g_object_unref (part_list);

	return is_ews_folder;
}
