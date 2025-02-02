# cmake_minimum_required(VERSION 3.20)
# project(kirigami-tutorial)

find_package(ECM 5.115 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")

include(KDEInstallDirs)
include(KDECMakeSettings)
# include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMFindQmlModule)
include(ECMQmlModule)
include(I18n)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Quick
    Test
    Gui
    QuickControls2
    Widgets
)

find_package(KF6 REQUIRED COMPONENTS
    Kirigami
    I18n
    CoreAddons
    QQC2DesktopStyle
    IconThemes
)

ecm_find_qmlmodule(org.kde.kirigami REQUIRED)


add_executable(wivrn-dashboard
    main.cpp
    wivrn_server.cpp
    wivrn_qdbus_types.cpp
    steam_apps.cpp
    escape_string.cpp
    settings.cpp
    rectangle_partitionner.cpp
    adb.cpp
    apk_installer.cpp
    vulkan_info.cpp
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml PROPERTIES INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/wivrn_qdbus_types.h")
qt_add_dbus_interface(DBUS_INTERFACE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml wivrn_server_dbus)
target_sources(wivrn-dashboard PRIVATE ${DBUS_INTERFACE_SRC})

set_source_files_properties(../images/wivrn.svg PROPERTIES QT_RESOURCE_ALIAS qml/wivrn.svg)

qt_add_qml_module(wivrn-dashboard
    URI io.github.wivrn.wivrn
    QML_FILES
        qml/Main.qml
        qml/ApkInstallPage.qml
        qml/SettingsPage.qml
        qml/HeadsetsPage.qml
        qml/HeadsetStatsPage.qml
        qml/WizardPage.qml
        qml/WizardStep.qml
        qml/DeviceCard.qml
        qml/BetterLabel.qml
        qml/ConnectUsbDialog.qml
        qml/SelectGame.qml
        qml/SteamLaunchOptions.qml
    RESOURCES
        ../images/wivrn.svg
)


configure_file(gui_config.h.in gui_config.h)
configure_file(io.github.wivrn.wivrn.desktop.in io.github.wivrn.wivrn.desktop)

target_link_libraries(wivrn-dashboard
    PRIVATE
    Qt6::DBus
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::I18n
    KF6::CoreAddons
    KF6::IconThemes
    KF6::Kirigami
    QCoro::Core
    QCoro::Network
    QCoro::Qml
    wivrn-common-base
    wivrn-external
)

if (WIVRN_CHECK_CAPSYSNICE)
    target_link_libraries(wivrn-dashboard PRIVATE PkgConfig::libcap)
endif()

install(TARGETS wivrn-dashboard ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

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

create_mo_files(wivrn-dashboard wivrn-dashboard "${CMAKE_CURRENT_SOURCE_DIR}/../locale")
