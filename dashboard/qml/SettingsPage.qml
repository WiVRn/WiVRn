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

    Settings {
        id: config
    }

    ColumnLayout {
        id: column
        anchors.fill: parent

        Kirigami.FormLayout {

            Controls.CheckBox {
                id: manual_foveation
                checked: config.scale != -1
                Layout.row: 0
                Layout.column: 0
                text: i18nc("automatic foveation setup", "Manual foveation")
            }

            GridLayout {
                columns: 5
                enabled: manual_foveation.checked
                Kirigami.FormData.label: i18n("Foveation strength:")

                Controls.Slider {
                    id: scale_slider
                    Layout.row: 0
                    Layout.column: 0
                    Layout.columnSpan: 3
                    implicitWidth: 20 * Kirigami.Units.gridUnit
                    from: 0
                    to: 80
                    stepSize: 1
                }

                Controls.Label {
                    Layout.row: 0
                    Layout.column: 3
                    text: i18n("%1 %", scale_slider.value)
                }

                Kirigami.ContextualHelpButton {
                    Layout.row: 0
                    Layout.column: 4
                    toolTipText: i18n("A stronger foveation makes the image sharper in the center than in the periphery and makes the decoding faster. This is better for fast paced games.\n\nA weaker foveation gives a uniform sharpness in the whole image.\n\nThe recommended values are between 20% and 50% for headsets without eye tracking and between 50% and 70% for headsets with eye tracking.")
                }

                Controls.Label {
                    Layout.row: 1
                    Layout.column: 0
                    text: i18nc("weaker foveation", "Weaker")
                }
                Item {
                    Layout.row: 1
                    Layout.column: 1
                    // spacer item
                    Layout.fillWidth: true
                }
                Controls.Label {
                    Layout.row: 1
                    Layout.column: 2
                    text: i18nc("stronger foveation", "Stronger")
                }
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
            }

            Controls.SpinBox {
                id: bitrate
                Kirigami.FormData.label: i18n("Bitrate:")
                from: 1
                to: 200

                textFromValue: (value, locale) => i18nc("bitrate", "%1 Mbit/s", value)
                valueFromText: function (text, locale) {
                    var prefix_suffix = i18nc("bitrate", "%1 Mbit/s", "%1").split('%1');
                    for (var i of prefix_suffix) {
                        text = text.replace(i, "");
                    }
                    return Number.fromLocaleString(text);
                }

                onValueModified: config.bitrate = value
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                text: i18n("The current encoder configuration is not supported")
                type: Kirigami.MessageType.Information
                visible: !config.simpleConfig
                actions: [
                    Kirigami.Action {
                        text: i18n("Reset")
                        onTriggered: config.encoder = Settings.EncoderAuto
                    }
                ]
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Encoder:")
                enabled: config.simpleConfig
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
                    onCurrentIndexChanged: if (settings.allowUpdates) {config.encoder = model[currentIndex].encoder}
                    textRole: "label"
                    Connections {
                        target: config
                        function onEncoderChanged() {
                            var encoder = config.encoder;
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

            RowLayout {
                Kirigami.FormData.label: i18n("Codec:")
                Controls.ComboBox {
                    id: codec_combo
                    enabled: config.simpleConfig && config.encoder != Settings.EncoderAuto
                    model: [
                        {
                            label: i18nc("automatic codec setup", "Auto"),
                            codec: Settings.CodecAuto
                        },
                        {
                            label: i18n("H.264"),
                            codec: Settings.H264
                        },
                        {
                            label: i18n("H.265"),
                            codec: Settings.H265
                        },
                        {
                            label: i18n("AV1"),
                            codec: Settings.Av1
                        }
                    ]
                    onCurrentIndexChanged: if (settings.allowUpdates) {config.codec = model[currentIndex].codec}
                    textRole: "label"

                    delegate: Controls.ItemDelegate {
                        required property string label
                        required property var codec

                        width: codec_combo.width
                        text: i18n(label)
                        highlighted: ListView.isCurrentItem
                        enabled: config.allowedCodecs.includes(codec)
                    }
                    Connections {
                        target: config
                        function onCodecChanged() {
                            var codec = config.codec;
                            var i = codec_combo.model.findIndex( item => item.codec == codec)
                            if (i > -1)
                                codec_combo.currentIndex = i
                        }
                    }
                }
                Controls.CheckBox {
                    enabled: config.can10bit
                    text: i18n("10 bits")
                    checked: config.tenbit
                    onCheckedChanged: config.tenbit = checked
                }
                Kirigami.ContextualHelpButton {
                    toolTipText: i18n("10-bit encoding improves image quality but is not supported by all codecs and hardware")
                }
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
                visible: config.hid_forwarding_supported
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
                visible: config.debug_gui_supported
            }
            RowLayout {
                visible: config.steamvr_lh_supported
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
                            if (openvr_libs.get(i).value == config.openvr) {
                                openvr_combobox.currentIndex = i;
                                return;
                            }
                        }
                        for(let i=0 ; i < openvr_libs.count; i++) {
                            if (openvr_libs.get(i).is_custom) {
                                openvr_text.text = config.openvr
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
            config.save(WivrnServer);

            applicationWindow().pageStack.pop();
        }
        onReset: {
            config.restore_defaults();
            settings.load();
        }
        onRejected: applicationWindow().pageStack.pop()
    }

    Component.onCompleted: {
        openvr_libs.init()
        config.load(WivrnServer);
        // If bitrate was manually set higher, keep the limit
        bitrate.to = Math.max(bitrate.to, config.bitrate)
        bitrate.value = config.bitrate
        settings.allowUpdates = true;
        settings.load();
    }

    function save() {
        config.scale = manual_foveation.checked ? 1 - scale_slider.value / 100.0 : -1;
        let openvr = openvr_combobox.model.get(openvr_combobox.currentIndex)
        if (openvr.is_custom) {
            config.openvr = openvr_text.text;
        } else {
            config.openvr = openvr.value
        }
        DashboardSettings.adb_custom = adb_custom.checked;
        DashboardSettings.adb_location = adb_location.text;
        Adb.setPath(DashboardSettings.adb_custom.checked ? adb_location.text : "adb");

        DashboardSettings.show_system_checks = show_system_checks.checked;

        config.debugGui = debug_gui.checked;
        config.steamVrLh = steamvr_lh.checked;
        config.hidForwarding = hid_forwarding.checked;
    }

    function load() {
        if (config.scale > 0) {
            scale_slider.value = Math.round(100 - config.scale * 100);
        } else {
            scale_slider.value = 50;
        }

        debug_gui.checked = config.debugGui;
        steamvr_lh.checked = config.steamVrLh;
        hid_forwarding.checked = config.hidForwarding;

        openvr_combobox.load()

        adb_custom.checked = DashboardSettings.adb_custom;
        adb_location.text = DashboardSettings.adb_location;

        show_system_checks.checked = DashboardSettings.show_system_checks;
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: applicationWindow().pageStack.pop()
    }
}
