glib_mkenums(e-m365-enumtypes e-m365-enums.h E_M365_ENUMTYPES_H)

set(DEPENDENCIES
	evolution-ews-common
)

set(SOURCES
	camel-sasl-xoauth2-microsoft365.c
	camel-m365-settings.c
	e-m365-connection.c
	e-m365-json-utils.c
	e-m365-tz-utils.c
	e-oauth2-service-microsoft365.c
	e-source-m365-folder.c
	${CMAKE_CURRENT_BINARY_DIR}/e-m365-enumtypes.c
)

set(HEADERS
	camel-m365-settings.h
	camel-sasl-xoauth2-microsoft365.h
	e-m365-connection.h
	e-m365-enums.h
	e-m365-json-utils.h
	e-m365-tz-utils.h
	e-oauth2-service-microsoft365.h
	e-source-m365-folder.h
	${CMAKE_CURRENT_BINARY_DIR}/e-m365-enumtypes.h
)

add_library(evolution-microsoft365 SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(evolution-microsoft365
	${DEPENDENCIES}
)

target_compile_definitions(evolution-microsoft365 PRIVATE
	-DG_LOG_DOMAIN=\"evolution-microsoft365\"
	-DM365_DATADIR=\"${ewsdatadir}\"
)

target_compile_options(evolution-microsoft365 PUBLIC
	${CAMEL_CFLAGS}
	${JSON_GLIB_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(evolution-microsoft365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/common
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${CAMEL_INCLUDE_DIRS}
	${JSON_GLIB_INCLUDE_DIRS}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(evolution-microsoft365
	${CAMEL_LDFLAGS}
	${JSON_GLIB_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${SOUP_LDFLAGS}
	${DEPENDENCIES}
)

install(TARGETS evolution-microsoft365
	DESTINATION ${privsolibdir}
)

install_development_files(evolution-microsoft365 "Microsoft 365 client library"
	"evolution-ews-common = ${PROJECT_VERSION} camel-1.2 >= ${PROJECT_VERSION} libebackend-1.2 >= ${PROJECT_VERSION} libecal-2.0 >= ${PROJECT_VERSION} libedata-cal-2.0 >= ${PROJECT_VERSION} libedataserver-1.2 >= ${PROJECT_VERSION} libsoup-3.0 json-glib-1.0"
	${HEADERS}
)
