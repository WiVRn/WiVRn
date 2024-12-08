import QtQuick
import io.github.wivrn.wivrn

ListModel {
    id: model

    ListElement {
        name: "None"
        imagePath: ""
        command: ""
        is_custom: false
    }

    ListElement {
        name: "Custom"
        imagePath: ""
        command: ""
        is_custom: true
    }

    Component.onCompleted: {
        var apps = SteamApps.apps;
        for (var i = 0; i < apps.length; i++) {
            model.append({
                "name": apps[i].name,
                "imagePath": apps[i].imagePath,
                "command": apps[i].command,
                "is_custom": false
            });
        }
    }
}
