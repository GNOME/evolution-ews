camel_provider_LTLIBRARIES = libcamelews.la
camel_provider_DATA = libcamelews.urls

libcamelews_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)/src			\
	-I$(top_srcdir)/src/camel		\
	-I$(top_srcdir)/src/server	        \
	$(CAMEL_CFLAGS)				\
	$(LIBEDATASERVER_CFLAGS)		\
	$(LIBECAL_CFLAGS)			\
	$(EVOLUTION_PLUGIN_CFLAGS)		\
	$(GNOME_INCLUDEDIR)			\
	$(SOUP_CFLAGS)                          \
	$(LIBEBACKEND_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)			\
	-DG_LOG_DOMAIN=\"camel-ews-provider\"

libcamelews_la_SOURCES = 			\
	camel-ews-folder.c                \
	camel-ews-store.c                 \
	camel-ews-summary.c               \
	camel-ews-provider.c		\
	camel-ews-store-summary.c         \
	camel-ews-utils.c			\
	camel-ews-transport.c     	       	

noinst_HEADERS =         			\
	camel-ews-folder.h		\
	camel-ews-private.h		\
	camel-ews-store.h			\
	camel-ews-summary.h		\
	camel-ews-store-summary.h		\
	camel-ews-utils.h			\
	camel-ews-transport.h		

libcamelews_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)

libcamelews_la_LIBADD =                                   \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(CAMEL_LIBS)                                           \
	$(EVOLUTION_PLUGIN_LIBS)		\
	$(LIBEDATASERVER_LIBS)			\
	$(LIBEBACKEND_LIBS)			\
	$(LIBECAL_LIBS)                                                    \
	$(SOUP_LIBS)							 \
	$(E_DATA_SERVER_LIBS)

EXTRA_DIST = libcamelews.urls

-include $(top_srcdir)/git.mk
