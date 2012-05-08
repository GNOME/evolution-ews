NULL =

SUBDIRS = src po
DIST_SUBDIRS= src po

changelogs = \
	ChangeLog \
	$(NULL)

EXTRA_DIST =  \
	$(changelog) \
	intltool-merge.in \
	intltool-update.in \
	intltool-extract.in \
	$(NULL)

CLEANFILES = \
	intltool-merge \
	intltool-update \
	intltool-extract \
	$(NULL)

MAINTAINERCLEANFILES = \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/autoscan.log \
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/configure.scan \
	$(srcdir)/depcomp \
	$(srcdir)/gtk-doc.make \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	$(NULL)

-include $(top_srcdir)/git.mk
