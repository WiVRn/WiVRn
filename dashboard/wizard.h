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

#include "adb.h"
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardItemModel>
#include <QTimer>
#include <QWizard>
#include <string>

namespace Ui
{
class Wizard;
}

class wivrn_server;

class wizard : public QWizard
{
	Q_OBJECT

	Ui::Wizard * ui = nullptr;
	QStandardItemModel headsets;

	wivrn_server * server;

	QSettings settings;
	QNetworkAccessManager manager;

	// Used to download the APK
	QNetworkReply * apk_reply = nullptr;

	// Used to query the latest release on github
	QNetworkReply * latest_release_reply = nullptr;

	// Used to get the info about the current version on github, either from the WiVRn or the WiVRn-APK repo
	QNetworkReply * apk_release_reply = nullptr;

	QSaveFile apk_file;
	std::string apk_url;
	bool apk_downloaded = false;

	std::string latest_release;

	adb adb_service;

	std::vector<adb::device> android_devices;
	std::unique_ptr<QProcess> process_adb_install;

public:
	wizard();
	~wizard();

	void changeEvent(QEvent * e) override;
	void retranslate();

	int nextId() const override;

	void on_page_changed(int id);

	void start_download();
	void cancel_download();
	void on_download_progress(qint64 bytesReceived, qint64 bytesTotal);
	void on_download_error(QNetworkReply::NetworkError code);
	void on_download_finished();

	void on_android_device_list_changed(const std::vector<adb::device> &);
	void on_selected_android_device_changed();

	void start_install();
	void on_install_finished(int exit_code, QProcess::ExitStatus exit_status);
	void on_install_stdout();
	void on_install_stderr();

	void on_latest_release_finished();
	void on_apk_release_finished();

	void update_welcome_page();
	void on_headset_connected_changed(bool);
	void on_custom_button_clicked(int which);
};
