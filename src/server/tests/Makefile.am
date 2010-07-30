#Placeholder for TestCases. Should have a target called  make test, test-run.

SERVERS_EWS_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libeews\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	$(EVOLUTION_DATA_SERVER_CFLAGS)					\
	$(LIBEDATASERVER_CFLAGS)			\
	$(E_DATA_SERVER_CFLAGS)

noinst_PROGRAMS = $(TEST_PROGS)

TESTS = testews
check_PROGRAMS = testews
testews_SOURCES = \
		test_libews.c \
		test_connection.c
testews_LDADD = -lglib-2.0
testews_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS)

DISTCLEANFILES = $(TEST_PROGS)