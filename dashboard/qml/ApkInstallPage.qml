pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ScrollablePage {
    id: apk_install
    title: i18n("Install the WiVRn app on your headset")

    flickable.interactive: false

    Kirigami.CardsListView {
        model: Adb
        delegate: DeviceCard {
            id: device_card
            required property string serial
            required property string model
            required property string manufacturer
            required property bool isWivrnInstalled

            name: device_card.manufacturer + " " + device_card.model
            serial_number: device_card.serial
            is_wivrn_installed: device_card.isWivrnInstalled
        }

        Kirigami.PlaceholderMessage {
            id: placeholder_message
            anchors.centerIn: parent
            width: parent.width - (Kirigami.Units.largeSpacing * 4)

            visible: parent.count == 0

            text: Adb.adbInstalled ? i18n("Connect a headset and make sure developper mode is enabled.") : i18n("ADB is not installed.")
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

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: applicationWindow().pageStack.pop()
    }
}
