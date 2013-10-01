ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

NULL =

SUBDIRS = src po
DIST_SUBDIRS= src po

changelogs = \
	ChangeLog \
	$(NULL)

EXTRA_DIST =  \
	$(changelog) \
	enumtypes.c.template \
	enumtypes.h.template \
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
	$(srcdir)/m4/gtk-doc.m4 \
	$(srcdir)/m4/intltool.m4 \
	$(srcdir)/m4/libtool.m4 \
	$(srcdir)/m4/ltoptions.m4 \
	$(srcdir)/m4/ltsugar.m4 \
	$(srcdir)/m4/ltversion.m4 \
	$(srcdir)/m4/lt~obsolete.m4 \
	$(NULL)

# XXX with-internal-lzx is temporary.  Remove this
#     once libmspack >= 0.4 becomes more widespread.
DISTCHECK_CONFIGURE_FLAGS = \
	--with-internal-lzx \
	$(NULL)

-include $(top_srcdir)/git.mk
