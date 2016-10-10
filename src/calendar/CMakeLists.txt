install(FILES windowsZones.xml
	DESTINATION ${ewsdatadir}
)

set(DEPENDENCIES
	evolution-ews
)

set(SOURCES
	e-cal-backend-ews.c
	e-cal-backend-ews.h
	e-cal-backend-ews-factory.c
	e-cal-backend-ews-utils.c
	e-cal-backend-ews-utils.h
)

add_library(ecalbackendews MODULE
	${SOURCES}
)

add_dependencies(ecalbackendews
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendews PRIVATE
	-DG_LOG_DOMAIN=\"ecalbackendews\"
	-DEXCHANGE_EWS_DATADIR=\"${ewsdatadir}\"
	-DEXCHANGE_EWS_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(ecalbackendews PUBLIC
	${CAMEL_CFLAGS}
	${EVOLUTION_CALENDAR_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ecalbackendews PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${EVOLUTION_CALENDAR_INCLUDE_DIRS}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendews
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${EVOLUTION_CALENDAR_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS ecalbackendews
	DESTINATION ${ecal_backenddir}
)
