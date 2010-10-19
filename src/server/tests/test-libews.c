/*Placeholder for license header*/
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>

void connection_tests_run ();
void autodiscovery_tests_run ();

int main (int argc, char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	/*Create test suites and use more features of g_test */

	/*Register tests*/
	g_test_add_func ("/libews/connections", connection_tests_run);
	g_test_add_func ("/libews/autodiscovery", autodiscovery_tests_run);

	return g_test_run ();
}
