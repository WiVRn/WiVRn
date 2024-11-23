if(WIVRN_BUILD_DISSECTOR)
	add_subdirectory(wireshark)
endif()

if (WIVRN_BUILD_WIVRNCTL)
	add_subdirectory(wivrnctl)
endif()
