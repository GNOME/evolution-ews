glib_mkenums(e-m365-enumtypes e-m365-enums.h E_M365_ENUMTYPES_H)

set(DEPENDENCIES
	evolution-ews-common
)

set(SOURCES
	camel-sasl-xoauth2-microsoft365.c
	camel-sasl-xoauth2-microsoft365.h
	camel-m365-settings.c
	camel-m365-settings.h
	e-m365-connection.c
	e-m365-connection.h
	e-m365-enums.h
	e-m365-json-utils.c
	e-m365-json-utils.h
	e-m365-tz-utils.c
	e-m365-tz-utils.h
	e-oauth2-service-microsoft365.c
	e-oauth2-service-microsoft365.h
	e-source-m365-folder.c
	e-source-m365-folder.h
	${CMAKE_CURRENT_BINARY_DIR}/e-m365-enumtypes.c
	${CMAKE_CURRENT_BINARY_DIR}/e-m365-enumtypes.h
)

add_library(evolution-microsoft365 SHARED
	${SOURCES}
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
