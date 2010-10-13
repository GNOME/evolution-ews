SERVERS_EWS_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libeews\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	$(EVOLUTION_DATA_SERVER_CFLAGS)					\
	$(LIBEDATASERVER_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)

lib_LTLIBRARIES = libeews-1.2.la

libeews_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libeews\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_builddir)/src/server				\
	$(SOUP_CFLAGS)					\
	$(LIBEBACKEND_CFLAGS)				\
	$(LIBEDATASERVER_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)				\
	$(DEBUG_CFLAGS)

libeews_1_2_la_SOURCES =		\
	build-timestamp.h		\
	e-ews-connection.c		\
	e-ews-connection.h		\
	e-ews-message.c			\
	soup-soap-message.c		\
	soup-soap-response.c

libeews_1_2_la_LIBADD =						\
	$(LIBEBACKEND_LIBS)						\
	$(LIBEDATASERVER_LIBS)						\
	$(E_DATA_SERVER_LIBS)						\
	$(SOUP_LIBS)						\
	$(SOCKET_LIBS)

libeews_1_2_la_LDFLAGS = $(NO_UNDEFINED)

libeewsincludedir = $(privincludedir)/ews

libeewsinclude_HEADERS =		\
        e-ews-connection.h		\
	e-ews-message.h			\
	soup-soap-message.h		\
	soup-soap-response.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libeews-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)


-include $(top_srcdir)/git.mk
