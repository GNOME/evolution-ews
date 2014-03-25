NULL =

ecal_backend_LTLIBRARIES = libecalbackendews.la

ewsdata_DATA = windowsZones.xml

libecalbackendews_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DEXCHANGE_EWS_LOCALEDIR=\"$(localedir)\" \
	-DEXCHANGE_EWS_DATADIR=\""$(ewsdatadir)"\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(LIBEBACKEND_CFLAGS) \
	$(LIBECAL_CFLAGS) \
	$(LIBEDATACAL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(DEBUG_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libecalbackendews_la_SOURCES = \
	e-cal-backend-ews-factory.c \
	e-cal-backend-ews.c \
	e-cal-backend-ews.h \
	e-cal-backend-ews-utils.c \
	e-cal-backend-ews-utils.h \
	$(NULL)

libecalbackendews_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(LIBEBACKEND_LIBS) \
	$(LIBECAL_LIBS) \
	$(LIBEDATACAL_LIBS) \
	$(SOUP_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

libecalbackendews_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) $(CODE_COVERAGE_LDFLAGS)

-include $(top_srcdir)/git.mk

EXTRA_DIST = $(ewsdata_DATA)
