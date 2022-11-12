// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "vk/vk_helpers.h"
#include "wivrn_packets.h"

#include <map>
#include <string>

namespace xrt::drivers::wivrn {

struct encoder_settings : public to_headset::video_stream_description::item
{
	// encoder identifier, such as nvenc, vaapi or x264
	std::string encoder_name;
	uint64_t bitrate;                           // bit/s
	std::map<std::string, std::string> options; // additional encoder-specific configuration
	// encoders in the same group are executed in sequence
	int group = 0;
};


std::vector<encoder_settings>
get_encoder_settings(vk_bundle *vk, uint16_t width, uint16_t height);


VkImageTiling
get_required_tiling(vk_bundle *vk, const std::vector<encoder_settings> &settings);

} // namespace xrt::drivers::wivrn
