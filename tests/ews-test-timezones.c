/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileContributor: Fabiano Fidêncio <fidencio@redhat.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "calendar/e-cal-backend-ews-utils.h"
#include "common/e-ews-connection.h"
#include "common/e-ews-debug.h"

#include "ews-test-common.h"

void (* populate_windows_zones) (void);
const gchar * (* ical_to_msdn_equivalent) (const gchar *);
gboolean (* convert_calcomp_to_xml) (ESoapRequest *request,
				     gpointer user_data,
				     GError **error);
GType (* cal_backend_ews_get_type) (void);

const gchar *str_comp =
	"BEGIN:VEVENT\n"
	"UID:20140114T172626Z-2238-1000-5564-0@srv\n"
	"DTSTAMP:20140114T172620Z\n"
	"DTSTART;TZID=/freeassociation.sourceforge.net/Tzfile/ICAL_TIMEZONE:\n"
	" 20140114T160000\n"
	"DTEND;TZID=/freeassociation.sourceforge.net/Tzfile/ICAL_TIMEZONE:\n"
	" 20140114T163000\n"
	"TRANSP:OPAQUE\n"
	"SEQUENCE:2\n"
	"SUMMARY:Test\n"
	"CLASS:PUBLIC\n"
	"ORGANIZER;CN=Someone:mailto:someone@provide.com\n"
	"ATTENDEE;CUTYPE=INDIVIDUAL;ROLE=REQ-PARTICIPANT;PARTSTAT=ACCEPTED;\n"
	" RSVP=TRUE;CN=Someone;LANGUAGE=en:mailto:someone@provider.com\n"
	"END:VEVENT";

/*
 * These timezones, present in tzdata, don't have a correspondent "Windows timezone".
 * In runtime, we fallback and set the server version as 2007_SP1 in the request, which
 * doesn't treat the transitions on the server side.
 * For tests purposes, this list of known unknown timezones is enough.
 */
static const gchar *unknown_timezones[] = {
	"Australia/Lord_Howe",
	"Australia/Eucla",
	"America/Atikokan",
	"Pacific/Easter",
	"Africa/Asmara",
	"Pacific/Chuuk",
	"Pacific/Pohnpei",
	"Atlantic/Faroe",
	"Pacific/Kiritimati",
	"Pacific/Norfolk",
	"Pacific/Chatham",
	"Pacific/Marquesas",
	"Pacific/Gambier",
	"America/Miquelon",
	"Pacific/Pitcairn",
	"Asia/Gaza",
	"Asia/Hebron",
	"America/Adak",
	"America/Metlakatla",
	"Asia/Ho_Chi_Minh",
	"Antarctica/Troll",
	"America/Argentina/Buenos_Aires",
	"America/Argentina/Cordoba",
	"America/Argentina/Jujuy",
	"America/Argentina/Catamarca",
	"America/Argentina/Mendoza",
	"America/Fort_Nelson",
	"America/Punta_Arenas",
	"Asia/Famagusta",
	"Asia/Atyrau",
	"Asia/Yangon",
	"Asia/Kathmandu",
	"Pacific/Bougainville",
	"Europe/Kirov",
	"Europe/Astrakhan",
	"Europe/Saratov",
	"Europe/Ulyanovsk",
	"Asia/Barnaul",
	"Asia/Tomsk",
	"Asia/Chita",
	"Asia/Srednekolymsk",
	"America/Kentucky/Louisville",
	"America/Indiana/Indianapolis",
	"Asia/Qostanay",
	"Asia/Istanbul",
	"Atlantic/Jan_Mayen",
	"CET",
	"EET",
	"EST",
	"Etc/GMT+8",
	"Etc/GMT+9",
	"Etc/GMT-14",
	"Etc/UCT",
	"Etc/UTC",
	"Europe/Nicosia",
	"HST",
	"MET",
	"MST",
	"WET",
	NULL
};

static ICalArray *builtin_timezones = NULL;

static gboolean
is_a_known_unknown_timezone (const gchar *zone)
{
	gint i;

	for (i = 0; unknown_timezones[i] != NULL; i++) {
		if (g_strcmp0 (zone, unknown_timezones[i]) == 0)
			return TRUE;
	}

	return FALSE;
}

static void
server_notify_resolver_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	UhmServer *local_server;
	UhmResolver *resolver;
	EwsTestData *etd;
	const gchar *hostname;

	local_server = UHM_SERVER (object);
	etd = user_data;
	hostname = etd->hostname;

	/* Set up the expected domain names here. This should technically be split up between
	 * the different unit test suites, but that's too much effort. */
	resolver = uhm_server_get_resolver (local_server);

	if (resolver != NULL) {
		const gchar *ip_address = uhm_server_get_address (local_server);

		uhm_resolver_add_A (resolver, hostname, ip_address);
	}
}

