AM_CPPFLAGS = -I .					\
	-DEVOLUTION_GLADEDIR=\""$(gladedir)"\" 		\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\" 		\
	$(EVOLUTION_PLUGIN_CFLAGS)			\
	$(CAMEL_CFLAGS)				\
	$(LIBECAL_CFLAGS)

### EVO_PLUGIN_RULE ### begin ###

%.eplug: %.eplug.in
	sed -e 's|\@PLUGINDIR\@|$(plugindir)|'		\
	-e 's|\@SOEXT\@|$(SOEXT)|'			\
	-e 's|\@GETTEXT_PACKAGE\@|$(GETTEXT_PACKAGE)|'	\
	-e 's|\@LOCALEDIR\@|$(localedir)|' $< > $@

%.eplug.in: %.eplug.xml
	LC_ALL=C $(INTLTOOL_MERGE) -x -u /tmp $< $@

%.error: %.error.xml
	LC_ALL=C $(INTLTOOL_MERGE) -x -u /tmp $< $@

### EVO_PLUGIN_RULE ### end ###

plugin_DATA = org-gnome-exchange-ews.eplug 	

plugin_LTLIBRARIES = liborg-gnome-exchange-ews.la

liborg_gnome_exchange_ews_la_SOURCES = 			\
	exchange-ews-account-setup.c			\
	exchange-ews-account-setup.h			\
	exchange-ews-account-listener.c			\
	exchange-ews-account-listener.h			\
	exchange-ews-account-out-of-office.c		\
	exchange-ews-account-out-of-office.h

liborg_gnome_exchange_ews_la_LIBADD = 			\
	$(top_builddir)/src/server/libeews-1.2.la	\
	$(top_builddir)/src/utils/libewsutils.la 	\
	$(top_builddir)/src/camel/libcamelews.la 	\
	$(EVOLUTION_PLUGIN_LIBS)			\
	$(EVOLUTION_SHELL_LIBS) \
	$(CAMEL_LIBS) 					\
	$(LIBECAL_LIBS)					\
	$(LIBEMAIL_ENGINE_LIBS)

liborg_gnome_exchange_ews_la_LDFLAGS = -module -avoid-version $(NO_UNDEFINED)
liborg_gnome_exchange_ews_la_CFLAGS = \
	$(AM_CPPFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/src/camel			\
	-I$(top_builddir)/src/camel			\
	-I$(top_srcdir)/src/server			\
	-I$(top_builddir)/src/server			\
	-I$(top_srcdir)/src/utils	        	\
	$(LIBEWS_CFLAGS)				\
	$(LIBEMAIL_ENGINE_CFLAGS)

EXTRA_DIST = 						\
	org-gnome-exchange-ews.eplug.xml

BUILT_SOURCES = org-gnome-exchange-ews.eplug	

CLEANFILES = $(BUILT_SOURCES)


-include $(top_srcdir)/git.mk
