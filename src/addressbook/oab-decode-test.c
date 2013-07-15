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
	guint collected_length;
	EBookBackendSqliteDB *summary;
	const gchar *folderid;
};

static void
ews_test_store_contact (EContact *contact,
                        goffset offset,
                        guint percent,
                        gpointer user_data,
                        GError **error)
{
	struct _db_data *data = (struct _db_data *) user_data;

	data->contact_collector = g_slist_prepend (data->contact_collector, g_object_ref (contact));
	data->collected_length += 1;

	if (data->collected_length == 1000 || percent >= 100) {
		data->contact_collector = g_slist_reverse (data->contact_collector);
		e_book_backend_sqlitedb_add_contacts (data->summary, data->folderid, data->contact_collector, FALSE, error);
		g_print ("percent complete %d \n", percent);

		g_slist_foreach (data->contact_collector, (GFunc) g_object_unref, NULL);
		g_slist_free (data->contact_collector);
		data->contact_collector = NULL;
		data->collected_length = 0;
	}
}

gint
main (gint argc,
      gchar *argv[])
{
	EBookBackendSqliteDB *summary;
	EwsOabDecoder *eod;
	GError *err = NULL;
	GTimer *timer;
	struct _db_data data;

	g_type_init ();

	if (argc != 3) {
		g_print ("Pass the oab filename  and cache dir as argument \n");
		return -1;
	}

	summary = e_book_backend_sqlitedb_new (argv[2], "dum", "de", "dum", TRUE, NULL);
	eod = ews_oab_decoder_new (argv[1], argv[2], &err);

	data.contact_collector = NULL;
	data.collected_length = 0;
	data.summary = summary;
	data.folderid = "de";

	timer = g_timer_new ();
	g_timer_start (timer);
	if (!ews_oab_decoder_decode (eod, ews_test_store_contact, &data, NULL, &err)) {
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

