/*
 * SPDX-FileCopyrightText: (C) 2013 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileContributor: Fabiano Fidêncio <fidencio@redhat.com>
 * SPDX-FileContributor: Philip Withnall <philip@tecnocode.co.uk>
 * SPDX-FileContributor: Vadim Rutkovsky <vrutkovs@redhat.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "common/e-ews-connection.h"
#include "common/e-ews-debug.h"
#include "common/e-ews-folder.h"

#include "ews-test-common.h"

static EwsFolderId *distinguished_folder_subfolder_id = NULL;
static EwsFolderId *non_distinguished_folder_subfolder_id = NULL;

static const gchar *invalid_folder_id =
"AAMkADQyYzVlYmU0LWNhNTUtNDNkYy04ZGYxLTk5ZTk5ZGY4NmJlMwAuAAAAAAB9G7pDgpwKQKc31aq6C3GTAQAi6qqMgDmPQrslhHoZnZkhAABOpnTeSTE=";

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

static gboolean
is_folder_on_server (EEwsConnection *connection,
		     EwsFolderId *fid,
		     gboolean should_have)
{
	gboolean found = !should_have;
	GSList *folders_ids = NULL;
	GSList *folders = NULL, *f;
	GError *error = NULL;

	folders_ids = g_slist_prepend (folders_ids, fid);
	e_ews_connection_get_folder_sync (
		connection, EWS_PRIORITY_MEDIUM, "IdOnly",
		NULL, folders_ids, &folders,
		NULL, &error);

	if (error != NULL) {
		if (g_error_matches (error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ITEMNOTFOUND))
			goto exit;

		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	for (f = folders; f != NULL; f = f->next) {
		EEwsFolder *folder = f->data;
		const EwsFolderId *result_fid;

		if (e_ews_folder_is_error (folder)) {
			const GError *folder_error;

			found = FALSE;

			folder_error = e_ews_folder_get_error (folder);
			if (g_error_matches (folder_error, EWS_CONNECTION_ERROR, EWS_CONNECTION_ERROR_ITEMNOTFOUND))
				break;
			else
				continue;
		}

		result_fid = e_ews_folder_get_id (folder);
		if (e_ews_folder_id_is_equal (fid, result_fid, TRUE)) {
			found = TRUE;
			break;
		}
	}

 exit:
	g_slist_free_full (folders, g_object_unref);
	g_slist_free (folders_ids);
	g_clear_error (&error);
	return found;
}

static void
test_create_existing_folder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = TRUE;
	gboolean folder_exists = FALSE;
	GError *error = NULL;
	EwsFolderId *fid;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "create_existing_folder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_create_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"msgfolderroot", TRUE,
		"inbox", E_EWS_FOLDER_TYPE_MAILBOX, &fid,
		NULL, &error);

	if (error != NULL) {
		folder_exists = g_error_matches (
			error,
			EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_FOLDEREXISTS);

		if (!folder_exists)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == FALSE && folder_exists == TRUE);
}

static void
test_create_folder_with_invalid_distinguished_parent_id (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = TRUE;
	gboolean parent_folder_not_found = FALSE;
	GError *error = NULL;
	EwsFolderId *fid;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "create_folder_with_invalid_distinguished_parent_id", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_create_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"invalidparentid", TRUE,
		"folder", E_EWS_FOLDER_TYPE_MAILBOX, &fid,
		NULL, &error);

	if (error != NULL) {
		parent_folder_not_found = g_error_matches (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_INTERNAL_SERVER_ERROR);

		if (!parent_folder_not_found)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == FALSE && parent_folder_not_found == TRUE);
}

static void
test_create_folder_with_invalid_non_distinguished_parent_id (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = TRUE;
	gboolean parent_folder_not_found = FALSE;
	GError *error = NULL;
	EwsFolderId *fid;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "create_folder_with_invalid_non_distinguished_parent_id", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_create_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		invalid_folder_id, FALSE,
		"folder", E_EWS_FOLDER_TYPE_MAILBOX, &fid,
		NULL, &error);

	if (error != NULL) {
		parent_folder_not_found =
			g_error_matches (
				error,
				EWS_CONNECTION_ERROR,
				EWS_CONNECTION_ERROR_NONEXISTENTMAILBOX) ||
			g_error_matches (
				error,
				EWS_CONNECTION_ERROR,
				EWS_CONNECTION_ERROR_PARENTFOLDERNOTFOUND);

		if (!parent_folder_not_found)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == FALSE && parent_folder_not_found == TRUE);
}

static void
test_create_folder_with_malformed_parent_id (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = TRUE;
	gboolean malformed_parent_id = FALSE;
	GError *error = NULL;
	EwsFolderId *fid;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "create_folder_with_malformed_parent_id", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_create_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"malformedparentid", FALSE,
		"folder", E_EWS_FOLDER_TYPE_MAILBOX, &fid,
		NULL, &error);

	if (error != NULL) {
		malformed_parent_id = g_error_matches (
			error,
			EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_INVALIDIDMALFORMED);

		if (!malformed_parent_id)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == FALSE && malformed_parent_id == TRUE);
}

static void
test_create_distinguished_folder_subfolder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean on_server = FALSE;
	GError *error = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "create_distinguished_folder_subfolder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_create_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"inbox", TRUE,
		"distinguished-folder-subfolder", E_EWS_FOLDER_TYPE_MAILBOX,
		&distinguished_folder_subfolder_id, NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	on_server = is_folder_on_server (etd->connection, distinguished_folder_subfolder_id, TRUE);

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == TRUE && on_server == TRUE);
}

static void
test_create_non_distinguished_folder_subfolder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean on_server = FALSE;
	GError *error = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "create_non_distinguished_folder_subfolder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_create_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		distinguished_folder_subfolder_id->id, FALSE,
		"non-distinguished-folder-subfolder", E_EWS_FOLDER_TYPE_MAILBOX,
		&non_distinguished_folder_subfolder_id,
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	on_server = is_folder_on_server (etd->connection, non_distinguished_folder_subfolder_id, TRUE);

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == TRUE && on_server == TRUE);
}

static void
test_move_valid_folder_to_invalid_folder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean malformed_parent_id = FALSE;
	GError *error = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "move_valid_folder_to_invalid_folder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_move_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"malformed_parent_id", distinguished_folder_subfolder_id->id,
		NULL, &error);

	if (error != NULL) {
		malformed_parent_id = g_error_matches (
			error,
			EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_INVALIDIDMALFORMED);

		if (!malformed_parent_id)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == FALSE && malformed_parent_id == TRUE);
}

static void
test_move_invalid_to_valid_folder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean malformed_parent_id = FALSE;
	GError *error = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "move_invalid_folder_to_valid_folder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_move_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		distinguished_folder_subfolder_id->id, "malformedparentid",
		NULL, &error);

	if (error != NULL) {
		malformed_parent_id = g_error_matches (
			error,
			EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_INVALIDIDMALFORMED);

		if (!malformed_parent_id)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == FALSE && malformed_parent_id == TRUE);
}

static void
test_move_folder_to_msgfolderroot (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean moved_to_msgfolderroot = FALSE;
	GError *error = NULL;
	GSList *folder_ids = NULL;
	GSList *folders1 = NULL;
	GSList *folders2 = NULL;
	EEwsFolder *folder1;
	EEwsFolder *folder2;
	EwsFolderId *msgfolderroot;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "move_folder_to_msgfolderroot", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_move_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		NULL, non_distinguished_folder_subfolder_id->id,
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	folder_ids = g_slist_prepend (folder_ids, non_distinguished_folder_subfolder_id);
	e_ews_connection_get_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"AllProperties", NULL, folder_ids, &folders1,
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	folder1 = folders1->data;

	msgfolderroot = e_ews_folder_id_new ("msgfolderroot", NULL, TRUE);
	folder_ids = g_slist_prepend (folder_ids, msgfolderroot);
	e_ews_connection_get_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"IdOnly", NULL, folder_ids, &folders2,
		NULL, &error);

	folder2 = folders2->data;

	moved_to_msgfolderroot = e_ews_folder_id_is_equal (
		e_ews_folder_get_parent_id (folder1),
		e_ews_folder_get_id (folder2),
		FALSE);

 exit:
	uhm_server_end_trace (local_server);

	g_slist_free_full (folders1, g_object_unref);
	g_slist_free_full (folders2, g_object_unref);
	g_slist_free (folder_ids);
	g_clear_error (&error);

	g_assert_true (retval == TRUE && moved_to_msgfolderroot == TRUE);
}

static void
test_move_folder_to_folder_that_has_subfolder_with_the_same_name (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = TRUE;
	gboolean move_failed = FALSE;
	GError *error = NULL;
	EwsFolderId *fid = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "move_folder_to_folder_that_has_subfolder_with_the_same_name", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	e_ews_connection_create_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		non_distinguished_folder_subfolder_id->id, FALSE,
		"inbox", E_EWS_FOLDER_TYPE_MAILBOX, &fid,
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_move_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		NULL, fid->id,
		NULL, &error);

	if (error != NULL) {
		move_failed = g_error_matches (
			error,
			EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_MOVECOPYFAILED);

		if (!move_failed)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	e_ews_folder_id_free (fid);

	g_clear_error (&error);

	g_assert_true (retval == FALSE && move_failed == TRUE);
}

static void
test_move_folder_to_valid_folder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean moved_to_correct_folder = FALSE;
	GError *error = NULL;
	GSList *folder_ids = NULL;
	GSList *folders1 = NULL;
	GSList *folders2 = NULL;
	EEwsFolder *folder1;
	EEwsFolder *folder2;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "move_folder_to_valid_folder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_move_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		distinguished_folder_subfolder_id->id, non_distinguished_folder_subfolder_id->id,
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	folder_ids = g_slist_prepend (folder_ids, non_distinguished_folder_subfolder_id);
	e_ews_connection_get_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"AllProperties", NULL, folder_ids, &folders1,
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	folder1 = folders1->data;

	folder_ids = g_slist_prepend (folder_ids, distinguished_folder_subfolder_id);
	e_ews_connection_get_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"IdOnly", NULL, folder_ids, &folders2,
		NULL, &error);

	folder2 = folders2->data;

	moved_to_correct_folder = e_ews_folder_id_is_equal (
		e_ews_folder_get_parent_id (folder1),
		e_ews_folder_get_id (folder2),
		FALSE);

 exit:
	uhm_server_end_trace (local_server);

	g_slist_free_full (folders1, g_object_unref);
	g_slist_free_full (folders2, g_object_unref);
	g_slist_free (folder_ids);
	g_clear_error (&error);

	g_assert_true (retval == TRUE && moved_to_correct_folder == TRUE);
}

static void
test_hard_delete_distinguished_folder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean distinguished_folder = FALSE;
	GError *error = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "hard_delete_root_subfolder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_delete_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		"inbox", TRUE, "HardDelete",
		NULL, &error);

	if (error != NULL) {
		distinguished_folder = g_error_matches (
			error,
			EWS_CONNECTION_ERROR,
			EWS_CONNECTION_ERROR_DELETEDISTINGUISHEDFOLDER);

		if (!distinguished_folder)
			g_printerr ("\n%s\n", error->message);
	}

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);

	g_assert_true (retval == FALSE && distinguished_folder == TRUE);
}

static void
test_hard_delete_non_distinguished_folder_subfolder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean on_server = TRUE;
	GError *error = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "hard_delete_non_distinguished_folder_subfolder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_delete_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		non_distinguished_folder_subfolder_id->id, FALSE, "HardDelete",
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	on_server = is_folder_on_server (etd->connection, non_distinguished_folder_subfolder_id, FALSE);

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);
	e_ews_folder_id_free (non_distinguished_folder_subfolder_id);
	non_distinguished_folder_subfolder_id = NULL;

	g_assert_true (retval == TRUE && on_server == FALSE);
}

static void
test_hard_delete_distinguished_folder_subfolder (gconstpointer user_data)
{
	UhmServer *local_server;
	gboolean retval = FALSE;
	gboolean on_server = TRUE;
	GError *error = NULL;
	EwsTestData *etd = (gpointer) user_data;

	local_server = ews_test_get_mock_server ();

	ews_test_server_set_trace_directory (local_server, etd->version, "camel/folder");
	ews_test_server_start_trace (local_server, etd, "hard_delete_distinguished_folder_subfolder", &error);
	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	retval = e_ews_connection_delete_folder_sync (
		etd->connection, EWS_PRIORITY_MEDIUM,
		distinguished_folder_subfolder_id->id, FALSE, "HardDelete",
		NULL, &error);

	if (error != NULL) {
		g_printerr ("\n%s\n", error->message);
		goto exit;
	}

	on_server = is_folder_on_server (etd->connection, distinguished_folder_subfolder_id, FALSE);

 exit:
	uhm_server_end_trace (local_server);
	g_clear_error (&error);
	e_ews_folder_id_free (distinguished_folder_subfolder_id);
	distinguished_folder_subfolder_id = NULL;

	g_assert_true (retval == TRUE && on_server == FALSE);
}

int main (int argc,
	  char **argv)
{
	gint retval;
	GList *etds, *l;
	UhmServer *server;

	retval = ews_test_init (argc, argv);

	if (retval < 0)
		goto exit;

	server = ews_test_get_mock_server ();
	etds = ews_test_get_test_data_list ();

	for (l = etds; l != NULL; l = l->next) {
		EwsTestData *etd = l->data;
		gchar *message;

		if (!uhm_server_get_enable_online (server))
			g_signal_connect (server, "notify::resolver", (GCallback) server_notify_resolver_cb, etd);

		/* Create folder */
		message = g_strdup_printf ("/%s/camel/folder/create_existing_folder", etd->version);
		g_test_add_data_func (message, etd, test_create_existing_folder);
		g_free (message);

		message = g_strdup_printf (
			"/%s/camel/folder/create_folder_with_invalid_distinguished_parent_id", etd->version);
		g_test_add_data_func (message, etd, test_create_folder_with_invalid_distinguished_parent_id);
		g_free (message);

		message = g_strdup_printf (
			"/%s/camel/folder/create_folder_with_invalid_non_distinguished_parent_id", etd->version);
		g_test_add_data_func (message, etd, test_create_folder_with_invalid_non_distinguished_parent_id);
		g_free (message);

		message = g_strdup_printf ("/%s/camel/folder/create_folder_with_malformed_parent_id", etd->version);
		g_test_add_data_func (message, etd, test_create_folder_with_malformed_parent_id);
		g_free (message);

		message = g_strdup_printf ("/%s/camel/folder/create_distinguished_folder_subfolder", etd->version);
		g_test_add_data_func (message, etd, test_create_distinguished_folder_subfolder);
		g_free (message);

		message = g_strdup_printf ("/%s/camel/folder/create_non_distinguished_folder_subfolder", etd->version);
		g_test_add_data_func (message, etd, test_create_non_distinguished_folder_subfolder);
		g_free (message);

		/* Move folder */
		message = g_strdup_printf ("/%s/camel/folder/move_valid_folder_to_invalid_folder", etd->version);
		g_test_add_data_func (message, etd, test_move_valid_folder_to_invalid_folder);
		g_free (message);

		message = g_strdup_printf (
			"/%s/camel/folder/move_invalid_folder_to_valid_folder", etd->version);
		g_test_add_data_func (message, etd, test_move_invalid_to_valid_folder);
		g_free (message);

		message = g_strdup_printf (
			"/%s/camel/folder/move_folder_to_msgfolderroot", etd->version);
		g_test_add_data_func (message, etd, test_move_folder_to_msgfolderroot);
		g_free (message);

		message = g_strdup_printf (
			"/%s/camel/folder/move_folder_to_folder_that_has_subfolder_with_the_same_name", etd->version);
		g_test_add_data_func (message, etd, test_move_folder_to_folder_that_has_subfolder_with_the_same_name);
		g_free (message);

		message = g_strdup_printf (
			"/%s/camel/folder/move_folder_to_valid_folder", etd->version);
		g_test_add_data_func (message, etd, test_move_folder_to_valid_folder);
		g_free (message);

		/* Delete folder */
		message = g_strdup_printf ("/%s/camel/folder/hard_delete_distinguished_folder", etd->version);
		g_test_add_data_func (message, etd, test_hard_delete_distinguished_folder);
		g_free (message);

		message = g_strdup_printf ("/%s/camel/folder/hard_delete_non_distinguished_folder_subfolder", etd->version);
		g_test_add_data_func (message, etd, test_hard_delete_non_distinguished_folder_subfolder);
		g_free (message);

		message = g_strdup_printf ("/%s/camel/folder/hard_delete_distinguished_folder_subfolder", etd->version);
		g_test_add_data_func (message, etd, test_hard_delete_distinguished_folder_subfolder);
		g_free (message);

	}

	retval = g_test_run ();

	if (!uhm_server_get_enable_online (server))
		for (l = etds; l != NULL; l = l->next)
			g_signal_handlers_disconnect_by_func (server, server_notify_resolver_cb, l->data);

 exit:
	ews_test_cleanup ();
	return retval;
}
