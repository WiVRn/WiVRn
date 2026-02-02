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

    onClosing: (close) => {
        if (WivrnServer.ownServer && WivrnServer.sessionRunning)
        {
            close.accepted = false;
            confirm_close.open();
        } else {
            Qt.quit();
        }
    }

    Kirigami.PromptDialog {
        id: confirm_close
        title: i18n("Quit WiVRn")
        subtitle: i18n("The WiVRn server is active.\nClosing the window will terminate it.")
        iconName: "dialog-warning"
        popupType: Controls.Popup.Native
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: Qt.quit()
    }

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

        function onJsonConfigurationChanged(value) {
            Settings.load(WivrnServer);
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
                columns: 3

                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                columnSpacing: 0

                Image {
                    source: Qt.resolvedUrl("wivrn.svg")
                    Layout.columnSpan: parent.width < implicitWidth + main_form.implicitWidth ? 3 : 1
                }

                Kirigami.FormLayout {
                    id: main_form
                    Layout.alignment: Qt.AlignTop
                    Layout.horizontalStretchFactor: 0

                    Kirigami.Heading {
                        text: i18n("Server status")
                        level: 1
                        type: Kirigami.Heading.Type.Primary
                    }

                    Controls.Switch {
                        Kirigami.FormData.label: i18nc("whether the server is running, displayed in front of a checkbox", "Running")
                        id: switch_running

                        checked: true
                        onCheckedChanged: {
                            if (checked && !root.server_started)
                                WivrnServer.start_server();
                            else if (!checked && root.server_started)
                                WivrnServer.stop_server();
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18nc("whether pairing is enabled, displayed in front of a checkbox", "Pairing")
                        enabled: root.server_started
                        Controls.Switch {
                            id: switch_pairing
                            onCheckedChanged: {
                                if (checked && !WivrnServer.pairingEnabled)
                                    WivrnServer.enable_pairing();
                                else if (!checked && WivrnServer.pairingEnabled)
                                    WivrnServer.disable_pairing();
                            }
                        }

                        Controls.Label {
                            visible: WivrnServer.pairingEnabled 
                            text: WivrnServer.pin
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.1
                        }
                    }

                    Kirigami.Heading {
                        text: i18n("Headset status")
                        level: 1
                        type: Kirigami.Heading.Type.Primary
                        enabled: root.server_started
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18nc("label for headset name/status under server running switch", "Headset")
                        enabled: root.server_started
                        Controls.Label {
                            text: WivrnServer.headsetConnected ? WivrnServer.systemName : i18n("Not connected")
                        }

                        Controls.Button {
                            text: i18n("Disconnect")
                            icon.name: "network-disconnect-symbolic"
                            onClicked: WivrnServer.disconnect_headset()
                            visible: root.server_started && WivrnServer.headsetConnected
                        }
                    }

                    // USB mode
                    Controls.Button {
                        Kirigami.FormData.label: i18nc("label for USB status or connect button", "USB")
                        text: i18nc("usb connect button", "Connect (wired)")
                        onClicked: select_usb_device.connect()
                        enabled: root.server_started && !WivrnServer.headsetConnected
                        visible: Adb.adbInstalled && select_usb_device.connected_headset_count > 0
                    }

                    Controls.Label {
                        Kirigami.FormData.label: i18nc("label for USB status or connect button", "USB")
                        visible: !Adb.adbInstalled && !WivrnServer.headsetConnected
                        enabled: root.server_started
                        text: i18n("ADB is not installed")
                    }

                    Controls.Label {
                        Kirigami.FormData.label: i18nc("label for USB status or connect button", "USB")
                        visible: Adb.adbInstalled && select_usb_device.connected_headset_count == 0 && !WivrnServer.headsetConnected
                        enabled: root.server_started
                        text: i18n("No headset is connected or your headset is not in developer mode");
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                }
                Item {
                    // spacer item
                    Layout.fillWidth: true
                    Layout.horizontalStretchFactor: 1
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                visible: steam_info.visible
            }

            Kirigami.Heading {
                level: 1
                type: Kirigami.Heading.Type.Primary
                wrapMode: Text.WordWrap
                text: i18n("Steam information")
                visible: steam_info.visible
            }
            SteamLaunchOptions {
                id: steam_info
                visible: root.server_started && WivrnServer.steamCommand != ""
            }

            Item {
                // spacer item
                Layout.fillHeight: true
            }
        }

        actions: [
            Kirigami.Action {
                text: i18n("Wizard")
                icon.name: "tools-wizard-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("WizardPage.qml").createObject())
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("Install the app")
                icon.name: "install-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("ApkInstallPage.qml").createObject())
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
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("Settings")
                icon.name: "settings-configure-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("SettingsPage.qml").createObject())
                enabled: root.server_started
            },
            Kirigami.Action {
                text: i18n("About")
                icon.name: "help-about"
                onTriggered: root.pageStack.push(Qt.createComponent("About.qml").createObject())
            },
            Kirigami.Action {
                text: i18n("Troubleshoot")
                icon.name: "help-contents-symbolic"
                onTriggered: root.pageStack.push(Qt.createComponent("TroubleshootPage.qml").createObject())
            }
        ]

        footer: Controls.Label {
            text: i18n("Version %1", ApkInstaller.currentVersion)
            horizontalAlignment: Text.AlignHCenter
            opacity: 0.6
            padding: Kirigami.Units.smallSpacing
        }
    }
}
