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

#include "vk/vk_helpers.h"
#include "wivrn_packets.h"

#include <map>
#include <string>

namespace xrt::drivers::wivrn
{

struct encoder_settings : public to_headset::video_stream_description::item
{
	// encoder identifier, such as nvenc, vaapi or x264
	std::string encoder_name;
	uint64_t bitrate;                           // bit/s
	std::map<std::string, std::string> options; // additional encoder-specific configuration
	// encoders in the same group are executed in sequence
	int group = 0;
};

std::vector<encoder_settings> get_encoder_settings(vk_bundle * vk, uint16_t width, uint16_t height);

VkImageTiling get_required_tiling(vk_bundle * vk, const std::vector<encoder_settings> & settings);

VkExternalMemoryHandleTypeFlags get_handle_types(const std::vector<encoder_settings> & settings);

} // namespace xrt::drivers::wivrn
