pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import io.github.wivrn.wivrn

Kirigami.Card {
    id: device_card

    property alias name: label_name.text
    property string serial_number
    property bool is_wivrn_installed
    property bool installing: false

    Connections {
        target: ApkInstaller
        function onBusyChanged(value) {
            if (!value)
                device_card.installing = false;
        }
    }

    implicitHeight: contentItem.implicitHeight + 2 * Kirigami.Units.largeSpacing

    contentItem: GridLayout {
        id: delegate_layout
        rowSpacing: Kirigami.Units.largeSpacing
        columnSpacing: Kirigami.Units.largeSpacing
        columns: 2

        Controls.Label {
            id: label_name
            Layout.fillWidth: true
            Layout.column: 0
            Layout.row: 0
            font.pixelSize: 20
        }

        Controls.Label {
            Layout.fillWidth: true
            Layout.column: 0
            Layout.row: 1
            text: device_card.is_wivrn_installed ? i18n("WiVRn is already installed on this device") : ""
        }

        Controls.Button {
            id: install_button
            Layout.column: 1
            Layout.row: 0
            Layout.rowSpan: 2
            text: device_card.is_wivrn_installed ? i18n("Reinstall WiVRn") : i18n("Install WiVRn")
            icon.name: "install-symbolic"
            onClicked: device_card.install()
            enabled: !ApkInstaller.busy
        }

        Controls.Label {
            id: progress_label
            Layout.column: 0
            Layout.row: 2
            Layout.columnSpan: 2
            visible: device_card.installing
            text: ApkInstaller.installStatus
        }

        RowLayout {
            Layout.column: 0
            Layout.row: 3
            Layout.columnSpan: 2
            visible: device_card.installing

            Controls.ProgressBar {
                id: progress
                Layout.fillWidth: true
                from: 0
                to: ApkInstaller.bytesTotal > 0 ? ApkInstaller.bytesTotal : 1
                indeterminate: ApkInstaller.bytesTotal < 0
                value: ApkInstaller.bytesReceived
            }
            Controls.Button {
                icon.name: "dialog-cancel"
                onClicked: device_card.cancel_install()
                enabled: ApkInstaller.cancellable
            }
        }
    }

    function install() {
        console.log("Installing WiVRn on " + device_card.serial_number);
        installing = true;
        ApkInstaller.installApk(device_card.serial_number).then(() => {
            Adb.checkIfWivrnIsInstalled(device_card.serial_number);
            applicationWindow().showPassiveNotification(ApkInstaller.installStatus);
        });
    }

    function cancel_install() {
        console.log("Cancelling installation");
        ApkInstaller.cancelInstallApk();
    }
}
