include(CompileGLSL)

add_library(pyrowave STATIC
	pyrowave_common.cpp
	pyrowave_encoder.cpp
	pyrowave_decoder.cpp
)

file(GLOB_RECURSE VULKAN_SHADERS CONFIGURE_DEPENDS "shaders/*.comp" "shaders/*.glsl")
wivrn_compile_glsl(pyrowave TARGET_ENV vulkan1.1 SHADERS ${VULKAN_SHADERS} NAMESPACE pyrowave)

target_compile_definitions(pyrowave PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)

target_link_libraries(pyrowave PRIVATE wivrn-common)
target_include_directories(pyrowave PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
