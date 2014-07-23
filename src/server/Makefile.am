NULL =

privlib_LTLIBRARIES = libeews-1.2.la

ENUM_TYPES = e-ews-enums.h

e-ews-enumtypes.h: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.h.template \
		--fhead "#ifndef E_EWS_ENUMTYPES_H\n#define E_EWS_ENUMTYPES_H\n" \
		--ftail "#endif /* E_EWS_ENUMTYPES_H */\n" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

e-ews-enumtypes.c: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.c.template \
		--fhead "#include \"e-ews-enumtypes.h\"" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

ENUM_GENERATED = \
	e-ews-enumtypes.h \
	e-ews-enumtypes.c \
	$(NULL)

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
	$(DEBUG_CFLAGS) \
	$(NULL)

libeews_1_2_la_SOURCES = \
	camel-ews-settings.h \
	camel-ews-settings.c \
	ews-errors.h \
	ews-errors.c \
	e-ews-connection.c \
	e-ews-connection.h \
	e-ews-connection-utils.c \
	e-ews-connection-utils.h \
	e-ews-debug.c \
	e-ews-debug.h \
	e-ews-enumtypes.c \
	e-ews-folder.c \
	e-ews-item.c \
	e-ews-item-change.c \
	e-ews-message.c \
	e-ews-notification.c \
	e-ews-notification.h \
	e-ews-oof-settings.c \
	e-soap-message.c \
	e-soap-message.h \
	e-soap-response.c \
	e-soap-response.h \
	e-source-ews-folder.c \
	e-source-ews-folder.h \
	e-soup-auth-negotiate.c \
	e-soup-auth-negotiate.h \
	$(NULL)

libeews_1_2_la_LIBADD = \
	$(SOUP_LIBS) \
	$(LIBECAL_LIBS) \
	$(LIBICAL_LIBS) \
	$(LIBEDATASERVER_LIBS) \
	$(SOCKET_LIBS) \
	$(NULL)

libeews_1_2_la_LDFLAGS = $(NO_UNDEFINED)

libeewsincludedir = $(privincludedir)/ews

libeewsinclude_HEADERS = \
	ews-errors.h \
	e-ews-connection.h \
	e-ews-connection-utils.h \
	e-ews-debug.h \
	e-ews-enums.h \
	e-ews-enumtypes.h \
	e-ews-folder.h \
	e-ews-item.h \
	e-ews-item-change.h \
	e-ews-message.h \
	e-ews-notification.h \
	e-ews-oof-settings.h \
	$(NULL)

BUILT_SOURCES = \
	$(ENUM_GENERATED) \
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
