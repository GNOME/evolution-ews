function(install_development_files _library _description _requires)
	install(FILES ${ARGN}
		DESTINATION ${INCLUDE_INSTALL_DIR}/evolution-ews/${_library}
	)

	set(PC_LIBRARY "${_library}")
	set(PC_DESCRIPTION "${_description}")
	set(PC_REQUIRES "${_requires}")
	set(PC_RPATH "")
	if(NOT WIN32)
		set(PC_RPATH " -Wl,-rpath,\${privlibdir}")
	endif()
	configure_file(${CMAKE_SOURCE_DIR}/cmake/evolution-ews.pc.in
		${CMAKE_CURRENT_BINARY_DIR}/${_library}.pc @ONLY
	)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_library}.pc
		DESTINATION ${LIB_INSTALL_DIR}/pkgconfig
	)
endfunction()
