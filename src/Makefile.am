SUBDIRS = server server/tests utils camel account-setup-eplugin calendar addressbook
DIST_SUBDIRS = server utils camel account-setup-eplugin calendar addressbook

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
