SUBDIRS = src po
DIST_SUBDIRS= src po

changelogs =			\
	ChangeLog

EXTRA_DIST = 			\
	$(changelog)		\
	intltool-merge.in	\
	intltool-update.in	\
	intltool-extract.in	

CLEANFILES = 	intltool-merge	\
		intltool-update	\
		intltool-extract

MAINTAINERCLEANFILES =			\
	$(srcdir)/INSTALL		\
	$(srcdir)/aclocal.m4		\
	$(srcdir)/autoscan.log		\
	$(srcdir)/compile		\
	$(srcdir)/config.guess		\
	$(srcdir)/config.h.in		\
	$(srcdir)/config.sub		\
	$(srcdir)/configure.scan	\
	$(srcdir)/depcomp		\
	$(srcdir)/gtk-doc.make		\
	$(srcdir)/install-sh		\
	$(srcdir)/ltmain.sh		\
	$(srcdir)/missing		\
	$(srcdir)/mkinstalldirs

-include $(top_srcdir)/git.mk
