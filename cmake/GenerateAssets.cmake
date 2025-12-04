function(add_file_to_target TARGET OUTPUT_FILE)
    string(REPLACE "/" "_" TARGET_SUFFIX ${OUTPUT_FILE})

    add_custom_target(${TARGET}-${TARGET_SUFFIX} ALL DEPENDS ${OUTPUT_FILE})
    add_dependencies(${TARGET} ${TARGET}-${TARGET_SUFFIX})
endfunction()

function(wivrn_generate_ktx)
    set(options)
    set(oneValueArgs SOURCE DESTINATION TARGET)
    set(multiValueArgs)

    cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    cmake_path(GET arg_DESTINATION PARENT_PATH DEST_DIR)
    file(MAKE_DIRECTORY ${DEST_DIR})

    add_custom_command(OUTPUT ${arg_DESTINATION}
        COMMAND ${TOKTX} --encode uastc --uastc_quality 4 --genmipmap --zcmp 20 ${arg_DESTINATION} ${arg_SOURCE}
        DEPENDS ${arg_SOURCE})

    if (DEFINED arg_TARGET)
        add_file_to_target(${arg_TARGET} ${arg_DESTINATION})
    endif()
endfunction()

function(wivrn_rasterize_svg)
    set(options)
    set(oneValueArgs SOURCE DESTINATION TARGET CSS WIDTH HEIGHT)
    set(multiValueArgs)

    cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    set(RSVG_OPTS)

    if(arg_WIDTH)
        set(RSVG_OPTS ${RSVG_OPTS} --width ${arg_WIDTH})
    endif()

    if(arg_HEIGHT)
        set(RSVG_OPTS ${RSVG_OPTS} --height ${arg_HEIGHT})
    endif()

    if(arg_CSS)
        set(RSVG_OPTS ${RSVG_OPTS} --stylesheet ${arg_CSS})
    endif()

    cmake_path(GET arg_DESTINATION PARENT_PATH DEST_DIR)
    file(MAKE_DIRECTORY ${DEST_DIR})

    add_custom_command(OUTPUT ${arg_DESTINATION}
       COMMAND ${RSVG_CONVERT} ${arg_SOURCE} ${RSVG_OPTS} -o ${arg_DESTINATION}
       DEPENDS ${arg_SOURCE} ${arg_CSS})

    if (DEFINED arg_TARGET)
        add_file_to_target(${arg_TARGET} ${arg_DESTINATION})
    endif()
endfunction()

function(wivrn_gltf_transform_uastc)
    set(options)
    set(oneValueArgs SOURCE DESTINATION TARGET ZSTD)
    set(multiValueArgs)

    cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    set(ARGS)
    if (DEFINED arg_ZSTD)
        set(ARGS ${ARGS} --zstd ${arg_ZSTD})
    endif()

    cmake_path(GET arg_DESTINATION PARENT_PATH DEST_DIR)
    file(MAKE_DIRECTORY ${DEST_DIR})

    add_custom_command(OUTPUT ${arg_DESTINATION}
        COMMAND ${GLTF_TRANSFORM} uastc ${ARGS} ${arg_SOURCE} ${arg_DESTINATION}
        DEPENDS ${arg_SOURCE})

    if (DEFINED arg_TARGET)
        add_file_to_target(${arg_TARGET} ${arg_DESTINATION})
    endif()
endfunction()

function(wivrn_gltf_transform_resize)
    set(options)
    set(oneValueArgs SOURCE DESTINATION TARGET WIDTH HEIGHT)
    set(multiValueArgs)

    cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    set(ARGS)
    if (DEFINED arg_WIDTH)
        set(ARGS ${ARGS} --width ${arg_WIDTH})
    endif()
    if (DEFINED arg_HEIGHT)
        set(ARGS ${ARGS} --height ${arg_HEIGHT})
    endif()

    cmake_path(GET arg_DESTINATION PARENT_PATH DEST_DIR)
    file(MAKE_DIRECTORY ${DEST_DIR})

    add_custom_command(OUTPUT ${arg_DESTINATION}
        COMMAND ${GLTF_TRANSFORM} resize ${ARGS} ${arg_SOURCE} ${arg_DESTINATION}
        DEPENDS ${arg_SOURCE})

    if (DEFINED arg_TARGET)
        add_file_to_target(${arg_TARGET} ${arg_DESTINATION})
    endif()
