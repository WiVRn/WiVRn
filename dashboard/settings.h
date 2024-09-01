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

#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>

namespace Ui
{
class Settings;
}
class wivrn_server;

class settings : public QDialog
{
	Q_OBJECT

	Ui::Settings * ui;
	wivrn_server * server_interface;

	QJsonDocument json_doc;

	void selected_rectangle_changed(int);

public:
	settings(wivrn_server * server_interface = nullptr);
	~settings();

	void on_settings_changed();
	void on_encoder_changed();
	void save_settings();
	void restore_defaults();
};
