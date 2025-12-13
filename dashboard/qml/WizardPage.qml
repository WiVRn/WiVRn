pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import QtQuick.Controls as Controls
import QtQuick.Templates as Templates
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.ScrollablePage {
    id: wizard
    title: i18n("Getting started")

    // flickable.interactive: false

    Component {
        id: page_component
        WizardStep {
            Rectangle {
                Layout.fillHeight: true
                Layout.fillWidth: true
                color: Qt.rgba(Math.random(), Math.random(), Math.random(), 1)
            }
            Component.onDestruction: console.log("page destroyed")
            actions: [
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: pageStack.pop()
                },
                Kirigami.Action {
                    text: "test"
                    onTriggered: pageStack.push(page_component.createObject(pageStack))
                },
                Kirigami.Action {
                    text: i18n("Cancel")
                    onTriggered: wizard.cancel()
                }
            ]
        }
    }

    property bool is_tagged: ApkInstaller.isTagged
    property bool is_latest: ApkInstaller.currentVersion == ApkInstaller.latestVersion
    property bool checking_newer: ApkInstaller.latestVersion == "" && ApkInstaller.isTagged
    property bool newer_version: ApkInstaller.currentVersion != ApkInstaller.latestVersion && ApkInstaller.latestVersion != "" && ApkInstaller.isTagged

    ConnectUsbDialog {
        id: select_usb_device
    }

    Component.onCompleted: {
        ApkInstaller.refreshLatestVersion();
    }

    Component {
        id: page_welcome_component
        WizardStep {
            id: page_welcome
            Controls.Label {
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Welcome to WiVRn")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("This wizard will help you set up WiVRn on your headset and connect it to your computer.")
            }

            Item {
                Layout.fillHeight: true
            }

            actions: [
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    enabled: false
                },
                Kirigami.Action {
                    text: i18n("Next")
                    icon.name: "go-next"
                    onTriggered: pageStack.push(page_install_component.createObject(pageStack))
                },
                Kirigami.Action {
                    text: i18n("Cancel")
                    onTriggered: wizard.cancel()
                }
            ]
        }
    }

    ListModel {
        id: devmode_url
        ListElement {
            manufacturer: "HTC"
            url: "https://developer.vive.com/resources/hardware-guides/vive-focus-specs-user-guide/how-do-i-put-focus-developer-mode/"
        }
        ListElement {
            manufacturer: "Meta"
            url: "https://developers.meta.com/horizon/documentation/native/android/mobile-device-setup/#enable-developer-mode"
        }
        ListElement {
            manufacturer: "Pico"
            url: "https://developer.picoxr.com/document/unreal/test-and-build/#Enable%20developer%20mode"
        }
    }
    Kirigami.Dialog {
        id: devmode_dialog
        title: i18n("How to enable developer mode")
        showCloseButton: false
        padding: 2 * Kirigami.Units.largeSpacing
        ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("Follow the manufacturer's instructions below:")
            }
            Repeater {
                model: devmode_url
                delegate: BetterLabel {
                    required property string manufacturer
                    required property string url
                    Layout.fillWidth: true
                    text: "<a href=\"%1\">%2</a>".arg(url).arg(manufacturer)
                }
            }
        }
    }
    Component {
        id: page_install_component
        WizardStep {
            Controls.Label {
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Install the WiVRn app on your headset")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("In order to use WiVRn, you will have to install the client app on your headset.\nYou can either install it from the Meta store or directly over USB.")
            }

            Controls.Label {
                Layout.topMargin: 4 * Kirigami.Units.largeSpacing
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Install from the Meta store")
            }
            BetterLabel {
                Layout.fillWidth: true
                text: i18n("For the Quest 2, 3, 3S, and Pro, you can get the app from the <a href=\"https://www.meta.com/experiences/7959676140827574/\">Meta store</a>.")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("Checking latest release...")
                visible: wizard.checking_newer
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("A newer release is available: %1 → %2, the client app on the Meta store may not be compatible.", ApkInstaller.currentVersion, ApkInstaller.latestVersion)
                visible: wizard.newer_version
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("This is a nightly build, the client app on the Meta store may not be compatible.")
                visible: !wizard.is_tagged
            }

            Controls.Label {
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.5
                Layout.topMargin: 4 * Kirigami.Units.largeSpacing
                Layout.bottomMargin: 4 * Kirigami.Units.largeSpacing
                Layout.preferredWidth: 20 * Kirigami.Units.largeSpacing
                horizontalAlignment: Qt.AlignHCenter
                text: i18nc("displayed between 'meta store' and 'install over USB' in the wizard", "Or")
            }

            Controls.Label {
                Layout.bottomMargin: 2 * Kirigami.Units.largeSpacing
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Install over USB")
            }

            BetterLabel {
                Layout.fillWidth: true
                text: i18n("Checking latest release...")
                visible: ApkInstaller.busy && !ApkInstaller.apkAvailable
            }

            BetterLabel {
                Layout.fillWidth: true
                text: i18n("No APK is available for this version")
                visible: !ApkInstaller.busy && !ApkInstaller.apkAvailable
            }

            BetterLabel {
                Layout.fillWidth: true
                text: i18n("Follow the instructions in the <a href=\"https://github.com/WiVRn/WiVRn/blob/master/docs/building.md#client-headset\">documentation</a> to build your own client.")
                visible: !ApkInstaller.busy && !ApkInstaller.apkAvailable
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("ADB is not installed")
                visible: device_list.count == 0 && !Adb.adbInstalled && ApkInstaller.apkAvailable
            }

            // TODO link to adb/udev rules depending on the distribution
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("<p>No headset detected. Make sure that:\n<ul><li>The ADB udev rules are installed</li><li>The headset is in <a href=\"#\">developer mode</a></li></ul></p>")
                onLinkActivated: devmode_dialog.open()
                MouseArea {
                    anchors.fill: parent
                    cursorShape: (parent as Controls.Label).hoveredLink == "" ? Qt.ArrowCursor : Qt.PointingHandCursor
                    acceptedButtons: Qt.NoButton
                }
                visible: device_list.count == 0 && Adb.adbInstalled && ApkInstaller.apkAvailable
            }

            Repeater {
                id: device_list
                model: ApkInstaller.apkAvailable ? Adb : []
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
            }

            Item {
                Layout.fillHeight: true
            }

            // image: Qt.resolvedUrl("wivrn.svg")

            actions: [
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: pageStack.goBack()
                },
                Kirigami.Action {
                    text: i18n("Next")
                    icon.name: "go-next"
                    onTriggered: pageStack.push(page_startwivrn_component.createObject(pageStack))
                },
                Kirigami.Action {
                    text: i18n("Cancel")
                    onTriggered: wizard.cancel()
                }
            ]
        }
    }

    Component {
        id: page_startwivrn_component

        WizardStep {
            id: page_startwivrn
            Component.onCompleted: {
                WivrnServer.headsetConnectedChanged.connect(onHeadsetConnectedChanged);
                WivrnServer.pairingEnabledChanged.connect(onCurrentItemChanged_or_PairingEnabledChanged);
                pageStack.currentItemChanged.connect(onCurrentItemChanged_or_PairingEnabledChanged);
            }

            function onCurrentItemChanged_or_PairingEnabledChanged() {
                if (pageStack?.currentItem === page_startwivrn && !WivrnServer.pairingEnabled)
                    WivrnServer.enable_pairing();
            }

            function onHeadsetConnectedChanged(value) {
                if (value && pageStack?.currentItem === page_startwivrn)
                    pageStack.push(page_connected_component.createObject(pageStack));
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Important information")
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("WiVRn entirely replaces SteamVR, you should not start SteamVR while WiVRn is running.")
            }

            SteamLaunchOptions {
                visible: WivrnServer.steamCommand != ""
                Layout.topMargin: 2 * Kirigami.Units.largeSpacing
            }

            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Start the WiVRn app on your headset")
                Layout.topMargin: 2 * Kirigami.Units.largeSpacing
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("To connect over WiFi, start the WiVRn app on your headset, connect to \"%1\" and enter PIN \"%2\".", WivrnServer.hostname, WivrnServer.pin)
            }

            RowLayout {
                Controls.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("To connect over USB, click on \"Connect by USB\".")
                }
                Controls.Button {
                    text: i18n("Connect by USB")
                    onClicked: select_usb_device.connect()
                    enabled: select_usb_device.connected_headset_count > 0

                    Controls.ToolTip.visible: WivrnServer.serverStatus == WivrnServer.Started&& hovered
                    Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                    Controls.ToolTip.text: {
                        if (!Adb.adbInstalled)
                            return i18n("ADB is not installed");
                        if (select_usb_device.connected_headset_count == 0)
                            return i18n("No headset is connected or your headset is not in developer mode");
                        return "";
                    }
                }
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                Layout.topMargin: 4 * Kirigami.Units.largeSpacing
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Troubleshooting")
            }
            BetterLabel {
                Layout.fillWidth: true
                text: i18n("If you installed WiVRn over USB on a Meta headset, the app is in the \"unknown sources\" section.")
                Layout.topMargin: Kirigami.Units.largeSpacing
            }
            BetterLabel {
                Layout.fillWidth: true
                text: i18n("If the server is not visible or the connection fails, check that ports 5353 (UDP) and 9757 (TCP and UDP) are open in your firewall.")
                Layout.topMargin: Kirigami.Units.largeSpacing
            }
            Item {
                Layout.fillHeight: true
            }
            actions: [
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: pageStack.goBack()
                },
                Kirigami.Action {
                    text: i18n("Skip")
                    // icon.name: "go-next"
                    onTriggered: pageStack.push(page_connection_skipped_component.createObject(pageStack))
                },
                Kirigami.Action {
                    text: i18n("Cancel")
                    onTriggered: wizard.cancel()
                }
            ]
        }
    }

    Component {
        id: page_connected_component
        WizardStep {
            id: page_connected
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Connection established")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("Your headset is now connected.")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("To connect another headset, make sure pairing is enabled in the dashboard before connecting.")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("When the headset is connected, select \"WiVRn\" as the default audio output device for applications to use it.")
            }
            Item {
                Layout.fillHeight: true
            }
            actions: [
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: pageStack.goBack()
                },
                Kirigami.Action {
                    text: i18n("Next")
                    icon.name: "go-next"
                    enabled: false
                },
                Kirigami.Action {
                    text: i18n("Finish")
                    onTriggered: wizard.cancel()
                }
            ]
        }
    }

    Component {
        id: page_connection_skipped_component
        WizardStep {
            id: page_finished
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                text: i18n("Finished")
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("To connect another headset, make sure pairing is enabled in the dashboard before connecting.")
            }
            Item {
                Layout.fillHeight: true
            }
            actions: [
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: pageStack.goBack()
                },
                Kirigami.Action {
                    text: i18n("Next")
                    icon.name: "go-next"
                    enabled: false
                },
                Kirigami.Action {
                    text: i18n("Finish")
                    onTriggered: wizard.cancel()
                }
            ]
        }
    }

    Kirigami.PageRow {
        id: pageStack
        globalToolBar.style: Kirigami.ApplicationHeaderStyle.Auto
        anchors.fill: parent
        focus: true

        globalToolBar {
            showNavigationButtons: Kirigami.ApplicationHeaderStyle.NoNavigationButtons
            // style: Kirigami.ApplicationHeaderStyle.None
        }

        defaultColumnWidth: width // Force non-wide mode
        interactive: false // Don't let the back/forward mouse button handle the pagestack
        popHiddenPages: true // pop hidden pages and use goBack() instead of pop() to animate the pages correctly when going back

        initialPage: page_welcome_component.createObject(pageStack)
    }

    footer: Item {
        Layout.fillWidth: true
        implicitHeight: footer_row.implicitHeight + 2 * Kirigami.Units.largeSpacing

        ColumnLayout {
            id: footer_row
            anchors.fill: parent
            Kirigami.Separator {
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing

                spacing: Kirigami.Units.largeSpacing

                Item {
                    // spacer item
                    Layout.fillWidth: true
                }

                Repeater {
                    model: (pageStack.trailingVisibleItem as WizardStep)?.actions
                    Controls.Button {
                        required property Kirigami.Action modelData
                        action: modelData
                        // enabled: pageStack.trailingVisibleItem == pageStack.leadingVisibleItem && modelData.enabled
                        visible: modelData.visible
                    }
                }
            }
        }
    }

    function cancel() {
        applicationWindow().pageStack.pop();
    }
}
