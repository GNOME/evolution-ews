/*Placeholder for license header*/
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <e-ews-folder.h>

/* Test result of created folder id */
extern EwsFolderId *folder_id;

void connection_tests_run ();
void autodiscovery_tests_run ();
void op_tests_run ();
void cuditem_tests_run ();
void autocompletion_tests_run ();
void createfolder_tests_run ();
void deletefolder_tests_run (gconstpointer data);
void get_attachments_tests_run ();

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
	/*Create test suites and use more features of g_test */

	/*Register tests*/
	g_test_add_func ("/libews/connections", connection_tests_run);
	g_test_add_func ("/libews/autodiscovery", autodiscovery_tests_run);
	g_test_add_func ("/libews/autocompletion", autocompletion_tests_run);

	/* delete folder uses the data from create_folder test. */
	g_test_add_func ("/libews/createfolder", createfolder_tests_run);
	g_test_add_data_func ("/libews/deletefolder", &folder_id, deletefolder_tests_run);

	g_test_add_func ("/libews/cuditem", cuditem_tests_run);

	g_test_add_func ("/libews/getattachment", get_attachments_tests_run);

	ret = g_test_run ();
	finalize_test_data ();

	return ret;
}
