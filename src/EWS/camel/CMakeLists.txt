install(FILES libcamelews.urls
	DESTINATION ${camel_providerdir}
)

# *************************************************
# Installed private library which can be linked to
# *************************************************

glib_mkenums(camel-ews-enumtypes camel-ews-enums.h CAMEL_EWS_ENUMTYPES_H)

set(DEPENDENCIES
	evolution-ews-common
	evolution-ews
)

set(SOURCES
	camel-ews-enums.h
	camel-ews-folder.c
	camel-ews-folder.h
	camel-ews-message-info.c
	camel-ews-message-info.h
	camel-ews-private.h
	camel-ews-store-summary.c
	camel-ews-store-summary.h
	camel-ews-store.c
	camel-ews-store.h
	camel-ews-summary.c
	camel-ews-summary.h
	camel-ews-utils.c
	camel-ews-utils.h
	camel-ews-transport.c
	camel-ews-transport.h
	${CMAKE_CURRENT_BINARY_DIR}/camel-ews-enumtypes.c
	${CMAKE_CURRENT_BINARY_DIR}/camel-ews-enumtypes.h
)

add_library(camelews-priv SHARED
	${SOURCES}
)

add_dependencies(camelews-priv
	${DEPENDENCIES}
)

target_compile_definitions(camelews-priv PRIVATE
	-DG_LOG_DOMAIN=\"camel-ews-provider\"
	-DEXCHANGE_EWS_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(camelews-priv PUBLIC
	${CAMEL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBECAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(camelews-priv PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/EWS
	${CMAKE_SOURCE_DIR}/src/EWS
	${CMAKE_SOURCE_DIR}/src/common
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${CAMEL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(camelews-priv
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS camelews-priv
	DESTINATION ${privsolibdir}
)

# *************************************************
# Provider module
# *************************************************

set(DEPENDENCIES
	camelews-priv
	evolution-ews
)

set(SOURCES
	camel-ews-provider.c
)

add_library(camelews MODULE
	${SOURCES}
)

add_dependencies(camelews
	${DEPENDENCIES}
)

target_compile_definitions(camelews PRIVATE
	-DG_LOG_DOMAIN=\"camel-ews-provider\"
	-DEXCHANGE_EWS_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(camelews PUBLIC
	${CAMEL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBECAL_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(camelews PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/EWS
	${CMAKE_SOURCE_DIR}/src/EWS
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${LIBECAL_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(camelews
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS camelews
	DESTINATION ${camel_providerdir}
)
