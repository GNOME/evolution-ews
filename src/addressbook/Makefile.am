NULL =

SUBDIRS = lzx .

ebook_backend_LTLIBRARIES = libebookbackendews.la

libebookbackendews_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libebookbackend\" \
	-DEXCHANGE_EWS_LOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	$(LIBEBACKEND_CFLAGS) \
	$(LIBEDATASERVER_CFLAGS) \
	$(LIBEBOOK_CFLAGS) \
	$(DB_CFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)

libebookbackendews_la_SOURCES = \
	ews-book-backend-sqlitedb.c \
	ews-book-backend-sqlitedb.h \
	ews-oab-props.h \
	ews-oab-decoder.c \
	ews-oab-decoder.h \
	e-book-backend-ews.c \
	e-book-backend-ews.h \
	e-book-backend-ews-factory.c \
	$(NULL)

libebookbackendews_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(top_builddir)/src/addressbook/lzx/liblzx.la \
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

noinst_PROGRAMS = ews-book-backend-sqlitedb-test

ews_book_backend_sqlitedb_test_CPPFLAGS = \
	$(libebookbackendews_la_CPPFLAGS) \
	$(NULL)

ews_book_backend_sqlitedb_test_SOURCES = \
	ews-book-backend-sqlitedb-test.c \
	ews-book-backend-sqlitedb.c \
	ews-book-backend-sqlitedb.h \
	$(NULL)

ews_book_backend_sqlitedb_test_LDADD =  \
	$(libebookbackendews_la_LIBADD) \
	$(NULL)

-include $(top_srcdir)/git.mk
