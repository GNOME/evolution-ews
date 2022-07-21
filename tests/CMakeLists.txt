macro(add_ews_test _name)
	set(DEPENDENCIES
		evolution-ews
	)

	set(SOURCES
		ews-test-common.c
		ews-test-common.h
	)

	add_executable(${_name}
		${SOURCES}
		${ARGN}
	)

	add_dependencies(${_name}
		${DEPENDENCIES}
	)

	target_compile_definitions(${_name} PRIVATE
		-DG_LOG_DOMAIN=\"${_name}\"
		-DTEST_FILE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
		-DCALENDAR_MODULE_DIR=\"${CMAKE_BINARY_DIR}/src/EWS/calendar/\"
	)

	target_compile_options(${_name} PUBLIC
		${LIBECAL_CFLAGS}
		${LIBEDATASERVER_CFLAGS}
		${UHTTPMOCK_CFLAGS}
	)

	target_include_directories(${_name} PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_SOURCE_DIR}
		${CMAKE_BINARY_DIR}/src/EWS
		${CMAKE_SOURCE_DIR}/src/EWS
		${LIBECAL_INCLUDE_DIRS}
		${LIBEDATASERVER_INCLUDE_DIRS}
		${UHTTPMOCK_INCLUDE_DIRS}
	)

	target_link_libraries(${_name}
		${DEPENDENCIES}
		${LIBECAL_LDFLAGS}
		${LIBEDATASERVER_LDFLAGS}
		${UHTTPMOCK_LDFLAGS}
	)

	add_check_test(${_name})
endmacro(add_ews_test)

add_ews_test(ews-test-camel ews-test-camel.c)
add_ews_test(ews-test-timezones ews-test-timezones.c)
