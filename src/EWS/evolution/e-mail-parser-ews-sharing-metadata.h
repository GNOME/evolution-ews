/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_MAIL_PARSER_EWS_SHARING_METADATA_H
#define E_MAIL_PARSER_EWS_SHARING_METADATA_H

#include <glib-object.h>
#include <gio/gio.h>

#include <em-format/e-mail-parser.h>

G_BEGIN_DECLS

void		e_mail_parser_ews_sharing_metadata_type_register (GTypeModule *type_module);
gboolean	e_mail_parser_ews_sharing_metadata_is_ews_folder (EMailParser *parser,
								  GCancellable *operation);

G_END_DECLS

#endif /* E_MAIL_PARSER_EWS_SHARING_METADATA_H */
