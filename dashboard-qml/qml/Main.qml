// Includes relevant modules used by the QML
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

    // contextDrawer: Kirigami.ContextDrawer {}

    Connections {
        target: WivrnServer
        function onCapSysNiceChanged(value) {
            if (WivrnServer.serverStatus != WivrnServer.Stopped)
                restart_capsysnice.visible = true
        }
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
                            WivrnServer.restart_server()
                            restart_capsysnice.visible = false
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
                        onClicked: function() {
                            WivrnServer.stop_server()

                            while(root.pageStack.depth > 1)
                                root.pageStack.pop(null);
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

        }
        actions: [
            Kirigami.Action {
                text: "Headsets"
                icon.name: "user-home-symbolic"
                // onTriggered: root.showPassiveNotification("Action 1 clicked")
                onTriggered: root.pageStack.push(headsets)
                enabled: root.server_started && root.pageStack.depth == 1
            },
            Kirigami.Action {
                text: "Settings"
                icon.name: "settings-configure-symbolic"
                // onTriggered: root.showPassiveNotification("Action 2 clicked")
                onTriggered: root.pageStack.push(settings)
                enabled: root.server_started && root.pageStack.depth == 1
            }
        ]
    }

    Component {
        id: headsets
        Kirigami.Page {

        }
    }

    Component {
        id: settings
        Kirigami.ScrollablePage {
            ColumnLayout {
                anchors.fill: parent
                Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: "Application"
                    }

                    GridLayout {
                        Kirigami.FormData.label: "Application to start when a headset connects:"
                        Kirigami.FormData.labelAlignment: Qt.AlignTop
                        columns: 2

                        Controls.ComboBox {
                            Layout.columnSpan: 2
                        }

                        Controls.TextField {
                        }

                        Controls.Button {
                            text: "Browse"
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: "Foveation"
                    }

                    Controls.Label {
                        text: "A stronger foveation makes the image sharper in the center than in the periphery and makes the decoding faster. This is better for fast paced games.\n\nA weaker foveation gives a uniform sharpness in the whole image.\n\nThe recommended values are between 20% and 50% for headsets without eye tracking and between 50% and 70% for headsets with eye tracking."
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    ColumnLayout {
                        Kirigami.FormData.label: "Foveation strength"
                        Kirigami.FormData.labelAlignment: Qt.AlignTop
                        Controls.RadioButton {
                            checked: true
                            text: "Automatic"
                            id: auto_foveation
                        }
                        Controls.RadioButton {
                            text: "Manual"
                            id: manual_foveation
                        }
                    }

                    GridLayout {
                        enabled: manual_foveation.checked
                        columns: 4

                        Controls.Slider {
                            id: x
                            Layout.columnSpan: 3
                            from: 0
                            to: 80
                            stepSize: 1
                        }

                        Controls.Label {
                            text: x.value + "%"
                        }

                        // RowLayout {
                            Controls.Label {
                                text: "Weaker"
                            }
                            Item {
                                // spacer item
                                Layout.fillWidth: true
                                // Layout.fillHeight: true
                            }
                            Controls.Label {
                                text: "Stronger"
                            }
                        // }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: "Bitrate"
                    }

                    Controls.SpinBox {
                        from: 1
                        to: 200

                        textFromValue: (value, locale) => qsTr("%1 Mbit/s").arg(value)
                        valueFromText: (text, locale) => Number.fromLocaleString(locale, text.replace("Mbit/s", ""))
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: "Encoder configuration"
                    }

                    Controls.RadioButton {
                        checked: true
                        text: "Automatic"
                    }
                    Controls.RadioButton {
                        text: "Manual"
                    }

                    Controls.Label {
                        text: "To add a new encoder, split an existing encoder by clicking near an edge.\nDrag an edge to resize or remove encoders."
                    }

                    // TODO partitioner
                }

                Item {
                    // spacer item
                    // Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                RowLayout {
                    Controls.Button {
                        text: "Pop!"
                        onClicked: root.pageStack.pop()
                    }
                    Controls.Button {
                        text: "Pop!"
                        onClicked: root.pageStack.pop()
                    }
                }
            }
        }
    }


    // globalDrawer: Kirigami.GlobalDrawer {
    //     title: "Global Drawer"
    //     // titleIcon: "applications-graphics"
    //     actions: [
    //         Kirigami.Action {
    //             text: "Kirigami Action 1"
    //             icon.name: "user-home-symbolic"
    //             // onTriggered: showPassiveNotification("Action 1 clicked")
    //         },
    //         Kirigami.Action {
    //             text: "Kirigami Action 2"
    //             icon.name: "settings-configure-symbolic"
    //             // onTriggered: showPassiveNotification("Action 2 clicked")
    //         },
    //         Kirigami.Action {
    //             text: i18n("Quit")
    //             icon.name: "application-exit-symbolic"
    //             shortcut: StandardKey.Quit
    //             onTriggered: Qt.quit()
    //         }
    //     ]
    // }

}
