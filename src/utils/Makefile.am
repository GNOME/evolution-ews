privlib_LTLIBRARIES = libewsutils.la

libewsutils_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libeews\"			\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src/server			\
	-I$(top_builddir)				\
	-I$(top_builddir)/src/server			\
	$(LIBEDATASERVER_CFLAGS)			\
	$(SQLITE3_CFLAGS)				\
	$(DEBUG_CFLAGS)					\
	$(CAMEL_CFLAGS)

libewsutils_la_SOURCES =				\
	ews-camel-compat.h				\
	ews-camel-compat.c				\
	ews-esource-utils.h				\
	ews-esource-utils.c				\
	e-sqlite3-vfs.c					\
	e-sqlite3-vfs.h					\
	ews-camel-common.c				\
	ews-camel-common.h

libewsutils_la_LIBADD =					\
	$(top_builddir)/src/server/libeews-1.2.la 	\
	$(SQLITE3_LIBS)	\
	$(LIBEDATASERVER_LIBS)				\
	$(CAMEL_LIBS)

libewsutils_la_LDFLAGS = $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk
