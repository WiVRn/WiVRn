/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QLoggingCategory>
#include <QMainWindow>
#include <QMenu>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVariantMap>

#include "adb.h"
#include "wivrn_qdbus_types.h"

Q_DECLARE_LOGGING_CATEGORY(wivrn_log_category)

namespace Ui
{
class MainWindow;
}
class IoGithubWivrnServerInterface;
class OrgFreedesktopDBusPropertiesInterface;

class settings;
class wivrn_server;
class wizard;

class main_window : public QMainWindow
{
	Q_OBJECT

	Ui::MainWindow * ui = nullptr;
	wivrn_server * server_interface = nullptr;

	settings * settings_window = nullptr;
	wizard * wizard_window = nullptr;

	QIcon icon{":/assets/wivrn.png"};
	QSystemTrayIcon systray{icon};

	QMenu systray_menu;
	QAction action_show;
	QAction action_hide;
	QAction action_exit;

	QProcess * server_process = nullptr;
	QTimer * server_process_timeout = nullptr;
	bool server_process_restart = false;

	QMenu * usb_device_menu;
	std::map<std::string, std::unique_ptr<QAction>> usb_actions;

	adb adb_service;

	QProcess * setcap_process = nullptr;

public:
	main_window();
	~main_window();

	void changeEvent(QEvent * e) override;
	bool event(QEvent * e) override;
	void closeEvent(QCloseEvent * event) override;
	void setVisible(bool visible) override;

private:
	void on_server_finished(int exit_code, QProcess::ExitStatus status);
	void on_server_error_occurred(QProcess::ProcessError error);
	void on_server_start_timeout();

	void on_button_details_toggled(bool);
	void on_banner_capsysnice(const QString & link);

	void on_server_running_changed(bool running);
	void on_headset_connected_changed(bool connected);
	void on_recommended_eye_size_changed(QSize size);
	void on_available_refresh_rates_changed(const std::vector<float> & rates);
	void on_preferred_refresh_rate_changed(float rate);
	void on_eye_gaze_changed(bool supported);
	void on_face_tracking_changed(bool supported);
	void on_field_of_view_changed(const std::vector<field_of_view> & fov);
	void on_hand_tracking_changed(bool supported);
	void on_mic_changed();
	void on_speaker_changed();
	void on_supported_codecs_changed(QStringList value);
	void on_steam_command_changed(QString value);

	void on_android_device_list_changed(const std::vector<adb::device> &);

	void on_action_settings();
	void on_action_wizard();
	void on_action_usb(const std::string & serial);
	void start_server();
	void stop_server();
	void disconnect_client();

	void retranslate();
};
