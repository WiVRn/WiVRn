/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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
#include <qqmlintegration.h>

class vrApp
{
	Q_GADGET
	QML_VALUE_TYPE(vrApp)
	Q_PROPERTY(QString name READ name WRITE setName)
	Q_PROPERTY(QString command READ command WRITE setCommand)

	QString m_name;
	QString m_command;

public:
	vrApp() = default;
	vrApp(const vrApp &) = default;
	vrApp(vrApp &&) = default;
	vrApp & operator=(const vrApp &) = default;
	vrApp & operator=(vrApp &&) = default;
	vrApp(QString name, QString command) :
	        m_name(name), m_command(command) {}

	QString name() const
	{
		return m_name;
	}

	QString command() const
	{
		return m_command;
	}

	void setName(QString value)
	{
		m_name = value;
	}

	void setCommand(QString value)
	{
		m_command = value;
	}

	void setName(const std::string & value)
	{
		m_name = QString::fromStdString(value);
	}

	void setCommand(const std::string & value)
	{
		m_command = QString::fromStdString(value);
	}
};

class Apps : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON

	Q_PROPERTY(QList<vrApp> apps READ apps)
	QList<vrApp> m_apps;

public:
	Apps(QObject * parent = nullptr);

	const QList<vrApp> & apps() const
	{
		return m_apps;
	}
};
