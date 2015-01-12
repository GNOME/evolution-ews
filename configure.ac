dnl Evolution-EWS version
m4_define([ews_major_version], [3])
m4_define([ews_minor_version], [12])
m4_define([ews_micro_version], [11])

m4_define([ews_version],
	[ews_major_version.ews_minor_version.ews_micro_version])

AC_PREREQ(2.58)
AC_INIT([evolution-ews], [ews_version], [http://bugzilla.gnome.org/browse.cgi?product=evolution-ews])
AM_INIT_AUTOMAKE([gnu 1.9 dist-xz no-dist-gzip -Wno-portability tar-ustar])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(README)
AC_CONFIG_HEADERS(config.h)

dnl ****************************
dnl Evolution-EWS API version
dnl ****************************
m4_define([EVO_EWS_API_VERSION_MACRO], [1.0])
EVO_EWS_API_VERSION=EVO_EWS_API_VERSION_MACRO
AC_SUBST(EVO_EWS_API_VERSION)

# Immediately after a release, every package gets a 'Post-release version bump'
# commit which increases the minor version to whatever the *next* release will
# be.
#
# On a release, the NEWS file is updated with a changelog for that release.
#
# Thus, during the development phase between releases, the first line of the
# NEWS file refers to the previous release, while ews_micro_version refers to
# the *next* release.
#
# If there are fixes in the Evolution-EWS tree which have been made since the
# latest release, it's useful to be able to build EWS straight from git.
# However, if we require that EDS and Evo precisely match [ews_version], that
# means that the user would have to locally rebuild both of those, *just* to
# pull in the cosmetic "Post-release version bump" commit, before they can
# build EWS.
#
# So don't do that. If the latest version mentioned in the NEWS file is
# precisely one minor behind [ews_version], then allow building against *that*
# version of EDS and Evo.
read x NEWSVER y < NEWS
PREVVER=ews_major_version.ews_minor_version.$(expr ews_micro_version - 1)
if test "$PREVVER" = "$NEWSVER"; then
   EVOREQVER=$PREVVER
else
   EVOREQVER=ews_major_version.ews_minor_version.ews_micro_version
fi

dnl *******************
dnl Required Packages
dnl *******************
m4_define([eds_minimum_version], [$EVOREQVER])
m4_define([evo_minimum_version], [$EVOREQVER])

dnl Keep these two definitions in agreement.
m4_define([glib_minimum_version], [2.34])
m4_define([glib_encoded_version], [GLIB_VERSION_2_34])

dnl Keep these two definitions in agreement.
m4_define([gdk_minimum_version], [3.0])
m4_define([gdk_encoded_version], [GDK_VERSION_3_0])

dnl Keep these two definitions in agreement.
m4_define([soup_minimum_version], [2.42])
m4_define([soup_encoded_version], [SOUP_VERSION_2_42])

dnl ***********************************
dnl Automake 1.11 - Silent Build Rules
dnl ***********************************
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl ****************************
dnl Initialize maintainer mode
dnl ****************************
AM_MAINTAINER_MODE([enable])

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

AC_CHECK_LIBM
AC_SUBST(LIBM)

dnl ***********************
dnl Compiler Warning Flags
dnl ***********************
AS_COMPILER_FLAGS(WARNING_FLAGS,
	"-Wall -Wextra
	-Wdeprecated-declarations
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

dnl *******************************
dnl Check for --enable-strict
dnl *******************************
AC_ARG_ENABLE([strict],
	[AS_HELP_STRING([--enable-strict],
	[enable strict building, like without deprecated symbols (default=auto); auto enables strict building only if .git subdirectory exists])],
	[enable_strict=$enableval], [enable_strict=auto])

AC_MSG_CHECKING([if strict building is enabled])
if test "x$enable_strict" = xauto; then
	dnl Be strict when compiling with .git subdirectory
	if test -d .git ; then
		enable_strict=yes
	else
		enable_strict=no
	fi
fi
AC_MSG_RESULT([$enable_strict])

dnl Need to migrate to EBookSqlite before
dnl we can use EDS_DISABLE_DEPRECATED again.
if test "x$enable_strict" = xyes; then
	CFLAGS="$CFLAGS -DG_DISABLE_DEPRECATED"
fi

dnl Warn about API usage that violates our minimum requirements.
CFLAGS="$CFLAGS -DGLIB_VERSION_MAX_ALLOWED=glib_encoded_version"
CFLAGS="$CFLAGS -DGDK_VERSION_MAX_ALLOWED=gdk_encoded_version"
CFLAGS="$CFLAGS -DSOUP_VERSION_MAX_ALLOWED=soup_encoded_version"

dnl These will suppress warnings about newly-deprecated symbols.  Ideally
dnl these settings should match our minimum requirements and we will clean
dnl up any new deprecation warnings after bumping our minimum requirements.
dnl But if the warnings get to be overwhelming, use fixed versions instead.
CFLAGS="$CFLAGS -DGLIB_VERSION_MIN_REQUIRED=glib_encoded_version"
CFLAGS="$CFLAGS -DGDK_VERSION_MIN_REQUIRED=gdk_encoded_version"
CFLAGS="$CFLAGS -DSOUP_VERSION_MIN_REQUIRED=soup_encoded_version"

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
	 gtk+-3.0 >= gdk_minimum_version])

PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= soup_minimum_version)
AC_SUBST(SOUP_CFLAGS)
AC_SUBST(SOUP_LIBS)
LIBSOUP_REQUIRED=soup_minimum_version
AC_SUBST(LIBSOUP_REQUIRED)

