AC_PREREQ(2.58)
AC_INIT([evolution-ews], [3.3.1.1], [http://bugzilla.gnome.org/browse.cgi?product=evolution-ews])
AM_INIT_AUTOMAKE([gnu 1.9])
AC_CONFIG_SRCDIR(README)
AC_CONFIG_HEADERS(config.h)

dnl ****************************
dnl Evolution-EWS API version
dnl ****************************
m4_define([EVO_EWS_API_VERSION_MACRO], [1.0])
EVO_EWS_API_VERSION=EVO_EWS_API_VERSION_MACRO
AC_SUBST(EVO_EWS_API_VERSION)


dnl *******************
dnl Required Packages
dnl *******************
m4_define([eds_minimum_version], [3.2.0])
m4_define([evo_minimum_version], [3.2.0])
m4_define([glib_minimum_version], [2.16.1])
m4_define([gconf_minimum_version], [2.0.0])
m4_define([libsoup_minimum_version], [2.30])


dnl ***********************************
dnl Automake 1.11 - Silent Build Rules
dnl ***********************************
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl ****************************
dnl Initialize maintainer mode
dnl ****************************
AM_MAINTAINER_MODE[enabled]

AC_PROG_CC
AC_PROG_CPP
AC_C_INLINE
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

dnl ******************************
dnl Check for Win32
dnl ******************************

AC_MSG_CHECKING([for Win32])
case "$host" in
*-mingw*)
	os_win32=yes
	NO_UNDEFINED='-no-undefined'
	SOCKET_LIBS='-lws2_32'
	;;
*)
	os_win32=no
	NO_UNDEFINED='-no-undefined'
	SOCKET_LIBS=''
	;;
esac

AC_MSG_RESULT([$os_win32])
AM_CONDITIONAL(OS_WIN32, [test "x$os_win32" = "xyes"])
AC_SUBST(NO_UNDEFINED)
AC_SUBST(SOCKET_LIBS)

dnl ***********************
dnl Compiler Warning Flags
dnl ***********************
AS_COMPILER_FLAGS(WARNING_FLAGS,
	"-DG_DISABLE_DEPRECATED
	-DPANGO_DISABLE_DEPRECATED
	-DGDK_DISABLE_DEPRECATED
	-DGDK_PIXBUF_DISABLE_DEPRECATED
	-DGNOME_DISABLE_DEPRECATED
	-DGSEAL_ENABLE
	-Wall -Wextra
	-Wno-missing-field-initializers
	-Wno-sign-compare
	-Wno-unused-parameter
	-Wdeclaration-after-statement
	-Wno-unused-but-set-variable
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
AM_PATH_GLIB_2_0(2.26.0,,,gobject gthread)

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

dnl ********************************
dnl Check for evolution-data-server
dnl ********************************
PKG_CHECK_MODULES(EVOLUTION_DATA_SERVER, evolution-data-server-1.2 >= eds_minimum_version)


dnl *****************************
dnl Check for GNOME Platform
dnl *****************************
PKG_CHECK_MODULES(GNOME_PLATFORM,
	[glib-2.0 >= glib_minimum_version
	gtk+-3.0 >= 2.90.4
	gconf-2.0 >= gconf_minimum_version])

PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= libsoup_minimum_version)
AC_SUBST(SOUP_CFLAGS)
AC_SUBST(SOUP_LIBS)
LIBSOUP_REQUIRED=libsoup_minimum_version
AC_SUBST(LIBSOUP_REQUIRED)

dnl ********************************
dnl Check for remaining evolution-data-server modules
dnl ********************************
PKG_CHECK_MODULES(LIBEDATASERVER, libedataserver-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATASERVERUI, libedataserverui-3.0 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEBACKEND, libebackend-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBECAL, libecal-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATACAL, libedata-cal-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBBOOK, libebook-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATABOOK, libedata-book-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(CAMEL, camel-1.2 >= eds_minimum_version)

dnl ****************************
dnl Check for evolution plugins 
dnl ****************************
PKG_CHECK_MODULES(EVOLUTION_PLUGIN, evolution-plugin-3.0 >= evo_minimum_version)

dnl ****************************
dnl Check for evolution shell 
dnl ****************************
PKG_CHECK_MODULES(EVOLUTION_SHELL, evolution-shell-3.0 >= evo_minimum_version)

dnl ****************************
dnl Check for sqlite3 
dnl ****************************
PKG_CHECK_MODULES(SQLITE3, sqlite3)

dnl ****************************
dnl Check for libical
dnl ****************************
PKG_CHECK_MODULES(LIBICAL, libical)

dnl ****************************
dnl Expose version information
dnl ****************************
API_VERSION=1.2
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
plugindir=`$PKG_CONFIG --variable=plugindir evolution-plugin-3.0`
AC_SUBST(plugindir)

camel_providerdir=`$PKG_CONFIG --variable=camel_providerdir camel-1.2`
AC_SUBST(camel_providerdir) 

privincludedir=`$PKG_CONFIG --variable=privincludedir libedataserver-1.2`
AC_SUBST(privincludedir)

privlibdir=`$PKG_CONFIG --variable=privlibdir evolution-data-server-1.2`
AC_SUBST(privlibdir)

edataserver_privincludedir=`$PKG_CONFIG --variable=privincludedir libedataserver-1.2`
AC_SUBST(edataserver_privincludedir)

ecal_backenddir=`$PKG_CONFIG --variable=backenddir libedata-cal-1.2`
AC_SUBST(ecal_backenddir)

ebook_backenddir=`$PKG_CONFIG --variable=backenddir libedata-book-1.2`
AC_SUBST(ebook_backenddir)


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
src/server/Makefile
src/server/tests/Makefile
src/server/libeews.pc
src/utils/Makefile
src/account-setup-eplugin/Makefile
src/addressbook/Makefile
src/addressbook/lzx/Makefile
src/calendar/Makefile
src/camel/Makefile
po/Makefile.in
])
AC_OUTPUT