endfunction()

function(wivrn_copy_file)
    set(options)
    set(oneValueArgs SOURCE DESTINATION TARGET)
    set(multiValueArgs)

    cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    cmake_path(GET arg_DESTINATION PARENT_PATH DEST_DIR)
    file(MAKE_DIRECTORY ${DEST_DIR})

    add_custom_command(OUTPUT ${arg_DESTINATION}
        COMMAND ${CMAKE_COMMAND} -E copy ${arg_SOURCE} ${arg_DESTINATION}
        DEPENDS ${arg_SOURCE})

    if (DEFINED arg_TARGET)
        add_file_to_target(${arg_TARGET} ${arg_DESTINATION})
    endif()
endfunction()

function(wivrn_webxr_download)
    if(NOT EXISTS ${FETCHCONTENT_BASE_DIR}/@webxr-input-profiles-1.0.20.tgz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/@webxr-input-profiles-1.0.20.tgz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/@webxr-input-profiles-1.0.20.tgz ${FETCHCONTENT_BASE_DIR}/@webxr-input-profiles-1.0.20.tgz SYMBOLIC)
        else()
            file(DOWNLOAD https://registry.npmjs.com/@webxr-input-profiles/assets/-/assets-1.0.20.tgz
                 ${FETCHCONTENT_BASE_DIR}/@webxr-input-profiles-1.0.20.tgz
                 EXPECTED_HASH SHA256=30df2a2268220fc0d0e034bed1550aabdd7a2500573c5216f64fc70d59c3d91e)
        endif()
    endif()

    file(ARCHIVE_EXTRACT INPUT ${FETCHCONTENT_BASE_DIR}/@webxr-input-profiles-1.0.20.tgz DESTINATION ${FETCHCONTENT_BASE_DIR}/@webxr-input-profiles)
endfunction()

function(wivrn_webxr_controller)
    set(options)
    set(oneValueArgs PROFILE DESTINATION TARGET)
    set(multiValueArgs)

    cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    set(arg_SOURCE ${FETCHCONTENT_BASE_DIR}/@webxr-input-profiles/package/dist/profiles/${arg_PROFILE})

    file(READ ${arg_SOURCE}/profile.json PROFILE_JSON)

    string(JSON NB_LAYOUTS LENGTH "${PROFILE_JSON}" layouts)
    math(EXPR NB_LAYOUTS "${NB_LAYOUTS} - 1")

    string(JSON PROFILE_ID GET "${PROFILE_JSON}" profileId)

    foreach(I RANGE ${NB_LAYOUTS})
        string(JSON LAYOUT_NAME MEMBER "${PROFILE_JSON}" layouts ${I})
        string(JSON ASSET_PATH GET "${PROFILE_JSON}" layouts ${LAYOUT_NAME} assetPath)

        if(WIVRN_COMPRESS_GLB)
            set(TEMPFILE ${PROFILE_ID}-${LAYOUT_NAME}.glb)

            wivrn_gltf_transform_resize(SOURCE ${arg_SOURCE}/${ASSET_PATH} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/${TEMPFILE} WIDTH 512 HEIGHT 512)
            wivrn_gltf_transform_uastc(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${TEMPFILE} DESTINATION ${arg_DESTINATION}/${arg_PROFILE}/${ASSET_PATH} ZSTD 20 TARGET ${arg_TARGET})
        else()
            wivrn_copy_file(SOURCE ${arg_SOURCE}/${ASSET_PATH} DESTINATION ${arg_DESTINATION}/${arg_PROFILE}/${ASSET_PATH} TARGET ${arg_TARGET})
        endif()
    endforeach()

    wivrn_copy_file(SOURCE ${arg_SOURCE}/profile.json DESTINATION ${arg_DESTINATION}/${arg_PROFILE}/profile.json TARGET ${arg_TARGET})
endfunction()
