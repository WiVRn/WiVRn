/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "video_encoder.h"
#include "video_encoder_nvenc_shared_state.h"
#include <array>
#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_loader.h>
#include <ffnvcodec/nvEncodeAPI.h>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{

class video_encoder_nvenc : public video_encoder
{
private:
	wivrn_vk_bundle & vk;
	// relevant part of the input image to encode
	vk::Rect2D rect;

	std::shared_ptr<video_encoder_nvenc_shared_state> shared_state;

	void * session_handle = nullptr;
	NV_ENC_OUTPUT_PTR bitstreamBuffer;

	struct in_t
	{
		vk::raii::Buffer yuv = nullptr;
		vk::raii::DeviceMemory mem = nullptr;
		NV_ENC_REGISTERED_PTR nvenc_resource;
	};
	std::array<in_t, num_slots> in;

	float fps;
	int bitrate;

public:
	video_encoder_nvenc(wivrn_vk_bundle & vk, encoder_settings & settings, float fps, uint8_t stream_idx);
	~video_encoder_nvenc();

	std::pair<bool, vk::Semaphore> present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t frame_index) override;
	std::optional<data> encode(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot) override;

	static std::array<int, 2> get_max_size(video_codec);
};

} // namespace wivrn
