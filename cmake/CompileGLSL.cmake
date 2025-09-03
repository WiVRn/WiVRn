set(_THIS_MODULE_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")

find_program(HEXDUMP hexdump REQUIRED)

function(compile_glsl_aux shader_stage shader_name glsl_filename output target_env)

    string(TOUPPER ${shader_stage} shader_stage_upper)
    cmake_path(REPLACE_FILENAME output "${shader_name}.spv" OUTPUT_VARIABLE SPV_FILE)

    file(APPEND ${output} "\
{ \"${shader_name}\", {
#include \"${shader_name}.spv\"
}},
")

    add_custom_command(
        OUTPUT "${SPV_FILE}-nopt"
        COMMAND Vulkan::glslangValidator -V --target-env ${target_env} -S ${shader_stage} -D${shader_stage_upper}_SHADER ${in_file} -o "${SPV_FILE}-nopt"
        DEPENDS "${glsl_filename}"
        VERBATIM
    )
    if (WIVRN_OPTIMIZE_SHADERS)
        add_custom_command(
            OUTPUT "${SPV_FILE}-opt"
            COMMAND spirv-opt --target-env=${target_env} -O "${SPV_FILE}-nopt" -o "${SPV_FILE}-opt"
            DEPENDS "${SPV_FILE}-nopt"
            VERBATIM
        )
        set(_spv "${SPV_FILE}-opt")
    else()
        set(_spv "${SPV_FILE}-nopt")
    endif()
    set(_format "/4 \"0x%02X, \"")
    add_custom_command(
        OUTPUT "${SPV_FILE}"
        COMMAND "${HEXDUMP}" -ve "${_format}" ${_spv} > "${SPV_FILE}"
        DEPENDS "${_spv}"
        VERBATIM
    )

    get_source_file_property(deps "${output}" OBJECT_DEPENDS)
    if (deps)
        set(deps "${deps};")
    else()
        set(deps "")
    endif()
    set_source_files_properties(${output} OBJECT_DEPENDS "${deps}${SPV_FILE}")

endfunction()



function(wivrn_compile_glsl target)
    set(options)
    set(oneValueArgs TARGET_ENV NAMESPACE)
    set(multiValueArgs SHADERS)
    cmake_parse_arguments(_args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT _args_TARGET_ENV)
        set(_args_TARGET_ENV vulkan1.0)
    endif()

    if (_args_NAMESPACE)
        set(_namespace_begin "namespace ${_args_NAMESPACE} {")
        set(_namespace_end "}")
    endif()

    cmake_path(APPEND OUTPUT "${CMAKE_CURRENT_BINARY_DIR}" "${target}_shaders.cpp")

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${target}_shaders.h" "\
#include <cstdint>
#include <map>
#include <vector>
#include <string>
${_namespace_begin}
extern const std::map<std::string, std::vector<uint32_t>> shaders;
${_namespace_end}")

    file(WRITE ${OUTPUT} "\
#include <cstdint>
#include <map>
#include <vector>
#include <string>
${_namespace_begin}
extern const std::map<std::string, std::vector<uint32_t>> shaders = {
")

    foreach(in_file IN LISTS _args_SHADERS)
        if (in_file MATCHES "\.\(vert|frag|tesc|tese|geom|comp\)\(\.glsl\)?$")
            set(shader_stage ${CMAKE_MATCH_1})
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            compile_glsl_aux(${shader_stage} ${shader_name} ${in_file} ${OUTPUT} ${_args_TARGET_ENV})
        else()
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            compile_glsl_aux(vert ${shader_name}.vert ${in_file} ${OUTPUT} ${_args_TARGET_ENV})
            compile_glsl_aux(frag ${shader_name}.frag ${in_file} ${OUTPUT} ${_args_TARGET_ENV})
        endif()


    endforeach()

    file(APPEND ${OUTPUT} "};${_namespace_end}")

    target_sources(${target} PRIVATE ${OUTPUT})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

endfunction()
