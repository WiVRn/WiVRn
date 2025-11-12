
if (WIVRN_USE_SYSTEM_FREETYPE)
    find_package(Freetype REQUIRED)
    find_package(harfbuzz REQUIRED)

    add_library(FreetypeHarfbuzz INTERFACE)
    target_link_libraries(FreetypeHarfbuzz INTERFACE Freetype::Freetype harfbuzz::harfbuzz)

else()
    set(HARFBUZZ_VERSION 10.0.1)
    set(FREETYPE_VERSION 2.13.3)

    if(NOT EXISTS ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz SYMBOLIC)
        else()
            file(DOWNLOAD https://downloads.sourceforge.net/project/freetype/freetype2/${FREETYPE_VERSION}/freetype-${FREETYPE_VERSION}.tar.xz
                "${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz"
                EXPECTED_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289)
        endif()
    endif()

    if(NOT EXISTS ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz SYMBOLIC)
        else()
            file(DOWNLOAD https://github.com/harfbuzz/harfbuzz/releases/download/${HARFBUZZ_VERSION}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz
                EXPECTED_HASH SHA256=b2cb13bd351904cb9038f907dc0dee0ae07127061242fe3556b2795c4e9748fc)
        endif()
    endif()

    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}.tar.xz DESTINATION ${CMAKE_BINARY_DIR})
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz DESTINATION ${CMAKE_BINARY_DIR})

    add_library(FreetypeHarfbuzz STATIC
        ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src/harfbuzz.cc
        ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src/hb-ft.cc
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/autofit/autofit.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbase.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbbox.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbdf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbitmap.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftcid.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftfstype.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftgasp.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftglyph.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftgxval.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftinit.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftmm.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftotval.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftpatent.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftpfr.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftstroke.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftsynth.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/fttype1.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftwinfnt.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/bdf/bdf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/bzip2/ftbzip2.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/cache/ftcache.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/cff/cff.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/cid/type1cid.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/gzip/ftgzip.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/lzw/ftlzw.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/pcf/pcf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/pfr/pfr.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/psaux/psaux.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/pshinter/pshinter.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/psnames/psnames.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/raster/raster.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/sdf/sdf.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/sfnt/sfnt.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/smooth/smooth.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/svg/svg.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/truetype/truetype.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/type1/type1.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/type42/type42.c
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/winfonts/winfnt.c

        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftdebug.c
    )

    target_sources(FreetypeHarfbuzz PRIVATE
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/builds/unix/ftsystem.c)

    target_include_directories(FreetypeHarfbuzz PUBLIC
        ${CMAKE_BINARY_DIR}/freetype-${FREETYPE_VERSION}/include
        ${CMAKE_BINARY_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src)

    target_compile_definitions(FreetypeHarfbuzz PRIVATE -DFT2_BUILD_LIBRARY=1 -DHAVE_FREETYPE=1 -DHAVE_UNISTD_H=1 -DHAVE_FCNTL_H=1)
    set_target_properties(FreetypeHarfbuzz PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden)
endif()
