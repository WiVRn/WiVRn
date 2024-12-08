import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ScrollablePage {
    id: settings
    title: "Settings"

    Settings2 {
        id: config
    }

    Apps {
        id: apps
    }

    Dialogs.FileDialog {
        id: app_browse
        onAccepted: app_text.text = new URL(selectedFile).pathname
    }

    ColumnLayout {
        id: column
        anchors.fill: parent

        Kirigami.FormLayout {
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Application"
            }

            GridLayout {
                // Kirigami.FormData.label: "Application to start when a headset connects:"
                // Kirigami.FormData.buddyFor: app_combobox
                Layout.fillWidth: true
                columns: 2

                Controls.ComboBox {
                    id: app_combobox
                    Layout.columnSpan: 2
                    textRole: "name"
                    model: apps
                }

                Controls.TextField {
                    id: app_text
                    placeholderText: app_combobox.currentText
                    // implicitWidth: 30 * Kirigami.Units.gridUnit
                    enabled: app_combobox.model.get(app_combobox.currentIndex).is_custom
                    Layout.fillWidth: true
                }

                Controls.Button {
                    text: "Browse"
                    enabled: app_combobox.model.get(app_combobox.currentIndex).is_custom
                    onClicked: app_browse.open()
                }
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Foveation"
            }

            Controls.Label {
                Layout.maximumWidth: 35 * Kirigami.Units.gridUnit
                text: "A stronger foveation makes the image sharper in the center than in the periphery and makes the decoding faster. This is better for fast paced games.\n\nA weaker foveation gives a uniform sharpness in the whole image.\n\nThe recommended values are between 20% and 50% for headsets without eye tracking and between 50% and 70% for headsets with eye tracking."
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                // Kirigami.FormData.label: "Foveation strength"
                // Kirigami.FormData.buddyFor: auto_foveation
                Controls.RadioButton {
                    id: auto_foveation
                    checked: true
                    text: "Automatic"
                }
                Controls.RadioButton {
                    id: manual_foveation
                    text: "Manual"
                }
            }

            GridLayout {
                enabled: manual_foveation.checked
                columns: 4

                Controls.Slider {
                    id: scale_slider
                    Layout.columnSpan: 3
                    implicitWidth: 20 * Kirigami.Units.gridUnit
                    from: 0
                    to: 80
                    stepSize: 1
                }

                Controls.Label {
                    text: scale_slider.value + "%"
                }

                Controls.Label {
                    text: "Weaker"
                }
                Item {
                    // spacer item
                    Layout.fillWidth: true
                }
                Controls.Label {
                    text: "Stronger"
                }
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Bitrate"
            }

            Controls.SpinBox {
                id: bitrate
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
                id: auto_encoders
                checked: true
                text: "Automatic"
            }
            Controls.RadioButton {
                id: manual_encoders
                text: "Manual"
            }

            Controls.Label {
                visible: manual_encoders.checked
                text: "To add a new encoder, split an existing encoder by clicking near an edge.\nDrag an edge to resize or remove encoders."
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // TODO partitioner

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
        config.load(WivrnServer);
        settings.load();
    }

    function save() {
        config.bitrate = bitrate.value * 1000000;
        config.scale = manual_foveation.checked ? 1 - scale_slider.value / 100.0 : -1;

        if (apps.get(app_combobox.currentIndex).is_custom)
            config.application = app_text.text;
        else
            config.application = apps.get(app_combobox.currentIndex).command;
    }

    function load() {
        bitrate.value = config.bitrate / 1000000;

        if (config.scale > 0) {
            scale_slider.value = Math.round(100 - config.scale * 100);
            manual_foveation.checked = true;
        } else {
            auto_foveation.checked = true;
        }

        var found = false;
        var custom_idx = -1;
        for (var i = 0; i < apps.count; i++) {
            var app = apps.get(i);
            if (app.is_custom)
                custom_idx = i;
            else if (app.command == config.application) {
                app_combobox.currentIndex = i;
                found = true;
                break;
            }
        }
        if (!found) {
            app_text.text = config.application;
            app_combobox.currentIndex = custom_idx;
        }
    }
}
