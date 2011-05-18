DIST_SUBDIRS = tests

lib_LTLIBRARIES = libeews-1.2.la

MARSHAL_GENERATED = ews-marshal.c ews-marshal.h

ews-marshal.h: ews-marshal.list
	$(AM_V_GEN) ( $(GLIB_GENMARSHAL) --prefix=ews_marshal $(srcdir)/ews-marshal.list --header > ews-marshal.tmp \
	&& mv ews-marshal.tmp ews-marshal.h ) \
	|| ( rm -f ews-marshal.tmp && exit 1 )

ews-marshal.c: ews-marshal.h
	$(AM_V_GEN) ( (echo '#include "ews-marshal.h"'; $(GLIB_GENMARSHAL) --prefix=ews_marshal $(srcdir)/ews-marshal.list --body) > ews-marshal.tmp \
	&& mv ews-marshal.tmp ews-marshal.c ) \
	|| ( rm -f ews-marshal.tmp && exit 1 )

e-ews-connection.c: $(MARSHAL_GENERATED)

libeews_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libeews\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_builddir)/src/server			\
	$(SOUP_CFLAGS)					\
	$(LIBEDATASERVER_CFLAGS)			\
	$(DEBUG_CFLAGS)

libeews_1_2_la_SOURCES =		\
        ews-errors.h			\
        ews-errors.c			\
	ews-marshal.h			\
	ews-marshal.c			\
	e-ews-connection.c		\
	e-ews-connection.h		\
	e-ews-folder.c			\
	e-ews-item.c			\
	e-ews-item-change.c		\
	e-ews-message.c			\
	e-soap-message.c		\
	e-soap-message.h		\
	e-soap-response.c		\
	e-soap-response.h		\
	e-ews-compat.h

libeews_1_2_la_LIBADD =						\
	$(SOUP_LIBS)						\
	$(LIBEDATASERVER_LIBS)					\
	$(SOCKET_LIBS)

libeews_1_2_la_LDFLAGS = $(NO_UNDEFINED)

libeewsincludedir = $(privincludedir)/ews

libeewsinclude_HEADERS =		\
        ews-errors.h			\
        e-ews-connection.h		\
	e-ews-folder.h			\
	e-ews-item.h			\
	e-ews-item-change.h		\
	e-ews-message.h			

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libeews-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) ews-marshal.list

DISTCLEANFILES = $(pkgconfig_DATA)
CLEANFILES = $(MARSHAL_GENERATED)

-include $(top_srcdir)/git.mk