static void
test_libical_timezones_compatibility (gconstpointer user_data)
{
	guint ii, nelems;

	nelems = i_cal_array_size (builtin_timezones);
	for (ii = 0; ii < nelems; ii++) {
		ICalTimezone *zone;
		const gchar *zone_location;

		zone = i_cal_timezone_array_element_at (builtin_timezones, ii);
		zone_location = i_cal_timezone_get_location (zone);

		if (ical_to_msdn_equivalent (zone_location) == NULL) {
			if (!is_a_known_unknown_timezone (zone_location)) {
				g_printerr ("\nMissing ical_tz_location: %s\n", zone_location);
			}
		}
		g_object_unref (zone);
	}
}

static void
test_time_zones_sync (gconstpointer user_data)
{
	gboolean retval = FALSE;
	guint ii, nelems;
	GError *error = NULL;
	UhmServer *local_server;
	EwsTestData *etd = (gpointer) user_data;
	EwsCalendarConvertData convert_data = { 0, };
	EwsFolderId *calendar_fid = NULL;
	ESourceRegistry *registry;
	ESource *dummy_source;
	gboolean includes_last_folder = FALSE;
	gchar *old_sync_state = NULL;
	gchar **tokens;
	GSList *zone_location_errors = NULL;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "calendar/timezones");
	ews_test_server_start_trace (local_server, etd, "get_server_time_zones_sync", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	while (!includes_last_folder) {
		GSList *folders_created = NULL;
		GSList *folders_updated = NULL;
		GSList *folders_deleted = NULL;
		GSList *l;
		gchar *new_sync_state = NULL;

		e_ews_connection_sync_folder_hierarchy_sync (
			etd->connection,
			EWS_PRIORITY_MEDIUM,
			old_sync_state,
			&new_sync_state,
			&includes_last_folder,
			&folders_created,
			&folders_updated,
			&folders_deleted,
			NULL,
			&error);

		if (error != NULL) {
			g_free (old_sync_state);
			g_printerr ("\n%s\n", error->message);
			goto exit;
		}

		for (l = folders_created; l != NULL; l = l->next) {
			EEwsFolder *folder = l->data;

			if (e_ews_folder_get_folder_type (folder) == E_EWS_FOLDER_TYPE_CALENDAR) {
				const EwsFolderId *fid;

				fid = e_ews_folder_get_id (folder);

				calendar_fid = e_ews_folder_id_new (fid->id, fid->change_key, fid->is_distinguished_id);
				break;
			}
		}

		g_slist_free_full (folders_created, g_object_unref);
		g_slist_free_full (folders_updated, g_object_unref);
		g_slist_free_full (folders_deleted, g_free);

		g_free (old_sync_state);
		old_sync_state = new_sync_state;

		if (calendar_fid) {
			g_free (new_sync_state);
			break;
		}
	}

	if (!calendar_fid) {
		g_printerr ("No calendar folder found\n");
		goto exit;
	}

	registry = e_source_registry_new_sync (NULL, NULL);
	dummy_source = e_source_new (NULL, NULL, NULL);

	convert_data.connection = etd->connection;
	convert_data.default_zone = i_cal_timezone_get_utc_timezone ();
	convert_data.timezone_cache = g_object_new (cal_backend_ews_get_type (),
		"registry", registry,
		"source", dummy_source,
		"kind", I_CAL_VEVENT_COMPONENT,
		NULL);

	g_clear_object (&dummy_source);
	g_clear_object (&registry);

	tokens = g_strsplit (str_comp, "ICAL_TIMEZONE", 0);

	nelems = i_cal_array_size (builtin_timezones);
	for (ii = 0; ii < nelems; ii++) {
		GSList *ll;
		GSList *ids = NULL;
		ICalTimezone *zone;
		ECalComponent *comp;
		const gchar *zone_location;
		gchar *str;

		zone = i_cal_timezone_array_element_at (builtin_timezones, ii);
		zone_location = i_cal_timezone_get_location (zone);

		if (is_a_known_unknown_timezone (zone_location)) {
			g_object_unref (zone);
			continue;
		}

		str = g_strdup_printf ("%s%s%s%s%s", tokens[0], zone_location, tokens[1], zone_location, tokens[2]);
		comp = e_cal_component_new_from_string (str);
		g_free (str);

		convert_data.icomp = e_cal_component_get_icalcomponent (comp);

		e_ews_connection_create_items_sync (
			etd->connection,
			EWS_PRIORITY_MEDIUM,
			"SaveOnly",
			"SendToNone",
			calendar_fid,
			convert_calcomp_to_xml,
			&convert_data,
			&ids,
			NULL,
			&error);

		g_object_unref (comp);

		if (error != NULL) {
			g_printerr ("\n%s\n", error->message);
			g_clear_error (&error);

			zone_location_errors = g_slist_append (zone_location_errors, g_strdup (zone_location));
			g_object_unref (zone);
			continue;
		}

		for (ll = ids; ll != NULL; ll = ll->next) {
			EEwsItem *item = ll->data;

			if (e_ews_item_get_item_type (item) == E_EWS_ITEM_TYPE_ERROR) {
				const GError *item_error = e_ews_item_get_error (item);
				g_printerr ("\n%s\n", item_error->message);
				g_clear_error (&error);

				zone_location_errors = g_slist_append (zone_location_errors, g_strdup (zone_location));
				g_object_unref (zone);
				continue;
			}
		}

		g_slist_free_full (ids, g_object_unref);
		g_object_unref (zone);
	}

	g_clear_object (&convert_data.timezone_cache);
	g_strfreev (tokens);

	retval = zone_location_errors == NULL;

 exit:
	if (zone_location_errors != NULL) {
		GSList *l;

		g_printerr ("Errors found in: \n");
		for (l = zone_location_errors; l != NULL; l = l->next)
			g_printerr (" - %s\n", (gchar *) l->data);

		g_slist_free_full (zone_location_errors, g_free);
	}

	uhm_server_end_trace (local_server);
	e_ews_folder_id_free (calendar_fid);
	g_clear_error (&error);

	g_assert_true (retval == TRUE);
}

