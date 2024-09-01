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

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSettings>
#include <QStandardItemModel>
#include <QWizard>

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
	QNetworkReply * reply = nullptr;
	QSaveFile apk_file;
	bool apk_downloaded = false;

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

	void on_headset_model_changed();
	void on_headset_connected_changed(bool);
	void on_custom_button_clicked(int which);
};
