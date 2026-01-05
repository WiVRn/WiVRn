pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ScrollablePage {
    id: headsets
    title: i18n("Headset statistics")

    Connections {
        target: WivrnServer
        function onHeadsetConnectedChanged(value) {
            if (!value)
                applicationWindow().pageStack.pop();
        }
    }

    // TODO: bling

    // Controls.Label {
	   //  anchors.fill: parent
	   //  horizontalAlignment: Qt.AlignHCenter
	   //  verticalAlignment: Qt.AlignVCenter
	   //  text: i18n("Connected headset")
    // }

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
        onActivated: {if (isCurrentPage) applicationWindow().pageStack.pop();}
    }
}
