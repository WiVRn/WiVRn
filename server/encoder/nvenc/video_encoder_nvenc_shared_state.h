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

#include <memory>

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_loader.h>
#include <ffnvcodec/nvEncodeAPI.h>

class video_encoder_nvenc_shared_state
{
public:
	struct deleter
	{
		void operator()(CudaFunctions * fn);
		void operator()(NvencFunctions * fn);
	};

	static std::shared_ptr<video_encoder_nvenc_shared_state> get();
	std::unique_ptr<CudaFunctions, deleter> cuda_fn;
	std::unique_ptr<NvencFunctions, deleter> nvenc_fn;
	NV_ENCODE_API_FUNCTION_LIST fn = {};
	CUcontext cuda = nullptr;

	video_encoder_nvenc_shared_state();
	~video_encoder_nvenc_shared_state();

	video_encoder_nvenc_shared_state(const video_encoder_nvenc_shared_state &) = delete;
	video_encoder_nvenc_shared_state & operator=(const video_encoder_nvenc_shared_state &) = delete;
	video_encoder_nvenc_shared_state(video_encoder_nvenc_shared_state &&) = delete;
	video_encoder_nvenc_shared_state & operator=(video_encoder_nvenc_shared_state &&) = delete;
};
