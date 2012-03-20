SUBDIRS = lzx .

if ENABLE_GTK3
ebook_backend_LTLIBRARIES = libebookbackendews.la
else
extension_LTLIBRARIES = libebookbackendews.la
endif

libebookbackendews_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)/src/addressbook			\
	-I$(top_srcdir)/src/server	                \
	-I$(top_srcdir)/src/utils	                \
	$(LIBEBACKEND_CFLAGS)				\
	$(LIBEDATASERVER_CFLAGS)			\
	$(LIBEBOOK_CFLAGS)				\
	$(DB_CFLAGS)					\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)                 \
	$(SQLITE3_CFLAGS)			\
	$(SOUP_CFLAGS)	

libebookbackendews_la_SOURCES =			\
	e-book-backend-sqlitedb.c		\
	e-book-backend-sqlitedb.h		\
	ews-oab-props.h				\
	ews-oab-decoder.c			\
	ews-oab-decoder.h			\
	e-book-backend-ews.c			\
	e-book-backend-ews.h			\
	e-book-backend-ews-factory.c

libebookbackendews_la_LIBADD =					\
	$(top_builddir)/src/server/libeews-1.2.la		\
	$(top_builddir)/src/utils/libewsutils.la		\
	$(top_builddir)/src/addressbook/lzx/liblzx.la		\
	$(LIBEBACKEND_LIBS)					\
	$(LIBEDATASERVER_LIBS)					\
	$(LIBEDATABOOK_LIBS) 					\
	$(LIBEBOOK_LIBS)					\
	$(DB_LIBS)						\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(SQLITE3_LIBS)						\
	$(SOUP_LIBS)

libebookbackendews_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED)

noinst_PROGRAMS = e-book-backend-sqlitedb-test

e_book_backend_sqlitedb_test_CPPFLAGS =					\
	$(libebookbackendews_la_CPPFLAGS)
e_book_backend_sqlitedb_test_SOURCES = e-book-backend-sqlitedb-test.c	\
			       e-book-backend-sqlitedb.c		\
			       e-book-backend-sqlitedb.h
e_book_backend_sqlitedb_test_LDADD = 					\
	$(libebookbackendews_la_LIBADD)

-include $(top_srcdir)/git.mk
