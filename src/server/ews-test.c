/* EWS test program / utility */

#include <glib/gmain.h>
#include "e-ews-connection.h"
#include "e-ews-message.h"
#include "e2k-context.h"
#include "e2k-uri.h"
#include "e2k-http-utils.h"

static GMainLoop *main_loop;
static char *arg_hostname, *arg_username, *arg_password, *arg_domain;
E2kContext *ctx = NULL;

SoupMessage *
e2k_ews_message_new_with_header (const char *uri, const char *auth_nego, 
		const char *method_name)
{
	SoupSoapMessage *msg;
	SoupMessage *soup_msg;
	char *method; 

	msg = soup_soap_message_new (SOUP_METHOD_POST, uri, FALSE, NULL, "soap", NULL);
	if (!msg) {
		g_warning (G_STRLOC ": Could not build SOAP message");
		return NULL;
	}

	soup_msg = SOUP_MESSAGE (msg);
	soup_message_headers_append (soup_msg->request_headers, "User-Agent",
			"Evolution/2.11.90");
	soup_message_headers_append (soup_msg->request_headers, "SOAPAction", method_name);

	if (auth_nego && *auth_nego && strlen (auth_nego) > 12) 
		soup_message_headers_append (soup_msg->request_headers, "Authorization", auth_nego);

	return soup_msg;
}

E2kContext *
e2k_ews_test_get_context (const char *uri)
{
	E2kContext *ctx;
	E2kUri *euri;

	ctx = e2k_context_new (uri);
	if (!ctx) {
		fprintf (stderr, "Could not parse %s as URI\n", uri);
		exit (1);
	}

	euri = e2k_uri_new (uri);
	e2k_context_set_auth (ctx, arg_username, arg_domain,
			"NTLM", arg_password);

	e2k_uri_free (euri);
	return ctx;

}

static void
print_header (gpointer name, gpointer value, gpointer data)
{
	g_print ("%s:%s\n", (char *)name, (char *)value);
}

static gboolean
idle_cb (gpointer data)
{
	SoupMessage *msg;
	static int i = 0;

	char *url = NULL;
	E2kHTTPStatus status = 0;

	url = g_strdup_printf ("https://%s/EWS/Exchange.asmx", arg_hostname);
	g_print ("\n The Url is:%s", url);

	ctx = e2k_ews_test_get_context (url);

	/* Sync Folder hierarchy request */
	msg = e_ews_message_new_with_header (url, "SyncFolderHierarchy");
	soup_soap_message_start_element (msg, "FolderShape", NULL, NULL);
	e_ews_message_write_string_parameter (msg, "BaseShape", "types", "AllProperties");
	soup_soap_message_end_element (msg);

	/* Complete the footer and print the request */
	e_ews_message_write_footer (msg);

	/* Send the request */
	status = e2k_context_send_message (ctx, NULL, msg);

	/* Print the response */
	e_ews_message_write_response (msg);

	g_print ("\n\nReturned %d\n", status);

	/* Create folder operation. Create a folder by name TestBharath */

	msg = e_ews_message_new_with_header (url, "CreateFolder");

	soup_soap_message_start_element (msg, "ParentFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", "types", NULL, "Id", "msgfolderroot");
	soup_soap_message_end_element (msg);

	soup_soap_message_start_element (msg, "Folders", NULL, NULL);
	soup_soap_message_start_element (msg, "Folder", "types", NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", "types", "TestBharath");
	soup_soap_message_end_element (msg);
	soup_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);
	status = e2k_context_send_message (ctx, NULL, msg);

	e_ews_message_write_response (msg);

	g_print ("\n\nReturned %d\n", status);

	/* Create folder operation. Create a folder by name Test123 */
	msg = e_ews_message_new_with_header (url, "CreateFolder");

	soup_soap_message_start_element (msg, "ParentFolderId", NULL, NULL);
	e_ews_message_write_string_parameter_with_attribute (msg, "DistinguishedFolderId", "types", NULL, "Id", "msgfolderroot");
	soup_soap_message_end_element (msg);

	soup_soap_message_start_element (msg, "Folders", NULL, NULL);
	soup_soap_message_start_element (msg, "Folder", "types", NULL);
	e_ews_message_write_string_parameter (msg, "DisplayName", "types", "Test123");
	soup_soap_message_end_element (msg);
	soup_soap_message_end_element (msg);

	e_ews_message_write_footer (msg);
	status = e2k_context_send_message (ctx, NULL, msg);

	e_ews_message_write_response (msg);

	g_print ("\n\nReturned %d\n", status);
//	soup_message_headers_foreach (msg->response_headers, print_header, NULL);

	g_main_loop_quit (main_loop);

	return FALSE;
}

int
main (int argc, char *argv[])
{
	g_type_init ();
	g_thread_init (NULL);

	if (argc < 5) {
		g_print ("Usage: %s hostname domain username password\n", argv[0]);
		return -1;
	}

	arg_hostname = argv[1];
	arg_domain = argv[2];
	arg_username = argv[3];
	arg_password = argv[4];

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);

	return 0;
}
