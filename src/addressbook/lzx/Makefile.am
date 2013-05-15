NULL =

lib_LTLIBRARIES = liblzx.la

if HAVE_MSPACK
USE_MSPACK_CFLAGS = $(MSPACK_CFLAGS)
USE_MSPACK_LIBS = $(MSPACK_LIBS)
else
MSPACK_SRCS = lzx.h lzxd.c readbits.h readhuff.h
endif

liblzx_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(GNOME_PLATFORM_CFLAGS) \
	$(USE_MSPACK_CFLAGS) \
	$(NULL)

liblzx_la_SOURCES = \
	ews-oal-decompress.h \
	ews-oal-decompress.c \
	$(MSPACK_SRCS) \
	$(NULL)

liblzx_la_LDFLAGS = $(NO_UNDEFINED)

liblzx_la_LIBADD = \
	$(GNOME_PLATFORM_LIBS) \
	$(LIBM) \
	$(USE_MSPACK_LIBS) \
	$(NULL)

noinst_PROGRAMS = gal-lzx-decompress-test

gal_lzx_decompress_test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(GNOME_PLATFORM_CFLAGS) \
	$(USE_MSPACK_CFLAGS) \
	$(NULL)

gal_lzx_decompress_test_SOURCES = \
	gal-lzx-decompress-test.c

gal_lzx_decompress_test_LDADD = \
	liblzx.la \
	$(GNOME_PLATFORM_LIBS) \
	$(NULL)

-include $(top_srcdir)/git.mk
