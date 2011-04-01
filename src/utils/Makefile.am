lib_LTLIBRARIES = libewsutils.la

libewsutils_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libeews\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_builddir)/src/server			\
	$(LIBEDATASERVER_CFLAGS)			\
	$(DEBUG_CFLAGS)

libewsutils_la_SOURCES =				\
        ews-esource-utils.h				\
        ews-esource-utils.c				\
	e-sqlite3-vfs.c					\
	e-sqlite3-vfs.h

libewsutils_la_LIBADD =					\
	$(top_builddir)/src/server/libeews-1.2.la 	\
	$(LIBEDATASERVER_LIBS)				

libewsutils_la_LDFLAGS = $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk
