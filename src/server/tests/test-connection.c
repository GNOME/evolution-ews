/*Place holder for connection test cases*/
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <e-ews-connection.h>
#include <e-ews-folder.h>
#include <e-ews-item.h>

void connection_util_get_login_info (gchar **username, gchar **password, gchar **uri);
void test_create_new_connection ();
static void con_test_create_new_connection ();
void connection_tests_run ();
void autodiscovery_tests_run ();
void op_tests_run ();
static void op_test_get_item ();

static GMainLoop *main_loop;

EwsFolderId *inbox_folder_id = NULL;
EwsId *first_item_id = NULL;

/*Utility functions */

static void
util_get_email_from_env (const gchar **email)
{
	*email = g_getenv ("EWS_TEST_EMAIL");
}

static void
util_get_login_info_from_env (const gchar **username, const gchar **password, const gchar **uri)
{
	*username = g_getenv ("EWS_TEST_USERNAME");
	*password = g_getenv ("EWS_TEST_PASSWORD");
	*uri = g_getenv ("EWS_TEST_URI");
}

static void
ews_conn_shutdown (EEwsConnection *cnc)
{
	g_print ("Terminating the connection. BYE... \n");
	g_main_loop_quit (main_loop);
}

/*Test cases*/

static void
con_test_create_new_connection ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);
	
	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	/* Quit the test suite */
	//g_signal_connect (cnc, "shutdown", G_CALLBACK (ews_conn_shutdown), NULL);
}

static void
con_test_autodiscover()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	const gchar *email, *domain;
	gchar *wrong_username;
	GError *error = NULL;

	/* FIXME username not needed here */
	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);

	util_get_email_from_env (&email);
	g_assert_cmpstr (email, !=, NULL);
	g_print("%s %s : password : %s \n", G_STRLOC, G_STRFUNC, password);
	g_print("%s %s : email : %s \n", G_STRLOC, G_STRFUNC, email);

	g_print ("Testing postive case... \n");
	uri = e_ews_autodiscover_ws_url (email, password, &error);
	if (error) {
		g_print ("Error code:%d desc: %s \n", error->code, error->message);
		g_clear_error (&error);
	}
	g_assert_cmpstr (uri, !=, NULL);

	g_print ("Testing wrong password... \n");
	uri = e_ews_autodiscover_ws_url (email, "wrongpassword", &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing email without domain ... \n");
	uri = e_ews_autodiscover_ws_url ("wronguseremail", password, &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing wrong email address and password... \n");
	uri = e_ews_autodiscover_ws_url ("godknows@donknow.com", "wrongpassword", &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing wrong user name ... \n");
	domain = g_strstr_len (email, -1, "@");
	wrong_username = g_strconcat ("godknows", domain, NULL);
	uri = e_ews_autodiscover_ws_url (wrong_username, password, &error);
	g_print ("Error code:%d desc: %s \n", error->code, error->message);
	g_clear_error (&error);
	g_free (wrong_username);
	g_assert_cmpstr (uri, ==, NULL);
}

/* FIXME Have a separate test for cancel without disrupting sync_hierarchy test 	
static void
cancel_sync_folder_hierarchy (gpointer data)
{
	GCancellable *cancellable = (GCancellable *) data;
	g_print ("\nCanceling sync_folder_hierarchy...");
	g_cancellable_cancel (cancellable);
} */

static void
folder_items_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GSList *items_created = NULL, *items_updated = NULL;
	GSList *items_deleted = NULL, *l;
	gchar *sync_state = NULL;
	GError *error = NULL;
	/* Only for test program */
	EwsId *i_id;

	e_ews_connection_sync_folder_items_finish	(cnc, res, &sync_state,
							 &items_created, &items_updated,
							 &items_deleted, &error);

	if (error != NULL) {
		g_print ("Unable to sync the folder items: %s :%d \n", error->message, error->code);
		return;
	}

	g_print ("Sync state for folder is\n  %s \n", sync_state);

	/* Only for testing the get item API later */
	i_id = e_ews_item_get_id ((EEwsItem *) items_created->data);
	first_item_id = g_new0 (EwsId, 1);
	first_item_id->id = g_strdup (i_id->id);
	first_item_id->change_key = g_strdup (i_id->change_key);

	g_print ("Items created \n");
	for (l = items_created; l != NULL;l = g_slist_next (l)) {
		EEwsItem *item = l->data;
		EwsId *item_id = e_ews_item_get_id (item);

		g_print ("Subject: %s \n Id: %s  \n ChangeKey: %s \n\n", e_ews_item_get_subject (item), item_id->id, item_id->change_key);
		g_object_unref (item);
	}

	g_free (sync_state);
	g_slist_free (items_created);
	g_slist_free (items_updated);
	g_slist_free (items_deleted);

	g_print ("\nTesting get item... \n");
	op_test_get_item ();
}

static void
op_test_sync_folder_items ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;
	/* const gchar *sync_state = NULL; */

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	e_ews_connection_sync_folder_items_start	(cnc, EWS_PRIORITY_MEDIUM, 
							 NULL, inbox_folder_id, 
							 "IdOnly", "Subject DisplayTo", 
							 100, folder_items_ready_callback, 
							 cancellable, NULL);
}

