
function(compile_glsl_aux shader_stage shader_name glsl_filename output)

    string(TOUPPER ${shader_stage} shader_stage_upper)

    add_custom_command(
            OUTPUT ${output}
            COMMAND echo "{ \"${shader_name}\", {"         >> ${output}
            COMMAND echo "#include \"${shader_name}.spv\"" >> ${output}
            COMMAND echo "}},"                             >> ${output}

            COMMAND Vulkan::glslangValidator -V -S ${shader_stage} -D${shader_stage_upper}_SHADER ${in_file} -x -o ${shader_name}.spv
            DEPENDS ${glsl_filename}
            VERBATIM
            APPEND
        )

endfunction()



function(compile_glsl target_name)

    add_custom_command(
                OUTPUT ${target_name}_shaders.cpp
                COMMAND echo "#include <cstdint>"                                              >  ${target_name}_shaders.cpp
                COMMAND echo "#include <map>"                                                  >> ${target_name}_shaders.cpp
                COMMAND echo "#include <vector>"                                               >> ${target_name}_shaders.cpp
                COMMAND echo "#include <string>"                                               >> ${target_name}_shaders.cpp
                COMMAND echo "extern const std::map<std::string, std::vector<uint32_t>> shaders = {"  >> ${target_name}_shaders.cpp
                VERBATIM)

    foreach(in_file IN LISTS ARGN)
        if (in_file MATCHES "\.\(vert|frag|tesc|tese|geom|comp\)\.glsl$")
            set(shader_stage ${CMAKE_MATCH_1})
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            compile_glsl_aux(${shader_stage} ${shader_name} ${in_file} ${target_name}_shaders.cpp)
        else()
            cmake_path(GET in_file STEM LAST_ONLY shader_name)
            compile_glsl_aux(vert ${shader_name}.vert ${in_file} ${target_name}_shaders.cpp)
            compile_glsl_aux(frag ${shader_name}.frag ${in_file} ${target_name}_shaders.cpp)
        endif()


    endforeach()

    add_custom_command(
                OUTPUT ${target_name}_shaders.cpp
                COMMAND echo "};"  >> ${target_name}_shaders.cpp
                APPEND
                VERBATIM)

    target_sources(${target_name} PRIVATE ${target_name}_shaders.cpp)

endfunction()
