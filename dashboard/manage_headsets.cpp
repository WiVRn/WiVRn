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

#include "manage_headsets.h"
#include "ui_manage_headsets.h"
#include "wivrn_server.h"
#include <QStandardItemModel>
#include <algorithm>

manage_headsets::manage_headsets(wivrn_server * server_interface) :
        server_interface(server_interface)
{
	ui = new Ui::ManageHeadsets;
	ui->setupUi(this);
	ui->headset_list->setModel(new QStandardItemModel);

	connect(server_interface, &wivrn_server::knownKeysChanged, this, &manage_headsets::update_headet_list);
	connect(ui->headset_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, &manage_headsets::on_selection_changed);
	connect(ui->button_remove, &QPushButton::clicked, this, &manage_headsets::on_remove_selected);

	QStandardItemModel * headsets = dynamic_cast<QStandardItemModel *>(ui->headset_list->model());
	connect(headsets, &QStandardItemModel::itemChanged, this, &manage_headsets::on_rename_selected);

	update_headet_list(server_interface->knownKeys());
}

void manage_headsets::update_headet_list(const std::vector<wivrn_server::HeadsetKey> & new_headsets)
{
	QStandardItemModel * current_headsets = dynamic_cast<QStandardItemModel *>(ui->headset_list->model());
	assert(current_headsets);

	for (int i = 0; i < current_headsets->rowCount();)
	{
		QString key = current_headsets->item(i)->data().toString();
		if (not std::ranges::any_of(new_headsets, [&](const wivrn_server::HeadsetKey & x) { return x.public_key == key; }))
			current_headsets->removeRow(i);
		else
			i++;
	}

	for (const wivrn_server::HeadsetKey & key: new_headsets)
	{
		bool found = false;
		for (int i = 0; i < current_headsets->rowCount(); i++)
		{
			if (current_headsets->item(i)->data().toString() == key.public_key)
			{
				found = true;
				break;
			}
		}

		if (not found)
		{
			QStandardItem * item = new QStandardItem;
			item->setText(key.name);
			item->setData(key.public_key);
			current_headsets->appendRow(item);
		}
	}
}

void manage_headsets::on_selection_changed()
{
	ui->button_remove->setDisabled(ui->headset_list->selectionModel()->selection().empty());
}

void manage_headsets::on_remove_selected()
{
	QStandardItemModel * current_headsets = dynamic_cast<QStandardItemModel *>(ui->headset_list->model());
	assert(current_headsets);

	std::vector<QString> keys_to_remove;
	for (QItemSelectionRange item: ui->headset_list->selectionModel()->selection())
	{
		for (QModelIndex index: item.indexes())
		{
			keys_to_remove.push_back(current_headsets->item(index.row())->data().toString());
		}
	}

	for (QString key: keys_to_remove)
		server_interface->revoke_key(key);
}

void manage_headsets::on_rename_selected(QStandardItem * item)
{
	server_interface->rename_key(item->data().toString(), item->text());
}
