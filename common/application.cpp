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

#include "utils/flatpak.h"
#include "utils/steam_app_info.h"
#include "utils/xdg_base_directory.h"
#include "utils/xdg_icon_lookup.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace wivrn
{

namespace
{
std::string read_file(const std::filesystem::path & path)
{
	std::ifstream f(path);
	std::istreambuf_iterator<char> begin{f}, end;

	return {begin, end};
}

std::filesystem::path home()
{
	auto home = std::getenv("HOME");
	return home ? home : "";
}

std::pair<std::string, std::optional<std::filesystem::path>> find_steam()
{
	auto h = home();
	// Flatpak Steam
	if (std::filesystem::exists(h / ".var/app/com.valvesoftware.Steam/.steam/steam"))
		return {"flatpak run com.valvesoftware.Steam", h / ".var/app/com.valvesoftware.Steam/.steam/steam"};

	// Debian Steam (accessed from flatpak)
	if (std::filesystem::exists(h / ".steam/debian-installation"))
		return {"steam", h / ".steam/debian-installation"};

	// system Steam
	if (std::filesystem::exists(xdg_data_home() / "Steam"))
		return {"steam", xdg_data_home() / "Steam"};

	// system Steam (accessed from flatpak)
	if (std::filesystem::exists(h / ".local/share/Steam"))
		return {"steam", h / ".local/share/Steam"};

	return {};
}

std::string read_vr_manifest(const std::filesystem::path & root)
{
	return read_file(root / "config/steamapps.vrmanifest");
}

std::unordered_map<uint32_t, wivrn::steam_icon> safe_read_steam_icons(const std::filesystem::path & root)
{
	try
	{
		return read_steam_icons(root / "appcache/appinfo.vdf");
	}
	catch (...)
	{}

	return {};
}

std::optional<std::filesystem::path> find_steam_icon(const std::filesystem::path & root, int app_id, const std::unordered_map<uint32_t, wivrn::steam_icon> & info)
{
	auto icon = info.find(app_id);
	if (icon == info.end())
		return std::nullopt;

	try
	{
		auto icon_path = root / "steam/games" / (icon->second.clienticon + ".ico");

		if (std::filesystem::exists(icon_path))
			return icon_path;
	}
	catch (...)
	{
	}

	try
	{
		auto icon_path = root / "steam/games" / (icon->second.linuxclienticon + ".zip");
		if (std::filesystem::exists(icon_path))
			return icon_path;
	}
	catch (...)
	{
	}

	return std::nullopt;
}

void read_steam_vr_apps(std::unordered_map<std::string, application> & res)
{
	auto [command, root] = find_steam();
	if (not root)
		return;

	auto manifest = read_vr_manifest(*root);
	auto info = safe_read_steam_icons(*root);

	if (manifest.empty())
		return;
	nlohmann::json json = nlohmann::json::parse(manifest);

	for (auto & i: json["applications"])
	{
		try
		{
			application app;

			if (i["launch_type"] == "url")
				app.exec = command + " " + (std::string)i["url"];
			else
				continue;

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

			try
			{
				const char prefix[] = "steam.app.";
				if (app_key.starts_with(prefix))
				{
					uint32_t app_id = stoll(app_key.substr(strlen(prefix)));
					app.icon_path = find_steam_icon(*root, app_id, info);
				}
			}
			catch (...)
			{
			}

			if (not app.exec.empty())
				res[app_key] = std::move(app);
		}
		catch (std::exception & e)
		{
			std::cerr << "Failed to parse Steam VR manifest: " << e.what() << std::endl;
		}
	}

	for (auto const & entry: std::filesystem::directory_iterator{*root / "userdata"})
	{
		auto shortcuts_vdf = entry.path() / "config/shortcuts.vdf";
		if (std::filesystem::exists(shortcuts_vdf))
		{
			try
			{
				for (auto && item: read_steam_shortcuts(shortcuts_vdf))
				{
					res[std::to_string(item.appid)] = {
					        .name = {{"", std::move(item.name)}},
					        // ¯\_(ツ)_/¯
					        .exec = command + " steam://rungameid/" + std::to_string((uint64_t(item.appid) << 32) + 0x2000000),
					        .icon_path = std::move(item.icon),
					};
				}
			}
			catch (std::exception & e)
			{
				std::cerr << "Failed to parse Steam shortcuts file " << shortcuts_vdf << ": " << e.what() << std::endl;
			}
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

bool contains(std::string_view entries, std::string_view val, char sep = ';')
{
	for (auto bounds: entries | std::views::split(sep))
	{
		if (std::string_view(bounds.begin(), bounds.end()) == val)
			return true;
	}
	return false;
}

std::optional<application> do_desktop_entry(const std::filesystem::path & filename)
{
	auto data = read_file(filename);
	std::optional<application> res;

	// Most apps are not VR, don't bother parsing the rest
	bool vr = false;
	for (auto bounds: data | std::views::split('\n'))
	{
		std::string_view line(bounds.begin(), bounds.end());
		key_value item(line);
		if (item.key == "Categories")
			vr = contains(item.value, "X-WiVRn-VR");
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

		key_value item(line);
		if (item.key == "Type" and item.value != "Application")
		{
			res.reset();
			return res;
		}
		if (item.key == "Name")
			res->name.emplace(item.locale, unescape(item.value));
		if (item.key == "Exec")
			res->exec = unescape(item.value);
		if (item.key == "Path")
			res->path = unescape(item.value);
		if (item.key == "Icon")
		{
			try
			{
				res->icon_path = xdg_icon_lookup(std::string{item.value}, 256);
			}
			catch (...)
			{}
		}
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
	try
	{
		for (const auto & entry: std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied))
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
	catch (std::exception & ex)
	{}
}
} // namespace

std::unordered_map<std::string, application> list_applications()
{
	std::unordered_map<std::string, application> res;

	read_steam_vr_apps(res);

	do_data_dir(xdg_data_home(), res);

	for (auto && dir: xdg_data_dirs())
		do_data_dir(std::move(dir), res);

	if (wivrn::is_flatpak())
	{
		// Try to guess host data dirs
		do_data_dir("/run/host/usr/share", res);
	}

	return res;
}

} // namespace wivrn
