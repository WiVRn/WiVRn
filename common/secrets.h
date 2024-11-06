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

#pragma once

#include "crypto.h"
#include <array>
#include <cstdint>
#include <string>

struct secrets
{
	std::array<std::uint8_t, 16> control_key;
	std::array<std::uint8_t, 16> control_iv_to_headset;
	std::array<std::uint8_t, 16> control_iv_from_headset;

	std::array<std::uint8_t, 16> stream_key;
	std::array<std::uint8_t, 8> stream_iv_header_to_headset;
	std::array<std::uint8_t, 8> stream_iv_header_from_headset;

	secrets(crypto::key & my_key, crypto::key & peer_key, const std::string & pin);
};
