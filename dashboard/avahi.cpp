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

#include "avahi.h"

#include "dashboard_utils.h"
#include "escape_sandbox.h"

#include <QDBusInterface>

bool avahi::installed()
{
	return not find_executable("avahi-daemon").isEmpty();
}

bool avahi::running()
{
	QDBusInterface dbus = QDBusInterface("org.freedesktop.Avahi", "/", "org.freedesktop.Avahi.Server", QDBusConnection::systemBus());
	return dbus.isValid();
}

bool avahi::canStart()
{
	return not(find_executable("systemctl").isEmpty() or find_executable("pkexec").isEmpty());
}

void avahi::start()
{
	auto pkexec = escape_sandbox("pkexec",
	                             "systemctl",
	                             "enable",
	                             "--now",
	                             "avahi-daemon.service");
	pkexec->setProcessChannelMode(QProcess::MergedChannels);
	pkexec->start();
	auto ptr = pkexec.get();

	QObject::connect(ptr, &QProcess::finished, this, [this, pkexec = std::move(pkexec)](int exit_code, QProcess::ExitStatus exit_status) mutable {
		if (exit_status != QProcess::NormalExit or exit_code)
			qWarning() << "avahi daemon start failed with status " << exit_status << "and code" << exit_code;
		runningChanged(running());
		pkexec.release()->deleteLater();
	});
}
