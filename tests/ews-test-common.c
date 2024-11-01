/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileContributor: Fabiano Fidêncio <fidencio@redhat.com>
 * SPDX-FileContributor: Philip Withnall <philip@tecnocode.co.uk>
 * SPDX-FileContributor: Vadim Rutkovsky <vrutkovs@redhat.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-ews-config.h"

#include <string.h>

#include "ews-test-common.h"

/* declaration of debug handler */
static void ews_test_debug_handler (const gchar *log_domain,
				    GLogLevelFlags log_level,
				    const gchar *message,
				    gpointer user_data);

/* Directory to output network trace files to, if trace output is enabled. (NULL otherwise.) */
static const GFile *trace_dir = NULL;

/*
 * TRUE if tests should be run and a trace file written for each;
 * FALSE if tests should run offline against existing trace files.
 */
static gboolean write_traces = FALSE;

/* Global mock server instance used by all tests */
static UhmServer *mock_server = NULL;

static GList *etds = NULL;

static gboolean no_interactive = FALSE;

static void
prepare_fake_data (guint *number,
		   GList **fake_versions,
		   GList **fake_hostnames,
		   GList **fake_server_uris)
{
	/* Underscores cannot be used in the host name (A disallowed SNI server name has been received).
	   See https://gitlab.com/uhttpmock/uhttpmock/-/issues/5 */
	struct _supported_versions {
		const gchar *hostname;
		const gchar *version;
	} supported_versions[] = {
		{ "2007SP1", "2007_SP1" },
		{ "2010SP2", "2010_SP2" }
	};
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (supported_versions); ii++) {
		*fake_versions = g_list_append (
			*fake_versions,
			g_strdup_printf ("Exchange%s", supported_versions[ii].version));
		*fake_hostnames = g_list_append (
			*fake_hostnames,
			g_strdup_printf ("exchange%s.server.com", supported_versions[ii].hostname));
		*fake_server_uris = g_list_append (
			*fake_server_uris,
			g_strdup_printf ("https://exchange%s.server.com:PORT/EWS/Exchange.asmx", supported_versions[ii].hostname));
	}

	*number = ii;
}

static void
help (gchar *program)
{
	/* We have to override --help in order to document --no-interactive and the trace flags. */
	g_print ("Usage:\n"
		 "  %s [OPTION...]\n\n"
		 "Help Options:\n"
		 "  -?, --help                      Show help options\n"
		 "Test Options:\n"
		 "  -l                              List test cases available in a test executable\n"
		 "  -seed=RANDOMSEED                Provide a random seed to reproduce test\n"
		 "                                  runs using random numbers\n"
		 "  --verbose                       Run tests verbosely\n"
		 "  -q, --quiet                     Run tests quietly\n"
		 "  -p TESTPATH                     Execute all tests matching TESTPATH\n"
		 "  -m {perf|slow|thorough|quick}   Execute tests according modes\n"
		 "  --debug-log                     Debug test logging output\n"
		 "  -i, --no-interactive            Only execute tests which don't require user interaction\n"
		 "  -t, --trace-dir [directory]     Read/Write trace files in the specified directory\n"
		 "  -w, --write-traces              Work online and write trace files to --trace-dir\n"
		 "  -c, --config                    In the format: key=value, where valid keys are:\n"
		 "                                      version, server-uri, username, password\n",
		 program);
}

