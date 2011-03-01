#Placeholder for TestCases. Should have a target called  make test, test-run.

SERVERS_EWS_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libeews\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	$(EVOLUTION_DATA_SERVER_CFLAGS)					\
	$(LIBEDATASERVER_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)

libeews = $(top_builddir)/src/server/libeews-1.2.la

noinst_PROGRAMS = $(TEST_PROGS)

TESTS = testews
check_PROGRAMS = testews
testews_SOURCES = \
		utils.c \
		test-connection.c \
		test-libews.c \
		utils.h

#Fixme: Ugly. Kill me later
testews_LDADD = -lglib-2.0 $(libeews)
testews_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS) -I$(top_srcdir)/src/server/

DISTCLEANFILES = $(TEST_PROGS)

-include $(top_srcdir)/git.mk
