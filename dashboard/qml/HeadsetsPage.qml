pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ScrollablePage {
    id: headsets
    title: i18n("Headsets")

    // flickable.interactive: false

    Kirigami.PromptDialog {
        id: rename_headset
        title: i18n("Rename headset")

        property string publicKey
        property alias headset_name: text_field.text

        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Rename")
                icon.name: "dialog-ok"
                onTriggered: rename_headset.accept()

                enabled: rename_headset.headset_name != ""
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: rename_headset.close();
            }
        ]
        ColumnLayout {
            Controls.TextField {
                id: text_field
                Layout.fillWidth: true
                placeholderText: i18n("Headset name…")
            }
        }

        onAccepted: {
            WivrnServer.rename_key(rename_headset.publicKey, rename_headset.headset_name);
            rename_headset.close();
        }

        Shortcut {
            sequences: ["Enter", "Return"]
            onActivated: rename_headset.accept()
        }

        onAboutToShow: text_field.selectAll()
    }

    Component {
        id: headset_delegate
        Kirigami.Card {
            id: headset
            required property string name
            required property string publicKey

            contentItem: GridLayout {
                id: delegate_layout
                rowSpacing: Kirigami.Units.largeSpacing
                columnSpacing: Kirigami.Units.largeSpacing
                columns: 3

                Controls.Label {
                    // TextInput {
                    Layout.fillWidth: true
                    Layout.column: 0
                    Layout.row: 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 20
                    text: headset.name
                }

                Controls.Label {
                    Layout.fillWidth: true
                    Layout.column: 0
                    Layout.row: 1
                    wrapMode: Text.WordWrap
                    // text: "Last connection: unknown"
                    text: ""
                }

                Controls.Button {
                    Layout.column: 1
                    Layout.rowSpan: 2
                    text: i18n("Rename")
                    icon.name: "edit-symbolic"
                    onClicked: {
                        rename_headset.publicKey = headset.publicKey;
                        rename_headset.headset_name = headset.name;
                        rename_headset.open();
                    }
                }

                Controls.Button {
                    Layout.column: 2
                    Layout.rowSpan: 2
                    text: i18n("Remove")
                    icon.name: "delete-symbolic"
                    onClicked: WivrnServer.revoke_key(headset.publicKey)
                }
            }
        }
    }

    header: RowLayout {
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 2 * Kirigami.Units.largeSpacing
            Layout.leftMargin: 2 * Kirigami.Units.largeSpacing
            Layout.rightMargin: 2 * Kirigami.Units.largeSpacing
            text: i18n("Pairing enabled, PIN: %1", WivrnServer.pin)
            type: Kirigami.MessageType.Information
            visible: WivrnServer.pairingEnabled && !placeholder_message.visible
            actions: [
                Kirigami.Action {
                    text: i18n("Disable pairing")
                    onTriggered: WivrnServer.disable_pairing()
                }
            ]
        }
    }

    Kirigami.CardsListView {
        model: WivrnServer.knownKeys
        delegate: headset_delegate

        Kirigami.PlaceholderMessage {
            id: placeholder_message
            anchors.centerIn: parent
            width: parent.width - (Kirigami.Units.largeSpacing * 4)

            visible: parent.count == 0

            text: WivrnServer.pairingEnabled ? i18n("Start the WiVRn app on your headset and use the PIN: %1", WivrnServer.pin) : i18n("Enable pairing to allow your headset to connect to this computer")

            // explanation: ""

            helpfulAction: Kirigami.Action {
                text: i18n("Enable pairing")
                onTriggered: WivrnServer.enable_pairing()
                enabled: !WivrnServer.pairingEnabled
            }
        }
    }

    footer: Controls.DialogButtonBox {
        standardButtons: Controls.DialogButtonBox.NoButton
        onAccepted: applicationWindow().pageStack.pop()

        Controls.Button {
            text: i18nc("go back to the home page", "Back")
            icon.name: "go-previous"
            Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
        }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Pair a new headset")
            icon.name: "list-add-symbolic"
            onTriggered: WivrnServer.enable_pairing()
            enabled: !WivrnServer.pairingEnabled
        }
    ]

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: applicationWindow().pageStack.pop()
    }
}
