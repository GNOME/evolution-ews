AM_CPPFLAGS = -I .					\
	-DEVOLUTION_GLADEDIR=\""$(gladedir)"\" 		\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\" 		\
	$(EVOLUTION_PLUGIN_CFLAGS)			\
	$(CAMEL_CFLAGS)				\
	$(LIBECAL_CFLAGS)

@EVO_PLUGIN_RULE@

plugin_DATA = org-gnome-exchange-ews.eplug 	

plugin_LTLIBRARIES = liborg-gnome-exchange-ews.la

liborg_gnome_exchange_ews_la_SOURCES = 		\
	exchange-ews-account-setup.c			\
	exchange-ews-account-setup.h			\
	exchange-ews-account-listener.c		\
	exchange-ews-account-listener.h

liborg_gnome_exchange_ews_la_LIBADD = 			\
	$(top_builddir)/src/libexchangeews/libexchangeews-1.0.la \
	$(EVOLUTION_PLUGIN_LIBS)			\
	$(CAMEL_LIBS) 					\
	$(LIBECAL_LIBS)

liborg_gnome_exchange_ews_la_LDFLAGS = -module -avoid-version -no-undefined
liborg_gnome_exchange_ews_la_CFLAGS = $(LIBEWS_CFLAGS)

EXTRA_DIST = 						\
	org-gnome-exchange-ews.eplug.xml

BUILT_SOURCES = org-gnome-exchange-ews.eplug	

CLEANFILES = $(BUILT_SOURCES)


-include $(top_srcdir)/git.mk
