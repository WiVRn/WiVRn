/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "vk/allocation.h"

#include <array>

namespace wivrn
{

class video_encoder_raw : public video_encoder
{
	std::array<buffer_allocation, num_slots> buffers;
	vk::Rect2D rect;

public:
	video_encoder_raw(wivrn_vk_bundle & vk, encoder_settings & settings, float fps, uint8_t stream_idx);

	std::pair<bool, vk::Semaphore> present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t frame_index) override;

	std::optional<data> encode(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot) override;
};
} // namespace wivrn
