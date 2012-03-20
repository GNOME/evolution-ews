/*Placeholder for license header*/
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <e-ews-folder.h>

/* Test result of created folder id */
extern EwsFolderId *folder_id;

void set_oof_settings_test_run ();
void get_oof_settings_test_run ();
void connection_tests_run ();
void op_tests_run ();
void cuditem_tests_run ();
void autocompletion_tests_run ();
void createfolder_tests_run ();
void deletefolder_tests_run (gconstpointer data);
void get_attachments_tests_run ();
void get_delegate_tests_run ();

static void
finalize_test_data ()
{
	if (folder_id) {
		e_ews_folder_free_fid (folder_id);
		folder_id = NULL;
	}
}

int main (int argc, char *argv[])
{
	gint ret;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	/*Create test suites and use more features of g_test */

	/*Register tests*/
//	g_test_add_func ("/libews/connections", connection_tests_run);
	g_test_add_func ("/libews/autocompletion", autocompletion_tests_run);

	g_test_add_func ("/libews/syncfolder", op_tests_run);
	g_test_add_func ("/libews/setoofsettings", set_oof_settings_test_run);
	g_test_add_func ("/libews/getoofsettings", get_oof_settings_test_run);

	/* delete folder uses the data from create_folder test. */
	g_test_add_func ("/libews/createfolder", createfolder_tests_run);
	g_test_add_data_func ("/libews/deletefolder", &folder_id, deletefolder_tests_run);

	g_test_add_func ("/libews/cuditem", cuditem_tests_run);
	g_test_add_func ("/libews/getdelegate", get_delegate_tests_run);


	ret = g_test_run ();
	finalize_test_data ();

	return ret;
}
