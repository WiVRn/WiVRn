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

#include <QAbstractListModel>
#include <QCoroCore>
#include <QCoroQml>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class adb : public QAbstractListModel
{
	Q_OBJECT
	QML_NAMED_ELEMENT(Adb)
	QML_SINGLETON

	struct device
	{
		QString serial;
		QString app;
		std::map<QString, QString> properties;
		bool is_wivrn_installed = false;
	};

	Q_PROPERTY(bool adbInstalled READ adbInstalled NOTIFY adbInstalledChanged)

	bool m_adb_installed = false;
	QString m_path;
	std::vector<device> m_android_devices;

	QCoro::Task<> on_poll_devices_timeout();

public:
	enum Roles
	{
		RoleSerial = Qt::UserRole + 1,
		RoleIsWivrnInstalled,
		RoleManufacturer,
		RoleModel,
		RoleProduct,
		RoleDevice,
	};
	adb();

	bool adbInstalled() const
	{
		return m_adb_installed;
	}

	Q_INVOKABLE QCoro::QmlTask startUsbConnection(QString serial, QString pin);
	Q_INVOKABLE QCoro::QmlTask checkIfWivrnIsInstalled(QString serial);

	int rowCount(const QModelIndex & parent) const override
	{
		return m_android_devices.size();
	}

	QVariant data(const QModelIndex & index, int role) const override;

	Q_INVOKABLE void setPath(QString path);

protected:
	QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
	void adbInstalledChanged(bool);

private:
	QCoro::Task<> checkIfAdbIsInstalled();
	QCoro::Task<> add_device(QString serial);
	QCoro::Task<> doStartUsbConnection(QString serial, QString pin);
};
