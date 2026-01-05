pragma ComponentBehavior: Bound

import QtCore as Core
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ScrollablePage {
    id: settings
    title: i18n("Settings")

    flickable.interactive: false // Make sure the Kirigami.ScrollablePage does not eat the vertical mouse dragging events

    property bool allowUpdates: false // ignore onXXX events until document is loaded

    ColumnLayout {
        id: column
        anchors.fill: parent

        Kirigami.FormLayout {

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: i18n("The current encoder configuration is not supported")
                type: Kirigami.MessageType.Information
                visible: !Settings.simpleConfig
                actions: [
                    Kirigami.Action {
                        text: i18n("Reset")
                        onTriggered: Settings.encoder = Settings.EncoderAuto
                    }
                ]
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Encoder:")
                enabled: Settings.simpleConfig
                Controls.ComboBox {
                    id: encoder_combo
                    model: [
                        {
                            label: i18nc("automatic encoder setup", "Auto"),
                            encoder: Settings.EncoderAuto
                        },
                        {
                            label: i18n("nvenc (NVIDIA GPUs)"),
                            encoder: Settings.Nvenc
                        },
                        {
                            label: i18n("vaapi (AMD and Intel GPUs)"),
                            encoder: Settings.Vaapi
                        },
                        {
                            label: i18n("Vulkan (Any modern GPU)"),
                            encoder: Settings.Vulkan
                        },
                        {
                            label: i18n("x264 (software encoding)"),
                            encoder: Settings.X264
                        }
                    ]
                    onCurrentIndexChanged: if (settings.allowUpdates) {Settings.encoder = model[currentIndex].encoder}
                    textRole: "label"
                    Connections {
                        target: Settings
                        function onEncoderChanged() {
                            var encoder = Settings.encoder;
                            var i = encoder_combo.model.findIndex( item => item.encoder == encoder)
                            if (i > -1)
                                encoder_combo.currentIndex = i
                        }
                    }
                }
                Kirigami.ContextualHelpButton {
                    toolTipText: i18n("\"Auto\" will use hardware acceleration if it is available")
                }
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
            }

            SelectGame {
                Kirigami.FormData.label: i18n("Autostart application:")
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
            }

            Kirigami.Heading {
                text: i18n("Advanced options")
                level: 1
                type: Kirigami.Heading.Type.Primary
            }
            Controls.CheckBox {
                id: show_system_checks
                text: i18n("Check system configuration on start")
            }
            RowLayout {
                visible: Settings.hid_forwarding_supported
                Controls.CheckBox {
                    id: hid_forwarding
                    text: i18n("Forward keyboard & mouse from headset")
                }
                Kirigami.ContextualHelpButton {
                    toolTipText: i18n("Keyboard and mouse connected to the client will act as if connected to the server. Client OS may reserve specific keys and combinations, which cannot be forwarded.")
                }
            }
            Controls.CheckBox {
                id: debug_gui
                text: i18n("Enable debug window")
                visible: Settings.debug_gui_supported
            }
            RowLayout {
                visible: Settings.steamvr_lh_supported
                Controls.CheckBox {
                    id: steamvr_lh
                    text: i18n("Enable SteamVR tracked devices support")
                }
                Kirigami.ContextualHelpButton {
                    toolTipText: i18n("Allows the use of lighthouse-based controllers and trackers.\nRequires SteamVR to be installed.\nDevices must be be powered on before connecting to WiVRn.\nAn external tool such as motoc is needed for calibration.")
                }
            }

            Controls.CheckBox {
                id: adb_custom
                Layout.row: 0
                Layout.column: 0
                text: i18n("Custom adb location")
            }

            Dialogs.FileDialog {
                id: adb_browse
                onAccepted: {
                    adb_location.text = new URL(selectedFile).pathname;
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Adb location:")
                Layout.fillWidth: true
                enabled: adb_custom.checked
                Controls.TextField {
                    id: adb_location
                    placeholderText: DashboardSettings.adb_location
                    Layout.fillWidth: true
                }
                Controls.Button {
                    text: i18nc("browse a file to choose the adb binary", "Browse")
                    onClicked: adb_browse.open()
                }
            }

            ListModel {
                id: openvr_libs

                function init() {
                    openvr_libs.append({
                        "name": i18n("Default"),
                        "value": "",
                        "is_custom": false
                    });
                    var libs = WivrnServer.openVRCompat;
                    for (var i = 0; i < libs.length; i++) {
                        openvr_libs.append({
                            "name": libs[i].name,
                            "value": libs[i].path,
                            "is_custom": false
                        });
                    }

                    openvr_libs.append({
                        "name": i18nc("set a custom OpenVR compatibility library", "Custom"),
                        "is_custom": true
                    });

                    openvr_libs.append({
                        "name": i18n("Disabled"),
                        "value": "-",
                        "is_custom": false
                    });
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("OpenVR compatibility library:")
                Layout.fillWidth: true
                Controls.ComboBox {
                    id: openvr_combobox
                    Layout.columnSpan: 2
                    textRole: "name"
                    model: openvr_libs

                    function load() {
                        for(let i=0 ; i < openvr_libs.count; i++) {
                            if (openvr_libs.get(i).value == Settings.openvr) {
                                openvr_combobox.currentIndex = i;
                                return;
                            }
                        }
                        for(let i=0 ; i < openvr_libs.count; i++) {
                            if (openvr_libs.get(i).is_custom) {
                                openvr_text.text = Settings.openvr
                                openvr_combobox.currentIndex = i;
                                return;
                            }
                        }
                    }
                }
                Controls.TextField {
                    id: openvr_text
                    placeholderText: i18n("Library path, excluding bin/linux64/vrclient.so")
                    visible: !!openvr_combobox.model.get(openvr_combobox.currentIndex)?.is_custom
                    Layout.fillWidth: true
                }
            }

        }

        Item {
            // spacer item
            Layout.fillHeight: true
        }
    }

    footer: Controls.DialogButtonBox {
        standardButtons: Controls.DialogButtonBox.Ok | Controls.DialogButtonBox.Cancel | Controls.DialogButtonBox.Reset

        onAccepted: {
            settings.save();
            Settings.save(WivrnServer);

            applicationWindow().pageStack.pop();
        }
        onReset: {
            Settings.restore_defaults();
            settings.load();
        }
        onRejected: applicationWindow().pageStack.pop()
    }

    Component.onCompleted: {
        openvr_libs.init()
        Settings.load(WivrnServer);
        settings.allowUpdates = true;
        settings.load();
    }

    function save() {
        let openvr = openvr_combobox.model.get(openvr_combobox.currentIndex)
        if (openvr.is_custom) {
            Settings.openvr = openvr_text.text;
        } else {
            Settings.openvr = openvr.value
        }
        DashboardSettings.adb_custom = adb_custom.checked;
        DashboardSettings.adb_location = adb_location.text;
        Adb.setPath(DashboardSettings.adb_custom.checked ? adb_location.text : "adb");

        DashboardSettings.show_system_checks = show_system_checks.checked;

        Settings.debugGui = debug_gui.checked;
        Settings.steamVrLh = steamvr_lh.checked;
        Settings.hidForwarding = hid_forwarding.checked;
    }

    function load() {
        debug_gui.checked = Settings.debugGui;
        steamvr_lh.checked = Settings.steamVrLh;
        hid_forwarding.checked = Settings.hidForwarding;

        openvr_combobox.load()

        adb_custom.checked = DashboardSettings.adb_custom;
        adb_location.text = DashboardSettings.adb_location;

        show_system_checks.checked = DashboardSettings.show_system_checks;
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: {if (isCurrentPage) applicationWindow().pageStack.pop();}
    }
}
