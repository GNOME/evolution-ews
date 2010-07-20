/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef CAMEL_EWS_JOURNAL_H
#define CAMEL_EWS_JOURNAL_H

#include <stdarg.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_EWS_JOURNAL \
	(camel_ews_journal_get_type ())
#define CAMEL_EWS_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EWS_JOURNAL, CamelEwsJournal))
#define CAMEL_EWS_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EWS_JOURNAL, CamelEwsJournalClass))
#define CAMEL_IS_EWS_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EWS_JOURNAL))
#define CAMEL_IS_EWS_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EWS_JOURNAL))
#define CAMEL_EWS_JOURNAL_GET_CLASS(obj) \
	(CAMEL_CHECK_GET_CLASS \
	((obj), CAMEL_TYPE_EWS_JOURNAL, CamelEwsJournalClass))

G_BEGIN_DECLS

typedef struct _CamelEwsJournal CamelEwsJournal;
typedef struct _CamelEwsJournalClass CamelEwsJournalClass;
typedef struct _CamelEwsJournalEntry CamelEwsJournalEntry;

struct _CamelEwsFolder;

enum {
	CAMEL_EWS_JOURNAL_ENTRY_APPEND,
	CAMEL_EWS_JOURNAL_ENTRY_TRANSFER
};

struct _CamelEwsJournalEntry {
	CamelDListNode node;

	guint32 type;

	gchar *uid;
	gchar *original_uid;
	gchar *source_container;
};

struct _CamelEwsJournal {
	CamelOfflineJournal parent;

};

struct _CamelEwsJournalClass {
	CamelOfflineJournalClass parent_class;

};

GType camel_ews_journal_get_type (void);

CamelOfflineJournal *camel_ews_journal_new (struct _CamelEwsFolder *folder, const gchar *filename);

/* interfaces for adding a journal entry */
gboolean camel_ews_journal_append (CamelEwsJournal *journal, CamelMimeMessage *message, const CamelMessageInfo *mi,
				     gchar **appended_uid, GError **error);
gboolean camel_ews_journal_transfer (CamelEwsJournal *journal, CamelEwsFolder *source_folder, CamelMimeMessage *message,
				       const CamelMessageInfo *mi, const gchar *orginal_uid, gchar **transferred_uid, GError **error);

G_END_DECLS

#endif /* CAMEL_EWS_JOURNAL_H */
