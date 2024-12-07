import QtQuick
import QtQuick.Controls as Controls

Controls.Label {
    // Layout.fillWidth: true
    wrapMode: Text.WordWrap

    onLinkActivated: link => Qt.openUrlExternally(link)
    MouseArea {
        anchors.fill: parent
        cursorShape: (parent as Controls.Label).hoveredLink == "" ? Qt.ArrowCursor : Qt.PointingHandCursor
        acceptedButtons: Qt.NoButton
    }
}