dnl ********************************
dnl Check for remaining evolution-data-server modules
dnl ********************************
PKG_CHECK_MODULES(LIBEDATASERVER, libedataserver-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEBACKEND, libebackend-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBECAL, libecal-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATACAL, libedata-cal-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBBOOK, libebook-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEDATABOOK, libedata-book-1.2 >= eds_minimum_version)
PKG_CHECK_MODULES(LIBEMAIL_ENGINE, libemail-engine >= eds_minimum_version)
PKG_CHECK_MODULES(CAMEL, camel-1.2 >= eds_minimum_version)

dnl ****************************
dnl Check for evolution shell 
dnl ****************************
PKG_CHECK_MODULES(EVOLUTION_SHELL, evolution-shell-3.0 >= evo_minimum_version)

dnl ****************************
dnl Check for evolution mail
dnl ****************************
PKG_CHECK_MODULES(EVOLUTION_MAIL, evolution-mail-3.0 >= evo_minimum_version)

dnl ****************************
dnl Check for evolution calendar
dnl ****************************
PKG_CHECK_MODULES(EVOLUTION_CALENDAR, evolution-calendar-3.0 >= evo_minimum_version)

dnl ****************************
dnl Check for sqlite3 
dnl ****************************
PKG_CHECK_MODULES(SQLITE3, sqlite3)

dnl ****************************
dnl Check for libical
dnl ****************************
PKG_CHECK_MODULES(LIBICAL, libical)

dnl ****************************
dnl Check for libmspack with OAB support
dnl ****************************
AC_ARG_WITH([internal-lzx],
	AS_HELP_STRING([--with-internal-lzx],
	    [Use internal LZX decompressor instead of libmspack [default=no]]),
	    [have_mspack=no],
	    [PKG_CHECK_MODULES(MSPACK, libmspack >= 0.4,
		[AC_DEFINE(USE_MSPACK, [1], [libmspack has OAB support])
		 have_mspack=yes],
		[AC_ERROR([libmspack >= 0.4 not found.]
[Make sure it is installed or run configure with --with-internal-lzx])])])
AM_CONDITIONAL(HAVE_MSPACK, [test "$have_mspack" = "yes"])

dnl ****************************
dnl Check for libsoup's SOUP_MESSAGE_IDEMPOTENT SoupMessage flag
dnl ****************************
AC_MSG_CHECKING([libsoup SOUP_MESSAGE_IDEMPOTENT flag])
save_cflags=$CFLAGS; CFLAGS=$SOUP_CFLAGS
save_libs=$LIBS; LIBS="$SOUP_LIBS"
AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[#include <libsoup/soup.h>]],
	[[SoupMessageFlags flag; flag = SOUP_MESSAGE_IDEMPOTENT]])],
	[AC_DEFINE(HAVE_SOUP_MESSAGE_IDEMPOTENT_FLAG, 1, [libsoup provides SOUP_MESSAGE_IDEMPOTENT flag]) ac_cv_have_smi=yes],[ac_cv_have_smi=no])
CFLAGS=$save_cflags
LIBS=$save_libs
AC_MSG_RESULT([$ac_cv_have_smi])

dnl ****************************
dnl Check for uhttpmock
dnl ****************************
AC_ARG_ENABLE([tests],
	[AS_HELP_STRING([--enable-tests],
	[Enable low level tests for EwsConnection @<:@default=no@:>@])],
	[enable_tests="$enableval"], [enable_tests=no])

if test "x$enable_tests" = "xyes" ; then
	PKG_CHECK_MODULES(UHTTPMOCK, [libuhttpmock-0.0], have_uhttpmock=yes,
			  have_uhttpmock=no)
	AC_SUBST(UHTTPMOCK_CFLAGS)
	AC_SUBST(UHTTPMOCK_LIBS)

	if test "x$have_uhttpmock" = "xyes" ; then
		AC_DEFINE(ENABLE_TESTS, 1, [Define to add support for low level tests])
	else
		AC_ERROR([

		libuhttpmock >= 0.0 not found.

		])
	fi
fi
AM_CONDITIONAL([ENABLE_TESTS], [test "x$have_uhttpmock" = "xyes"])

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
errordir=`$PKG_CONFIG --variable=errordir evolution-shell-3.0`
AC_SUBST(errordir)

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

eds_moduledir=`$PKG_CONFIG --variable=moduledir libebackend-1.2`
AC_SUBST(eds_moduledir)

evo_moduledir=`$PKG_CONFIG --variable=moduledir evolution-shell-3.0`
AC_SUBST(evo_moduledir)

ewsdatadir=`$PKG_CONFIG --variable=privdatadir evolution-data-server-1.2`/ews
AC_SUBST(ewsdatadir)

dnl *************
dnl Gtk Doc stuff
dnl *************
GTK_DOC_CHECK(1.9)

dnl **************************
dnl Makefiles
dnl **************************
AC_CONFIG_FILES([
Makefile 
src/Makefile
src/server/Makefile
src/utils/Makefile
src/addressbook/Makefile
src/calendar/Makefile
src/camel/Makefile
src/collection/Makefile
src/configuration/Makefile
po/Makefile.in
tests/Makefile
])
AC_OUTPUT

AC_MSG_NOTICE([
	Strict building : $enable_strict
	Use libmspack   : $have_mspack
	Enable tests	: $enable_tests
])

