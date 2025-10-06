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

    Settings {
        id: config
    }

    Core.Settings {
        property alias adb_custom: settings.adb_custom
        property alias adb_location: settings.adb_location
    }
    property bool adb_custom
    property string adb_location

    ColumnLayout {
        id: column
        anchors.fill: parent

        Kirigami.FormLayout {

            Controls.CheckBox {
                id: manual_foveation
                checked: true
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
            }

            Controls.ComboBox {
                id: encoder_layout
                Kirigami.FormData.label: i18n("Encoder layout:")
                model: [
                    {
                        label: i18nc("encoder preset", "Automatic"),
                        value: null
                    },
                    {
                        label: i18nc("encoder preset", "Manual"),
                        value: null
                    },
                    {
                        label: i18nc("encoder preset", "Default preset"),
                        value: [
                            {
                                width: 0.5,
                                height: 1,
                                offset_x: 0,
                                offset_y: 0
                            },
                            {
                                width: 0.5,
                                height: 1,
                                offset_x: 0.5,
                                offset_y: 0
                            }
                        ]
                    },
                    {
                        label: i18nc("encoder preset", "Low latency preset"),
                        value: [
                            {
                                width: 0.5,
                                height: 1,
                                offset_x: 0.5,
                                offset_y: 0,
                                encoder: 'x264',
                                codec: 'h264'
                            },
                            {
                                width: 0.5,
                                height: 1,
                                offset_x: 0,
                                offset_y: 0
                            }
                        ]
                    },
                    {
                        label: i18nc("encoder preset", "Safe preset"),
                        value: [
                            {
                                width: 1,
                                height: 1,
                                offset_x: 0,
                                offset_y: 0
                            }]
                    }
                ]
                textRole: "label"
                valueRole: "value"
                onCurrentValueChanged: {
                    if (currentValue)
                    {
                        config.set_encoder_preset(currentValue);
                        partitionner.currentIndex = -1;
                    }
                }
            }

            Controls.Label {
                visible: encoder_layout.currentIndex > 0
                text: i18n("To add a new encoder, split an existing encoder by clicking near an edge.\nDrag an edge to resize or remove encoders.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RectanglePartitionner {
                id: partitionner
                implicitWidth: 600
                implicitHeight: 300
                visible: encoder_layout.currentIndex > 0
                settings: config
                onCodecChanged: {
                    for (var i = 0; i < codec_combo.model.length; i++) {
                        if (codec_combo.model[i].name == codec)
                            codec_combo.currentIndex = i;
                    }
                }
                onEncoderChanged: {
                    for (var i = 0; i < encoder_combo.model.length; i++) {
                        if (encoder_combo.model[i].name == encoder)
                            encoder_combo.currentIndex = i;
                    }
                }
                onEncoderLayoutChanged: encoder_layout.currentIndex = 1 // Manual layout
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Encoder:")
                enabled: partitionner.selected
                visible: encoder_layout.currentIndex > 0
                Controls.ComboBox {
                    id: encoder_combo
                    model: [
                        // Keep it in sync with rectangle_partitionner.h (encoder_name_from_setting)
                        {
                            name: "auto",
                            label: i18nc("automatic encoder setup", "Auto"),
                            codecs: "auto"
                        },
                        {
                            name: "nvenc",
                            label: i18n("nvenc (NVIDIA GPUs)"),
                            codecs: "auto,h264,h265,av1"
                        },
                        {
                            name: "vaapi",
                            label: i18n("vaapi (AMD and Intel GPUs)"),
                            codecs: "auto,h264,h265,av1"
                        },
                        {
                            name: "x264",
                            label: i18n("x264 (software encoding)"),
                            codecs: "h264"
                        },
                        {
                            name: "vulkan",
                            label: i18n("Vulkan (Vulkan Video)"),
                            codecs: "h264"
                        }
                    ]
                    textRole: "label"
                    onCurrentIndexChanged: {
                        partitionner.encoder = encoder_combo.model[currentIndex].name;

                        // Check if the currently selected codec is supported by the new encoder
                        var supported_codecs = model[currentIndex].codecs.split(",");
                        var current_codec = codec_combo.model[codec_combo.currentIndex].name;

                        if (!supported_codecs.includes(current_codec)) {
                            for (var i = 0; i < codec_combo.model.length; i++) {
                                if (supported_codecs.includes(codec_combo.model[i].name)) {
                                    codec_combo.currentIndex = i;
                                    break;
                                }
                            }
                        }
                    }
                }
                Kirigami.ContextualHelpButton {
                    toolTipText: i18n("\"Auto\" will use hardware acceleration if it is available")
                }
            }

            Controls.ComboBox {
                id: codec_combo
                Kirigami.FormData.label: i18n("Codec:")
                enabled: partitionner.selected
                visible: encoder_layout.currentIndex > 0
                model: [
                    // Keep it in sync with rectangle_partitionner.h (codec_name_from_setting)
                    {
                        name: "auto",
                        label: i18nc("automatic codec setup", "Auto")
                    },
                    {
                        name: "h264",
                        label: i18n("H.264")
                    },
                    {
                        name: "h265",
                        label: i18n("H.265")
                    },
                    {
                        name: "av1",
                        label: i18n("AV1")
                    }
                ]
                textRole: "label"
                onCurrentIndexChanged: partitionner.codec = model[currentIndex].name

                delegate: Controls.ItemDelegate {
                    required property string label
                    required property string name

                    width: codec_combo.width
                    text: i18n(label)
                    highlighted: ListView.isCurrentItem
                    enabled: encoder_combo.model[encoder_combo.currentIndex].codecs.split(",").includes(name)
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
                    placeholderText: settings.adb_location
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
        settings.load();
    }

    function save() {
        config.bitrate = bitrate.value * 1000000;
        config.scale = manual_foveation.checked ? 1 - scale_slider.value / 100.0 : -1;
        config.manualEncoders = encoder_layout.currentIndex > 0;
        let openvr = openvr_combobox.model.get(openvr_combobox.currentIndex)
        if (openvr.is_custom) {
            config.openvr = openvr_text.text;
        } else {
            config.openvr = openvr.value
        }
        settings.adb_custom = adb_custom.checked;
        settings.adb_location = adb_location.text;
        Adb.setPath(adb_custom.checked ? adb_location.text : "adb");

        config.debugGui = debug_gui.checked;
        config.steamVrLh = steamvr_lh.checked;
    }

    function load() {
        bitrate.value = config.bitrate / 1000000;

        if (config.scale > 0) {
            scale_slider.value = Math.round(100 - config.scale * 100);
            manual_foveation.checked = true;
        } else {
            scale_slider.value = 50;
            manual_foveation.checked = false;
        }

        if (config.manualEncoders) {
            // auto_encoders.checked = false;
            encoder_layout.currentIndex = 1;
        } else {
            // auto_encoders.checked = true;
            encoder_layout.currentIndex = 0;
        }

        debug_gui.checked = config.debugGui;
        steamvr_lh.checked = config.steamVrLh;

        openvr_combobox.load()

        adb_custom.checked = settings.adb_custom;
        adb_location.text = settings.adb_location;
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: applicationWindow().pageStack.pop()
    }
}
