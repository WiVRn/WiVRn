/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "xdg_icon_lookup.h"

#include "ini.h"
#include "strings.h"
#include "utils/flatpak.h"
#include "xdg_base_directory.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>

namespace
{
enum class icon_theme_type
{
	fixed,
	scalable,
	threshold
};

struct icon_theme_dir
{
	std::filesystem::path path;

	int size;
	int scale;
	icon_theme_type type;
	int min_size;
	int max_size;
	int threshold;

	bool match_size(int required_size, int required_scale) const
	{
		if (scale != required_scale)
			return false;

		switch (type)
		{
			case icon_theme_type::fixed:
				return size == required_size;

			case icon_theme_type::scalable:
				return min_size <= required_size and required_size <= max_size;

			case icon_theme_type::threshold:
				return size - threshold <= required_size and required_size <= size + threshold;
		}

		return false;
	}

	int size_distance(int required_size, int required_scale) const
	{
		switch (type)
		{
			case icon_theme_type::fixed:
				return abs(size * scale - required_size * required_scale);

			case icon_theme_type::scalable:
				if (required_size * required_scale < min_size * scale)
					return min_size * scale - required_size * required_scale;

				if (required_size * required_scale > max_size * scale)
					return required_size * required_scale - max_size * scale;

				return 0;

			case icon_theme_type::threshold:
				if (required_size * required_scale < (size - threshold) * scale)
					return min_size * scale - required_size * required_scale;

				if (required_size * required_scale > (size + threshold) * scale)
					return required_size * required_scale - max_size * scale;

				return 0;
		}

		return std::numeric_limits<int>::max();
	}
};

void find_icon_theme_dirs_helper(const std::vector<std::filesystem::path> & base_dirs, std::vector<icon_theme_dir> & dirs, std::vector<std::string> & themes, std::string theme)
{
	// Theme is passed by copy because the themes vector might be resized
	for (const auto & base_dir: base_dirs)
	{
		const auto theme_dir = base_dir / "icons" / theme;
		const auto index_theme = theme_dir / "index.theme";

		if (not std::filesystem::exists(index_theme))
			continue;

		try
		{
			std::ifstream file{index_theme};
			wivrn::ini index{file};

			for (const auto & inherited_theme: utils::split(index.get<std::string>("Icon Theme", "Inherits", ""), ","))
			{
				if (inherited_theme != "" and not std::ranges::contains(themes, inherited_theme))
					themes.push_back(inherited_theme);
			}

			for (auto directory: utils::split(index.get<std::string>("Icon Theme", "Directories", ""), ","))
			{
				int size = index.get<int>(directory, "Size");
				int scale = index.get<int>(directory, "Scale", 1);
				int min_size = index.get<int>(directory, "MinSize", size);
				int max_size = index.get<int>(directory, "MaxSize", size);
				int threshold = index.get<int>(directory, "Threshold", 2);

				std::string type_str = index.get<std::string>(directory, "Type", "Threshold");

				icon_theme_type type = icon_theme_type::threshold;
				if (type_str == "Fixed")
					type = icon_theme_type::fixed;
				else if (type_str == "Scalable")
					type = icon_theme_type::scalable;
				else if (type_str == "Threshold")
					type = icon_theme_type::threshold;

				dirs.push_back(icon_theme_dir{
				        .path = theme_dir / directory,
				        .size = size,
				        .scale = scale,
				        .type = type,
				        .min_size = min_size,
				        .max_size = max_size,
				        .threshold = threshold,
				});
			}
		}
		catch (std::exception & e)
		{
		}
	}
}

std::vector<icon_theme_dir> find_icon_theme_dirs(std::vector<std::string> themes)
{
	std::vector<icon_theme_dir> dirs;
	std::vector<std::filesystem::path> base_dirs = xdg_data_dirs();

	if (wivrn::is_flatpak())
	{
		// Try to guess host data dirs
		base_dirs.push_back("/run/host/usr/share");
	}

	for (size_t i = 0; i < themes.size(); ++i)
		find_icon_theme_dirs_helper(base_dirs, dirs, themes, themes[i]);

	if (not std::ranges::contains(themes, "hicolor"))
		find_icon_theme_dirs_helper(base_dirs, dirs, themes, "hicolor");

	return dirs;
}
} // namespace

std::optional<std::filesystem::path> wivrn::xdg_icon_lookup(const std::string & icon_name, int size, int scale)
{
	if (std::filesystem::exists(icon_name))
		return icon_name;

	static const std::vector<icon_theme_dir> dirs = find_icon_theme_dirs({"hicolor"});

	for (const auto & dir: dirs)
	{
		if (dir.match_size(size, scale))
		{
			for (const auto & ext: {".png", ".svg", ".xpm"})
			{
				if (std::filesystem::exists(dir.path / (icon_name + ext)))
					return dir.path / (icon_name + ext);
			}
		}
	}

	int best_distance = std::numeric_limits<int>::max();
	std::optional<std::filesystem::path> best_path;

	for (const auto & dir: dirs)
	{
		if (dir.size_distance(size, scale) < best_distance)
		{
			for (const auto & ext: {".png", ".svg", ".xpm"})
			{
				if (std::filesystem::exists(dir.path / (icon_name + ext)))
				{
					best_distance = dir.size_distance(size, scale);
					best_path = dir.path / (icon_name + ext);
				}
			}
		}
	}

	return best_path;
}
