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

#include "ffmpeg_helper.h"
#include "video_encoder_ffmpeg.h"

#include <array>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{
struct encoder_settings;

class video_encoder_va : public video_encoder_ffmpeg
{
	vk_bundle & vk;
	vk::raii::CommandPool cmd_pool;
	struct in_t
	{
		av_frame_ptr va_frame;
		av_frame_ptr drm_frame;
		vk::raii::Fence fence = nullptr;
		vk::raii::CommandBuffer cmd = nullptr;
		vk::raii::Image luma = nullptr;
		vk::raii::Image chroma = nullptr;
		std::vector<vk::raii::DeviceMemory> mem;
	};
	av_buffer_ptr drm_frame_ctx;
	std::array<in_t, num_slots> in;

public:
	video_encoder_va(wivrn::vk_bundle &, const wivrn::encoder_settings & settings, uint8_t stream_index);

	void present_image(vk::Image y_cbcr, vk::SemaphoreSubmitInfo info, uint8_t slot, uint64_t frame_index) override;

protected:
	void push_frame(bool idr, uint8_t slot) override;
};
} // namespace wivrn
