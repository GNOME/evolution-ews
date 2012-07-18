NULL =

DIST_SUBDIRS = tests

privlib_LTLIBRARIES = libeews-1.2.la

MARSHAL_GENERATED = ews-marshal.c ews-marshal.h

ews-marshal.h: ews-marshal.list
	$(AM_V_GEN) ( $(GLIB_GENMARSHAL) --prefix=ews_marshal $(srcdir)/ews-marshal.list --header > ews-marshal.tmp \
	&& mv ews-marshal.tmp ews-marshal.h ) \
	|| ( rm -f ews-marshal.tmp && exit 1 )

ews-marshal.c: ews-marshal.h
	$(AM_V_GEN) ( (echo '#include "ews-marshal.h"'; $(GLIB_GENMARSHAL) --prefix=ews_marshal $(srcdir)/ews-marshal.list --body) > ews-marshal.tmp \
	&& mv ews-marshal.tmp ews-marshal.c ) \
	|| ( rm -f ews-marshal.tmp && exit 1 )

ENUM_TYPES = e-ews-enums.h

e-ews-enumtypes.h: e-ews-enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-ews-enumtypes.h.template $(ENUM_TYPES)) > $@

e-ews-enumtypes.c: e-ews-enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-ews-enumtypes.c.template $(ENUM_TYPES)) > $@

ENUM_GENERATED = \
	e-ews-enumtypes.h \
	e-ews-enumtypes.c \
	$(NULL)

e-ews-connection.c: $(MARSHAL_GENERATED)

if ENABLE_KRB5
KERBEROS_FILES = \
	ews-kerberos.c \
	ews-kerberos.h
else
KERBEROS_FILES =
endif

libeews_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libeews\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir) \
	-I$(top_builddir)/src \
	$(SOUP_CFLAGS) \
	$(LIBECAL_CFLAGS) \
	$(LIBICAL_CFLAGS) \
	$(LIBEDATASERVER_CFLAGS) \
	$(KRB5_CFLAGS)	\
	$(DEBUG_CFLAGS) \
	$(NULL)

libeews_1_2_la_SOURCES = \
	camel-ews-settings.h \
	camel-ews-settings.c \
	ews-errors.h \
	ews-errors.c \
	ews-marshal.h \
	ews-marshal.c \
	$(KERBEROS_FILES) \
	e-ews-connection.c \
	e-ews-connection.h \
	e-ews-folder.c \
	e-ews-item.c \
	e-ews-item-change.c \
	e-ews-message.c \
	e-soap-message.c \
	e-soap-message.h \
	e-soap-response.c \
	e-soap-response.h \
	e-source-ews-folder.c \
	e-source-ews-folder.h \
	$(NULL)

libeews_1_2_la_LIBADD = \
	$(SOUP_LIBS) \
	$(LIBECAL_LIBS) \
	$(LIBICAL_LIBS) \
	$(LIBEDATASERVER_LIBS) \
	$(KRB5_LIBS) \
	$(SOCKET_LIBS) \
	$(NULL)

libeews_1_2_la_LDFLAGS = $(NO_UNDEFINED)

libeewsincludedir = $(privincludedir)/ews

libeewsinclude_HEADERS = \
	ews-errors.h \
	e-ews-connection.h \
	e-ews-folder.h \
	e-ews-item.h \
	e-ews-item-change.h \
	e-ews-message.h \
	$(NULL)

%-$(API_VERSION).pc: %.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libeews-$(API_VERSION).pc

BUILT_SOURCES = \
	$(ENUM_GENERATED) \
	$(MARSHAL_GENERATED) \
	$(NULL)

EXTRA_DIST = \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
	e-ews-enumtypes.h.template \
	e-ews-enumtypes.c.template \
	ews-marshal.list \
	$(NULL)

DISTCLEANFILES = $(pkgconfig_DATA)
CLEANFILES = $(MARSHAL_GENERATED)

-include $(top_srcdir)/git.mk
