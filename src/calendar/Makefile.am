extension_LTLIBRARIES = libecalbackendews.la

libecalbackendews_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/src/calendar			\
	-I$(top_builddir)/src/calendar			\
	-I$(top_srcdir)/src/server		\
	-I$(top_builddir)/src/server		\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(LIBEBACKEND_CFLAGS)    	                \
	$(LIBECAL_CFLAGS)               	        \
	$(LIBEDATACAL_CFLAGS)                   	\
	$(SOUP_CFLAGS)					\
	$(DEBUG_CFLAGS)

libecalbackendews_la_SOURCES =		\
	e-cal-backend-ews-factory.c	\
	e-cal-backend-ews-factory.h	\
	e-cal-backend-ews.c		\
	e-cal-backend-ews.h		\
	e-cal-backend-ews-utils.c		\
	e-cal-backend-ews-utils.h

libecalbackendews_la_LIBADD =					\
	$(top_builddir)/src/server/libeews-1.2.la		\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(LIBEBACKEND_LIBS)    	                \
	$(LIBECAL_LIBS)               	        \
	$(LIBEDATACAL_LIBS)                   	\
	$(SOUP_LIBS)

libecalbackendews_la_LDFLAGS =		\
	-module -avoid-version -no-undefined

-include $(top_srcdir)/git.mk
