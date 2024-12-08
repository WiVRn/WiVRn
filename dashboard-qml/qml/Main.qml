import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ApplicationWindow {
    id: root

    width: 800
    height: 600

    title: qsTr("WiVRn dashboard")

    property bool server_started: WivrnServer.serverStatus == WivrnServer.Started

    Connections {
        target: WivrnServer
        function onCapSysNiceChanged(value) {
            if (WivrnServer.serverStatus != WivrnServer.Stopped)
                restart_capsysnice.visible = true;
        }
    }

    Component.onCompleted: {
        if (WivrnServer.serverStatus == WivrnServer.Stopped)
            WivrnServer.start_server();
    }

    pageStack.initialPage: Kirigami.ScrollablePage {
        ColumnLayout {
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: "The server does not have CAP_SYS_NICE capabilities."
                type: Kirigami.MessageType.Warning
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
                text: "The CAP_SYS_NICE capability will be used when the server is restarted."
                type: Kirigami.MessageType.Information
                showCloseButton: true
                visible: false
                actions: [
                    Kirigami.Action {
                        text: i18n("Restart now")
                        onTriggered: {
                            WivrnServer.restart_server();
                            restart_capsysnice.visible = false;
                        }
                    }
                ]
            }

            Kirigami.InlineMessage {
                id: nvidia_monado_layers
                Layout.fillWidth: true
                text: "The <a href=\"https://github.com/WiVRn/WiVRn/issues/180\">Monado Vulkan layer</a> for NVIDIA GPUs is not installed."
                type: Kirigami.MessageType.Warning
                showCloseButton: true
                visible: true
            }

            RowLayout {
                Layout.fillWidth: true

                Image {
                    source: Qt.resolvedUrl("wivrn.svg")
                }

                ColumnLayout {
                    Layout.preferredWidth: Number.POSITIVE_INFINITY
                    Controls.Label {
                        font.pixelSize: 22
                        text: root.server_started ? "Started" : "Stopped"
                        Layout.alignment: Qt.AlignCenter
                    }

                    Controls.Button {
                        visible: root.server_started
                        text: "Stop"
                        Layout.alignment: Qt.AlignCenter
                        onClicked: function () {
                            WivrnServer.stop_server();

                            while (root.pageStack.depth > 1)
                                root.pageStack.pop();
                        }
                    }

                    Controls.Button {
                        visible: !root.server_started
                        text: "Start"
                        Layout.alignment: Qt.AlignCenter
                        onClicked: WivrnServer.start_server()
                    }
                }

            }

            Controls.Label {
                font.pixelSize: 22
                text: "Use this PIN to pair your headset: %1".arg(WivrnServer.pin)
                visible: WivrnServer.pairingEnabled
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }


        }
        actions: [
            Kirigami.Action {
                text: "Headsets"
                icon.name: "user-home-symbolic"
                // onTriggered: root.showPassiveNotification("Action 1 clicked")
                onTriggered: root.pageStack.push(Qt.resolvedUrl("Headsets.qml"))
                enabled: root.server_started && root.pageStack.depth == 1
            },
            Kirigami.Action {
                text: "Settings"
                icon.name: "settings-configure-symbolic"
                // onTriggered: root.showPassiveNotification("Action 2 clicked")
                onTriggered: root.pageStack.push(Qt.resolvedUrl("Settings.qml"))
                enabled: root.server_started && root.pageStack.depth == 1
            }
        ]
    }
}
