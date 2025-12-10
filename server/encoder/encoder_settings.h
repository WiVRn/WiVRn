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

#include "wivrn_packets.h"

#include <map>
#include <optional>
#include <string>

namespace wivrn
{
struct wivrn_vk_bundle;

struct encoder_settings
{
	uint16_t width;
	uint16_t height;
	video_codec codec; // left, right, alpha
	float fps;
	// encoder identifier, such as nvenc, vaapi or x264
	std::string encoder_name;
	uint64_t bitrate;                           // bit/s
	double bitrate_multiplier;                  // encoder bitrate / global bitrate
	std::map<std::string, std::string> options; // additional encoder-specific configuration
	// encoders in the same group are executed in sequence
	int group = 0;
	int bit_depth;
	std::optional<std::string> device;
};

std::array<encoder_settings, 3> get_encoder_settings(wivrn_vk_bundle &, const from_headset::headset_info_packet & info, const from_headset::settings_changed & settings);

void print_encoders(const std::array<wivrn::encoder_settings, 3> & encoders);

} // namespace wivrn
