import QtCore as Core
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import Qt.labs.platform
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ApplicationWindow {
    id: root
    title: i18n("WiVRn dashboard")

    ConnectUsbDialog {
        id: select_usb_device
    }

    SystemTrayIcon {
        id: systray
        visible: true
        icon.source: Qt.resolvedUrl("wivrn.svg")
        onActivated: root.visible = !root.visible
    }

    Component{
        id: error_template
        Kirigami.InlineMessage{
            visible: true
            property string where
            property string message

            Layout.fillWidth: true
            text: where + (where ? "\n" : "") + message
            type: Kirigami.MessageType.Error
            showCloseButton: true
        }
    }

    onClosing: Qt.quit()

    width: 900
    height: 800

    property bool server_started: WivrnServer.serverStatus == WivrnServer.Started
    property bool json_loaded: false
    property bool prev_headset_connected: false

    Connections {
        target: WivrnServer
        function onServerStatusChanged(value) {
            var started = value == WivrnServer.Started;

            // Only set the switch value if it has changed, to avoid loops
            if (switch_running.checked != started)
                switch_running.checked = started;

            // Set the visible property here because dismissing the message removes the binding
            if (value == WivrnServer.FailedToStart)
                message_failed_to_start.visible = true;

            if (DashboardSettings.first_run && started) {
                console.log("First run");
                root.pageStack.push(Qt.createComponent("WizardPage.qml").createObject());
                DashboardSettings.first_run = false;
            }
        }

        function onPairingEnabledChanged(value) {
            if (switch_pairing.checked != WivrnServer.pairingEnabled)
                switch_pairing.checked = WivrnServer.pairingEnabled;
        }

        function onServerError(info) {
            console.log(info.where);
            console.log(info.message);
            messages.append(error_template.createObject(
                null,
                {
                    where: info.where,
                    message: info.message
                }
            ));
        }
    }

    Connections {
        target: Avahi
        function onRunningChanged(value) {
            if (value) {
                if (WivrnServer.serverStatus != WivrnServer.Started) {
                    WivrnServer.start_server();
                    message_failed_to_start.visible = false;
                }
            }
        }
    }

    Component.onCompleted: {
        if (WivrnServer.serverStatus == WivrnServer.Stopped)
            WivrnServer.start_server();

        if (DashboardSettings.last_run_version != ApkInstaller.currentVersion) {
            DashboardSettings.last_run_version = ApkInstaller.currentVersion;
        }
    }

    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.NoNavigationButtons
    pageStack.defaultColumnWidth: width // Force non-wide mode
    pageStack.interactive: false // Don't let the back/forward mouse button handle the pagestack

    pageStack.initialPage: Kirigami.ScrollablePage {
        ColumnLayout {
            anchors.fill: parent
            Repeater{
                model: ObjectModel {
                    id: messages

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        text: i18n("Avahi daemon is not installed")
                        type: Kirigami.MessageType.Warning
                        showCloseButton: true
                        visible: DashboardSettings.show_system_checks && !Avahi.installed && !Avahi.running
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        text: i18n("Avahi daemon is not started")
                        type: Kirigami.MessageType.Warning
                        showCloseButton: true
                        visible: DashboardSettings.show_system_checks && Avahi.installed && !Avahi.running
                        actions: [
                            Kirigami.Action {
                                visible: Avahi.canStart
                                text: i18n("Fix it")
                                onTriggered: Avahi.start()
                            }
                        ]
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        text: i18n("Firewall may not allow port 9757")
                        type: Kirigami.MessageType.Warning
                        showCloseButton: true
                        visible: DashboardSettings.show_system_checks && Firewall.needSetup
                        actions: [
                            Kirigami.Action {
                                text: i18n("Fix it")
                                onTriggered: Firewall.doSetup()
                            }
                        ]
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

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        text: i18n("No OpenVR compatibility detected, Steam games won't be able to load VR.\nInstall xrizer or OpenComposite.")
                        type: Kirigami.MessageType.Warning
                        showCloseButton: true
                        visible: WivrnServer.openVRCompat.length == 0 && Settings.openvr == ""
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        text: i18n("Steam is installed as a snap. Snaps are not compatible with WiVRn.")
                        type: Kirigami.MessageType.Warning
                        showCloseButton: true
                        visible: DashboardSettings.show_system_checks && Steam.snap
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        text: i18n("Steam is installed as a flatpak but does not have sufficient permissions.")
                        type: Kirigami.MessageType.Warning
                        showCloseButton: true
                        visible: DashboardSettings.show_system_checks && Steam.flatpakNeedPerm
                        actions: [
                            Kirigami.Action {
                                text: i18n("Fix it")
                                onTriggered: Steam.fixFlatpakPerm()
                            }
                        ]
                    }

                    Kirigami.InlineMessage {
                        id: config_restart
                        Layout.fillWidth: true
                        text: i18n("Restart the server for configuration changes to take effect")
                        type: Kirigami.MessageType.Information
                        showCloseButton: true
                        visible: false
                        actions: [
                            Kirigami.Action {
                                text: i18nc("restart the server", "Restart now")
                                onTriggered: {
                                    WivrnServer.restart_server();
                                    config_restart.visible = false;
                                }
                            }
                        ]
                        Connections {
                            target: Settings
                            function onSettingsChanged() {
                                if (WivrnServer.sessionRunning)
                                    config_restart.visible = true;
                            }
                        }
                        Connections {
                            target: WivrnServer
                            function onServerStatusChanged(value) {
                                config_restart.visible = false;
                            }
                        }
                    }

                    Kirigami.InlineMessage {
                        id: message_failed_to_start
                        Layout.fillWidth: true
                        text: i18n("Server failed to start")
                        type: Kirigami.MessageType.Error
                        showCloseButton: true
                        // visible: WivrnServer.serverStatus == WivrnServer.FailedToStart
                        actions: [
                            Kirigami.Action {
                                text: i18n("Open server logs")
                                onTriggered: WivrnServer.open_server_logs()
                            }
                        ]
                    }
                }
            }

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
                        enabled: root.server_started && Adb.adbInstalled && select_usb_device.connected_headset_count > 0
                        visible: !WivrnServer.headsetConnected

                        Controls.ToolTip.visible: root.server_started && hovered
                        Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                        Controls.ToolTip.text: {
                            if (!Adb.adbInstalled)
                                return i18n("ADB is not installed");
                            if (select_usb_device.connected_headset_count == 0)
                                return i18n("No headset is connected or your headset is not in developer mode");
                            return "";
                        }
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
                    opacity: steam_info.opacity
                    visible: steam_info.visible
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }

                Kirigami.Heading {
                    level: 1
                    wrapMode: Text.WordWrap
                    text: i18n("Steam information")
                    opacity: steam_info.opacity
                    visible: steam_info.visible
                    Layout.maximumHeight: root.server_started ? -1 : 0
                }
                SteamLaunchOptions {
                    id: steam_info
                    visible: WivrnServer.steamCommand != ""
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
                onTriggered: root.pageStack.push(Qt.createComponent("WizardPage.qml").createObject())
                visible: root.pageStack.depth == 1
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("Install the app")
                icon.name: "install-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("ApkInstallPage.qml").createObject())
                visible: root.pageStack.depth == 1
                enabled: root.server_started && Adb.adbInstalled
                tooltip: {
                    if (!root.server_started)
                        return "";
                    if (!Adb.adbInstalled)
                        return i18n("ADB is not installed");
                    return "";
                }
            },
            Kirigami.Action {
                text: i18n("Headsets")
                icon.name: "item-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("HeadsetsPage.qml").createObject())
                visible: root.pageStack.depth == 1
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("Settings")
                icon.name: "settings-configure-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("SettingsPage.qml").createObject())
                visible: root.pageStack.depth == 1
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("Troubleshoot")
                icon.name: "help-contents-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("TroubleshootPage.qml").createObject())
                enabled: root.pageStack.depth == 1
            }
        ]
    }
}
