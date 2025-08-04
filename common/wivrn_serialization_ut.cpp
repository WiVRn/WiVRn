/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "wivrn_serialization.h"

using namespace wivrn;

namespace
{
template <typename T>
constexpr uint64_t hash(T s)
{
	return details::hash_context{}.feed(s);
}

static_assert(hash("test") == 0xf9e6e6ef197c2b25);
static_assert(hash(1234) == 0x1fabbdf10314a21d);
static_assert(hash(-1234) == 0x77fa1c9653db5a84);
static_assert(hash(0) == 0xaf63ad4c86019caf);

static_assert(serialization_type_hash<bool>(0) == hash("uint8"));
static_assert(serialization_type_hash<int>(0) == hash("int32"));
static_assert(serialization_type_hash<float>(0) == hash("float32"));
static_assert(serialization_type_hash<double>(0) == hash("float64"));
static_assert(serialization_type_hash<std::chrono::nanoseconds>(0) == hash("duration<int64,1/1000000000>"));
static_assert(serialization_type_hash<std::optional<int>>(0) == hash("optional<int32>"));
static_assert(serialization_type_hash<std::vector<int>>(0) == hash("vector<int32>"));
static_assert(serialization_type_hash<std::array<int, 42>>(0) == hash("array<int32,42>"));
static_assert(serialization_type_hash<std::string>(0) == hash("string"));
static_assert(serialization_type_hash<std::variant<int, float>>(0) == hash("variant<int32,float32>"));

struct test
{
	int x;
	float y;
};
static_assert(serialization_type_hash<test>(0) == hash("structure{int32,float32}"));
} // namespace
