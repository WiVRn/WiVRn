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

#include "adb.h"

#include "escape_sandbox.h"
#include <QCoroProcess>
#include <QCoroQmlTask>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <vector>

using namespace std::chrono_literals;

adb::adb()
{
	QTimer::singleShot(0, this, &adb::checkIfAdbIsInstalled);
}

QCoro::Task<> adb::checkIfAdbIsInstalled()
{
	auto which_adb = escape_sandbox("which", "adb");
	which_adb->setProcessChannelMode(QProcess::ForwardedErrorChannel);
	auto co_which_adb = qCoro(*which_adb);

	co_await co_which_adb.start();
	co_await co_which_adb.waitForFinished();

	if (which_adb->exitStatus() == QProcess::NormalExit and which_adb->exitCode() == 0)
	{
		qDebug() << "adb found at" << QString{which_adb->readAllStandardOutput()}.trimmed();

		if (!m_adb_installed)
		{
			adbInstalledChanged(m_adb_installed = true);
			co_await on_poll_devices_timeout();
		}
	}
	else
	{
		qDebug() << "adb not found";

		if (m_adb_installed)
		{
			adbInstalledChanged(m_adb_installed = false);
		}
	}
}

// BEGIN Poll devices
QCoro::Task<> adb::on_poll_devices_timeout()
{
	if (m_android_devices.empty())
	{
		auto wait_for_usb_device = escape_sandbox("adb", "wait-for-usb-device");
		auto co_wait_for_usb_device = qCoro(*wait_for_usb_device);

		co_await co_wait_for_usb_device.start();
		co_await co_wait_for_usb_device.waitForFinished(-1);
	}

	auto adb_devices = escape_sandbox("adb", "devices");
	adb_devices->setProcessChannelMode(QProcess::ForwardedErrorChannel);
	auto co_adb_devices = qCoro(*adb_devices);

	co_await co_adb_devices.start();
	co_await co_adb_devices.waitForFinished();

	if (adb_devices->exitStatus() == QProcess::NormalExit and adb_devices->exitCode() == 0)
	{
		auto out = adb_devices->readAllStandardOutput();
		auto lines = QString{out}.split('\n');

		// Remove "List of devices attached"
		if (not lines.empty())
			lines.pop_front();

		std::vector<QString> devs;
		for (auto & line: lines)
		{
			auto words = line.split('\t', Qt::SkipEmptyParts);
			if (words.size() < 2)
				continue;

			const QString & serial = words[0];
			const QString & state = words[1];

			if (state != "device")
				continue;

			devs.push_back(serial);
		}

		for (size_t i = 0; i < m_android_devices.size();)
		{
			auto it = std::ranges::find(devs, m_android_devices[i].serial);
			if (it == devs.end())
			{
				beginRemoveRows({}, i, i);
				m_android_devices.erase(m_android_devices.begin() + i);
				endRemoveRows();
			}
			else
			{
				++i;
			}
		}

		for (const QString & serial: devs)
		{
			if (not std::ranges::contains(m_android_devices, serial, &device::serial))
				co_await add_device(serial);
		}

		QTimer::singleShot(500, this, &adb::on_poll_devices_timeout);
	}
	else
	{
		qDebug() << "adb failed";
	}
}
// END

// BEGIN Add device
QCoro::QmlTask adb::checkIfWivrnIsInstalled(QString serial)
{
	return add_device(std::move(serial));
}

