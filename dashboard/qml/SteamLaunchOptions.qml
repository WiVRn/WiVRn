import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import io.github.wivrn.wivrn

ColumnLayout {
    Controls.Label {
        text: i18n("For Steam games, right click on the game in Steam, go in Properties > Shortcut and paste this in \"Launch options\":")
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    RowLayout {
        Controls.TextField {
            text: WivrnServer.steamCommand
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            readOnly: true
            // cursorVisible: true
            Controls.ToolTip.text: i18n("Paste this in the Steam launch options for the app you want to start")
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.delay: 1000
        }

        Controls.Button {
            text: i18nc("copy text to the clipboard", "Copy")
            icon.name: "edit-copy-symbolic"
            onClicked: {
                WivrnServer.copy_steam_command();
                showPassiveNotification(i18n("Copied Steam launch command"), 2000);
            }
        }
    }
}
