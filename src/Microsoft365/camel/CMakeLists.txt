install(FILES libcamelmicrosoft365.urls
	DESTINATION ${camel_providerdir}
)

# *************************************************
# Installed private library which can be linked to
# *************************************************

set(DEPENDENCIES
	evolution-ews-common
	evolution-microsoft365
)

set(SOURCES
	camel-m365-folder.c
	camel-m365-folder.h
	camel-m365-folder-summary.c
	camel-m365-folder-summary.h
	camel-m365-message-info.c
	camel-m365-message-info.h
	camel-m365-store.c
	camel-m365-store.h
	camel-m365-store-summary.c
	camel-m365-store-summary.h
	camel-m365-transport.c
	camel-m365-transport.h
	camel-m365-utils.c
	camel-m365-utils.h
)

add_library(camelmicrosoft365-priv SHARED
	${SOURCES}
)

add_dependencies(camelmicrosoft365-priv
	${DEPENDENCIES}
)

target_compile_definitions(camelmicrosoft365-priv PRIVATE
	-DG_LOG_DOMAIN=\"camel-microsoft365-provider\"
	-DM365_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(camelmicrosoft365-priv PUBLIC
	${CAMEL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBECAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(camelmicrosoft365-priv PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/common
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(camelmicrosoft365-priv
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS camelmicrosoft365-priv
	DESTINATION ${privsolibdir}
)

# *************************************************
# Provider module
# *************************************************

set(DEPENDENCIES
	camelmicrosoft365-priv
	evolution-ews
)

set(SOURCES
	camel-m365-provider.c
)

add_library(camelmicrosoft365 MODULE
	${SOURCES}
)

add_dependencies(camelmicrosoft365
	${DEPENDENCIES}
)

target_compile_definitions(camelmicrosoft365 PRIVATE
	-DG_LOG_DOMAIN=\"camel-microsoft365-provider\"
	-DM365_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(camelmicrosoft365 PUBLIC
	${CAMEL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBECAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(camelmicrosoft365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/common
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(camelmicrosoft365
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS camelmicrosoft365
	DESTINATION ${camel_providerdir}
)
