add_library(evolution-ews-common SHARED
	e-ms-oapxbc-util.c
	e-ms-oapxbc-util.h
)

target_compile_options(evolution-ews-common PUBLIC
	${CAMEL_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(evolution-ews-common PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/src/common
	${CAMEL_INCLUDE_DIRS}
	${LIBEDATASERVER_INCLUDE_DIRS}
	${SOUP_INCLUDE_DIRS}
)

target_link_libraries(evolution-ews-common
	${CAMEL_LDFLAGS}
	${LIBEDATASERVER_LDFLAGS}
	${SOUP_LDFLAGS}
)

install(TARGETS evolution-ews-common
	DESTINATION ${privsolibdir}
)
