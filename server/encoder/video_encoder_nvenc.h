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
#include <array>
#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_loader.h>
#include <ffnvcodec/nvEncodeAPI.h>
#include <vulkan/vulkan_raii.hpp>

namespace xrt::drivers::wivrn
{

class VideoEncoderNvenc : public VideoEncoder
{
public:
	struct deleter
	{
		void operator()(CudaFunctions * fn);
		void operator()(NvencFunctions * fn);
	};

private:
	wivrn_vk_bundle & vk;
	// relevant part of the input image to encode
	vk::Rect2D rect;

	std::unique_ptr<CudaFunctions, deleter> cuda_fn;
	std::unique_ptr<NvencFunctions, deleter> nvenc_fn;
	NV_ENCODE_API_FUNCTION_LIST fn;
	CUcontext cuda;
	void * session_handle = nullptr;
	NV_ENC_OUTPUT_PTR bitstreamBuffer;

	struct in_t
	{
		vk::raii::Buffer yuv = nullptr;
		vk::raii::DeviceMemory mem = nullptr;
		NV_ENC_REGISTERED_PTR nvenc_resource;
	};
	std::array<in_t, num_slots> in;

	uint32_t width;
	uint32_t height;
	float fps;
	int bitrate;

public:
	VideoEncoderNvenc(wivrn_vk_bundle & vk, encoder_settings & settings, float fps);
	~VideoEncoderNvenc();

	void present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot) override;
	std::optional<data> encode(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot) override;

	static std::array<int, 2> get_max_size(video_codec);
};

} // namespace xrt::drivers::wivrn
