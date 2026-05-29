/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "vdf.h"

#include <boost/iostreams/device/mapped_file.hpp>
#include <iostream>

namespace
{

void consume_whitespace(const char *& pos, const char * end)
{
	for (; pos != end; ++pos)
	{
		if (not std::isspace(*pos))
			return;
	}
	throw std::runtime_error("malformed VDF file");
}

wivrn::vdf::string read_string(const char *& pos, const char * end)
{
	consume_whitespace(pos, end);

	const bool quoted = *pos == '"';
	if (quoted)
		++pos;

	const char * begin = pos;

	for (; pos != end; ++pos)
	{
		if (*pos == '\\')
		{
			++pos;
			if (pos == end)
				break;
			continue;
		}

		if (quoted)
		{
			if (*pos == '"')
				return {std::string_view(begin, pos++)};
		}
		else
		{
			if (std::isspace(*pos) or *pos == '{' or *pos == '}')
				return {std::string_view(begin, pos)};
		}
	}
	throw std::runtime_error("malformed string in VDF file");
}

std::variant<wivrn::vdf::string, std::vector<wivrn::vdf::keyvalue>> read_value(const char *& pos, const char * end);

wivrn::vdf::keyvalue read_key_value(const char *& pos, const char * end)
{
	return wivrn::vdf::keyvalue{
	        .key = read_string(pos, end),
	        .value = read_value(pos, end),
	};
}

std::vector<wivrn::vdf::keyvalue> read_dict(const char *& pos, const char * end)
{
	consume_whitespace(pos, end);
	if (*pos != '{')
		throw std::runtime_error("malformed VDF file");
	++pos;

	std::vector<wivrn::vdf::keyvalue> res;
	while (true)
	{
		consume_whitespace(pos, end);
		if (*pos == '}')
		{
			++pos;
			return res;
		}
		res.push_back(read_key_value(pos, end));
	}

	throw std::runtime_error("malformed dict in VDF file");
}

std::variant<wivrn::vdf::string, std::vector<wivrn::vdf::keyvalue>> read_value(const char *& pos, const char * end)
{
	consume_whitespace(pos, end);

	if (*pos == '{')
		return read_dict(pos, end);
	else
		return read_string(pos, end);
}

} // namespace

bool wivrn::vdf::string::operator==(const char * b) const
{
	const char * a = data.begin();
	for (; a != data.end(); ++a, ++b)
	{
		if (*b == '\0')
			return false;
		if (*a == '\\')
		{
			++a;
			// This should be taken care of by the parser
			assert(a != data.end());
		}

		if (std::tolower(*b) != std::tolower(*a))
			return false;
	}
	return *b == '\0';
}

wivrn::vdf::root::root(const std::filesystem::path & file)
{
	boost::iostreams::mapped_file in{file};
	opaque = in;

	const char * pos = in.begin();
	key = read_string(pos, in.end());
	value = read_value(pos, in.end());
}

void dump(const wivrn::vdf::keyvalue & kv, int depth)
{
	for (int i = 0; i < depth; ++i)
		std::cout << '\t';
	std::cout << '"' << kv.key.data << "\": ";
	struct V
	{
		int depth;
		void operator()(const wivrn::vdf::string & s)
		{
			std::cout << '"' << s.data << '"';
		}
		void operator()(const std::vector<wivrn::vdf::keyvalue> & kv)
		{
			std::cout << '{' << std::endl;
			for (const auto & i: kv)
			{
				dump(i, depth + 1);
				if (&i != &kv.back())
					std::cout << ',';
				std::cout << std::endl;
			}
			for (int i = 0; i < depth; ++i)
				std::cout << '\t';
			std::cout << "}";
		}
	};
	std::visit(V{depth}, kv.value);
}

#ifdef WIVRN_BUILD_TEST
void dump(const wivrn::vdf::root & kv)
{
	std::cout << '{';
	dump(kv, 0);
	std::cout << '}';
}

int main(int argc, char ** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		wivrn::vdf::root doc{argv[i]};
		dump(doc);
	}
}
#endif
