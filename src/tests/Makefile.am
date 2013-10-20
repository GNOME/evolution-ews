include $(top_srcdir)/gtester.mk

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	-DTEST_FILE_DIR="\"$(top_srcdir)/src/tests/\"" \
	-DG_LOG_DOMAIN=\"libeews\" \
	-DLOCALEDIR="\"$(datadir)/locale\"" \
	$(UHTTPMOCK_CFLAGS) \
	$(LIBEDATASERVER_CFLAGS) \
	$(NULL)


noinst_PROGRAMS = $(TEST_PROGS)

LIBS = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(UHTTPMOCK_LIBS) \
	$(LIBEDATASERVER_LIBS) \
	$(NULL)

TEST_SRCS = \
	ews-test-common.c \
	ews-test-common.h \
	$(NULL)

TEST_PROGS			+= ews-test-camel
ews_test_camel_SOURCES		 = ews-test-camel.c $(TEST_SRCS)

-include $(top_srcdir)/git.mk
