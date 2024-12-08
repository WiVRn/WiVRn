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

#include "steam_apps.h"
#include "escape_string.h"
#include "utils/flatpak.h"
#include <QProcess>
#include <QtLogging>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
std::string read_vr_manifest()
{
	const char * home = getenv("HOME");

	std::filesystem::path vrmanifest = std::string{home ? home : ""} + "/.steam/steam/config/steamapps.vrmanifest";

	if (wivrn::flatpak_key(wivrn::flatpak::section::session_bus_policy, "org.freedesktop.Flatpak") == "talk")
	{
		QProcess flatpak_spawn;
		flatpak_spawn.start("flatpak-spawn", {"--host", "cat", QString::fromStdString(vrmanifest)});
		flatpak_spawn.waitForFinished();
		return flatpak_spawn.readAllStandardOutput().toStdString();
	}
	else
	{
		std::ifstream f(vrmanifest);
		std::istreambuf_iterator<char> begin{f}, end;

		return {begin, end};
	}
}
} // namespace

SteamApps::SteamApps(QObject * parent)
{
	auto manifest = read_vr_manifest();
	if (manifest.empty())
	{
		return;
	}

	nlohmann::json json;
	try
	{
		json = nlohmann::json::parse(manifest);
	}
	catch (std::exception & e)
	{
		qWarning() << "Error parsing VR manifest: " << e.what();
		return;
	}

	for (auto & i: json["applications"])
	{
		try
		{
			steamApp app;

			app.setImagePath(i.value("image_path", ""));
			app.setName(i["strings"]["en_us"]["name"]);

			if (i["launch_type"] == "url")
			{
				app.setCommand("steam " + (std::string)i["url"]);
			}
			else if (i["launch_type"] == "binary")
			{
				std::string app_key = i["app_key"];

				const char prefix[] = "steam.app.";
				if (app_key.starts_with(prefix))
				{
					// ¯\_(ツ)_/¯
					uint64_t appkey = stoll(app_key.substr(strlen(prefix)));
					app.setCommand("steam steam://rungameid/" + std::to_string((appkey << 32) + 0x2000000));
				}
			}

			if (app.command() != "")
				m_apps.push_back(app);
		}
		catch (std::exception & e)
		{
			qWarning() << "Error adding app from VR manifest: " << e.what();
		}
	}

	std::ranges::sort(m_apps, [](const steamApp & a, const steamApp & b) {
		return a.name() < b.name();
	});
}

#include "moc_steam_apps.cpp"
