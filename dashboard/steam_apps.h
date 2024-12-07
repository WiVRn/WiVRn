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
#include <qqmlintegration.h>

class steamApp
{
	Q_GADGET
	QML_VALUE_TYPE(steamApp)
	QML_ELEMENT
	Q_PROPERTY(QString name READ name WRITE setName)
	Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath)
	Q_PROPERTY(QString command READ command WRITE setCommand)

	QString m_name;
	QString m_imagePath;
	QString m_command;

public:
	steamApp() = default;
	steamApp(const steamApp &) = default;
	steamApp(steamApp &&) = default;
	steamApp & operator=(const steamApp &) = default;
	steamApp & operator=(steamApp &&) = default;
	steamApp(QString name, QString imagePath, QString command) :
	        m_name(name), m_imagePath(imagePath), m_command(command) {}

	QString name() const
	{
		return m_name;
	}

	QString imagePath() const
	{
		return m_imagePath;
	}

	QString command() const
	{
		return m_command;
	}

	void setName(QString value)
	{
		m_name = value;
	}

	void setImagePath(QString value)
	{
		m_imagePath = value;
	}

	void setCommand(QString value)
	{
		m_command = value;
	}

	void setName(const std::string & value)
	{
		m_name = QString::fromStdString(value);
	}

	void setImagePath(const std::string & value)
	{
		m_imagePath = QString::fromStdString(value);
	}

	void setCommand(const std::string & value)
	{
		m_command = QString::fromStdString(value);
	}
};

class SteamApps : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON

	Q_PROPERTY(QList<steamApp> apps READ apps)
	QList<steamApp> m_apps;

public:
	SteamApps(QObject * parent = nullptr);

	const QList<steamApp> & apps() const
	{
		return m_apps;
	}
};
