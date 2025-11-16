
if (WIVRN_USE_SYSTEM_FREETYPE)
    find_package(Freetype REQUIRED)
    find_package(harfbuzz REQUIRED)

    add_library(FreetypeHarfbuzz INTERFACE)
    target_link_libraries(FreetypeHarfbuzz INTERFACE Freetype::Freetype harfbuzz::harfbuzz)

else()
    set(HARFBUZZ_VERSION 12.2.0)
    set(HARFBUZZ_SHA256 ecb603aa426a8b24665718667bda64a84c1504db7454ee4cadbd362eea64e545)
    set(FREETYPE_VERSION 2.14.1)
    set(FREETYPE_SHA256 32427e8c471ac095853212a37aef816c60b42052d4d9e48230bab3bdf2936ccc)

    if(NOT EXISTS ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz SYMBOLIC)
        else()
            file(DOWNLOAD https://downloads.sourceforge.net/project/freetype/freetype2/${FREETYPE_VERSION}/freetype-${FREETYPE_VERSION}.tar.xz
                 "${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz"
                 EXPECTED_HASH SHA256=${FREETYPE_SHA256})
        endif()
    endif()

    if(NOT EXISTS ${FETCHCONTENT_BASE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz ${FETCHCONTENT_BASE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz SYMBOLIC)
        else()
            file(DOWNLOAD https://github.com/harfbuzz/harfbuzz/releases/download/${HARFBUZZ_VERSION}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz
                 "${FETCHCONTENT_BASE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz"
                 EXPECTED_HASH SHA256=${HARFBUZZ_SHA256})
        endif()
    endif()

    file(ARCHIVE_EXTRACT INPUT ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}.tar.xz DESTINATION ${FETCHCONTENT_BASE_DIR})
    file(ARCHIVE_EXTRACT INPUT ${FETCHCONTENT_BASE_DIR}/harfbuzz-${HARFBUZZ_VERSION}.tar.xz DESTINATION ${FETCHCONTENT_BASE_DIR})

    add_library(FreetypeHarfbuzz STATIC
        ${FETCHCONTENT_BASE_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src/harfbuzz.cc
        ${FETCHCONTENT_BASE_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src/hb-ft.cc
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/autofit/autofit.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbase.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbbox.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbdf.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftbitmap.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftcid.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftfstype.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftgasp.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftglyph.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftgxval.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftinit.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftmm.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftotval.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftpatent.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftpfr.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftstroke.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftsynth.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/fttype1.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftwinfnt.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/bdf/bdf.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/bzip2/ftbzip2.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/cache/ftcache.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/cff/cff.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/cid/type1cid.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/gzip/ftgzip.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/lzw/ftlzw.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/pcf/pcf.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/pfr/pfr.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/psaux/psaux.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/pshinter/pshinter.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/psnames/psnames.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/raster/raster.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/sdf/sdf.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/sfnt/sfnt.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/smooth/smooth.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/svg/svg.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/truetype/truetype.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/type1/type1.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/type42/type42.c
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/winfonts/winfnt.c

        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/src/base/ftdebug.c
    )

    target_sources(FreetypeHarfbuzz PRIVATE
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/builds/unix/ftsystem.c)

    target_include_directories(FreetypeHarfbuzz PUBLIC
        ${FETCHCONTENT_BASE_DIR}/freetype-${FREETYPE_VERSION}/include
        ${FETCHCONTENT_BASE_DIR}/harfbuzz-${HARFBUZZ_VERSION}/src)

    target_compile_definitions(FreetypeHarfbuzz PRIVATE -DFT2_BUILD_LIBRARY=1 -DHAVE_FREETYPE=1 -DHAVE_UNISTD_H=1 -DHAVE_FCNTL_H=1)
    set_target_properties(FreetypeHarfbuzz PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden)
endif()
