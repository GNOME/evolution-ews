/*
 * SPDX-FileCopyrightText: (C) 1999-2011 Novell, Inc. (www.novell.com)
 * SPDX-FileContributor: Chenthill Palanisamy <pchenthill@novell.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#undef EDS_DISABLE_DEPRECATED

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>

#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>

#include "ews-oab-decoder.h"
#include "ews-oab-props.h"

struct _db_data {
	GSList *contact_collector;
	GSList *sha1_collector;
	guint collected_length;
	EBookSqlite *summary;
	const gchar *folderid;
};

static void
ews_test_store_contact (EContact *contact,
                        goffset offset,
			const gchar *sha1,
                        guint percent,
                        gpointer user_data,
			GCancellable *cancellable,
                        GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;

	data->contact_collector = g_slist_prepend (data->contact_collector, g_object_ref (contact));
	data->sha1_collector = g_slist_prepend (data->sha1_collector, g_strdup (sha1));
	data->collected_length += 1;

	if (data->collected_length == 1000 || percent >= 100) {
		data->contact_collector = g_slist_reverse (data->contact_collector);
		e_book_sqlite_add_contacts (data->summary, data->contact_collector,
					    data->sha1_collector,
					    FALSE, NULL, error);
		g_print ("percent complete %d \n", percent);

		g_slist_free_full (data->contact_collector, g_object_unref);
		g_slist_free_full (data->sha1_collector, g_free);
		data->contact_collector = NULL;
		data->sha1_collector = NULL;
		data->collected_length = 0;
	}
}

gint
main (gint argc,
      gchar *argv[])
{
	EBookSqlite *summary;
	EwsOabDecoder *eod;
	GError *err = NULL;
	GTimer *timer;
	struct _db_data data;

	g_type_init ();

	if (argc != 3) {
		g_print ("Pass the oab filename  and cache dir as argument \n");
		return -1;
	}

	summary = e_book_sqlite_new (argv[2], NULL, NULL, &err);
	if (err) {
		printf("err: %s\n", err->message);
		exit(1);
	}
	eod = ews_oab_decoder_new (argv[1], argv[2], &err);
	if (err) {
		printf("err: %s\n", err->message);
		exit(1);
	}

	data.sha1_collector = data.contact_collector = NULL;
	data.collected_length = 0;
	data.summary = summary;
	data.folderid = "de";

	timer = g_timer_new ();
	g_timer_start (timer);
	if (!ews_oab_decoder_decode (eod, NULL, ews_test_store_contact, &data, NULL, &err)) {
		g_print ("Unable to decode %s \n", err->message);
	}
	g_timer_stop (timer);
	g_print ("Time elapsed %lf \n", g_timer_elapsed (timer, NULL));

	if (err)
		g_clear_error (&err);

	g_object_unref (eod);
	g_object_unref (summary);

	return 0;
}

