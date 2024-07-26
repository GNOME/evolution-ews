set(DEPENDENCIES
	evolution-ews-common
	evolution-microsoft365
)

set(SOURCES
	e-cal-backend-m365.c
	e-cal-backend-m365.h
	e-cal-backend-m365-factory.c
	e-cal-backend-m365-recur-blob.c
	e-cal-backend-m365-recur-blob.h
	e-cal-backend-m365-utils.c
	e-cal-backend-m365-utils.h
)

add_library(ecalbackendmicrosoft365 MODULE
	${SOURCES}
)

add_dependencies(ecalbackendmicrosoft365
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendmicrosoft365 PRIVATE
	-DG_LOG_DOMAIN=\"ecalbackendmicrosoft365\"
	-DM365_DATADIR=\"${ewsdatadir}\"
	-DM365_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DM365_SRCDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

target_compile_options(ecalbackendmicrosoft365 PUBLIC
	${CAMEL_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ecalbackendmicrosoft365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
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

target_link_libraries(ecalbackendmicrosoft365
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS ecalbackendmicrosoft365
	DESTINATION ${ecal_backenddir}
)
