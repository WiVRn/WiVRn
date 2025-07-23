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

#include "application.h"

#include "utils/xdg_base_directory.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace wivrn
{

namespace
{

std::string read_file(const std::filesystem::path & path)
{
#if 0
	if (wivrn::flatpak_key(wivrn::flatpak::section::session_bus_policy, "org.freedesktop.Flatpak") == "talk")
	{
		QProcess flatpak_spawn;
		flatpak_spawn.start("flatpak-spawn", {"--host", "cat", QString::fromStdString(vrmanifest)});
		flatpak_spawn.waitForFinished();
		return flatpak_spawn.readAllStandardOutput().toStdString();
	}
	else
#endif
	{
		std::ifstream f(path);
		std::istreambuf_iterator<char> begin{f}, end;

		return {begin, end};
	}
}

std::string read_vr_manifest()
{
	const char * home = getenv("HOME");

	return read_file(std::string{home ? home : ""} + "/.steam/steam/config/steamapps.vrmanifest");
}

void read_steam_vr_apps(std::unordered_map<std::string, application> & res)
{
	auto manifest = read_vr_manifest();
	if (manifest.empty())
		return;
	nlohmann::json json = nlohmann::json::parse(manifest);

	for (auto & i: json["applications"])
	{
		try
		{
			application app;

			std::string app_key = i["app_key"];
			for (auto [locale, items]: i["strings"].items())
			{
				if (auto it = items.find("name"); it != items.end())
					app.name[locale] = *it;
			}

			if (not app.name.contains(""))
			{
				auto it = app.name.find("en_us");
				if (it == app.name.end())
					it = app.name.begin();
				if (it != app.name.end())
					app.name[""] = it->second;
			}

			if (i["launch_type"] == "url")
			{
				app.exec = "steam " + (std::string)i["url"];
			}
			else if (i["launch_type"] == "binary")
			{
				const char prefix[] = "steam.app.";
				if (app_key.starts_with(prefix))
				{
					// ¯\_(ツ)_/¯
					uint64_t appkey = stoll(app_key.substr(strlen(prefix)));
					app.exec = "steam steam://rungameid/" + std::to_string((appkey << 32) + 0x2000000);
				}
			}

			if (not app.exec.empty())
				res[app_key] = std::move(app);
		}
		catch (std::exception &)
		{
		}
	}
}

std::string unescape(std::string_view in)
{
	std::string out;
	out.reserve(in.size());
	for (auto c = in.begin(); c != in.end(); ++c)
	{
		if (*c == '\\')
		{
			++c;
			if (c == in.end())
				break;
			switch (*c)
			{
				case 's':
					out += ' ';
					break;
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case 'r':
					out += '\r';
					break;
				default:
					out += *c;
			}
		}
		else
		{
			out += *c;
		}
	}
	return out;
}

struct key_value
{
	std::string_view key;
	std::string_view locale;
	std::string_view value;
	key_value(std::string_view l)
	{
		auto pos = l.find_first_of("[ =");
		if (pos == std::string_view::npos)
			return;
		key = l.substr(0, pos);
		l = l.substr(pos);
		if (l[0] == '[')
		{
			pos = l.find(']');
			if (pos == std::string_view::npos)
			{
				key = std::string_view();
				return;
			}
			locale = l.substr(1, pos - 1);
			l = l.substr(pos + 1);
		}
		pos = l.find('=');
		if (pos == std::string_view::npos)
		{
			key = std::string_view();
			locale = std::string_view();
			return;
		}
		l = l.substr(pos + 1);
		pos = l.find_first_not_of(" ");
		if (pos == std::string_view::npos)
		{
			key = std::string_view();
			locale = std::string_view();
			return;
		}
		value = l.substr(pos);
	}
};
std::optional<application> do_desktop_entry(const std::filesystem::path & filename)
{
	auto data = read_file(filename);
	std::optional<application> res;

	// Most apps are not VR, don't bother parsing the rest
	bool vr = false;
	for (auto bounds: data | std::views::split('\n'))
	{
		std::string_view line(bounds.begin(), bounds.end());
		key_value items(line);
		if (items.key == "X-WiVRn-VR")
			vr = items.value == "true";
	}
	if (not vr)
		return res;

	res.emplace();
	for (auto bounds: data | std::views::split('\n'))
	{
		std::string_view line(bounds.begin(), bounds.end());
		if (line.empty())
			continue;
		if (line.starts_with("#"))
			continue;

		// Only process the main section
		if (line.starts_with("[") and line != "[Desktop Entry]")
			break;

		key_value items(line);
		if (items.key == "Type" and items.value != "Application")
		{
			res.reset();
			return res;
		}
		if (items.key == "Name")
			res->name.emplace(items.locale, unescape(items.value));
		if (items.key == "Exec")
			res->exec = unescape(items.value);
		if (items.key == "Path")
			res->path = unescape(items.value);
	}

	if (res->exec.empty() or not res->name.contains(""))
		res.reset();

	return res;
}

void do_data_dir(std::filesystem::path dir, std::unordered_map<std::string, application> & res)
{
	dir = dir / "applications";
	if (not std::filesystem::is_directory(dir))
		return;
	for (const auto & entry: std::filesystem::recursive_directory_iterator(dir))
	{
		if (entry.is_directory())
			continue;

		if (entry.path().extension() != ".desktop")
			continue;

		// https://specifications.freedesktop.org/desktop-entry-spec/latest/file-naming.html#desktop-file-id
		auto file_id = entry.path()
		                       .lexically_relative(dir)
		                       .replace_extension("")
		                       .string();
		std::ranges::replace(file_id, '/', '-');

		if (res.contains(file_id))
			continue;

		auto app = do_desktop_entry(entry.path());
		if (app)
			res.emplace(std::move(file_id), std::move(*app));
	}
}
} // namespace

std::unordered_map<std::string, application> list_applications(bool include_steam)
{
	std::unordered_map<std::string, application> res;

	if (include_steam)
		read_steam_vr_apps(res);

	do_data_dir(xdg_data_home(), res);
	for (auto && dir: xdg_data_dirs())
		do_data_dir(std::move(dir), res);

	return res;
}

} // namespace wivrn