QCoro::Task<> adb::add_device(QString serial)
{
	device new_dev{.serial = serial};

	auto list_packages = escape_sandbox("adb", "-s", serial, "shell", "pm", "list", "packages");
	list_packages->setProcessChannelMode(QProcess::ForwardedErrorChannel);

	auto co_list_packages = qCoro(*list_packages);

	co_await co_list_packages.start();
	co_await co_list_packages.waitForFinished();

	if (list_packages->exitStatus() != QProcess::NormalExit or list_packages->exitCode() != 0)
	{
		co_return;
	}

	auto out_list_packages = list_packages->readAllStandardOutput();

	for (auto & line: QString{out_list_packages}.split('\n'))
	{
		if (line == "package:org.meumeu.wivrn" or line.startsWith("package:org.meumeu.wivrn."))
		{
			new_dev.is_wivrn_installed = true;
			new_dev.app = line.mid(8);
			break;
		}
	}

	auto getprop = escape_sandbox("adb", "-s", new_dev.serial, "shell", "getprop");
	getprop->setProcessChannelMode(QProcess::ForwardedErrorChannel);

	auto co_get_prop = qCoro(*getprop);

	co_await co_get_prop.start();
	co_await co_get_prop.waitForFinished();

	if (getprop->exitStatus() != QProcess::NormalExit or getprop->exitCode() != 0)
	{
		co_return;
	}

	auto out_getprop = getprop->readAllStandardOutput();
	static const QRegularExpression re{R"(\[(?<name>.*)\]: \[(?<value>.*)\])"};

	for (auto & line: QString{out_getprop}.split('\n'))
	{
		if (auto match = re.match(line); match.hasMatch())
		{
			new_dev.properties.insert({match.captured("name"), match.captured("value")});
		}
	}

	if (not new_dev.properties.contains("ro.product.manufacturer") or not new_dev.properties.contains("ro.product.model"))
		co_return;

	auto it = std::ranges::find(m_android_devices, new_dev.serial, &device::serial);
	if (it == m_android_devices.end())
	{
		auto idx = m_android_devices.size();
		beginInsertRows({}, idx, idx);
		m_android_devices.push_back(new_dev);
		endInsertRows();
	}
	else
	{
		*it = std::move(new_dev);
		auto idx = index(m_android_devices.begin() - it);
		dataChanged(idx, idx);
	}
}
// END

// BEGIN Model implementation
QHash<int, QByteArray> adb::roleNames() const
{
	return QHash<int, QByteArray>{
	        {RoleSerial, "serial"},
	        {RoleIsWivrnInstalled, "isWivrnInstalled"},
	        {RoleManufacturer, "manufacturer"},
	        {RoleModel, "model"},
	        {RoleProduct, "product"},
	        {RoleDevice, "device"},
	};
}

QVariant adb::data(const QModelIndex & index, int role) const
{
	switch (role)
	{
		case RoleSerial:
			return m_android_devices.at(index.row()).serial;

		case RoleProduct: {
			auto it = m_android_devices.at(index.row()).properties.find("ro.product.name");
			if (it == m_android_devices.at(index.row()).properties.end())
				return "";
			else
				return it->second;
		}

		case RoleManufacturer: {
			auto it = m_android_devices.at(index.row()).properties.find("ro.product.manufacturer");
			if (it == m_android_devices.at(index.row()).properties.end())
				return "";
			else
				return it->second;
		}

		case RoleModel: {
			auto it = m_android_devices.at(index.row()).properties.find("ro.product.model");
			if (it == m_android_devices.at(index.row()).properties.end())
				return "";
			else
				return it->second;
		}

		case RoleDevice: {
			auto it = m_android_devices.at(index.row()).properties.find("ro.product.device");
			if (it == m_android_devices.at(index.row()).properties.end())
				return "";
			else
				return it->second;
		}

		case RoleIsWivrnInstalled:
			return m_android_devices.at(index.row()).is_wivrn_installed;

		default:
			return {};
	}
}
// END

QCoro::QmlTask adb::startUsbConnection(QString serial, QString pin)
{
	return doStartUsbConnection(serial, pin);
}

QCoro::Task<> adb::doStartUsbConnection(QString serial, QString pin)
{
	auto it = std::ranges::find(m_android_devices, serial, &device::serial);

	if (it == m_android_devices.end())
		co_return;

	auto adb_reverse = escape_sandbox("adb", "-s", serial, "reverse", "tcp:9757", "tcp:9757");
	adb_reverse->setProcessChannelMode(QProcess::ForwardedChannels);
	auto co_process = qCoro(*adb_reverse);

	co_await co_process.start();
	co_await co_process.waitForFinished();

	if (adb_reverse->exitStatus() != QProcess::NormalExit or adb_reverse->exitCode() != 0)
	{
		// TODO display error
		co_return;
	}

	QString uri = pin == "" ? "wivrn+tcp://127.0.0.1:9757" : "wivrn+tcp://:" + pin + "@127.0.0.1:9757";

	auto adb_start_activity = escape_sandbox("adb", "-s", serial, "shell", "am", "start", "-a", "android.intent.action.VIEW", "-d", uri, it->app);
	adb_start_activity->setProcessChannelMode(QProcess::ForwardedChannels);
	auto co_adb_start_activity = qCoro(*adb_start_activity);

	co_await co_adb_start_activity.start();
	co_await co_adb_start_activity.waitForFinished();

	if (adb_start_activity->exitStatus() != QProcess::NormalExit or adb_start_activity->exitCode() != 0)
	{
		// TODO display error
		co_return;
	}

	// TODO display success
}

#include "moc_adb.cpp"
