#noinst_PROGRAMS = create-account

SERVERS_EWS_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libeews\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	libeews-1.2.la					\
	$(EVOLUTION_DATA_SERVER_CFLAGS)					\
	$(LIBEDATASERVER_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)

#create_account_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS)
#create_account_SOURCES = create-account.c
#create_account_LDADD = 						\
	$(SOUP_LIBS)						\
	$(LIBEBACKEND_LIBS)	\
	$(LIBEDATASERVER_LIBS)	\
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

libeews_1_2_la_SOURCES =		\
	build-timestamp.h		\
	e-ews-connection.c		\
	e-ews-connection.h		\
	e-ews-message.c			\
	e2k-action.c			\
	e2k-action.h			\
	e2k-autoconfig.c		\
	e2k-autoconfig.h		\
	e2k-context.c			\
	e2k-context.h			\
	e2k-http-utils.c		\
	e2k-http-utils.h		\
	e2k-marshal.c			\
	e2k-marshal.h			\
	e2k-operation.c			\
	e2k-operation.h			\
	e2k-properties.c		\
	e2k-properties.h		\
	e2k-propnames.c			\
	e2k-propnames.h			\
	e2k-restriction.c		\
	e2k-restriction.h		\
	e2k-result.c			\
	e2k-result.h			\
	e2k-rule.c			\
	e2k-rule.h			\
	e2k-types.h			\
	e2k-uri.c			\
	e2k-uri.h			\
	e2k-utils.c			\
	e2k-utils.h			\
	e2k-validate.h			\
	e2k-xml-utils.c			\
	e2k-xml-utils.h			\
	mapi.h				\
	xntlm.c				\
	xntlm.h				\
	xntlm-des.c			\
	xntlm-des.h			\
	xntlm-md4.c			\
	xntlm-md4.h			\
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
