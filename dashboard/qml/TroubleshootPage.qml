pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ScrollablePage {
    title: i18n("Troubleshoot")
    id: page

    Kirigami.Theme.colorSet: Kirigami.Theme.View

    Kirigami.CardsLayout {
        maximumColumns: 1
        rowSpacing: Kirigami.Units.largeSpacing * 2

        TroubleshootCard {
            title: i18n("I cannot find the WiVRn app on my headset")
            details: i18n("If you installed WiVRn over USB on a Meta headset, the app is in the \"unknown sources\" section.")
        }
        TroubleshootCard {
            title: i18n("I cannot see my computer in the WiVRn app")
            details: i18n("If you have a firewall, make sure that port 5353/UDP is open.")
        }
        TroubleshootCard {
            title: i18n("Connecting remains stuck on \"Connection to ...\"")
            details: i18n("If you have a firewall, make sure that port 9757 is open both for TCP and UDP.")
        }
        TroubleshootCard {
            title: i18n("Connection fails with \"no route to host\"")
            details: i18n("If you have a firewall, make sure that port 9757 is open both for TCP and UDP.")
        }
        TroubleshootCard {
            title: i18n("I have an \"Incompatible WiVRn server\" error on my headset")
            details: i18n("<p>The version of the headset app and the server on your computer must match.</p><ul><li>The headset app version is in the \"About\" page</li><li>The server version is <b>%1</b></li></ul>", ApkInstaller.currentVersion)
        }
        TroubleshootCard {
            title: i18n("I have a \"Pairing is disabled\" error")
            details: i18n("If you have reinstalled the server or the client or if you want to connect another headset, you must enable pairing before connecting.") + "\n\n" + (WivrnServer.pairingEnabled ? i18n("Pairing is currently enabled with PIN %1.", WivrnServer.pin) : i18n("Pairing is currently disabled."))

            actions: [
                Kirigami.Action {
                    text: i18n("Enable pairing")
                    visible: !WivrnServer.pairingEnabled
                    onTriggered: WivrnServer.enable_pairing()
                },
                Kirigami.Action {
                    text: i18n("Disable pairing")
                    visible: WivrnServer.pairingEnabled
                    onTriggered: WivrnServer.disable_pairing()
                }
            ]
        }
        TroubleshootCard {
            title: i18n("I have no sound on my headset")
            details: i18n("When the headset is connected, select \"WiVRn\" as the default audio output device for applications to use it.")
        }
        TroubleshootCard {
            title: i18n("Some games fail to start or have input issues")
            details: i18n("For game specific issues, check on <a href=\"https://db.vronlinux.org/\">vronlinux DB</a> if there are known bugs or workarounds.")
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
            text: i18n("Report an issue")
            icon.source: Qt.resolvedUrl("Github.svg")
            onTriggered: Qt.openUrlExternally("https://github.com/WiVRn/WiVRn/issues")
        },
        Kirigami.Action {
            text: i18n("Ask on Discord")
            icon.source: Qt.resolvedUrl("Discord.svg")
            onTriggered: Qt.openUrlExternally("https://discord.gg/EHAYe3tTYa")
        },
        Kirigami.Action {
            text: i18n("Open server logs")
            icon.name: "viewlog-symbolic"
            onTriggered: WivrnServer.open_server_logs()
        }
    ]

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: {if (isCurrentPage) applicationWindow().pageStack.pop();}
    }
}
