set(DEPENDENCIES
	evolution-ews
)

set(SOURCES
	ews-oab-props.h
	ews-oab-decoder.c
	ews-oab-decoder.h
	ews-oab-decompress.h
	e-book-backend-ews.c
	e-book-backend-ews.h
	e-book-backend-ews-factory.c
)

if(WITH_MSPACK)
	list(APPEND SOURCES
		ews-oab-decompress.c
	)
else(WITH_MSPACK)
	list(APPEND SOURCES
		mspack/lzx.h
		mspack/lzxd.c
		mspack/readbits.h
		mspack/readhuff.h
		mspack/oab-decompress.c
	)
endif(WITH_MSPACK)

add_library(ebookbackendews MODULE
	${SOURCES}
)

add_dependencies(ebookbackendews
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendews PRIVATE
	-DG_LOG_DOMAIN=\"ebookbackendews\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
	-DEXCHANGE_EWS_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(ebookbackendews PUBLIC
	${LIBEBACKEND_CFLAGS}
	${LIBEBOOK_CFLAGS}
	${LIBEDATABOOK_CFLAGS}
	${LIBEDATASERVER_CFLAGS}
	${LIBICAL_GLIB_CFLAGS}
	${MSPACK_CFLAGS}
	${SOUP_CFLAGS}
)

target_include_directories(ebookbackendews PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src/EWS
	${CMAKE_SOURCE_DIR}/src/EWS
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

target_link_libraries(ebookbackendews
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

install(TARGETS ebookbackendews
	DESTINATION ${ebook_backenddir}
)

# ******************************
# Internal test programs
# ******************************

if(WITH_MSPACK)
	add_executable(gal-lzx-decompress-test
		ews-oab-decompress.c
		ews-oab-decompress.h
		gal-lzx-decompress-test.c
	)

	target_compile_definitions(gal-lzx-decompress-test PRIVATE
		-DG_LOG_DOMAIN=\"gal-lzx-decompress-test\"
	)

	target_compile_options(gal-lzx-decompress-test PUBLIC
		${GNOME_PLATFORM_CFLAGS}
		${MSPACK_CFLAGS}
	)

	target_include_directories(gal-lzx-decompress-test PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_CURRENT_BINARY_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}
		${GNOME_PLATFORM_INCLUDE_DIRS}
		${MSPACK_INCLUDE_DIRS}
	)

	target_link_libraries(gal-lzx-decompress-test
		${GNOME_PLATFORM_LDFLAGS}
		${MSPACK_LDFLAGS}
	)

	# **************************************************************

	add_executable(oab-decode-test
		ews-oab-decoder.c
		ews-oab-decoder.h
		oab-decode-test.c
	)

	target_compile_definitions(oab-decode-test PRIVATE
		-DG_LOG_DOMAIN=\"oab-decode-test\"
	)

	target_compile_options(oab-decode-test PUBLIC
		${GNOME_PLATFORM_CFLAGS}
		${LIBEBOOK_CFLAGS}
		${LIBEDATABOOK_CFLAGS}
		${MSPACK_CFLAGS}
	)

	target_include_directories(oab-decode-test PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_CURRENT_BINARY_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}
		${GNOME_PLATFORM_INCLUDE_DIRS}
		${LIBEBOOK_INCLUDE_DIRS}
		${LIBEDATABOOK_INCLUDE_DIRS}
		${MSPACK_INCLUDE_DIRS}
	)

	target_link_libraries(oab-decode-test
		${GNOME_PLATFORM_LDFLAGS}
		${LIBEBOOK_LDFLAGS}
		${LIBEDATABOOK_LDFLAGS}
		${MSPACK_LDFLAGS}
		${MATH_LDFLAGS}
	)
endif(WITH_MSPACK)
