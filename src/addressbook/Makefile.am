NULL =

ebook_backend_LTLIBRARIES = libebookbackendews.la

if HAVE_MSPACK
USE_MSPACK_CFLAGS = $(MSPACK_CFLAGS)
USE_MSPACK_LIBS = $(MSPACK_LIBS)
MSPACK_SRCS = ews-oab-decompress.c
else
MSPACK_SRCS = mspack/lzx.h mspack/lzxd.c mspack/readbits.h mspack/readhuff.h mspack/oab-decompress.c
endif

libebookbackendews_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libebookbackend\" \
	-DEXCHANGE_EWS_LOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	$(USE_MSPACK_CFLAGS) \
	$(LIBEBACKEND_CFLAGS) \
	$(LIBEDATASERVER_CFLAGS) \
	$(LIBEBOOK_CFLAGS) \
	$(DB_CFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)

libebookbackendews_la_SOURCES = \
	ews-oab-props.h \
	ews-oab-decoder.c \
	ews-oab-decoder.h \
	ews-oab-decompress.h \
	e-book-backend-ews.c \
	e-book-backend-ews.h \
	e-book-backend-ews-factory.c \
	$(MSPACK_SRCS) \
	$(NULL)

libebookbackendews_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(USE_MSPACK_LIBS) \
	$(LIBEBACKEND_LIBS) \
	$(LIBEDATASERVER_LIBS) \
	$(LIBEDATABOOK_LIBS)  \
	$(LIBEBOOK_LIBS) \
	$(DB_LIBS) \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(SQLITE3_LIBS) \
	$(SOUP_LIBS) \
	$(LIBM) \
	$(NULL)

libebookbackendews_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED)

noinst_PROGRAMS = gal-lzx-decompress-test oab-decode-test

oab_decode_test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(LIBEBOOK_CFLAGS) \
	$(LIBEDATABOOK_CFLAGS)  \
	$(GNOME_PLATFORM_CFLAGS) \
	$(NULL)

oab_decode_test_SOURCES = \
	oab-decode-test.c

oab_decode_test_LDADD = \
	$(GNOME_PLATFORM_LIBS) \
	$(LIBEBOOK_LIBS) \
	$(LIBEDATABOOK_LIBS)  \
	libebookbackendews.la
	$(NULL)

gal_lzx_decompress_test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(GNOME_PLATFORM_CFLAGS) \
	$(USE_MSPACK_CFLAGS) \
	$(NULL)

gal_lzx_decompress_test_SOURCES = \
	gal-lzx-decompress-test.c \
	$(MSPACK_SRCS)

gal_lzx_decompress_test_LDADD = \
	$(GNOME_PLATFORM_LIBS) \
	$(USE_MSPACK_LIBS) \
	$(NULL)

-include $(top_srcdir)/git.mk
