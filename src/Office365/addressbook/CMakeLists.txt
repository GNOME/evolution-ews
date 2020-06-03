set(DEPENDENCIES
	evolution-office365
)

set(SOURCES
	e-book-backend-o365.c
	e-book-backend-o365.h
	e-book-backend-o365-factory.c
)

add_library(ebookbackendoffice365 MODULE
	${SOURCES}
)

add_dependencies(ebookbackendoffice365
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendoffice365 PRIVATE
	-DG_LOG_DOMAIN=\"ebookbackendoffice365\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
	-DO365_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(ebookbackendoffice365 PUBLIC
	${LIBEBACKEND_CFLAGS}
	${LIBEBOOK_CFLAGS}
	${LIBEDATABOOK_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBICAL_GLIB_CFLAGS}
	${MSPACK_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ebookbackendoffice365 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/Office365
	${CMAKE_SOURCE_DIR}/src/Office365
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

target_link_libraries(ebookbackendoffice365
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

install(TARGETS ebookbackendoffice365
	DESTINATION ${ebook_backenddir}
)
