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

#include "wivrn_server.h"
#include <QDialog>
#include <QStandardItem>

namespace Ui
{
class ManageHeadsets;
}
class IoGithubWivrnServerInterface;
class OrgFreedesktopDBusPropertiesInterface;

class settings;
class wivrn_server;
class wizard;

class manage_headsets : public QDialog
{
	Q_OBJECT

	Ui::ManageHeadsets * ui;
	wivrn_server * server_interface;

public:
	manage_headsets(wivrn_server * server_interface);

private:
	void update_headet_list(const std::vector<wivrn_server::HeadsetKey> & keys);
	void on_selection_changed();
	void on_remove_selected();
	void on_rename_selected(QStandardItem * item);
};