gint
ews_test_init (gint argc,
	       gchar **argv)
{
	guint ret = 0;
	guint number = 0;
	guint i;
	GList *versions = NULL;
	GList *server_uris = NULL;
	GList *usernames = NULL;
	GList *passwords = NULL;
	GList *fake_versions = NULL;
	GList *fake_hostnames = NULL;
	GList *fake_server_uris = NULL;

#if !GLIB_CHECK_VERSION (2, 35, 0)
	g_type_init ();
#endif

	/* Parse the custom options */
	for (i = 1; i < argc; i++) {
		if (g_strcmp0 ("--no-interactive", argv[i]) == 0 || g_strcmp0 ("-i", argv[i]) == 0) {
			no_interactive = TRUE;
			argv[i] = (char *) "";
		} else if (g_strcmp0 ("--trace-dir", argv[i]) == 0 || g_strcmp0 ("-t", argv[i]) == 0) {
			if (i >= argc - 1) {
				g_list_free_full (versions, g_free);
				g_list_free_full (server_uris, g_free);
				g_list_free_full (usernames, g_free);
				g_list_free_full (passwords, g_free);

				g_error ("Error: Missing directory for --trace-dir option.\n");
			}

			trace_dir = g_file_new_for_path (argv[i + 1]);

			argv[i] = (char *) "";
			argv[i + 1] = (char *) "";
			i += 2;
		} else if (g_strcmp0 ("--config", argv[i]) == 0 || g_strcmp0 ("-c", argv[i]) == 0) {
			const gchar *val;
			const gchar *value;
			gchar *key;
			gsize len;

			if (i >= argc - 1) {
				g_list_free_full (versions, g_free);
				g_list_free_full (server_uris, g_free);
				g_list_free_full (usernames, g_free);
				g_list_free_full (passwords, g_free);

				g_error ("Error: Missing arguments for --config option.\n"
					 "       Expected: --config key=value, with valid these valid keys:\n"
					 "                 version, server-uri, username, password\n");
			}

			value = argv[i + 1];

			if (!(val = strchr (value, '='))) {
				g_list_free_full (versions, g_free);
				g_list_free_full (server_uris, g_free);
				g_list_free_full (usernames, g_free);
				g_list_free_full (passwords, g_free);

				g_error ("Error: Missing arguments for --config option.\n"
					 "       Expected: --config key=value, with valid these valid keys:\n"
					 "                 version, server-uri, username, password\n");
			}

			len = val - value;
			val++;
			key = g_strndup (value, len);

			if (g_strcmp0 (key, "number") == 0)
				number = g_ascii_strtoll (val, NULL, 0);
			else if (g_strcmp0 (key, "version") == 0)
				versions = g_list_append (versions, g_strdup (val));
			else if (g_strcmp0 (key, "server-uri") == 0)
				server_uris = g_list_append (server_uris, g_strdup (val));
			else if (g_strcmp0 (key, "username") == 0)
				usernames = g_list_append (usernames, g_strdup (val));
			else if (g_strcmp0 (key, "password") == 0)
				passwords = g_list_append (passwords, g_strdup (val));

			g_free (key);

			argv[i] = (char *) "";
			argv[i + 1] = (char *) "";
			i++;
		} else if (g_strcmp0 ("--write-traces", argv[i]) == 0 || g_strcmp0 ("-w", argv[i]) == 0) {
			write_traces = TRUE;
			argv[i] = (char *) "";
		} else if (g_strcmp0 ("-?", argv[i]) == 0 || g_strcmp0 ("--help", argv[i]) == 0 || g_strcmp0 ("-h" , argv[i]) == 0) {
			help (argv[0]);

			exit (0);
		}
	}

	if ((number == 0 && write_traces) ||
	    (number > 0 &&
	     (number != g_list_length (versions) ||
	      number != g_list_length (server_uris) ||
	      number != g_list_length (usernames) ||
	      number != g_list_length (passwords)))) {
		g_printerr ("Error while parsing options\n");
		help (argv[0]);

		g_list_free_full (versions, g_free);
		g_list_free_full (server_uris, g_free);
		g_list_free_full (usernames, g_free);
		g_list_free_full (passwords, g_free);

		exit (1);
	}

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/show_bug.cgi?id=");

	g_setenv ("EWS_DEBUG", "4", TRUE);
	g_setenv ("EWS_TEST_WINDOWS_ZONES_XML_PATH", TEST_FILE_DIR "/../src/EWS/calendar/", TRUE);

	mock_server = uhm_server_new ();
	uhm_server_set_default_tls_certificate (mock_server);
	uhm_server_set_enable_logging (mock_server, write_traces);
	uhm_server_set_enable_online (mock_server, write_traces);

	/* Set handler of debug information */
	g_log_set_handler ("libeews", G_LOG_LEVEL_DEBUG, ews_test_debug_handler, mock_server);

	if (number == 0)
		prepare_fake_data (&number, &fake_versions, &fake_hostnames, &fake_server_uris);

	for (i = 0; i < number; i++) {
		EwsTestData *etd;

		etd = g_new0 (EwsTestData, 1);
		if (write_traces) {
			CamelEwsSettings *settings = NULL;

			settings = g_object_new (
				CAMEL_TYPE_EWS_SETTINGS,
				"user", g_list_nth_data (usernames, i),
				NULL);

			etd->version = g_list_nth_data (versions, i);
			etd->connection = e_ews_connection_new (NULL, g_list_nth_data (server_uris, i), settings);
			e_ews_connection_set_testing_sources (etd->connection, TRUE);
			e_ews_connection_set_password (etd->connection, g_list_nth_data (passwords, i));
			e_ews_connection_set_server_version_from_string (etd->connection, etd->version);

			g_object_unref (settings);
		} else {
			etd->version = g_list_nth_data (fake_versions, i);
			etd->server_uri = g_list_nth_data (fake_server_uris, i);
			etd->hostname = g_list_nth_data (fake_hostnames, i);
		}

		etds = g_list_append (etds, etd);
	}

	g_list_free (versions);
	g_list_free (usernames);
	g_list_free (server_uris);
	g_list_free (passwords);
	g_list_free (fake_versions);
	g_list_free (fake_server_uris);
	g_list_free (fake_hostnames);
	return ret;
}

