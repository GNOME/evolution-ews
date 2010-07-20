noinst_PROGRAMS = create-account soap-test

SERVERS_EWS_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libeews\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	libeews-1.2.la					\
	$(EVOLUTION_DATA_SERVER_CFLAGS)					\
	$(LIBEDATASERVER_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)

create_account_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS)
create_account_SOURCES = create-account.c
create_account_LDADD = 						\
	$(SOUP_LIBS)						\
	$(LIBEBACKEND_LIBS)	\
	$(LIBEDATASERVER_LIBS)	\
	$(E_DATA_SERVER_LIBS)

soap_test_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS)
soap_test_SOURCES = soap-test.c
soap_test_LDADD = 						\
	$(SOUP_LIBS)						\
	libeews-1.2.la					\
	$(E_DATA_SERVER_LIBS)

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

libeews_1_2_la_SOURCES =			\
	e-ews-connection.c			\
	e-ews-connection.h			\
	e-ews-container.c			\
	e-ews-container.h			\
	e-ews-item.c				\
	e-ews-item.h				\
	e-ews-sendoptions.h			\
	e-ews-sendoptions.c			\
	e-ews-message.c				\
	e-ews-message.h                          \
	e-ews-filter.c                           \
	e-ews-filter.h                           \
	e-ews-recur-utils.c                      \
	e-ews-recur-utils.h			\
	e-ews-proxy.h				\
	e-ews-proxy.c				\
	soup-soap-message.c			\
	soup-soap-response.c			\
	build-timestamp.h

libeews_1_2_la_LIBADD =						\
	$(LIBEBACKEND_LIBS)						\
	$(LIBEDATASERVER_LIBS)						\
	$(E_DATA_SERVER_LIBS)						\
	$(SOUP_LIBS)

libeews_1_2_la_LDFLAGS = -no-undefined

libeewsincludedir = $(privincludedir)/ews

libeewsinclude_HEADERS =                  \
        e-ews-connection.h                       \
	e-ews-container.h                        \
	e-ews-message.h                          \
	e-ews-item.h                             \
	e-ews-sendoptions.h			\
	e-ews-filter.h                        	\
	e-ews-recur-utils.h			\
	e-ews-proxy.h				\
	soup-soap-message.h			\
	soup-soap-response.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libeews-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)


-include $(top_srcdir)/git.mk
