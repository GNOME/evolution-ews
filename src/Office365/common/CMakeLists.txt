set(SOURCES
	camel-sasl-xoauth2-office365.c
	camel-sasl-xoauth2-office365.h
	camel-o365-settings.c
	camel-o365-settings.h
	e-oauth2-service-office365.c
	e-oauth2-service-office365.h
	e-source-o365-folder.c
	e-source-o365-folder.h
)

add_library(evolution-office365 SHARED
	${SOURCES}
)

target_compile_definitions(evolution-office365 PRIVATE
	-DG_LOG_DOMAIN=\"evolution-office365\"
)

target_compile_options(evolution-office365 PUBLIC
	${CAMEL_CFLAGS}
	${EVOLUTION_CALENDAR_CFLAGS}
	${LIBEBACKEND_CFLAGS}
	${LIBECAL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(evolution-office365 PUBLIC
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
	${LIBEDATASERVER_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(evolution-office365
	${CAMEL_LDFLAGS}
	${EVOLUTION_CALENDAR_LDFLAGS}
	${LIBEBACKEND_LDFLAGS}
	${LIBECAL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS evolution-office365
	DESTINATION ${privsolibdir}
)
