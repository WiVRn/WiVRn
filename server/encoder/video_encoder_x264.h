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

#include "video_encoder.h"
#include "vk/allocation.h"
#include "x264.h"

#include <list>
#include <mutex>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{

class video_encoder_x264 : public video_encoder
{
	x264_param_t param = {};
	x264_t * enc;
	bool control;

	x264_picture_t pic_out = {};

	struct in_t
	{
		x264_picture_t pic;
		buffer_allocation luma;
		buffer_allocation chroma;
	};
	std::array<in_t, num_slots> in;
	uint32_t chroma_width;

	struct pending_nal
	{
		int first_mb;
		int last_mb;
		std::vector<uint8_t> data;
	};

	std::mutex mutex;
	int next_mb;
	int num_mb; // Number of macroblocks in a frame
	std::list<pending_nal> pending_nals;

public:
	video_encoder_x264(wivrn_vk_bundle & vk, const encoder_settings & settings, uint8_t stream_idx);

	std::pair<bool, vk::Semaphore> present_image(vk::Image y_cbcr, bool transferred, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t frame_index) override;

	std::optional<data> encode(uint8_t slot, uint64_t frame_index) override;

	~video_encoder_x264();

private:
	static void ProcessCb(x264_t * h, x264_nal_t * nal, void * opaque);

	void ProcessNal(pending_nal && nal);

	void InsertInPendingNal(pending_nal && nal);
};

} // namespace wivrn
