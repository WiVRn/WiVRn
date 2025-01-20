
function(compile_glsl_aux shader_stage shader_name glsl_filename output)

    string(TOUPPER ${shader_stage} shader_stage_upper)
    cmake_path(REPLACE_FILENAME output "${shader_name}.spv" OUTPUT_VARIABLE SPV_FILE)

    file(APPEND ${output} "\
{ \"${shader_name}\", {
#include \"${shader_name}.spv\"
}},
")

    add_custom_command(
        OUTPUT "${SPV_FILE}"
        COMMAND Vulkan::glslangValidator -V -S ${shader_stage} -D${shader_stage_upper}_SHADER ${in_file} -x -o "${SPV_FILE}"
        DEPENDS "${glsl_filename}"
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

    cmake_path(APPEND OUTPUT "${CMAKE_CURRENT_BINARY_DIR}" "${target}_shaders.cpp")

    file(WRITE ${OUTPUT} "\
#include <cstdint>
#include <map>
#include <vector>
#include <string>
extern const std::map<std::string, std::vector<uint32_t>> shaders = {
")

    foreach(in_file IN LISTS ARGN)
        if (in_file MATCHES "\.\(vert|frag|tesc|tese|geom|comp\)\.glsl$")
            set(shader_stage ${CMAKE_MATCH_1})
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            compile_glsl_aux(${shader_stage} ${shader_name} ${in_file} ${OUTPUT})
        else()
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            compile_glsl_aux(vert ${shader_name}.vert ${in_file} ${OUTPUT})
            compile_glsl_aux(frag ${shader_name}.frag ${in_file} ${OUTPUT})
        endif()


    endforeach()

    file(APPEND ${OUTPUT} "};")

    target_sources(${target} PRIVATE ${OUTPUT})

endfunction()