int main (int argc,
	  char **argv)
{
	gint retval;
	GList *etds, *l;
	UhmServer *server;
	const gchar *module_path;
	GModule *module = NULL;
	gpointer symbol = NULL;

	retval = ews_test_init (argc, argv);

	if (retval < 0) {
		g_printerr ("Failed to initialize test\n");
		goto exit;
	}

	if (!g_module_supported ()) {
		g_printerr ("GModule not supported\n");
		retval = 1;
		goto exit;
	}

	module_path = CALENDAR_MODULE_DIR "libecalbackendews.so";
	module = g_module_open (module_path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);

	if (module == NULL) {
		g_printerr ("Failed to load module '%s': %s\n", module_path, g_module_error ());
		retval = 2;
		goto exit;
	}

	if (!g_module_symbol (module, "e_cal_backend_ews_populate_windows_zones", &symbol)) {
		g_printerr ("\n%s\n", g_module_error ());
		retval = 3;
		goto exit;
	}

	populate_windows_zones = symbol;

	if (!g_module_symbol (module, "e_cal_backend_ews_tz_util_get_msdn_equivalent", &symbol)) {
		g_printerr ("\n%s\n", g_module_error ());
		retval = 4;
		goto exit;
	}

	ical_to_msdn_equivalent = symbol;

	if (!g_module_symbol (module, "e_cal_backend_ews_convert_calcomp_to_xml", &symbol)) {
		g_printerr ("\n%s\n", g_module_error ());
		retval = 5;
		goto exit;
	}

	convert_calcomp_to_xml = symbol;

	if (!g_module_symbol (module, "e_cal_backend_ews_get_type_for_testing_sources", &symbol)) {
		g_printerr ("\n%s\n", g_module_error ());
		retval = 6;
		goto exit;
	}

	cal_backend_ews_get_type = symbol;

	server = ews_test_get_mock_server ();
	etds = ews_test_get_test_data_list ();

	/* Set handler of debug information */
	populate_windows_zones ();
	builtin_timezones = i_cal_timezone_get_builtin_timezones ();

	for (l = etds; l != NULL; l = l->next) {
		EwsTestData *etd = l->data;
		gchar *message;

		if (!uhm_server_get_enable_online (server))
			g_signal_connect (server, "notify::resolver", (GCallback) server_notify_resolver_cb, etd);

		if (e_ews_debug_get_server_version_from_string (etd->version) >= E_EWS_EXCHANGE_2010) {
			message = g_strdup_printf ("/%s/calendar/timezones/ical_compatibility", etd->version);
			g_test_add_data_func (message, etd, test_libical_timezones_compatibility);
			g_free (message);

			message = g_strdup_printf ("/%s/calendar/timezones/time_zones_sync", etd->version);
			g_test_add_data_func (message, etd, test_time_zones_sync);
			g_free (message);
		}
	}

	retval = g_test_run ();

	if (!uhm_server_get_enable_online (server))
		for (l = etds; l != NULL; l = l->next)
			g_signal_handlers_disconnect_by_func (server, server_notify_resolver_cb, l->data);

 exit:
	if (module != NULL)
		g_module_close (module);
	i_cal_timezone_free_builtin_timezones ();
	ews_test_cleanup ();

	return retval;
}
