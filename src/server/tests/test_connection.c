/*Place holder for connection test cases*/
#include <stdio.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <e-ews-connection.h>

void connection_util_get_login_info (gchar **username, gchar **password, gchar **uri);
void test_create_new_connection ();
static void con_test_create_new_connection ();
static void util_get_login_info (gchar **username, gchar **password, gchar **uri);

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
	e_ews_autodiscover (username, password, email);
}

/*Run tests*/

void connection_tests_run ()
{
	con_test_autodiscover();
	/* con_test_create_new_connection (); */
}

void autodiscovery_tests_run ()
{
	g_printf ("Testing Autodiscovery\n");
}
