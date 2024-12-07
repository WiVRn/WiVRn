import QtCore as Core
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import QtQuick.Controls as Controls
import Qt.labs.platform
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ApplicationWindow {
    id: root
    title: i18n("WiVRn dashboard")

    Settings {
        id: config
    }

    ConnectUsbDialog {
        id: select_usb_device
    }

    SystemTrayIcon {
        id: systray
        visible: true
        icon.source: Qt.resolvedUrl("wivrn.svg")
        onActivated: root.visible = !root.visible
    }
    onClosing: Qt.quit()

    Core.Settings {
        id: settings
        property alias first_run: root.first_run
    }
    property bool first_run: true

    width: 900
    height: 800

    property bool server_started: WivrnServer.serverStatus == WivrnServer.Started
    property bool json_loaded: false
    property bool prev_headset_connected: false

    Connections {
        target: WivrnServer
        function onCapSysNiceChanged(value) {
            if (WivrnServer.serverStatus != WivrnServer.Stopped)
                restart_capsysnice.visible = true;
        }

        function onServerStatusChanged(value) {
            var started = value == WivrnServer.Started;

            if (switch_running.checked != started)
                switch_running.checked = started;
        }

        function onPairingEnabledChanged(value) {
            if (switch_pairing.checked != WivrnServer.pairingEnabled)
                switch_pairing.checked = WivrnServer.pairingEnabled;
        }

        // function onHeadsetConnectedChanged(value) {
        //     if (value != root.prev_headset_connected) {
        //         root.prev_headset_connected = value;
        //
        //         if (value && root.pageStack.depth == 1)
        //             root.pageStack.push(Qt.resolvedUrl("HeadsetStatsPage.qml"));
        //     }
        // }
    }

    Component.onCompleted: {
        if (WivrnServer.serverStatus == WivrnServer.Stopped)
            WivrnServer.start_server();

        console.log("Settings at " + settings.location);
        if (root.first_run)
        {
            console.log("First run");
            root.pageStack.push(Qt.resolvedUrl("WizardPage.qml"));
            root.first_run = false;
        }
    }

    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.NoNavigationButtons
    pageStack.defaultColumnWidth: width // Force non-wide mode
    pageStack.interactive: false // Don't let the back/forward mouse button handle the pagestack

    pageStack.initialPage: Kirigami.ScrollablePage {
        ColumnLayout {
            anchors.fill: parent
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: i18n("The server does not have CAP_SYS_NICE capabilities.")
                // type: Kirigami.MessageType.Warning
                type: Kirigami.MessageType.Information
                showCloseButton: true
                visible: !WivrnServer.capSysNice
                actions: [
                    Kirigami.Action {
                        text: i18n("Fix it")
                        onTriggered: WivrnServer.grant_cap_sys_nice()
                    }
                ]
            }

            Kirigami.InlineMessage {
                id: restart_capsysnice
                Layout.fillWidth: true
                text: i18n("The CAP_SYS_NICE capability will be used when the server is restarted.")
                type: Kirigami.MessageType.Information
                showCloseButton: true
                visible: false
                actions: [
                    Kirigami.Action {
                        text: i18nc("restart the server", "Restart now")
                        onTriggered: {
                            WivrnServer.restart_server();
                            restart_capsysnice.visible = false;
                        }
                    }
                ]
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: i18n("ADB is not installed.")
                type: Kirigami.MessageType.Information
                showCloseButton: true
                visible: !Adb.adbInstalled
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: i18n("NVIDIA GPUs require at least driver version 565.77, current version is %1", VulkanInfo.driverVersion)
                type: Kirigami.MessageType.Error
                showCloseButton: true
                visible: VulkanInfo.driverId == "NvidiaProprietary" && VulkanInfo.driverVersionCode < 2371043328 /* Version 565.77 */
                actions: [
                    Kirigami.Action {
                        text: i18n("More info")
                        onTriggered: Qt.openUrlExternally("https://github.com/WiVRn/WiVRn/issues/180")
                    }
                ]
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: i18n("Use the Mesa (radv) driver for AMD GPUs\nHardware encoding with vaapi does not work with AMDVLK and AMDGPU-PRO")
                type: Kirigami.MessageType.Warning
                showCloseButton: true
                visible: VulkanInfo.driverId == "AmdProprietary" || VulkanInfo.driverId == "AmdOpenSource"
            }

            // RowLayout {
            //     Controls.Button {
            //         text: "refresh latest version"
            //         onClicked: ApkInstaller.refreshLatestVersion()
            //     }
            //     Controls.Label {
            //         text: "current version " + ApkInstaller.currentVersion + "\nlatest version " + ApkInstaller.latestVersion
            //     }
            // }

            GridLayout {
                columns: 2

                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Item {}
                Item {
                    Layout.fillWidth: true
                }

                Image {
                    source: Qt.resolvedUrl("wivrn.svg")
                }

                GridLayout {
                    columns: 2
                    Layout.fillWidth: true
                    Controls.Switch {
                        id: switch_running
                        Layout.row: 0
                        Layout.column: 0
                        text: i18nc("whether the server is running, displayed in front of a checkbox", "Running")

                        checked: true
                        onCheckedChanged: {
                            if (checked && !root.server_started)
                                WivrnServer.start_server();
                            else if (!checked && root.server_started)
                                WivrnServer.stop_server();
                        }
                    }

                    Controls.Switch {
                        id: switch_pairing
                        Layout.row: 1
                        Layout.column: 0
                        text: i18nc("whether pairing is enabled, displayed in front of a checkbox", "Pairing")
                        onCheckedChanged: {
                            if (checked && !WivrnServer.pairingEnabled)
                                WivrnServer.enable_pairing();
                            else if (!checked && WivrnServer.pairingEnabled)
                                WivrnServer.disable_pairing();
                        }
                        enabled: root.server_started
                    }

                    Controls.Label {
                        Layout.row: 1
                        Layout.column: 1
                        text: WivrnServer.pairingEnabled ? i18n("PIN: %1", WivrnServer.pin) : ""
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Controls.Button {
                        Layout.row: 2
                        Layout.column: 0
                        text: i18n("Connect by USB")
                        onClicked: select_usb_device.connect()
                        enabled: root.server_started && Adb.adbInstalled && !WivrnServer.headsetConnected && select_usb_device.connected_headset_count > 0
                        visible: !WivrnServer.headsetConnected
                    }

                    Controls.Button {
                        Layout.row: 2
                        Layout.column: 0
                        text: i18n("Disconnect")
                        icon.name: "network-disconnect-symbolic"
                        onClicked: WivrnServer.disconnect_headset()
                        visible: root.server_started && WivrnServer.headsetConnected
                    }
                }

                Kirigami.Separator {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    opacity: root.server_started
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }

                Kirigami.Heading {
                    level: 1
                    wrapMode: Text.WordWrap
                    text: i18nc("automatically started application", "Application")
                    opacity: root.server_started
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }

                SelectGame {
                    id: select_game
                    opacity: root.server_started
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }

                Kirigami.Separator {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    opacity: root.server_started
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }

                Kirigami.Heading {
                    level: 1
                    wrapMode: Text.WordWrap
                    text: i18n("Steam informations")
                    opacity: root.server_started
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }
                SteamLaunchOptions {
                    opacity: root.server_started
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }

                Item {
                    // spacer item
                    Layout.fillHeight: true
                }
            }
        }

        actions: [
            Kirigami.Action {
                text: i18n("Wizard")
                icon.name: "tools-wizard-symbolic"
                onTriggered: root.pageStack.push(Qt.resolvedUrl("WizardPage.qml"))
                visible: root.pageStack.depth == 1
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("Install the app")
                icon.name: "install-symbolic"
                onTriggered: root.pageStack.push(Qt.resolvedUrl("ApkInstallPage.qml"))
                visible: root.pageStack.depth == 1
                enabled: root.server_started && Adb.adbInstalled
            },
            // Kirigami.Action {
            //     text: i18n("Statistics")
            //     icon.name: "office-chart-line-symbolic"
            //     onTriggered: root.pageStack.push(Qt.resolvedUrl("HeadsetStatsPage.qml"))
            //     visible: root.pageStack.depth == 1
            //     enabled: root.server_started && Adb.adbInstalled && WivrnServer.headsetConnected
            // },

            Kirigami.Action {
                text: i18n("Headsets")
                icon.name: "item-symbolic"
                onTriggered: root.pageStack.push(Qt.resolvedUrl("HeadsetsPage.qml"))
                visible: root.pageStack.depth == 1
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("Settings")
                icon.name: "settings-configure-symbolic"
                onTriggered: root.pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                visible: root.pageStack.depth == 1
                enabled: root.server_started
            }/*,
            Kirigami.Action {
                text: i18n("Troubleshoot")
                icon.name: "help-contents-symbolic"
                onTriggered: root.pageStack.push(Qt.resolvedUrl("TroubleshootPage.qml"))
                enabled: root.server_started && root.pageStack.depth == 1
            }*/


        ]
    }

    pageStack.onCurrentItemChanged: select_game.reload()
}
