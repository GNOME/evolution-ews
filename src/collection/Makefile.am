NULL =

eds_module_LTLIBRARIES = module-ews-backend.la

module_ews_backend_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DEXCHANGE_EWS_LOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	$(LIBEDATASERVER_CFLAGS) \
	$(LIBEBACKEND_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)

module_ews_backend_la_SOURCES = \
	module-ews-backend.c \
	e-ews-backend.c \
	e-ews-backend.h \
	e-ews-backend-factory.c \
	e-ews-backend-factory.h \
	$(NULL)

module_ews_backend_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(LIBEDATASERVER_LIBS) \
	$(LIBEBACKEND_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)

module_ews_backend_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk
