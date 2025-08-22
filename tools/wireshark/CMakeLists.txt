add_library(wivrn-dissector SHARED
	dissector.cpp
	)

set_target_properties(wivrn-dissector PROPERTIES
	PREFIX ""
	OUTPUT_NAME "wivrn"
	DEFINE_SYMBOL ""
	)

target_link_libraries(wivrn-dissector PRIVATE epan wivrn-common)
set_target_properties(wivrn-dissector PROPERTIES CXX_VISIBILITY_PRESET hidden)

if (WIVRN_USE_SYSTEM_OPENXR)
	target_link_libraries(wivrn-dissector PRIVATE OpenXR::headers)
else()
	FetchContent_MakeAvailable(openxr_loader)
	get_target_property(OPENXR_LOADER_INCLUDES openxr_loader INCLUDE_DIRECTORIES)
	target_include_directories(wivrn-dissector PRIVATE ${OPENXR_LOADER_INCLUDES})
endif()

install(TARGETS wivrn-dissector
	LIBRARY DESTINATION "${Wireshark_PLUGIN_LIBDIR}/epan" NAMELINK_SKIP
)
