NULL =

# module-ews-backend is for evolution-data-server

eds_module_LTLIBRARIES = module-ews-backend.la

module_ews_backend_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	$(LIBEDATASERVER_CFLAGS) \
	$(LIBEBACKEND_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)

module_ews_backend_la_SOURCES = \
	module-ews-backend.c

module_ews_backend_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(LIBEDATASERVER_LIBS) \
	$(LIBEBACKEND_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)

module_ews_backend_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

# module-ews-mail-config is for evolution

evo_module_LTLIBRARIES = module-ews-mail-config.la

module_ews_mail_config_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	$(EVOLUTION_MAIL_CFLAGS) \
	$(NULL)

module_ews_mail_config_la_SOURCES = \
	module-ews-mail-config.c \
	e-mail-config-ews-autodiscover.c \
	e-mail-config-ews-autodiscover.h \
	e-mail-config-ews-backend.c \
	e-mail-config-ews-backend.h \
	e-mail-config-ews-gal.c \
	e-mail-config-ews-gal.h \
	e-mail-config-ews-oal-combo-box.c \
	e-mail-config-ews-oal-combo-box.h \
	e-mail-config-ews-ooo-page.c \
	e-mail-config-ews-ooo-page.h \
	$(NULL)

module_ews_mail_config_la_LIBADD = \
	$(top_builddir)/src/server/libeews-1.2.la \
	$(top_builddir)/src/utils/libewsutils.la \
	$(EVOLUTION_MAIL_LIBS) \
	$(NULL)

module_ews_mail_config_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED)

error_DATA = module-ews-mail-config.error

%.error: %.error.xml
	$(AM_V_GEN) LC_ALL=C $(INTLTOOL_MERGE) -x -u /tmp/notthere $< $@

EXTRA_DIST = module-ews-mail-config.error.xml

BUILT_SOURCES = $(error_DATA)

CLEANFILES = $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
