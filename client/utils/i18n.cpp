/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "i18n.h"

#include "utils/mapped_file.h"
#include <map>
#include <string>
#include <vector>

extern std::map<std::string, std::vector<int>> glyph_set_per_language;

std::vector<boost::locale::gnu_gettext::messages_info> get_locales()
{
	boost::locale::gnu_gettext::messages_info messages_info;
	messages_info.paths.push_back("locale");
	messages_info.encoding = "UTF-8";

	messages_info.domains.push_back(boost::locale::gnu_gettext::messages_info::domain("wivrn"));
	messages_info.callback = open_locale_file;
	std::vector<boost::locale::gnu_gettext::messages_info> res;

	messages_info.language = "en";
	messages_info.country = "US";
	res.push_back(messages_info);

	for (const auto & [code, glyphs]: glyph_set_per_language)
	{
		auto pos = code.find("_");
		messages_info.language = code.substr(0, pos);
		if (pos == std::string::npos)
			messages_info.country = "";
		else
			messages_info.country = code.substr(pos + 1);
		res.push_back(messages_info);
	}
	return res;
}

std::vector<char> open_locale_file(const std::string & file_name, const std::string & encoding)
{
	std::vector<char> buffer;
	try
	{
		utils::mapped_file file("assets://" + file_name);
		buffer.resize(file.size());
		memcpy(buffer.data(), file.data(), file.size());
	}
	catch (...)
	{
	}

	return buffer;
}
