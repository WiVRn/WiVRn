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
#include <QPromise>
#include <exception>
#include <memory>

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

std::shared_ptr<QPromise<std::vector<adb::device>>> adb::devices()
{
	auto process = escape_sandbox("adb", "devices", "-l");

	process->start();

	auto promise = std::make_shared<QPromise<std::vector<adb::device>>>();

	auto p = process.get();

	QObject::connect(p, &QProcess::finished, [p = std::move(process), promise](int exitCode, QProcess::ExitStatus exitStatus) {
		try
		{
			if (exitCode != 0 or exitStatus != QProcess::NormalExit)
			{
				qDebug() << "adb devices exited with code" << exitCode << ", status" << exitStatus;

				QString out = p->readAllStandardOutput();
				qDebug() << "stdout:" << out;

				QString err = p->readAllStandardError();
				qDebug() << "stderr:" << err;

				throw std::runtime_error("adb devices failed");
			}
			else
			{
				auto out = p->readAllStandardOutput();
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

				promise->addResult(std::move(devs));
			}
		}
		catch (...)
		{
			promise->setException(std::current_exception());
		}

		promise->finish();
	});

	return promise;
}

std::unique_ptr<QProcess> adb::device::install(const std::filesystem::path & path)
{
	return escape_sandbox("adb", "-s", _serial, "install", "-r", path);
}

std::unique_ptr<QProcess> adb::device::uninstall(const std::string & app)
{
	return escape_sandbox("adb", "-s", _serial, "uninstall", app);
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
// void adb::device::start(const std::string& app, const std::string& action, const std::string& uri)
// {
// 	// action: "android.intent.action.VIEW" or "android.intent.action.MAIN"
// 	auto process = escape_sandbox("adb", "-s", _serial, "shell", "am", "start", "-a", action, "-d", uri, app);
// 	process->start();
// 	process->waitForFinished();
// }
