NULL =

lib_LTLIBRARIES = liblzx.la

liblzx_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(GNOME_PLATFORM_CFLAGS) \
	$(NULL)

liblzx_la_SOURCES = \
	lzx.h \
	ews-oal-decompress.h \
	lzxd.c \
	ews-oal-decompress.c \
	readbits.h \
	readhuff.h \
	$(NULL)

liblzx_la_LDFLAGS = $(NO_UNDEFINED)

liblzx_la_LIBADD = \
	$(GNOME_PLATFORM_LIBS) \
	$(LIBM) \
	$(NULL)

noinst_PROGRAMS = gal-lzx-decompress-test

gal_lzx_decompress_test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(GNOME_PLATFORM_CFLAGS) \
	$(NULL)

gal_lzx_decompress_test_SOURCES = \
	gal-lzx-decompress-test.c

gal_lzx_decompress_test_LDADD = \
	liblzx.la

-include $(top_srcdir)/git.mk
