NULL =

camel_provider_LTLIBRARIES = libcamelews.la
camel_provider_DATA = libcamelews.urls

ENUM_TYPES = camel-ews-enums.h

camel-ews-enumtypes.h: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.h.template \
		--fhead "#ifndef CAMEL_EWS_ENUMTYPES_H\n#define CAMEL_EWS_ENUMTYPES_H\n" \
		--ftail "#endif /* CAMEL_EWS_ENUMTYPES_H */\n" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

camel-ews-enumtypes.c: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.c.template \
		--fhead "#include \"camel-ews-enumtypes.h\"" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

ENUM_GENERATED = \
	camel-ews-enumtypes.h \
	camel-ews-enumtypes.c \
	$(NULL)

libcamelews_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-DEXCHANGE_EWS_LOCALEDIR=\"$(localedir)\" \
	-DG_LOG_DOMAIN=\"camel-ews-provider\" \
	$(CAMEL_CFLAGS) \
	$(LIBEDATASERVER_CFLAGS) \
	$(LIBECAL_CFLAGS) \
	$(EVOLUTION_SHELL_CFLAGS) \
	$(GNOME_INCLUDEDIR) \
	$(SOUP_CFLAGS) \
	$(LIBEBACKEND_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libcamelews_la_SOURCES = \
	camel-ews-enumtypes.c \
	camel-ews-folder.c \
	camel-ews-store-summary.c \
	camel-ews-store.c \
	camel-ews-summary.c \
	camel-ews-provider.c \
	camel-ews-utils.c \
	camel-ews-transport.c \
	$(NULL)

noinst_HEADERS = \
	camel-ews-enums.h \
	camel-ews-enumtypes.h \
	camel-ews-folder.h \
	camel-ews-private.h \
	camel-ews-store-summary.h \
	camel-ews-store.h \
	camel-ews-summary.h \
	camel-ews-utils.h \
	camel-ews-transport.h \
	$(NULL)

libcamelews_la_LDFLAGS = -avoid-version $(NO_UNDEFINED) $(CODE_COVERAGE_LDFLAGS)

libcamelews_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la  \
	$(top_builddir)/src/utils/libewsutils.la  \
	$(CAMEL_LIBS) \
	$(EVOLUTION_SHELL_LIBS) \
	$(LIBEDATASERVER_LIBS) \
	$(LIBEBACKEND_LIBS) \
	$(LIBECAL_LIBS) \
	$(SOUP_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

BUILT_SOURCES = \
	$(ENUM_GENERATED) \
	$(NULL)

EXTRA_DIST = \
	libcamelews.urls \
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
