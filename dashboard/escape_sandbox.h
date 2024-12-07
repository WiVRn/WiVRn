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

#include "utils/flatpak.h"
#include <QProcess>
#include <string>

template <typename... Args>
std::unique_ptr<QProcess> escape_sandbox(const std::string & executable, Args &&... args_orig)
{
	auto process = std::make_unique<QProcess>();
	QStringList args;

	if (wivrn::flatpak_key(wivrn::flatpak::section::session_bus_policy, "org.freedesktop.Flatpak") == "talk")
	{
		process->setProgram("flatpak-spawn");
		args.push_back("--host");
		args.push_back(QString::fromStdString(executable));
	}
	else
	{
		process->setProgram(QString::fromStdString(executable));
	}

	auto to_QString = [](const auto & s) {
		using T = std::decay_t<std::remove_cv_t<std::remove_reference_t<decltype(s)>>>;

		if constexpr (std::is_same_v<T, QString>)
			return s;
		if constexpr (std::is_same_v<T, char *>)
			return s;
		if constexpr (std::is_same_v<T, std::string>)
			return QString::fromStdString(s);
	};

	(args.push_back(to_QString(args_orig)), ...);

	process->setArguments(args);

	return process;
}
