/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <em-format/e-mail-parser.h>
#include <em-format/e-mail-parser-extension.h>

#include "e-mail-parser-ews-sharing-metadata.h"
#include "e-mail-parser-ews-multipart-mixed.h"

typedef EMailParserExtension EMailParserEwsMultipartMixed;
typedef EMailParserExtensionClass EMailParserEwsMultipartMixedClass;

GType e_mail_parser_ews_multipart_mixed_get_type (void);

G_DEFINE_DYNAMIC_TYPE (EMailParserEwsMultipartMixed, e_mail_parser_ews_multipart_mixed, E_TYPE_MAIL_PARSER_EXTENSION)

static const gchar *parser_mime_types[] = {
	"multipart/mixed",
	NULL
};

static gboolean
emp_ews_mp_mixed_parse (EMailParserExtension *extension,
			EMailParser *parser,
			CamelMimePart *part,
			GString *part_id,
			GCancellable *cancellable,
			GQueue *out_mail_parts)
{
	CamelMultipart *mp;
	gboolean handled = FALSE;

	/* Allow this only in an EWS folder, because the EWS connection is needed */
	if (!e_mail_parser_ews_sharing_metadata_is_ews_folder (parser, cancellable))
		return FALSE;

	mp = (CamelMultipart *) camel_medium_get_content (CAMEL_MEDIUM (part));

	if (CAMEL_IS_MULTIPART (mp)) {
		CamelMimePart *sharing_subpart = NULL;
		gint ii, nparts, ntexts = 0, nsharings = 0;

		nparts = camel_multipart_get_number (mp);

		for (ii = 0; ii < nparts; ii++) {
			CamelMimePart *subpart;
			CamelContentType *ct;

			subpart = camel_multipart_get_part (mp, ii);
			ct = camel_mime_part_get_content_type (subpart);

			if (ct && (camel_content_type_is (ct, "text", "plain") || camel_content_type_is (ct, "text", "html"))) {
				ntexts++;
			} else if (ct && camel_content_type_is (ct, "application", "x-sharing-metadata-xml")) {
				sharing_subpart = subpart;
				nsharings++;
			}
		}

		/* Hide the text parts only if there is only a single sharing subpart */
		if (nsharings == 1 && nsharings + ntexts == nparts) {
			gint len;

			len = part_id->len;

			g_string_append_printf (part_id, ".mixed.ews-sharing");
			handled = e_mail_parser_parse_part (parser, sharing_subpart, part_id, cancellable, out_mail_parts);
			g_string_truncate (part_id, len);
		}
	}

	return handled;
}

static void
e_mail_parser_ews_multipart_mixed_class_init (EMailParserExtensionClass *klass)
{
	klass->mime_types = parser_mime_types;
	klass->priority = G_PRIORITY_LOW - 1;
	klass->flags = E_MAIL_PARSER_EXTENSION_COMPOUND_TYPE;
	klass->parse = emp_ews_mp_mixed_parse;
}

static void
e_mail_parser_ews_multipart_mixed_class_finalize (EMailParserExtensionClass *klass)
{
}

static void
e_mail_parser_ews_multipart_mixed_init (EMailParserExtension *extension)
{
}

void
e_mail_parser_ews_multipart_mixed_type_register (GTypeModule *type_module)
{
	e_mail_parser_ews_multipart_mixed_register_type (type_module);
}
