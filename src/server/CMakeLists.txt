glib_mkenums(e-ews-enumtypes e-ews-enums.h E_EWS_ENUMTYPES_H)

set(SOURCES
	camel-ews-settings.c
	camel-ews-settings.h
	camel-sasl-xoauth2-office365.c
	camel-sasl-xoauth2-office365.h
	e-ews-calendar-utils.c
	e-ews-calendar-utils.h
	e-ews-camel-common.c
	e-ews-camel-common.h
	e-ews-connection.c
	e-ews-connection.h
	e-ews-connection-utils.c
	e-ews-connection-utils.h
	e-ews-debug.c
	e-ews-debug.h
	e-ews-folder.c
	e-ews-folder.h
	e-ews-item.c
	e-ews-item.h
	e-ews-item-change.c
	e-ews-item-change.h
	e-ews-message.c
	e-ews-message.h
	e-ews-notification.c
	e-ews-notification.h
	e-ews-oof-settings.c
	e-ews-query-to-restriction.c
	e-ews-query-to-restriction.h
	e-oauth2-service-office365.c
	e-oauth2-service-office365.h
	e-soup-auth-negotiate.c
	e-soup-auth-negotiate.h
	e-soap-message.c
	e-soap-message.h
	e-soap-response.c
	e-soap-response.h
	e-source-ews-folder.c
	e-source-ews-folder.h
	ews-errors.c
	ews-errors.h
	${CMAKE_CURRENT_BINARY_DIR}/e-ews-enumtypes.c
	${CMAKE_CURRENT_BINARY_DIR}/e-ews-enumtypes.h
)

add_library(evolution-ews SHARED
	${SOURCES}
)

target_compile_definitions(evolution-ews PRIVATE
	-DG_LOG_DOMAIN=\"evolution-ews\"
)

target_compile_options(evolution-ews PUBLIC
	${CAMEL_CFLAGS}
	${EVOLUTION_CALENDAR_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBICAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(evolution-ews PUBLIC
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
	${LIBEDATASERVER_INCLUDE_DIRS}
	${LIBICAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(evolution-ews
	${CAMEL_LDFLAGS}
	${EVOLUTION_CALENDAR_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${LIBICAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS evolution-ews
	DESTINATION ${privsolibdir}
)
