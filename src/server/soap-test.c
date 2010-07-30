#include <config.h>
#include <glib.h>
#include "e-ews-connection.h"

static GMainLoop *main_loop;
static gchar *arg_hostname, *arg_username, *arg_password;

static void
display_container (EEwsContainer *container)
{
	g_print (" Container: %s\n", e_ews_container_get_name (container));
	g_print ("\tID: %s\n", e_ews_container_get_id (container));

	g_print ("\n");
}

static gboolean
idle_cb (gpointer data)
{
	EEwsConnection *cnc;

	cnc = e_ews_connection_new (arg_hostname, arg_username, arg_password);
	if (E_IS_EWS_CONNECTION (cnc)) {
		GList *container_list = NULL;

		g_print ("Connected to %s!\n", arg_hostname);

		/* get list of containers */
		g_print ("Getting list of containers...\n");
		if (e_ews_connection_get_container_list (cnc, "folders", &container_list) == E_EWS_CONNECTION_STATUS_OK) {
			GList *container;

			for (container = container_list; container != NULL; container = container->next)
				display_container (E_EWS_CONTAINER (container->data));

			e_ews_connection_free_container_list (container_list);
		}

		g_object_unref (cnc);
	} else
		g_print ("ERROR: Could not connect to %s\n", arg_hostname);

	g_main_loop_quit (main_loop);

	return FALSE;
}

gint
main (gint argc, gchar *argv[])
{
	g_type_init ();
	g_thread_init (NULL);

	if (argc != 3 && argc != 4) {
		g_print ("Usage: %s hostname username [password]\n", argv[0]);
		return -1;
	}

	arg_hostname = argv[1];
	arg_username = argv[2];
	if (argc == 4)
		arg_password = argv[3];
	else
		arg_password = NULL;

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);

	return 0;
}
