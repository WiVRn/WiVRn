import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import QtQuick.Controls as Controls
import io.github.wivrn.wivrn

ColumnLayout {
    id: select_game

    Component.onCompleted: {
        apps.append({
            "name": i18nc("don't start an application automatically", "None"),
            "command": "",
            "is_custom": false
        });

        apps.append({
            "name": i18nc("choose a custom application to start", "Custom"),
            "command": "",
            "is_custom": true
        });

        var vr_apps = Apps.apps;
        for (var i = 0; i < vr_apps.length; i++) {
            apps.append({
                "name": vr_apps[i].name,
                "command": vr_apps[i].command,
                "is_custom": false
            });
        }

        select_game.load();
    }

    ListModel {
        id: apps
    }

    Connections {
        target: WivrnServer
        function onJsonConfigurationChanged() {
            select_game.load();
        }
    }

    Dialogs.FileDialog {
        id: app_browse
        onAccepted: {
            app_text.text = WivrnServer.host_path(new URL(selectedFile).pathname);
            select_game.save();
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Controls.ComboBox {
            id: app_combobox
            Layout.columnSpan: 2
            textRole: "name"
            model: apps
            onActivated: select_game.save()
        }
        Controls.TextField {
            id: app_text
            placeholderText: app_combobox.currentText
            visible: !!app_combobox.model.get(app_combobox.currentIndex)?.is_custom && WivrnServer.serverStatus == WivrnServer.Started
            Layout.fillWidth: true
            onTextEdited: select_game.save()
        }
        Controls.Button {
            text: i18nc("browse a file to choose the application to start", "Browse")
            visible: app_text.visible
            onClicked: app_browse.open()
        }
    }

    function load() {
        if (WivrnServer.jsonConfiguration == "")
            return;

        Settings.load(WivrnServer);

        var found = false;
        var custom_idx = -1;
        for (var i = 0; i < apps.count; i++) {
            var app = apps.get(i);
            if (app.is_custom)
                custom_idx = i;
            else if (app.command == Settings.application) {
                app_combobox.currentIndex = i;
                found = true;
                break;
            }
        }

        if (!found) {
            app_text.text = Settings.application;
            app_combobox.currentIndex = custom_idx;
        }
    }

    function save() {
        if (apps.get(app_combobox.currentIndex).is_custom)
            Settings.application = app_text.text;
        else
            Settings.application = apps.get(app_combobox.currentIndex).command;
    }
}
