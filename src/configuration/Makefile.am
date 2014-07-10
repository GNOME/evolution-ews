NULL =

evo_module_LTLIBRARIES = module-ews-configuration.la

module_ews_configuration_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DEXCHANGE_EWS_LOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	$(EVOLUTION_MAIL_CFLAGS) \
	$(EVOLUTION_SHELL_CFLAGS) \
	$(LIBECAL_CFLAGS) \
	$(NULL)

module_ews_configuration_la_SOURCES = \
	module-ews-configuration.c \
	e-cal-config-ews.c \
	e-cal-config-ews.h \
	e-book-config-ews.c \
	e-book-config-ews.h \
	e-mail-config-ews-autodiscover.c \
	e-mail-config-ews-autodiscover.h \
	e-mail-config-ews-backend.c \
	e-mail-config-ews-backend.h \
	e-mail-config-ews-gal.c \
	e-mail-config-ews-gal.h \
	e-mail-config-ews-notebook.c \
	e-mail-config-ews-notebook.h \
	e-mail-config-ews-oal-combo-box.c \
	e-mail-config-ews-oal-combo-box.h \
	e-mail-config-ews-delegates-page.c \
	e-mail-config-ews-delegates-page.h \
	e-mail-config-ews-folder-sizes-page.c \
	e-mail-config-ews-folder-sizes-page.h \
	e-mail-config-ews-ooo-page.c \
	e-mail-config-ews-ooo-page.h \
	e-ews-config-ui-extension.c \
	e-ews-config-ui-extension.h \
	e-ews-config-utils.c \
	e-ews-config-utils.h \
	e-ews-edit-folder-permissions.c \
	e-ews-edit-folder-permissions.h \
	e-ews-search-user.c \
	e-ews-search-user.h \
	e-ews-subscribe-foreign-folder.c \
	e-ews-subscribe-foreign-folder.h \
	e-ews-ooo-notificator.c \
	e-ews-ooo-notificator.h \
	$(NULL)

module_ews_configuration_la_LIBADD = \
	$(top_builddir)/src/camel/libcamelews-priv.la \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(EVOLUTION_MAIL_LIBS) \
	$(EVOLUTION_SHELL_LIBS) \
	$(LIBECAL_LIBS) \
	$(NULL)

module_ews_configuration_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED)

error_DATA = module-ews-configuration.error

%.error: %.error.xml
	$(AM_V_GEN) LC_ALL=C $(INTLTOOL_MERGE) -x -u /tmp/notthere $< $@

EXTRA_DIST = module-ews-configuration.error.xml

BUILT_SOURCES = $(error_DATA)

CLEANFILES = $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
