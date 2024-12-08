import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.Page {
    id: settings
    title: "Settings"

    background: Rectangle {
        color: "green"
    }

    leftPadding: 0
    rightPadding: 0

    ColumnLayout {
        id: column
        anchors.fill: parent

        Controls.ScrollView {
            id: scroll
            Layout.fillHeight: true
            Layout.fillWidth: true
            leftPadding: Kirigami.Units.gridUnit
            rightPadding: Kirigami.Units.gridUnit
            background: Rectangle {
                color: "red"
            }

            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

            Kirigami.FormLayout {
                implicitWidth: scroll.width - scroll.effectiveScrollBarWidth - scroll.leftPadding - scroll.rightPadding

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: "Application"
                }

                GridLayout {
                    Kirigami.FormData.label: "Application to start when a headset connects:"
                    Kirigami.FormData.buddyFor: app_combobox
                    columns: 2

                    Controls.ComboBox {
                        id: app_combobox
                        Layout.columnSpan: 2
                        textRole: "name"
                        model: Apps {}
                    }

                    Controls.TextField {
                        placeholderText: app_combobox.currentText
                        enabled: app_combobox.model.get(app_combobox.currentIndex).is_custom
                    }

                    Controls.Button {
                        text: "Browse"
                        enabled: app_combobox.model.get(app_combobox.currentIndex).is_custom
                        onClicked: {}
                    }
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: "Foveation"
                }

                Controls.Label {
                    id: toto
                    text: "A stronger foveation makes the image sharper in the center than in the periphery and makes the decoding faster. This is better for fast paced games.\n\nA weaker foveation gives a uniform sharpness in the whole image.\n\nThe recommended values are between 20% and 50% for headsets without eye tracking and between 50% and 70% for headsets with eye tracking."
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Controls.Label {
                    text: "label width %1\nform width %2\nscrollview width %3\ncolumn width %4".arg(toto.width).arg(parent.width).arg(scroll.width).arg(column.width)
                }

                ColumnLayout {
                    Kirigami.FormData.label: "Foveation strength"
                    Kirigami.FormData.buddyFor: auto_foveation
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
                        id: x
                        Layout.columnSpan: 3
                        from: 0
                        to: 80
                        stepSize: 1
                    }

                    Controls.Label {
                        text: x.value + "%"
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
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // TODO partitioner
            }
        }

        RowLayout {
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit

            Item {
                // spacer item
                Layout.fillWidth: true
            }

            Controls.Button {
                text: "Ok"
                onClicked: applicationWindow().pageStack.pop()
            }

            Controls.Button {
                text: "Cancel"
                onClicked: applicationWindow().pageStack.pop()
            }
        }
    }
}
