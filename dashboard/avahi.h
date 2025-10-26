/*
 * WiVRn VR streaming
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

#include <QObject>
#include <QtQml/qqmlregistration.h>

class avahi : public QObject
{
public:
	Q_OBJECT
	QML_NAMED_ELEMENT(Avahi)
	QML_SINGLETON

	Q_PROPERTY(bool installed READ installed NOTIFY installChanged)
	Q_PROPERTY(bool running READ running NOTIFY runningChanged)
	Q_PROPERTY(bool canStart READ canStart CONSTANT)
public:
	bool installed();
	bool running();

	bool canStart();
	Q_INVOKABLE void start();

Q_SIGNALS:
	void installChanged(bool);
	void runningChanged(bool);
};
