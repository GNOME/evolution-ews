
#include "ews-oal-decompress.h"

gint
main (gint argc,
      gchar *argv[])
{
	if (argc != 3) {
		g_print ("Pass an lzx file and an output filename as argument \n");
		return -1;
	}

	if (oal_decompress_v4_full_detail_file (argv[1], argv[2], NULL))
		g_print ("Successfully decompressed \n");
	else
		g_print ("decompression failed \n");

	return 0;
}
