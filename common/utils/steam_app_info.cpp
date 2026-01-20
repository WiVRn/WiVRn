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
#include <cctype>
#include <cstring>
#include <fstream>
#include <span>
#include <string_view>

namespace
{
template <typename T>
T read(std::span<char> & buffer);

template <typename T>
        requires std::is_integral_v<T>
T read(std::span<char> & buffer)
{
	if (sizeof(T) > buffer.size())
		throw std::runtime_error{"File truncated"};

	T value;
	memcpy(&value, buffer.data(), sizeof(T));
	buffer = buffer.subspan(sizeof(T));

	static_assert(std::endian::native == std::endian::big or std::endian::native == std::endian::little);

	if constexpr (std::endian::native == std::endian::big)
		return std::byteswap(value);
	else
		return value;
}

template <>
std::string_view read<std::string_view>(std::span<char> & buffer)
{
	size_t size = strnlen((char *)buffer.data(), buffer.size());
	if (size == buffer.size())
		throw std::runtime_error{"File truncated"};

	char * str = buffer.data();
	buffer = buffer.subspan(size + 1);
	return {str, size};
}

std::span<char> read(std::span<char> & buffer, size_t size)
{
	if (size > buffer.size())
		throw std::runtime_error{"File truncated"};

	std::span<char> value{buffer.data(), size};
	buffer = buffer.subspan(size);
	return value;
}

std::vector<std::string_view> read_string_table(std::span<char> buffer)
{
	std::vector<std::string_view> string_table;

	uint32_t count = read<uint32_t>(buffer);
	string_table.reserve(count);

	for (size_t i = 0; i < count; i++)
		string_table.emplace_back(read<std::string_view>(buffer));

	return string_table;
}

void read_vdf(wivrn::steam_app_info::info & info, const std::string & prefix, std::span<char> & bindata, const std::vector<std::string_view> & string_table)
{
	int type = read<uint8_t>(bindata);

	while (type != 8)
	{
		std::string name{string_table.at(read<uint32_t>(bindata))};
		for (char & c: name)
			c = std::tolower(c);

		// https://github.com/ValveResourceFormat/ValveKeyValue/blob/master/ValveKeyValue/ValveKeyValue/KeyValues1/KV1BinaryNodeType.cs
		switch (type)
		{
			case 0: // Dictionary
				read_vdf(info, prefix + name + ".", bindata, string_table);
				break;

			case 1: // UTF-8 string
				info.emplace(prefix + name, read<std::string_view>(bindata));
				break;

			case 2: // uint32_t
				info.emplace(prefix + name, read<uint32_t>(bindata));
				break;

				// case 3: // float
				// break;

			default:
				throw std::runtime_error{"Unknown object type " + std::to_string(type)};
		}

		type = read<uint8_t>(bindata);
	}
}
} // namespace

wivrn::steam_app_info::steam_app_info(std::filesystem::path path)
{
	std::vector<std::string_view> string_table;

	{
		std::ifstream f{path};
		data = std::vector<char>{std::istreambuf_iterator{f}, {}};
	}
	std::span<char> buffer = data;

	// See https://github.com/SteamDatabase/SteamAppInfo/blob/master/README.md#file-header
	uint32_t magic = read<uint32_t>(buffer);
	if ((magic & 0xff'ff'ff'00) != 0x07'56'44'00)
		throw std::runtime_error{"Wrong magic number"};
	int version = magic & 0xff;

	if (version < 41)
		throw std::runtime_error{"Unsupported version"};

	int universe = read<uint32_t>(buffer);
	if (version >= 41)
	{
		size_t string_offset = read<uint64_t>(buffer);
		string_table = read_string_table(std::span<char>{data}.subspan(string_offset));
	}
	else
		throw std::runtime_error{"Unsupported version"};

	while (true)
	{
		// See https://github.com/SteamDatabase/SteamAppInfo/blob/master/README.md#app-entry-repeated

		auto app_id = read<uint32_t>(buffer);
		if (app_id == 0)
			break;

		auto size_data = read<uint32_t>(buffer);
		char * current_offset = buffer.data();

		read<uint32_t>(buffer); // info_state
		read<uint32_t>(buffer); // last_updated
		read<uint64_t>(buffer); // pics_token
		read(buffer, 20);       // SHA1 of the app info
		read<uint32_t>(buffer); // change number
		// if (version < 38)
		// read<uint8_t>(buffer); // section type
		read(buffer, 20); // SHA1 of the bin data
		auto bindata = read(buffer, size_data - (buffer.data() - current_offset));

		try
		{
			bindata = bindata.subspan(4 + 1); // Skip the checksum + type (must be 0)

			info app_info;
			read_vdf(app_info, "", bindata, string_table);

			app_data.emplace(app_id, app_info);
		}
		catch (...)
		{
			// Ignore errors
		}
	}
}

namespace
{

class binary_reader
{
	std::ifstream in;

public:
	binary_reader(std::ifstream && in) : in{std::move(in)} {}

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

template <typename T>
void read_vdf(binary_reader & in, T & visitor, int version)
{
	if (version >= 41)
	{
		std::vector<std::string> string_table;
		auto count = in.read<uint32_t>();
		string_table.reserve(count);
		for (size_t i = 0; i < count; ++i)
			string_table.emplace_back(in.read<std::string>());

		read_dict(in, visitor, &string_table);
	}
	else
	{
		read_dict(in, visitor);
	}
}

} // namespace

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
				if (key == "AppName")
					current.name = std::move(val);
				else if (key == "icon")
					current.icon = std::move(val);
			}
		}
		void on_uint32_t(const std::string & key, uint32_t val)
		{
			if (depth == 1)
			{
				if (key == "appid")
					current.appid = val;
				if (key == "OpenVR")
					VR = val;
			}
		}
	} v{res};

	read_vdf(in, v, 0);

	return res;
}
