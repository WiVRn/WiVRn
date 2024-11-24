# Qt 6.7 changed qt_add_translations()
try_compile(AT_LEAST_QT67 SOURCE_FROM_CONTENT check-qt-version.cpp "
#include <QtGlobal>

#if (QT_VERSION < QT_VERSION_CHECK(6, 7, 0))
#error
#endif

int main() {}
" LINK_LIBRARIES Qt6::Core)

qt_add_executable(wivrn-dashboard
        main.cpp
        main_window.cpp
        main_window.h
        main_window.ui
        settings.cpp
        settings.h
        settings.ui
        wivrn_server.cpp
        wivrn_server.h
        wizard.cpp
        wizard.h
        wizard.ui
        rectangle_partitionner.cpp
        rectangle_partitionner.h
        steam_app.cpp
        steam_app.h
        adb.cpp
        adb.h
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wivrn.png
    COMMAND ${RSVG_CONVERT} ${CMAKE_CURRENT_SOURCE_DIR}/../images/wivrn.svg -w 540 -h 540 -o ${CMAKE_CURRENT_BINARY_DIR}/wivrn.png
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../images/wivrn.svg)

add_custom_target(dashboard-assets ALL DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/wivrn.png
)

add_dependencies(wivrn-dashboard dashboard-assets)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../images/wivrn.svg PROPERTIES QT_RESOURCE_ALIAS "images/wivrn.svg")
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/wivrn.png           PROPERTIES QT_RESOURCE_ALIAS "images/wivrn.png")

qt_add_resources(wivrn-dashboard wivrn-dashboard-resources PREFIX / FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../images/wivrn.svg
    ${CMAKE_CURRENT_BINARY_DIR}/wivrn.png
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml PROPERTIES INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/wivrn_qdbus_types.h")
qt_add_dbus_interface(DBUS_INTERFACE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml wivrn_server_dbus)
target_sources(wivrn-dashboard PRIVATE ${DBUS_INTERFACE_SRC})

target_compile_features(wivrn-dashboard PRIVATE cxx_std_20)
target_include_directories(wivrn-dashboard PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

configure_file(gui_config.h.in gui_config.h)
configure_file(io.github.wivrn.wivrn.desktop.in io.github.wivrn.wivrn.desktop)

target_link_libraries(wivrn-dashboard PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
        Qt6::Network
        nlohmann_json
        wivrn-common-base
        Vulkan::Vulkan
)

target_compile_definitions(wivrn-dashboard PRIVATE JSON_DIAGNOSTICS=1)

if (WIVRN_CHECK_CAPSYSNICE)
    target_link_libraries(wivrn-dashboard PRIVATE PkgConfig::libcap)
endif()

file(GLOB TRANSLATION_FILES ${CMAKE_CURRENT_SOURCE_DIR}/wivrn_*.ts)

if (AT_LEAST_QT67)
    qt_add_translations(wivrn-dashboard SOURCE_TARGETS wivrn-dashboard TS_FILES ${TRANSLATION_FILES})
else()
    qt_add_translations(wivrn-dashboard TS_FILES ${TRANSLATION_FILES})
endif()

# https://16bpp.net/blog/post/localizing-a-qt-app-or-anything-else-for-that-matter/, CMake Setup
# https://gist.github.com/define-private-public/f01ff2f9bbb74eded144558a2e02966c#file-cmakelists-2-txt
add_dependencies(wivrn-dashboard release_translations)

install(TARGETS wivrn-dashboard)

install(FILES io.github.wivrn.wivrn.metainfo.xml
	DESTINATION share/metainfo
)

install(FILES ../images/wivrn.svg
	DESTINATION share/icons/hicolor/scalable/apps
	RENAME io.github.wivrn.wivrn.svg
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.wivrn.wivrn.desktop
	DESTINATION share/applications
)
