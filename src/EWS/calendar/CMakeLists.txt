install(FILES windowsZones.xml
	DESTINATION ${ewsdatadir}
)

# helper static library

set(DEPENDENCIES
	evolution-ews-common
	evolution-ews
)

add_library(ews-m365 STATIC
	e-cal-backend-ews-m365.h
	e-cal-backend-ews-m365.c
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/camel-m365-settings.c
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/camel-m365-settings.h
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/e-m365-connection.c
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/e-m365-connection.h
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/e-m365-json-utils.c
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/e-m365-json-utils.h
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/e-m365-tz-utils.c
	${CMAKE_SOURCE_DIR}/src/Microsoft365/common/e-m365-tz-utils.h
	${CMAKE_SOURCE_DIR}/src/Microsoft365/calendar/e-cal-backend-m365-utils.c
	${CMAKE_SOURCE_DIR}/src/Microsoft365/calendar/e-cal-backend-m365-utils.h
	${CMAKE_SOURCE_DIR}/src/Microsoft365/calendar/e-cal-backend-m365-recur-blob.c
	${CMAKE_SOURCE_DIR}/src/Microsoft365/calendar/e-cal-backend-m365-recur-blob.h
)

add_dependencies(ews-m365
	${DEPENDENCIES}
)

target_compile_definitions(ews-m365 PRIVATE
	-DG_LOG_DOMAIN=\"ecalbackendews-m365\"
	-DM365_DATADIR=\"${ewsdatadir}\"
)

target_compile_options(ews-m365 PUBLIC
	${CAMEL_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ews-m365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/EWS
	${CMAKE_SOURCE_DIR}/src/EWS
	${CMAKE_BINARY_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/common
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(ews-m365
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

# the main module

set(DEPENDENCIES
	evolution-ews-common
	evolution-ews
	ews-m365
)

set(SOURCES
	e-cal-backend-ews.c
	e-cal-backend-ews.h
	e-cal-backend-ews-factory.c
	e-cal-backend-ews-m365.h
	e-cal-backend-ews-utils.c
	e-cal-backend-ews-utils.h
)

add_library(ecalbackendews MODULE
	${SOURCES}
)

add_dependencies(ecalbackendews
	${DEPENDENCIES}
)

set_target_properties(ecalbackendews PROPERTIES
	C_VISIBILITY_PRESET hidden
)

target_compile_definitions(ecalbackendews PRIVATE
	-DG_LOG_DOMAIN=\"ecalbackendews\"
	-DEXCHANGE_EWS_DATADIR=\"${ewsdatadir}\"
	-DEXCHANGE_EWS_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(ecalbackendews PUBLIC
	${CAMEL_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ecalbackendews PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/EWS
	${CMAKE_SOURCE_DIR}/src/EWS
	${CMAKE_SOURCE_DIR}/src/common
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendews
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS ecalbackendews
	DESTINATION ${ecal_backenddir}
)
