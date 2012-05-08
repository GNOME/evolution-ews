#Placeholder for TestCases. Should have a target called  make test, test-run.

NULL =

SERVERS_EWS_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libeews\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	$(SOUP_CFLAGS) \
	$(EVOLUTION_DATA_SERVER_CFLAGS) \
	$(LIBEDATASERVER_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(NULL)

libeews = $(top_builddir)/src/server/libeews-1.2.la

noinst_PROGRAMS = $(TEST_PROGS)

TESTS = testews
check_PROGRAMS = testews
testews_SOURCES =  \
	utils.c  \
	test-connection.c \
	test-syncfolder.c \
	test-cuditem.c  \
	test-getattachment.c \
	test-autocompletion.c \
	test-createfolder.c \
	test-deletefolder.c \
	test-getdelegate.c \
	test-libews.c  \
	test-get-oof-settings.c \
	test-set-oof-settings.c \
	utils.h \
	$(NULL)

#Fixme: Ugly. Kill me later
testews_LDADD = $(LIBEDATASERVER_LIBS) $(libeews)
testews_CPPFLAGS = $(SERVERS_EWS_CPPFLAGS)

DISTCLEANFILES = $(TEST_PROGS)

-include $(top_srcdir)/git.mk