UhmServer *
ews_test_get_mock_server (void)
{
	return mock_server;
}

GList * /* EwsTestData */
ews_test_get_test_data_list (void)
{
	return etds;
}

static void
ews_test_data_free (EwsTestData *etd)
{
	if (etd != NULL) {
		if (etd->connection != NULL)
			g_object_unref (etd->connection);

		g_free (etd->hostname);
		g_free (etd->version);
		g_free (etd->server_uri);
		g_free (etd);
	}
}

void
ews_test_cleanup (void)
{
	g_list_free_full (ews_test_get_test_data_list (), (GDestroyNotify) ews_test_data_free);
	g_object_unref (ews_test_get_mock_server ());
}

static void
ews_test_set_https_port (UhmServer *server,
			 EwsTestData *etd)
{
	CamelEwsSettings *ews_settings;
	guint16 port;
	gchar *uri;
	gchar **tokens;

	port = uhm_server_get_port (server);

	tokens = g_strsplit (etd->server_uri, "PORT", 0);
	uri = g_strdup_printf ("%s%d%s", tokens[0], port, tokens[1]);

	ews_settings = g_object_new (
		CAMEL_TYPE_EWS_SETTINGS,
		"user", "foo",
		NULL);

	etd->connection = e_ews_connection_new (NULL, uri, ews_settings);
	e_ews_connection_set_testing_sources (etd->connection, TRUE);
	e_ews_connection_set_password (etd->connection, "bar");
	e_ews_connection_set_server_version_from_string (etd->connection, etd->version);

	g_free (uri);
	g_strfreev (tokens);
	g_object_unref (ews_settings);
}

void
ews_test_server_start_trace (UhmServer *server,
			     EwsTestData *etd,
			     const gchar *trace_filename,
			     GError **error)
{
	uhm_server_start_trace (server, trace_filename, error);
	if (!uhm_server_get_enable_online (server))
		ews_test_set_https_port (server, etd);
}

void
ews_test_server_set_trace_directory (UhmServer *server,
				     const gchar *version,
				     const gchar *tests)
{
	gchar *path;
	GFile *trace_directory;

	path = g_strdup_printf (TEST_FILE_DIR "/traces/%s/%s", version, tests);
	trace_directory = g_file_new_for_path (path);
	g_free (path);
	uhm_server_set_trace_directory (server, trace_directory);
	g_object_unref (trace_directory);
}

static void
ews_test_debug_handler (const gchar *log_domain,
			GLogLevelFlags log_level,
			const gchar *message,
			gpointer user_data)
{
	UhmServer *server = user_data;

	if (message != NULL &&
	    (*message == '<' || *message == '>' || *message == ' ') &&
	    *(message + 1) == ' ') {
		uhm_server_received_message_chunk (server, message, strlen (message), NULL);
	}
}