static void
folder_hierarchy_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	GSList *folders_created = NULL, *folders_updated = NULL;
	GSList *folders_deleted = NULL, *l;
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	gchar *sync_state = NULL;
	GError *error = NULL;

	e_ews_connection_sync_folder_hierarchy_finish	(cnc, res, &sync_state,
							 &folders_created, &folders_updated,
							 &folders_deleted, &error);

	if (error != NULL) {
		g_print ("Unable to fetch the folder hierarchy: %s :%d \n", error->message, error->code);
		return;
	}

	g_print ("Sync state \n  %s \n", sync_state);

	g_print ("Folders created \n");
	for (l = folders_created; l != NULL;l = g_slist_next (l)) {
		EEwsFolder *folder = l->data;
		EwsFolderId *fid = e_ews_folder_get_id (folder);

		g_print ("Name: %s \n Id: %s  \n ChangeKey: %s \n\n", e_ews_folder_get_name (folder), fid->id, fid->change_key);
		if (!strcmp (e_ews_folder_get_name (folder), "Inbox")) {
			inbox_folder_id = g_new0 (EwsFolderId, 1);
			inbox_folder_id->id = g_strdup (fid->id);
			inbox_folder_id->change_key = g_strdup (fid->change_key);
		}
		g_object_unref (folder);
	}

	g_free (sync_state);
	g_slist_free (folders_created);
	g_slist_free (folders_updated);
	g_slist_free (folders_deleted);

	g_print ("\n Testing sync folder items... \n");
	op_test_sync_folder_items ();
}

static void 
op_test_sync_folder_hierarchy ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	e_ews_connection_sync_folder_hierarchy_start	(cnc, EWS_PRIORITY_MEDIUM, 
							 NULL, folder_hierarchy_ready_callback, 
							 cancellable, NULL);

	/* FIXME Have a separate test for cancel without disrupting sync_hierarchy test 	
	thread = g_thread_create ((GThreadFunc) cancel_sync_folder_hierarchy, cancellable, FALSE, &error);
	if (error || !thread) {
		g_warning ("%s: Creation of the thread failed with error: %s", G_STRFUNC, error->message);
		g_error_free (error);
	} */
}

static void
get_item_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	GError *error = NULL;
	EEwsItem *item;

	e_ews_connection_get_item_finish	(cnc, res, &item, &error);

	if (error != NULL) {
		g_print ("Unable to get item: %s :%d \n", error->message, error->code);
		return;
	}

	g_print ("\nMime content is:\n%s\n", e_ews_item_get_mime_content (item));

}

static void 
op_test_get_item ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	e_ews_connection_get_item_start		(cnc, EWS_PRIORITY_MEDIUM, 
						 first_item_id, "IdOnly", NULL, "true",
						 get_item_ready_callback, 
						 cancellable, NULL);
}

static void
create_folder_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	guint folder_id;
	GError *error = NULL;

	e_ews_connection_create_folder_finish	(cnc, res, folder_id, &error);

	if (error != NULL) {
		g_print ("Unable to create folder: %s :%d \n", error->message, error->code);
		return;
	}
}

static void 
op_test_create_folder ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	e_ews_connection_create_folder_start	(cnc, EWS_PRIORITY_LOW, 
						 create_folder_ready_callback, 
						 cancellable, NULL);

	/* FIXME api fix
	e_ews_connection_create_folder (cnc, cancellable); */
}

static void
resolve_names_ready_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
	EEwsConnection *cnc = E_EWS_CONNECTION (object);
	guint folder_id;
	GError *error = NULL;

	e_ews_connection_resolve_names_finish	(cnc, res, &error);

	if (error != NULL) {
		g_print ("Unable to create folder: %s :%d \n", error->message, error->code);
		return;
	}
}

static void 
op_test_resolve_names ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	e_ews_connection_resolve_names_start	(cnc, EWS_PRIORITY_HIGH,
						"bharath",
						resolve_names_ready_callback,
						cancellable, NULL);
}

static void 
op_test_find_item ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	GCancellable *cancellable;

	cancellable = g_cancellable_new ();

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);

	cnc = e_ews_connection_new (uri, username, password, NULL);
	g_assert (cnc != NULL);

	/* FIXME api fix
	e_ews_connection_find_item (cnc, "contacts", cancellable); */
}

/*Run tests*/
void connection_tests_run ()
{
	g_printf ("Testing Connection..... \n");
	con_test_create_new_connection ();
}

void autodiscovery_tests_run ()
{
	g_printf ("Testing Autodiscovery.... \n");
	con_test_autodiscover();
}

static gboolean
idle_cb (gpointer data)
{
	g_print ("\nTesting create folder at the top level... \n");
	op_test_create_folder ();

	g_print ("\nTesting create folder at the top level again... \n");
	op_test_create_folder ();

	g_print ("\nTesting the sync_hierarchy... \n");
	op_test_sync_folder_hierarchy ();

	g_print ("\nTesting create folder at the top level again third time... \n");
	op_test_create_folder ();

	g_print ("\nTesting find item... \n");
	op_test_find_item ();

	g_print ("\nTesting resolve names... \n");
	op_test_resolve_names ();

	return FALSE;
}

void op_tests_run ()
{
	g_print ("Testing operations... \n");

	g_type_init ();
	g_thread_init (NULL);

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
