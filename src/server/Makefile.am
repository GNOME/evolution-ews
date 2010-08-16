#noinst_PROGRAMS = create-account
noinst_PROGRAMS = soap-test ews-test

SERVERS_EWS_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libeews\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	$(EVOLUTION_DATA_SERVER_CFLAGS)					\
	$(LIBEDATASERVER_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)

soap_test_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS)
soap_test_SOURCES = soap-test.c
soap_test_LDADD = 						\
	$(SOUP_LIBS)						\
	libeews-1.2.la					\
	$(E_DATA_SERVER_LIBS)

ews_test_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS)
ews_test_SOURCES = ews-test.c
ews_test_LDADD = 						\
	$(SOUP_LIBS)						\
	libeews-1.2.la					\
	$(E_DATA_SERVER_LIBS)				\
	$(GNOME_PLATFORM_LIBS)

build-timestamp.h : $(top_builddir)/config.h
	echo "const char *build_timestamp = \"Evolution-Data-Server-@BASE_VERSION@-`date +%F`\";" > $@

BUILT_SOURCES = build-timestamp.h
NODIST_FILES = build-timestamp.h
CLEANFILES = build-timestamp.h

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
	e-ews-context.c			\
	e-ews-context.h			\
	e-ews-http-utils.h		\
	e-ews-message.c			\
	e-ews-operation.c		\
	e-ews-operation.h		\
	e-ews-types.h			\
	e-ews-uri.c			\
	e-ews-uri.h			\
	e-ews-utils.c			\
	e-ews-utils.h			\
	e-ews-xml-utils.c		\
	e-ews-xml-utils.h		\
	soup-soap-message.c		\
	soup-soap-response.c

libeews_1_2_la_LIBADD =						\
	$(LIBEBACKEND_LIBS)						\
	$(LIBEDATASERVER_LIBS)						\
	$(E_DATA_SERVER_LIBS)						\
	$(SOUP_LIBS)

libeews_1_2_la_LDFLAGS = -no-undefined

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
