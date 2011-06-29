lib_LTLIBRARIES = liblzx.la

liblzx_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)				\
	$(GNOME_PLATFORM_CFLAGS)

liblzx_la_SOURCES =				\
	lzx.h					\
	ews-oal-decompress.h			\
	lzxd.c					\
	ews-oal-decompress.c

liblzx_la_LDFLAGS = $(NO_UNDEFINED)

liblzx_la_LIBADD = 				\
	$(GNOME_PLATFORM_LIBS)
