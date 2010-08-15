# Commenting out the other components. Lets fix server first.
#SUBDIRS = server calendar addressbook camel
#DIST_SUBDIRS = server calendar addressbook camel

SUBDIRS = server
DIST_SUBDIRS = server

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

-include $(top_srcdir)/git.mk
