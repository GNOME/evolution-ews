if ENABLE_GTK3
ebook_backend_LTLIBRARIES = libebookbackendews.la
else
extension_LTLIBRARIES = libebookbackendews.la
endif

libebookbackendews_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)/src/addressbook			\
	-I$(top_srcdir)/src/server	                \
	$(LIBEBACKEND_CFLAGS)				\
	$(LIBEDATASERVER_CFLAGS)			\
	$(LIBEBOOK_CFLAGS)				\
	$(DB_CFLAGS)					\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)                 \
	$(SOUP_CFLAGS)	

libebookbackendews_la_SOURCES =		\
	e-book-backend-ews.c			\
	e-book-backend-ews.h			\
	e-book-backend-ews-factory.c

libebookbackendews_la_LIBADD =				\
	$(top_builddir)/src/server/libeews-1.2.la		\
	$(LIBEBACKEND_LIBS)					\
	$(LIBEDATASERVER_LIBS)					\
	$(LIBEDATABOOK_LIBS) \
	$(LIBEBOOK_LIBS)					\
	$(DB_LIBS)						\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(SOUP_LIBS)

libebookbackendews_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk
