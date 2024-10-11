
if (GIT_DESC)
    configure_file(version.cpp.in version.cpp)
else ()
    add_custom_target(wivrn-dashboard-version ALL
        COMMAND ${CMAKE_COMMAND}
            -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
            -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
            -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/version.cpp
            -D CMAKE_PROJECT_VERSION=${CMAKE_PROJECT_VERSION}
            -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
        )
endif()

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
        ../wivrn-dashboard.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
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
)
if (WIVRN_CHECK_CAPSYSNICE)
    target_link_libraries(wivrn-dashboard PRIVATE PkgConfig::libcap)
endif()

file(GLOB TRANSLATION_FILES ${CMAKE_CURRENT_SOURCE_DIR}/wivrn_*.ts)
qt_add_translations(wivrn-dashboard TS_FILES ${TRANSLATION_FILES})

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
