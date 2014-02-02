include $(top_srcdir)/gtester.mk

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	-DTEST_FILE_DIR="\"$(top_srcdir)/tests/\"" \
	-DCALENDAR_MODULE_DIR="\"$(top_srcdir)/src/calendar/\"" \
	-DG_LOG_DOMAIN=\"libeews-tests\" \
	$(UHTTPMOCK_CFLAGS) \
	$(LIBEDATASERVER_CFLAGS) \
	$(LIBECAL_CFLAGS) \
	$(NULL)

noinst_PROGRAMS = $(TEST_PROGS)

LIBS = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(UHTTPMOCK_LIBS) \
	$(LIBEDATASERVER_LIBS) \
	$(LIBECAL_LIBS) \
	$(NULL)

TEST_SRCS = \
	ews-test-common.c \
	ews-test-common.h \
	$(NULL)

EXTRA_DIST += \
	traces/Exchange2007_SP1/camel/folder/create_distinguished_folder_subfolder \
	traces/Exchange2007_SP1/camel/folder/create_existing_folder \
	traces/Exchange2007_SP1/camel/folder/create_folder_with_invalid_distinguished_parent_id \
	traces/Exchange2007_SP1/camel/folder/create_folder_with_invalid_non_distinguished_parent_id \
	traces/Exchange2007_SP1/camel/folder/create_folder_with_malformed_parent_id \
	traces/Exchange2007_SP1/camel/folder/create_non_distinguished_folder_subfolder \
	traces/Exchange2007_SP1/camel/folder/hard_delete_distinguished_folder_subfolder \
	traces/Exchange2007_SP1/camel/folder/hard_delete_non_distinguished_folder_subfolder \
	traces/Exchange2007_SP1/camel/folder/hard_delete_root_subfolder \
	traces/Exchange2007_SP1/camel/folder/move_folder_to_folder_that_has_subfolder_with_the_same_name \
	traces/Exchange2007_SP1/camel/folder/move_folder_to_msgfolderroot \
	traces/Exchange2007_SP1/camel/folder/move_folder_to_valid_folder \
	traces/Exchange2007_SP1/camel/folder/move_invalid_folder_to_valid_folder \
	traces/Exchange2007_SP1/camel/folder/move_valid_folder_to_invalid_folder \
	\
	traces/Exchange2010_SP2/camel/folder/create_distinguished_folder_subfolder \
	traces/Exchange2010_SP2/camel/folder/create_existing_folder \
	traces/Exchange2010_SP2/camel/folder/create_folder_with_invalid_distinguished_parent_id \
	traces/Exchange2010_SP2/camel/folder/create_folder_with_invalid_non_distinguished_parent_id \
	traces/Exchange2010_SP2/camel/folder/create_folder_with_malformed_parent_id \
	traces/Exchange2010_SP2/camel/folder/create_non_distinguished_folder_subfolder \
	traces/Exchange2010_SP2/camel/folder/hard_delete_distinguished_folder_subfolder \
	traces/Exchange2010_SP2/camel/folder/hard_delete_non_distinguished_folder_subfolder \
	traces/Exchange2010_SP2/camel/folder/hard_delete_root_subfolder \
	traces/Exchange2010_SP2/camel/folder/move_folder_to_folder_that_has_subfolder_with_the_same_name \
	traces/Exchange2010_SP2/camel/folder/move_folder_to_msgfolderroot \
	traces/Exchange2010_SP2/camel/folder/move_folder_to_valid_folder \
	traces/Exchange2010_SP2/camel/folder/move_invalid_folder_to_valid_folder \
	traces/Exchange2010_SP2/camel/folder/move_valid_folder_to_invalid_folder \
	\
	traces/Exchange2010_SP2/calendar/timezones/get_server_time_zones_sync \
	$(NULL)

TEST_PROGS			+= ews-test-camel ews-test-timezones

ews_test_camel_SOURCES		 = ews-test-camel.c $(TEST_SRCS)
ews_test_timezones_SOURCES	 = ews-test-timezones.c $(TEST_SRCS)

-include $(top_srcdir)/git.mk
