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

#include <QDebug>
#include <QProcess>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;

template <typename... Args>
std::unique_ptr<QProcess> escape_sandbox(const std::string & executable, Args &&... args_orig)
{
	auto process = std::make_unique<QProcess>();
	QStringList args;

	if (std::filesystem::exists("/.flatpak-info"))
	{
		process->setProgram("flatpak-spawn");
		args.push_back("--host");
		args.push_back(QString::fromStdString(executable));
	}
	else
	{
		process->setProgram(QString::fromStdString(executable));
	}

	(args.push_back(QString::fromStdString(args_orig)), ...);

	process->setArguments(args);

	return process;
}

adb::adb()
{
	poll_devices_timer.setInterval(500ms);
	connect(&poll_devices_timer, &QTimer::timeout, this, &adb::on_poll_devices_timeout);

	on_poll_devices_timeout();
	poll_devices_timer.start();
}

void adb::on_poll_devices_timeout()
{
	if (poll_devices_process)
		return;

	poll_devices_process = escape_sandbox("adb", "devices", "-l");
	connect(poll_devices_process.get(), &QProcess::finished, this, &adb::on_poll_devices_process_finished);
	poll_devices_process->start();
}

void adb::on_poll_devices_process_finished(int exit_code, QProcess::ExitStatus exit_status)
{
	if (exit_code != 0 or exit_status != QProcess::NormalExit)
	{
		qDebug() << "adb devices exited with code" << exit_code << ", status" << exit_status;

		QString out = poll_devices_process->readAllStandardOutput();
		qDebug() << "stdout:" << out;

		QString err = poll_devices_process->readAllStandardError();
		qDebug() << "stderr:" << err;
	}
	else
	{
		auto out = poll_devices_process->readAllStandardOutput();
		auto lines = QString{out}.split('\n');

		if (not lines.empty())
			lines.pop_front();

		std::vector<adb::device> devs;
		for (auto & line: lines)
		{
			auto words = line.split(' ', Qt::SkipEmptyParts);
			device dev;

			if (words.empty())
				continue;
			dev._serial = words.front().toStdString();
			words.pop_front();

			if (words.empty())
				continue;
			dev._state = words.front().toStdString();
			words.pop_front();

			for (auto & word: words)
			{
				auto idx = word.indexOf(':');
				if (idx >= 0)
				{
					dev._properties[word.left(idx).toStdString()] = word.mid(idx + 1).toStdString();
				}
			}

			devs.push_back(dev);
		}

		_android_devices = std::move(devs);
		android_devices_changed(_android_devices);
	}

	poll_devices_process.release()->deleteLater();
}

std::unique_ptr<QProcess> adb::device::install(const std::filesystem::path & path)
{
	return escape_sandbox("adb", "-s", _serial, "install", "-r", path);
}

std::unique_ptr<QProcess> adb::device::uninstall(const std::string & app)
{
	return escape_sandbox("adb", "-s", _serial, "uninstall", app);
}

std::vector<std::string> adb::device::installed_apps()
{
	auto process = escape_sandbox("adb", "-s", _serial, "shell", "pm", "list", "packages");
	process->start();
	process->waitForFinished();

	if (process->exitCode() != 0 or process->exitStatus() != QProcess::NormalExit)
		throw std::runtime_error("Cannot list packages");

	auto out = process->readAllStandardOutput();
	auto lines = QString{out}.split('\n');

	std::vector<std::string> apps;

	for (auto & line: lines)
	{
		QString prefix = "package:";
		if (line.startsWith(prefix))
		{
			apps.push_back(line.toStdString().substr(prefix.size()));
		}
	}

	return apps;
}

// void adb::device::start(const std::string& app)
// {
// 	auto process = escape_sandbox("adb", "-s", _serial, "shell", "am", "start", app);
// 	process->start();
// 	process->waitForFinished();
// }
//
// void adb::device::stop(const std::string& app)
// {
// 	auto process = escape_sandbox("adb", "-s", _serial, "shell", "am", "force-stop", app);
// 	process->start();
// 	process->waitForFinished();
// }
//
void adb::device::start(const std::string & app, const std::string & action, const std::string & uri)
{
	// action: "android.intent.action.VIEW" or "android.intent.action.MAIN"
	auto process = escape_sandbox("adb", "-s", _serial, "shell", "am", "start", "-a", action, "-d", uri, app);
	process->start();
	process->waitForFinished();
}

void adb::device::reverse_forward(int local_port, int device_port)
{
	auto process = escape_sandbox("adb", "-s", _serial, "reverse", "tcp:" + std::to_string(local_port), "tcp:" + std::to_string(device_port));
	process->start();
	process->waitForFinished();
}

#include "moc_adb.cpp"
