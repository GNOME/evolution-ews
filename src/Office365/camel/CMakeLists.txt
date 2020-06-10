install(FILES libcameloffice365.urls
	DESTINATION ${camel_providerdir}
)

set(DEPENDENCIES
	evolution-office365
)

set(SOURCES
	camel-o365-provider.c
	camel-o365-store.c
	camel-o365-store.h
	camel-o365-store-summary.c
	camel-o365-store-summary.h
	camel-o365-transport.c
	camel-o365-transport.h
)

add_library(cameloffice365 MODULE
	${SOURCES}
)

add_dependencies(cameloffice365
	${DEPENDENCIES}
)

target_compile_definitions(cameloffice365 PRIVATE
	-DG_LOG_DOMAIN=\"camel-office365-provider\"
	-DO365_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(cameloffice365 PUBLIC
	${CAMEL_CFLAGS}
	${EVOLUTION_SHELL_CFLAGS}
	${EVOLUTION_MAIL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBECAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(cameloffice365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/Office365
	${CMAKE_SOURCE_DIR}/src/Office365
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${EVOLUTION_SHELL_INCLUDE_DIRS}
	${EVOLUTION_MAIL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(cameloffice365
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${EVOLUTION_SHELL_LDFLAGS}
	${EVOLUTION_MAIL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS cameloffice365
	DESTINATION ${camel_providerdir}
)
