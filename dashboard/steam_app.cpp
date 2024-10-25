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

#include "steam_app.h"
#include <QProcess>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
std::string read_vr_manifest()
{
	const char * home = getenv("HOME");

	std::filesystem::path vrmanifest = std::string{home ? home : ""} + "/.steam/steam/config/steamapps.vrmanifest";

	if (std::filesystem::exists("/.flatpak-info"))
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

std::vector<steam_app> steam_apps(const std::string & locale)
{
	std::vector<steam_app> apps;

	auto manifest = read_vr_manifest();
	if (manifest.empty())
	{
		return apps;
	}

	nlohmann::json json;
	try
	{
		json = nlohmann::json::parse(manifest);
	}
	catch (std::exception & e)
	{
		std::cerr << e.what() << std::endl;
		return apps;
	}

	for (auto & i: json["applications"])
	{
		try
		{
			steam_app app;

			app.image_path = i.value("image_path", "");
			app.name = i["strings"]["en_us"]["name"];

			if (i["launch_type"] == "url")
			{
				app.url = i["url"];
			}
			else if (i["launch_type"] == "binary")
			{
				std::string app_key = i["app_key"];

				const char prefix[] = "steam.app.";
				if (app_key.starts_with(prefix))
					app.url = "steam://rungameid/" + app_key.substr(strlen(prefix));
			}

			if (app.url != "")
				apps.push_back(app);
		}
		catch (std::exception & e)
		{
			std::cerr << e.what() << std::endl;
		}
	}

	std::ranges::sort(apps, [](const steam_app & a, const steam_app & b) {
		return a.name < b.name;
	});

	return apps;
}
