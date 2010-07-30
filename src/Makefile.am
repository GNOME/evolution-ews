SUBDIRS = server server/tests
# TODO : 
# calendar addressbook camel
DIST_SUBDIRS = server calendar addressbook camel

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
