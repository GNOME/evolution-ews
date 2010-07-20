#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="evolution-ews"
REQUIRED_AUTOCONF_VERSION=2.58
REQUIRED_AUTOMAKE_VERSION=1.9
REQUIRED_LIBTOOL_VERSION=2.2
REQUIRED_INTLTOOL_VERSION=0.35.5

(test -f $srcdir/configure.ac \
  && test -f $srcdir/ChangeLog \
  && test -d $srcdir/src/camel) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the" >&2
    echo " top-level $PKG_NAME directory" >&2
    exit 1
}

which gnome-autogen.sh || {
    echo "You need to install gnome-common from the GNOME git" >&2
    exit 1
}
USE_GNOME2_MACROS=1 . gnome-autogen.sh
