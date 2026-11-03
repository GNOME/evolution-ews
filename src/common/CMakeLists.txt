set(SOURCES
	e-ews-common-utils.c
	e-ms-oapxbc-util.c
)

set(HEADERS
	e-ews-common-utils.h
	e-ms-oapxbc-util.h
)

add_library(evolution-ews-common SHARED
	${SOURCES}
	${HEADERS}
)

target_compile_options(evolution-ews-common PUBLIC
	${CAMEL_CFLAGS}
	${LIBEDATACAL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(evolution-ews-common PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/src/common
	${CAMEL_INCLUDE_DIRS}
	${LIBEDATACAL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(evolution-ews-common
	${CAMEL_LDFLAGS}
	${LIBEDATACAL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS evolution-ews-common
	DESTINATION ${privsolibdir}
)

install_development_files(evolution-ews-common "Shared utilities for Evolution EWS and Microsoft 365"
	"camel-1.2 >= ${PROJECT_VERSION} libedata-cal-2.0 >= ${PROJECT_VERSION} libedataserver-1.2 >= ${PROJECT_VERSION} libsoup-3.0"
	${HEADERS}
)
