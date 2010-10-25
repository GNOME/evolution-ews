/*Place holder for connection test cases*/
#include <stdio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <e-ews-connection.h>

void connection_util_get_login_info (gchar **username, gchar **password, gchar **uri);
void test_create_new_connection ();
static void con_test_create_new_connection ();
static void util_get_login_info (gchar **username, gchar **password, gchar **uri);

static GMainLoop *main_loop;

/*Utility functions */

static void
util_get_email_from_env (gchar **email)
{
	*email = g_getenv ("EWS_TEST_EMAIL");
}

static void
util_get_login_info_from_env (gchar **username, gchar **password, gchar **uri)
{
	*username = g_getenv ("EWS_TEST_USERNAME");
	*password = g_getenv ("EWS_TEST_PASSWORD");
	*uri = g_getenv ("EWS_TEST_URI");
}

/*Test cases*/

static void con_test_create_new_connection ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);
	
	cnc = e_ews_connection_new (uri, username, password);
	g_assert (cnc != NULL);

}

static void
con_test_autodiscover()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	const gchar *email;
	EEwsConnection *cnc;

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);

	util_get_email_from_env (&email);
	g_assert_cmpstr (email, !=, NULL);
	g_print("%s %s : username : %s \n", G_STRLOC, G_STRFUNC, username);
	g_print("%s %s : password : %s \n", G_STRLOC, G_STRFUNC, password);
	g_print("%s %s : email : %s \n", G_STRLOC, G_STRFUNC, email);

	g_print ("Testing postive case... \n");
	uri = e_ews_autodiscover_ws_url (username, password, email);
	g_assert_cmpstr (uri, !=, NULL);

	g_print ("Testing wrong password... \n");
	uri = e_ews_autodiscover_ws_url (username, "wrongpassword", email);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing wrong username... \n");
	uri = e_ews_autodiscover_ws_url ("wrongusername", password, email);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing wrong username and password... \n");
	uri = e_ews_autodiscover_ws_url ("wrongusername", "wrongpassword", email);
	g_assert_cmpstr (uri, ==, NULL);

	g_print ("Testing malformed email address... \n");
	uri = e_ews_autodiscover_ws_url (username, password, "emailnoatserver");
	g_assert_cmpstr (uri, ==, NULL);
}

static void op_test_sync_folder_hierarchy ()
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	ESoapMessage *msg;
	guint status;
	GList *folder_list = NULL;

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);
	
	cnc = e_ews_connection_new (uri, username, password);
	g_assert (cnc != NULL);

	status = e_ews_connection_sync_folder_hierarchy (cnc, NULL, &folder_list);

	if (status == E_EWS_CONNECTION_STATUS_OK)
		g_print ("Folder hierarchy fetched... \n");
	else
		g_print ("The sync state data passed is invalid... \n");
}

static void
ews_conn_shutdown (EEwsConnection *cnc)
{
	g_print ("Terminating the connection. BYE... \n");
	g_main_loop_quit (main_loop);
}

static gboolean
idle_cb (gpointer data)
{
	const gchar *username;
	const gchar *password;
	const gchar *uri;
	EEwsConnection *cnc;
	ESoapMessage *msg;
	guint status;
	GList *folder_list = NULL;

	util_get_login_info_from_env (&username, &password, &uri);
	g_assert_cmpstr (username, !=, NULL);
	g_assert_cmpstr (password, !=, NULL);
	g_assert_cmpstr (uri, !=, NULL);
	
	cnc = e_ews_connection_new (uri, username, password);
	g_assert (cnc != NULL);

	g_print ("Testing create folder at the top level... \n");
	e_ews_connection_create_folder (cnc);

	g_signal_connect (cnc, "shutdown", G_CALLBACK (ews_conn_shutdown), NULL);

	return FALSE;
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


void op_tests_run ()
{
	g_print ("Testing operations... \n");

	g_print ("\n Testing the sync_hierarchy... \n");
	op_test_sync_folder_hierarchy ();

	g_type_init ();
	g_thread_init (NULL);

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);
}
