
#include <config.h>
#include <gconf/gconf-client.h>
#include <glib.h>
#include <libedataserver/e-source-list.h>

static GConfClient *conf_client;
static GMainLoop *main_loop;
static gchar *arg_hostname, *arg_username, *arg_password;

static void
add_account (const gchar *conf_key, const gchar *hostname, const gchar *username)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	gchar *group_name;

	source_list = e_source_list_new_for_gconf (conf_client, conf_key);

	group_name = g_strdup (hostname);
	group = e_source_group_new (group_name, "groupwise://");
	e_source_list_add_group (source_list, group, -1);

	g_free (group_name);
	group_name = g_strdup_printf ("%s:7181/soap/", hostname);
	source = e_source_new ("Calendar", group_name);
	e_source_set_property (source, "auth", "1");
	e_source_set_property (source, "username", username);
	e_source_group_add_source (group, source, -1);

	e_source_list_sync (source_list, NULL);

	g_free (group_name);
	g_object_unref (source);
	g_object_unref (group);
	g_object_unref (source_list);
}

static gboolean
idle_cb (gpointer data)
{
	add_account ("/apps/evolution/calendar/sources", arg_hostname, arg_username);
	add_account ("/apps/evolution/tasks/sources", arg_hostname, arg_username);

	g_main_loop_quit (main_loop);

	return FALSE;
}

gint
main (gint argc, gchar *argv[])
{
	g_type_init ();
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

	conf_client = gconf_client_get_default ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_object_unref (conf_client);
	g_main_loop_unref (main_loop);

	return 0;
}
