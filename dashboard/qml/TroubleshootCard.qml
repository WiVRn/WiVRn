import org.kde.kirigami as Kirigami

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// Kirigami.Card {
//     id: card
//     property string title
//     property alias details: label.text
//
//     banner.title: title
//     contentItem: BetterLabel {
// 	    id: label
//     }
// }

Control {
    // T.ItemDelegate {
    id: card
    property alias title: label_title.text
    property alias details: label_details.text

    Kirigami.Theme.inherit: false
    // Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.colorSet: Kirigami.Theme.Button

    property double expanded: 0
    Behavior on expanded {
        PropertyAnimation {
            duration: Kirigami.Units.shortDuration
            easing.type: Easing.InOutCubic
        }
    }

    implicitHeight: column.anchors.topMargin + label_title.implicitHeight + card.expanded * (column.spacing + label_details.implicitHeight + column.spacing + actionsToolBar.implicitHeight) + column.anchors.bottomMargin
    implicitWidth: parent.width
    clip: true

    // background: Kirigami.ShadowedRectangle {
    //     anchors.fill: parent
    //     border.width: 1
    //     border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    //     radius: Kirigami.Units.cornerRadius
    // }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing

        BetterLabel {
            id: label_title
            font.pixelSize: 20
            Layout.preferredWidth: Math.min(implicitWidth, parent.width)
            // Layout.fillWidth: true

            MouseArea {
                anchors.fill: parent
                onClicked: card.expanded = 1 - card.expanded
                cursorShape: Qt.PointingHandCursor
            }
        }

        BetterLabel {
            id: label_details
            Layout.fillWidth: true
        }

        Kirigami.ActionToolBar {
            id: actionsToolBar
            actions: card.actions
            position: ToolBar.Footer
            flat: false
        }
    }
    /*
    Rectangle {
        anchors.fill: column
        color: Qt.rgba(0, 0, 0.5, 0.5)
    }*/

    property list<T.Action> actions
}
