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
#include "utils/steam_info.h"
#include "utils/xdg_base_directory.h"
#include "utils/xdg_icon_lookup.h"

#include <filesystem>
#include <fstream>
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

	for (auto & steam: steam::find_installations())
	{
		auto cmd = steam.get_steam_command() + " ";
		for (auto && app: steam.list_applications())
		{
			res.emplace(std::to_string(app.appid),
			            application{
			                    .name = std::move(app.name),
			                    .exec = cmd + app.url,
			                    .icon_path = steam.get_icon(app.appid),
			            });
		}
	}

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
