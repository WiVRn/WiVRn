import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    default property alias content: inner.children
    property alias image: img.source

    property list<Kirigami.Action> actions

    globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None
    RowLayout {
        anchors.fill: parent
        ColumnLayout {
            Image {
                id: img
                source: Qt.resolvedUrl("wivrn.svg")
            }
            Item {
                Layout.fillHeight: true
            }
        }

        ColumnLayout {
            id: inner
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
