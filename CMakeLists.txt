# Evolution-EWS build script

cmake_minimum_required(VERSION 3.15)
cmake_policy(VERSION 3.15)

project(evolution-ews
	VERSION 3.59.1
	LANGUAGES C)
set(PROJECT_BUGREPORT "https://gitlab.gnome.org/GNOME/evolution-ews/issues/")

# Required for FindIntltool module
set(GETTEXT_PACKAGE ${PROJECT_NAME})
set(GETTEXT_PO_DIR ${CMAKE_SOURCE_DIR}/po)

# Required for 'disttest' and 'distcheck' of DistTarget module
set(PROJECT_DISTCONFIGURE_PARAMS
	-DENABLE_TESTS=ON
)

# Keep these two definitions in agreement.
set(glib_minimum_version 2.68)
set(glib_encoded_version GLIB_VERSION_2_68)

# Keep these two definitions in agreement.
set(gdk_minimum_version 3.10)
set(gdk_encoded_version GDK_VERSION_3_10)

# Keep these two definitions in agreement.
set(soup_minimum_version 3.0)
set(soup_encoded_version SOUP_VERSION_3_0)

# Warn about API usage that violates our minimum requirements.
add_definitions(-DGLIB_VERSION_MAX_ALLOWED=${glib_encoded_version})
add_definitions(-DGDK_VERSION_MAX_ALLOWED=${gdk_encoded_version})
add_definitions(-DSOUP_VERSION_MAX_ALLOWED=${soup_encoded_version})

# These will suppress warnings about newly-deprecated symbols. Ideally
# these settings should match our minimum requirements and we will clean
# up any new deprecation warnings after bumping our minimum requirements.
# But if the warnings get to be overwhelming, use fixed versions instead.
add_definitions(-DGLIB_VERSION_MIN_REQUIRED=${glib_encoded_version})
add_definitions(-DGDK_VERSION_MIN_REQUIRED=${gdk_encoded_version})
add_definitions(-DSOUP_VERSION_MIN_REQUIRED=${soup_encoded_version})

set(eds_minimum_version ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})
set(evo_minimum_version ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})
set(mspack_minimum_version 0.4)
set(libical_glib_minimum_version 3.0.5)
set(json_glib_minimum_version 1.0.4)
set(uhttpmock_minimum_version 0.9)

# Load modules from the source tree
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Packagers might want to need different settings for the RPATH related things
# From experience, especially CMAKE_BUILD_WITH_INSTALL_RPATH might need to be
# switched to ON, if CMake fails to set the right values during make install
set(CMAKE_SKIP_RPATH OFF CACHE BOOL INTERNAL)
set(CMAKE_SKIP_BUILD_RPATH OFF CACHE BOOL INTERNAL)
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF CACHE BOOL INTERNAL)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON CACHE BOOL INTERNAL)
# CMAKE_INSTALL_RPATH is set below

# CMake modules
include(CheckCSourceCompiles)

# Project custom modules
include(PrintableOptions)

add_printable_variable(LIB_SUFFIX "Library directory suffix, usually defined to '64' for x86_64 systems" "")
add_printable_variable_bare(CMAKE_INSTALL_PREFIX)
add_printable_variable_path(BIN_INSTALL_DIR "Install directory for binary files, defaults to CMAKE_INSTALL_PREFIX/bin" "")
add_printable_variable_path(INCLUDE_INSTALL_DIR "Install directory for header files, defaults to CMAKE_INSTALL_PREFIX/include" "")
add_printable_variable_path(LIB_INSTALL_DIR "Install directory for library files, defaults to CMAKE_INSTALL_PREFIX/lib{LIB_SUFFIX}" "")
add_printable_variable_path(LIBEXEC_INSTALL_DIR "Install directory for library executable files, defaults to CMAKE_INSTALL_PREFIX/libexec" "")
add_printable_variable_path(SHARE_INSTALL_PREFIX "Install directory for shared files, defaults to CMAKE_INSTALL_PREFIX/share" "")
add_printable_variable_path(LOCALE_INSTALL_DIR "Install directory for locale files, defaults to SHARE_INSTALL_PREFIX/locale" "")
add_printable_variable_path(SYSCONF_INSTALL_DIR "Install directory for system configuration files, defaults to CMAKE_INSTALL_PREFIX/etc" "")

