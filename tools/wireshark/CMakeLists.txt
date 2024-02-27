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

install(TARGETS wivrn-dissector
	LIBRARY DESTINATION "${Wireshark_PLUGIN_LIBDIR}/epan" NAMELINK_SKIP
)
