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

#include "steam_app_info.h"
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>

namespace
{

class binary_reader
{
	std::ifstream in;

public:
	binary_reader(std::ifstream && in) : in{std::move(in)} {}

	void discard(size_t bytes)
	{
		in.seekg(bytes, std::ios_base::cur);
	}

	template <typename T>
	T read();

	template <typename T>
	        requires std::is_integral_v<T>
	T read()
	{
		T res;
		in.read((char *)&res, sizeof(T));
		if (not in)
			throw std::runtime_error{"File truncated"};

		static_assert(std::endian::native == std::endian::big or std::endian::native == std::endian::little);

		if constexpr (std::endian::native == std::endian::big)
			return std::byteswap(res);
		else
			return res;
	}

	std::vector<std::string> string_table();

	auto tellg()
	{
		return in.tellg();
	}
	void seekg(size_t pos)
	{
		in.seekg(pos);
	}
};

template <>
std::string binary_reader::read()
{
	std::string res;
	std::getline(in, res, '\0');
	if (not in)
		throw std::runtime_error{"File truncated"};
	return res;
}

std::vector<std::string> binary_reader::string_table()
{
	std::vector<std::string> res;
	size_t string_offset = read<uint64_t>();
	auto pos = in.tellg();

	in.seekg(string_offset);
	auto count = read<uint32_t>();
	res.reserve(count);
	for (size_t i = 0; i < count; ++i)
		res.emplace_back(read<std::string>());
	in.seekg(pos);
	return res;
}

template <typename T>
void read_dict(binary_reader & in, T & visitor, const std::vector<std::string> * string_table = nullptr)
{
	while (true)
	{
		auto type = in.read<uint8_t>();
		if (type == 8)
		{
			visitor.end_dict();
			return;
		}

		// Key
		const std::string & key = string_table ? string_table->at(in.read<uint32_t>()) : in.read<std::string>();

		// Value
		switch (type)
		{
			case 0: // Dictionary
				visitor.begin_dict(key);
				read_dict(in, visitor, string_table);
				break;
			case 1: // UTF-8 string
				visitor.on_string(key, in.read<std::string>());
				break;
			case 2: // uint32_t
				visitor.on_uint32_t(key, in.read<uint32_t>());
				break;
				// case 3: // float
			default:
				throw std::runtime_error{"Unknown object type " + std::to_string(type)};
		}
	}
}
} // namespace

std::unordered_map<uint32_t, wivrn::steam_icon> wivrn::read_steam_icons(std::filesystem::path path)
{
	binary_reader in{std::ifstream{path}};

	uint32_t magic = in.read<uint32_t>();
	if ((magic & 0xff'ff'ff'00) != 0x07'56'44'00)
		throw std::runtime_error{"Wrong magic number"};

	auto universe = in.read<uint32_t>();

	int version = magic & 0xff;

	std::vector<std::string> string_table;
	std::vector<std::string> * string_table_ptr = nullptr;
	if (version >= 41)
	{
		string_table = in.string_table();
		string_table_ptr = &string_table;
	}

	std::unordered_map<uint32_t, wivrn::steam_icon> res;

	struct V
	{
		wivrn::steam_icon current{};

		void begin_dict(const std::string & key)
		{
		}
		void end_dict()
		{
		}
		void on_string(const std::string & key, std::string && val)
		{
			if (strcasecmp(key.c_str(), "clienticon") == 0)
				current.clienticon = std::move(val);
			else if (strcasecmp(key.c_str(), "linuxclienticon") == 0)
				current.linuxclienticon = std::move(val);
		}
		void on_uint32_t(const std::string & key, uint32_t val)
		{
		}
	};

	while (true)
	{
		// See https://github.com/SteamDatabase/SteamAppInfo/blob/master/README.md#app-entry-repeated
		auto app_id = in.read<uint32_t>();
		if (app_id == 0)
			break;

		size_t size_data = in.read<uint32_t>();
		size_t pos = in.tellg();
		in.discard(
		        4    // info_state
		        + 4  // last_updated
		        + 8  // pics_token
		        + 20 // SHA1 of the app info
		        + 4  // change number
		        + 20 // SHA1 of the bin data
		        + 4  // checksum
		        + 1  // type
		);
		try
		{
			V v;
			read_dict(in, v, string_table_ptr);
			if (not(v.current.clienticon.empty() and v.current.linuxclienticon.empty()))
				res.emplace(app_id, std::move(v.current));
		}
		catch (...)
		{
			// Ignore errors
		}
		in.seekg(pos + size_data);
	}

	return res;
}

std::vector<wivrn::steam_shortcut> wivrn::read_steam_shortcuts(std::filesystem::path path)
{
	binary_reader in{std::ifstream{path}};
	auto type = in.read<uint8_t>();
	auto name = in.read<std::string>();

	if (type != 0 or name != "shortcuts")
		throw std::runtime_error{"Invalid type for shortcuts file"};

	std::vector<wivrn::steam_shortcut> res;
	struct V
	{
		std::vector<wivrn::steam_shortcut> & items;

		int depth = 0;
		wivrn::steam_shortcut current{};
		bool VR = false;

		void begin_dict(const std::string & key)
		{
			++depth;
			if (depth == 1)
			{
				current = decltype(current){};
				VR = false;
			}
		}
		void end_dict()
		{
			--depth;
			if (depth == 0)
			{
				if (VR)
					items.push_back(std::move(current));
			}
		}
		void on_string(const std::string & key, std::string && val)
		{
			if (depth == 1)
			{
				if (strcasecmp(key.c_str(), "AppName") == 0)
					current.name = std::move(val);
				else if (strcasecmp(key.c_str(), "icon") == 0 and not val.empty())
					current.icon = std::move(val);
			}
		}
		void on_uint32_t(const std::string & key, uint32_t val)
		{
			if (depth == 1)
			{
				if (strcasecmp(key.c_str(), "appid") == 0)
					current.appid = val;
				else if (strcasecmp(key.c_str(), "OpenVR") == 0)
					VR = val;
			}
		}
	} v{res};

	read_dict(in, v);

	return res;
}
