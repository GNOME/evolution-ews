set(DEPENDENCIES
	evolution-microsoft365
)

set(SOURCES
	e-book-backend-m365.c
	e-book-backend-m365.h
	e-book-backend-m365-factory.c
)

add_library(ebookbackendmicrosoft365 MODULE
	${SOURCES}
)

add_dependencies(ebookbackendmicrosoft365
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendmicrosoft365 PRIVATE
	-DG_LOG_DOMAIN=\"ebookbackendmicrosoft365\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
	-DM365_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(ebookbackendmicrosoft365 PUBLIC
	${LIBEBACKEND_CFLAGS}
	${LIBEBOOK_CFLAGS}
	${LIBEDATABOOK_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBICAL_GLIB_CFLAGS}
	${MSPACK_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ebookbackendmicrosoft365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/Microsoft365
	${CMAKE_SOURCE_DIR}/src/Microsoft365
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${LIBEBACKEND_INCLUDE_DIRS}
	${LIBEBOOK_INCLUDE_DIRS}
	${LIBEDATABOOK_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${LIBICAL_GLIB_INCLUDE_DIRS}
	${MSPACK_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendmicrosoft365
	${DEPENDENCIES}
	${LIBEBACKEND_LDFLAGS}
	${LIBEBOOK_LDFLAGS}
	${LIBEDATABOOK_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${LIBICAL_GLIB_LDFLAGS}
	${MATH_LDFLAGS}
	${MSPACK_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS ebookbackendmicrosoft365
	DESTINATION ${ebook_backenddir}
)
