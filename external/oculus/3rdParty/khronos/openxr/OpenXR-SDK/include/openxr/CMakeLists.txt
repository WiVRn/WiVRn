# Copyright (c) 2017-2022, The Khronos Group Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Author:
#

# Copy the openxr_platform_defines.h file and place it in the binary (build) directory.
configure_file(openxr_platform_defines.h ${CMAKE_CURRENT_BINARY_DIR}/openxr_platform_defines.h COPYONLY)

# Generate OpenXR header files.


set(HEADERS
    openxr.h
    openxr_platform.h
    openxr_reflection.h)
set(HAVE_PREGENERATED TRUE)
set(SOURCE_HEADERS)
foreach(output ${HEADERS})
    list(APPEND SOURCE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/${output})
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${output})
        set(HAVE_PREGENERATED FALSE)
    endif()
endforeach()

set(XR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(HAVE_PREGENERATED AND NOT BUILD_FORCE_GENERATION)
    add_custom_target(generate_openxr_header
        COMMENT "Using found pre-generated OpenXR headers.")

    set(INSTALL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/openxr_platform_defines.h
        ${SOURCE_HEADERS})
else()

    set(GENERATED_HEADERS)
    set(OUTPUT_STAMPS)
    # Copy the openxr_platform_defines.h file and place it in the binary (build) directory.
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/openxr_platform_defines.h
        ${CMAKE_CURRENT_BINARY_DIR}/openxr_platform_defines.h
        COPYONLY)

    # Generate the header files and place it in the binary (build) directory.
    foreach(output ${HEADERS})
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${output}
            COMMAND ${PYTHON_EXECUTABLE} ${XR_ROOT}/specification/scripts/genxr.py
                -registry ${XR_ROOT}/specification/registry/xr.xml
                -o ${CMAKE_CURRENT_BINARY_DIR} ${output}
            DEPENDS
                ${XR_ROOT}/specification/scripts/genxr.py
                ${XR_ROOT}/specification/scripts/cgenerator.py
                ${XR_ROOT}/specification/scripts/creflectiongenerator.py
                ${XR_ROOT}/specification/scripts/generator.py
                ${XR_ROOT}/specification/scripts/reg.py
                ${XR_ROOT}/specification/registry/xr.xml
            COMMENT "Generating ${CMAKE_CURRENT_BINARY_DIR}/${output}"
        )
        list(APPEND GENERATED_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/${output}")
    endforeach()

    set_source_files_properties(
        ${GENERATED_HEADERS}
        PROPERTIES GENERATED TRUE
    )

    set(INSTALL_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/openxr_platform_defines.h
        ${GENERATED_HEADERS})


    # Define generate_openxr_header target to generate the OpenXR header files.
    # Other targets that need the OpenXR headers should use generate_openxr_header as a dependency.
    add_custom_target(generate_openxr_header
        SOURCES ${XR_ROOT}/specification/registry/xr.xml
        DEPENDS
        ${GENERATED_HEADERS}
        ${OUTPUT_STAMPS}
    )
endif()
set_target_properties(generate_openxr_header PROPERTIES FOLDER ${CODEGEN_FOLDER})
if(NOT CMAKE_INSTALL_INCDIR)
    set(CMAKE_INSTALL_INCDIR include)
endif()

INSTALL(FILES ${INSTALL_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCDIR}/openxr
    COMPONENT Headers
)
