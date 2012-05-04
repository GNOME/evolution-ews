NULL =

privlib_LTLIBRARIES = libewsutils.la

libewsutils_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libeews\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	$(LIBEDATASERVER_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(DEBUG_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(LIBEDATACAL_CFLAGS) \
	$(NULL)

libewsutils_la_SOURCES = \
	e-sqlite3-vfs.c \
	e-sqlite3-vfs.h \
	ews-camel-common.c \
	ews-camel-common.h \
	e-ews-query-to-restriction.c \
	e-ews-query-to-restriction.h \
	$(NULL)

libewsutils_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la  \
	$(SQLITE3_LIBS) \
	$(LIBEDATASERVER_LIBS) \
	$(CAMEL_LIBS) \
	$(LIBEDATACAL_LIBS) \
	$(NULL)

libewsutils_la_LDFLAGS = $(NO_UNDEFINED)

noinst_PROGRAMS = ews-test-finditem-query

TESTS = ews-test-finditem-query
check_PROGRAMS = ews-test-finditem-query

ews_test_finditem_query_CPPFLAGS = \
	$(libewsutils_la_CPPFLAGS) \
	$(NULL)

ews_test_finditem_query_SOURCES =  \
	ews-test-finditem-query.c \
	e-ews-query-to-restriction.c \
	e-ews-query-to-restriction.h \
	$(NULL)

ews_test_finditem_query_LDADD = \
	$(libewsutils_la_LIBADD) \
	$(NULL)

-include $(top_srcdir)/git.mk
