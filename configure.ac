AC_PREREQ(2.58)
AC_INIT([evolution-ews], [0.31.0], [http://bugzilla.gnome.org/browse.cgi?product=evolution-ews])
AM_INIT_AUTOMAKE([gnu 1.9])
AC_CONFIG_SRCDIR(README)
AC_CONFIG_HEADERS(config.h)

BASE_VERSION=3.0
m4_define([base_version], [3.0])
AC_SUBST(BASE_VERSION)

dnl ****************************
dnl Evolution-EWS API version
dnl ****************************
m4_define([EVO_EWS_API_VERSION_MACRO], [1.0])
EVO_EWS_API_VERSION=EVO_EWS_API_VERSION_MACRO
AC_SUBST(EVO_EWS_API_VERSION)

dnl Some requirements have versioned package names
dnl XXX In the spirit of getting rid of versioned
dnl     files, can we please drop these suffixes?
EDS_PACKAGE=1.2

dnl *******************
dnl Required Packages
dnl *******************
m4_define([eds_minimum_version], [2.31.1])
m4_define([evo_minimum_version], [2.31.1])
m4_define([glib_minimum_version], [2.16.1])
m4_define([gconf_minimum_version], [2.0.0])

dnl **********************************
dnl Gtk-3 compatibility
dnl Use the API version 3.0 while compiling with gtk+-3
dnl **********************************
AC_ARG_ENABLE([gtk3],
	AS_HELP_STRING([--enable-gtk3],
	[Attempt to use gtk+-3]),
	[enable_gtk3=$enableval],[enable_gtk3=no])

if test "x${enable_gtk3}" = "xyes"; then
	LIBEDATASERVER_UI_API_VERSION=3.0
	GTK_PACKAGE="gtk+-3.0"
	EVO_PLUGIN_PACKAGE="evolution-plugin-3.0"
	gtk_minimum_version="2.90.4"
	gweather_minimum_version="2.90.0"
else	
	LIBEDATASERVER_UI_API_VERSION=1.2
	GTK_PACKAGE="gtk+-2.0"
	EVO_PLUGIN_PACKAGE="evolution-plugin"
	gtk_minimum_version="2.20.0"
	gweather_minimum_version="2.25.4"
fi

AM_CONDITIONAL([ENABLE_GTK3], [test "$enable_gtk3" = yes])
GTK_MODULE="$GTK_PACKAGE >= $gtk_minimum_version"
AC_SUBST(GTK_MODULE)

dnl ***********************************
dnl Automake 1.11 - Silent Build Rules
dnl ***********************************
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl ****************************
dnl Initialize maintainer mode
dnl ****************************
AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_CPP
AC_C_INLINE
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

dnl ***********************
dnl Compiler Warning Flags
dnl ***********************
AS_COMPILER_FLAGS(WARNING_FLAGS,
	"-DG_DISABLE_DEPRECATED
	-DPANGO_DISABLE_DEPRECATED
	-DGDK_DISABLE_DEPRECATED
	-DGDK_PIXBUF_DISABLE_DEPRECATED
	-DGTK_DISABLE_DEPRECATED
	-DGNOME_DISABLE_DEPRECATED
	-DGSEAL_ENABLE
	-Wall -Wextra
	-Wno-missing-field-initializers
	-Wno-sign-compare
	-Wno-unused-parameter
	-Wdeclaration-after-statement
	-Werror-implicit-function-declaration
	-Wformat-nonliteral -Wformat-security -Winit-self
	-Wmissing-declarations -Wmissing-include-dirs
	-Wmissing-noreturn -Wnested-externs -Wpointer-arith
	-Wundef -Wwrite-strings")
AC_SUBST(WARNING_FLAGS)

dnl Other useful compiler warnings for test builds only.
dnl These may produce warnings we have no control over.
dnl
dnl	-Wmissing-format-attribute
dnl	-Wshadow
dnl	-Wredundant-decls

CFLAGS="$CFLAGS $WARNING_FLAGS"

dnl ************
dnl i18n stuff
dnl ************
IT_PROG_INTLTOOL([0.35.5])
AM_GLIB_GNU_GETTEXT

GETTEXT_PACKAGE=evolution-ews
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Package name for gettext])

localedir='$(prefix)/$(DATADIRNAME)/locale'
AC_SUBST(localedir)

dnl ****************************
dnl Initialize libtool
dnl ****************************
AM_DISABLE_STATIC
AC_PROG_LIBTOOL

PKG_PROG_PKG_CONFIG

dnl *****************************
dnl Check for GNOME Platform
dnl *****************************
PKG_CHECK_MODULES(GNOME_PLATFORM,
	[glib-2.0 >= glib_minimum_version
	$GTK_MODULE
	gconf-2.0 >= gconf_minimum_version])

dnl ********************************
dnl Check for evolution-data-server 
dnl ********************************
PKG_CHECK_MODULES(EVOLUTION_DATA_SERVER, evolution-data-server-$EDS_PACKAGE >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATASERVER, libedataserver-$EDS_PACKAGE >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEBACKEND, libebackend-$EDS_PACKAGE >= eds_minimum_version)
PKG_CHECK_MODULES(LIBECAL, libecal-$EDS_PACKAGE >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATACAL, libedata-cal-$EDS_PACKAGE >= eds_minimum_version)
PKG_CHECK_MODULES(LIBBOOK, libebook-$EDS_PACKAGE >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATABOOK, libedata-book-$EDS_PACKAGE >= eds_minimum_version)
PKG_CHECK_MODULES(CAMEL, camel-provider-$EDS_PACKAGE >= eds_minimum_version)

dnl ****************************
dnl Check for evolution plugins 
dnl ****************************
PKG_CHECK_MODULES(EVOLUTION_PLUGIN, evolution-plugin >= evo_minimum_version)

dnl ****************************
dnl Expose version information
dnl ****************************
API_VERSION=$EDS_PACKAGE
AC_SUBST(API_VERSION)

EDS_REQUIRED=eds_minimum_version
AC_SUBST(EDS_REQUIRED)

EVO_REQUIRED=evo_minimum_version
AC_SUBST(EVO_REQUIRED)

dnl *******************************
dnl Add evolution plugin rules here
dnl *******************************
EVO_PLUGIN_RULE=$srcdir/eplugin-rule.mk
AC_SUBST_FILE(EVO_PLUGIN_RULE)

dnl *******************
dnl Special directories
dnl *******************
plugindir=`$PKG_CONFIG --variable=plugindir evolution-plugin`
AC_SUBST(plugindir)

extensiondir=`$PKG_CONFIG --variable=extensiondir evolution-data-server-$EDS_PACKAGE`
AC_SUBST(extensiondir)

camel_providerdir=`$PKG_CONFIG --variable=camel_providerdir camel-provider-$EDS_PACKAGE`
AC_SUBST(camel_providerdir) 

privdatadir='${datadir}'/evolution-data-server-$BASE_VERSION
AC_SUBST(privdatadir)

privincludedir='${includedir}'/evolution-data-server-$BASE_VERSION
AC_SUBST(privincludedir)

privlibdir='${libdir}'/evolution-data-server-$BASE_VERSION
AC_SUBST(privlibdir)

imagesdir='${datadir}'/pixmaps/evolution-data-server
AC_SUBST(imagesdir)

uidir="$privdatadir/ui"
AC_SUBST(uidir)

edataserver_privincludedir=`$PKG_CONFIG --variable=privincludedir libedataserver-$EDS_PACKAGE`
AC_SUBST(edataserver_privincludedir)

dnl *************
dnl Gtk Doc stuff
dnl *************
GTK_DOC_CHECK(1.9)

dnl **************************
dnl Makefiles, generated .pc's
dnl **************************
AC_CONFIG_FILES([
Makefile 
src/Makefile
src/account-setup-eplugin/Makefile
src/addressbook/Makefile
src/calendar/Makefile
src/camel/Makefile
src/server/Makefile
src/server/libeews.pc
po/Makefile.in
])
AC_OUTPUT
