pragma Singleton

import QtCore

Settings {
	property bool first_run: true
	property bool show_system_checks: true
	property string last_run_version: ""

	property bool adb_custom: false
	property string adb_location: ""
}

