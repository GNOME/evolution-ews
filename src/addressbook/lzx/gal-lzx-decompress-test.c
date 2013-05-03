
#include "ews-oal-decompress.h"

gint
main (gint argc,
      gchar *argv[])
{
	if (argc != 3 && argc != 4) {
		g_print ("Pass an lzx file and an output filename as argument \n");
		return -1;
	}

	if (argc == 4) {
		g_print("Applying binary patch %s to %s to create %s\n",
			argv[1], argv[2], argv[3]);
		if (oal_apply_binpatch(argv[1], argv[2], argv[3], NULL))
			g_print("Successfully applied\n");
		else
			g_print("apply failed\n");
	} else


	if (oal_decompress_v4_full_detail_file (argv[1], argv[2], NULL))
		g_print ("Successfully decompressed \n");
	else
		g_print ("decompression failed \n");

	return 0;
}
