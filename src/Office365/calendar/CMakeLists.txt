set(DEPENDENCIES
	evolution-office365
)

set(SOURCES
	e-cal-backend-o365.c
	e-cal-backend-o365.h
	e-cal-backend-o365-factory.c
)

add_library(ecalbackendoffice365 MODULE
	${SOURCES}
)

add_dependencies(ecalbackendoffice365
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendoffice365 PRIVATE
	-DG_LOG_DOMAIN=\"ecalbackendoffice365\"
	-DO365_DATADIR=\"${ewsdatadir}\"
	-DO365_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DO365_SRCDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

target_compile_options(ecalbackendoffice365 PUBLIC
	${CAMEL_CFLAGS}
	${EVOLUTION_CALENDAR_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ecalbackendoffice365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/Office365
	${CMAKE_SOURCE_DIR}/src/Office365
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${EVOLUTION_CALENDAR_INCLUDE_DIRS}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendoffice365
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${EVOLUTION_CALENDAR_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS ecalbackendoffice365
	DESTINATION ${ecal_backenddir}
)
