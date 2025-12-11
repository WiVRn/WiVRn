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

#include "json_string.h"

std::string json_string(const std::string_view & in)
{
	std::string out;
	out.reserve(in.size() + 2);

	out += '"';

	for (char c: in)
	{
		switch (c)
		{
			case '\b':
				out += "\\b";
				break;

			case '\f':
				out += "\\f";
				break;

			case '\n':
				out += "\\n";
				break;

			case '\r':
				out += "\\r";
				break;

			case '\t':
				out += "\\t";
				break;

			case '"':
				out += "\\\"";
				break;

			case '\\':
				out += "\\\\";
				break;

			default:
				out += c;
				break;
		}
	}

	out += '"';

	return out;
}