macro(ensure_default_value _var _defvalue)
	if(${_var} STREQUAL "")
		set(${_var} ${_defvalue})
	endif(${_var} STREQUAL "")
endmacro(ensure_default_value)

ensure_default_value(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/bin")
ensure_default_value(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/include")
ensure_default_value(LIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}")
ensure_default_value(LIBEXEC_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/libexec")
ensure_default_value(SHARE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/share")
ensure_default_value(LOCALE_INSTALL_DIR "${SHARE_INSTALL_PREFIX}/locale")
ensure_default_value(SYSCONF_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/etc")

# ******************************
# Dependencies
# ******************************

include(CodeCoverageGCOV)
include(CheckTarget)
include(DistTargets)
include(EvolutionMacros)
include(GLibTools)
include(PkgConfigEx)
include(SetupBuildFlags)
include(UninstallTarget)

include(FindIntltool)

add_printable_option(ENABLE_MAINTAINER_MODE "Enable maintainer mode" OFF)

if(ENABLE_MAINTAINER_MODE)
	set(BUILD_TESTING ON)
endif(ENABLE_MAINTAINER_MODE)

# Setup compiler/linker flags
setup_build_flags(${ENABLE_MAINTAINER_MODE})

if(${ENABLE_MAINTAINER_MODE})
	add_definitions(-DCAMEL_DISABLE_DEPRECATED=1 -DEDS_DISABLE_DEPRECATED=1)
endif(${ENABLE_MAINTAINER_MODE})

set(MATH_LDFLAGS -lm)

pkg_check_modules(CAMEL REQUIRED camel-1.2>=${eds_minimum_version})
pkg_check_modules(EVOLUTION_DATA_SERVER REQUIRED evolution-data-server-1.2>=${eds_minimum_version})
pkg_check_modules(LIBEBACKEND REQUIRED libebackend-1.2>=${eds_minimum_version})
pkg_check_modules(LIBEBOOK REQUIRED libebook-1.2>=${eds_minimum_version})
pkg_check_modules(LIBECAL REQUIRED libecal-2.0>=${eds_minimum_version})
pkg_check_modules(LIBEDATABOOK REQUIRED libedata-book-1.2>=${eds_minimum_version})
pkg_check_modules(LIBEDATACAL REQUIRED libedata-cal-2.0>=${eds_minimum_version})
pkg_check_modules(LIBEDATASERVER REQUIRED libedataserver-1.2>=${eds_minimum_version})

pkg_check_modules(EVOLUTION_SHELL REQUIRED evolution-shell-3.0>=${evo_minimum_version})
pkg_check_modules(EVOLUTION_MAIL REQUIRED evolution-mail-3.0>=${evo_minimum_version})
pkg_check_modules(EVOLUTION_CALENDAR REQUIRED evolution-calendar-3.0>=${evo_minimum_version})

pkg_check_modules(GNOME_PLATFORM REQUIRED
	glib-2.0>=${glib_minimum_version}
	gtk+-3.0>=${gdk_minimum_version}
)

pkg_check_modules(SOUP REQUIRED libsoup-3.0>=${soup_minimum_version})

pkg_check_modules(LIBICAL_GLIB REQUIRED libical-glib>=${libical_glib_minimum_version})

pkg_check_modules(JSON_GLIB REQUIRED json-glib-1.0>=${json_glib_minimum_version})

# ******************************
# libmspack with OAB support
# ******************************

add_printable_option(WITH_MSPACK "Use libmspack for OAB decompress" ON)

if(WITH_MSPACK)
	pkg_check_modules_for_option(WITH_MSPACK "libmspack for OAB decompress" MSPACK libmspack>=${mspack_minimum_version})
endif(WITH_MSPACK)

# ******************************
# SOUP_MESSAGE_IDEMPOTENT flag
# ******************************

set(CMAKE_REQUIRED_INCLUDES ${SOUP_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${SOUP_LDFLAGS})

CHECK_C_SOURCE_COMPILES("#include <libsoup/soup.h>
			int main(void) { SoupMessageFlags flag; flag = SOUP_MESSAGE_IDEMPOTENT; return 0; }" HAVE_SOUP_MESSAGE_IDEMPOTENT_FLAG)

unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

# ******************************
# uhttpmock for tests
# ******************************

add_printable_option(ENABLE_TESTS "Enable low level tests for EwsConnection" OFF)

if(ENABLE_TESTS)
	pkg_check_modules_for_option(ENABLE_TESTS "low level tests" UHTTPMOCK libuhttpmock-1.0>=${uhttpmock_minimum_version})
endif(ENABLE_TESTS)

# ******************************
# Office365 OAuth2 support
# ******************************

add_printable_variable(WITH_OFFICE365_TENANT "Office365.com OAuth 2.0 tenant" "")

if(WITH_OFFICE365_TENANT STREQUAL "")
	set(WITH_OFFICE365_TENANT "common")
endif(WITH_OFFICE365_TENANT STREQUAL "")

add_printable_variable(WITH_OFFICE365_CLIENT_ID "Office365.com OAuth 2.0 client ID" "")

if(WITH_OFFICE365_CLIENT_ID STREQUAL "")
	set(WITH_OFFICE365_CLIENT_ID "20460e5d-ce91-49af-a3a5-70b6be7486d1")
endif(WITH_OFFICE365_CLIENT_ID STREQUAL "")

add_printable_variable(WITH_OFFICE365_REDIRECT_URI "Office365.com OAuth 2.0 redirect URI" "")

if(WITH_OFFICE365_REDIRECT_URI STREQUAL "")
	set(WITH_OFFICE365_REDIRECT_URI "https://login.microsoftonline.com/common/oauth2/nativeclient")
endif(WITH_OFFICE365_REDIRECT_URI STREQUAL "")

add_printable_variable(WITH_OFFICE365_ENDPOINT_HOST "Office365.com OAuth 2.0 endpoint host" "")

if(WITH_OFFICE365_ENDPOINT_HOST STREQUAL "")
	set(WITH_OFFICE365_ENDPOINT_HOST "login.microsoftonline.com")
endif(WITH_OFFICE365_ENDPOINT_HOST STREQUAL "")

# ******************************
# Microsoft365 OAuth2 support (v2.0)
# ******************************

add_printable_variable(WITH_MICROSOFT365_TENANT "Microsoft 365 OAuth 2.0 tenant" "")

if(WITH_MICROSOFT365_TENANT STREQUAL "")
	set(WITH_MICROSOFT365_TENANT "common")
endif(WITH_MICROSOFT365_TENANT STREQUAL "")

add_printable_variable(WITH_MICROSOFT365_CLIENT_ID "Microsoft 365 OAuth 2.0 client ID" "")

if(WITH_MICROSOFT365_CLIENT_ID STREQUAL "")
	set(WITH_MICROSOFT365_CLIENT_ID "20460e5d-ce91-49af-a3a5-70b6be7486d1")
endif(WITH_MICROSOFT365_CLIENT_ID STREQUAL "")

add_printable_variable(WITH_MICROSOFT365_REDIRECT_URI "Microsoft 365 OAuth 2.0 redirect URI" "")

if(WITH_MICROSOFT365_REDIRECT_URI STREQUAL "")
	set(WITH_MICROSOFT365_REDIRECT_URI "https://login.microsoftonline.com/common/oauth2/nativeclient")
endif(WITH_MICROSOFT365_REDIRECT_URI STREQUAL "")

add_printable_variable(WITH_MICROSOFT365_ENDPOINT_HOST "Microsoft 365 OAuth 2.0 endpoint host" "")

if(WITH_MICROSOFT365_ENDPOINT_HOST STREQUAL "")
	set(WITH_MICROSOFT365_ENDPOINT_HOST "login.microsoftonline.com")
endif(WITH_MICROSOFT365_ENDPOINT_HOST STREQUAL "")

# ******************************
# Special directories
# ******************************

set(privlibdir "${LIB_INSTALL_DIR}/${PROJECT_NAME}")

pkg_check_variable(camel_providerdir camel-1.2 camel_providerdir)
pkg_check_variable(ebook_backenddir libedata-book-1.2 backenddir)
pkg_check_variable(ecal_backenddir libedata-cal-2.0 backenddir)
pkg_check_variable(eds_moduledir libebackend-1.2 moduledir)
pkg_check_variable(ewsdatadir evolution-data-server-1.2 privdatadir)
pkg_check_variable(evo_privlibdir evolution-shell-3.0 privlibdir)
pkg_check_variable(errordir evolution-shell-3.0 errordir)
pkg_check_variable(evo_moduledir evolution-shell-3.0 moduledir)

add_printable_variable(FORCE_INSTALL_PREFIX "Whether to force install evolution-data-server and evolution files into the install prefix" OFF)
if(FORCE_INSTALL_PREFIX)
	pkg_check_variable(eds_prefix evolution-data-server-1.2 prefix)
	string(REGEX REPLACE "^${eds_prefix}" "${CMAKE_INSTALL_PREFIX}" camel_providerdir "${camel_providerdir}")
	string(REGEX REPLACE "^${eds_prefix}" "${CMAKE_INSTALL_PREFIX}" ebook_backenddir "${ebook_backenddir}")
	string(REGEX REPLACE "^${eds_prefix}" "${CMAKE_INSTALL_PREFIX}" ecal_backenddir "${ecal_backenddir}")
	string(REGEX REPLACE "^${eds_prefix}" "${CMAKE_INSTALL_PREFIX}" eds_moduledir "${eds_moduledir}")
	string(REGEX REPLACE "^${eds_prefix}" "${CMAKE_INSTALL_PREFIX}" ewsdatadir "${ewsdatadir}")

	pkg_check_variable(evo_prefix evolution-shell-3.0 prefix)
	string(REGEX REPLACE "^${evo_prefix}" "${CMAKE_INSTALL_PREFIX}" errordir "${errordir}")
	string(REGEX REPLACE "^${evo_prefix}" "${CMAKE_INSTALL_PREFIX}" evo_moduledir "${evo_moduledir}")
endif(FORCE_INSTALL_PREFIX)

set(ewsdatadir "${ewsdatadir}/ews")

SET(CMAKE_INSTALL_RPATH "${privlibdir}:${evo_privlibdir}")

if(WIN32)
	# On Win32 there is no "rpath" mechanism. We install the private
	# shared libraries in $libdir, meaning the DLLs will actually be in
	# $bindir. This means just having $bindir in PATH will be enough.
	set(privsolibdir "${LIB_INSTALL_DIR}")
else(WIN32)
	set(privsolibdir "${privlibdir}")
endif(WIN32)

# ******************************
# libical-glib API changes
# ******************************

set(CMAKE_REQUIRED_DEFINITIONS ${LIBECAL_CFLAGS})
set(CMAKE_REQUIRED_INCLUDES ${LIBECAL_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${LIBECAL_LDFLAGS})

CHECK_C_SOURCE_COMPILES("#define LIBICAL_GLIB_UNSTABLE_API 1
			#include <libical-glib/libical-glib.h>
			int main(void) {
				i_cal_recurrence_get_by (NULL, I_CAL_BY_MONTH, 0);
				return 0;
			}" HAVE_I_CAL_RECURRENCE_GET_BY)

unset(CMAKE_REQUIRED_DEFINITIONS)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

# Generate the ${PROJECT_NAME}-config.h file
CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config.h)

print_build_options()

add_appdata_file(org.gnome.Evolution-ews.metainfo.xml.in org.gnome.Evolution-ews.metainfo.xml)

add_subdirectory(po)
add_subdirectory(src)

if(ENABLE_TESTS)
	add_subdirectory(tests)
endif(ENABLE_TESTS)
