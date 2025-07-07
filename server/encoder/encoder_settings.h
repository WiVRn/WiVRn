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
#include <vector>

namespace wivrn
{
struct wivrn_vk_bundle;

struct encoder_settings : public to_headset::video_stream_description::item
{
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

std::vector<encoder_settings> get_encoder_settings(wivrn_vk_bundle &, uint32_t & width, uint32_t & height, const from_headset::headset_info_packet & info);

void print_encoders(const std::vector<wivrn::encoder_settings> & encoders);

} // namespace wivrn
