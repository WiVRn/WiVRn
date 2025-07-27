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

#include "video_encoder_nvenc_shared_state.h"
#include "util/u_logging.h"

#include <bits/unique_lock.h>

void video_encoder_nvenc_shared_state::deleter::operator()(CudaFunctions * fn)
{
	cuda_free_functions(&fn);
}
void video_encoder_nvenc_shared_state::deleter::operator()(NvencFunctions * fn)
{
	nvenc_free_functions(&fn);
}

#define NVENC_CHECK_NOENCODER(x)                                                            \
	do                                                                                  \
	{                                                                                   \
		NVENCSTATUS status = x;                                                     \
		if (status != NV_ENC_SUCCESS)                                               \
		{                                                                           \
			U_LOG_E("NVENC Init Error: %s:%d: %d", __FILE__, __LINE__, status); \
			throw std::runtime_error("nvenc init error");                       \
		}                                                                           \
	} while (0)

#define CU_CHECK(x)                                                                                                \
	do                                                                                                         \
	{                                                                                                          \
		CUresult status = x;                                                                               \
		if (status != CUDA_SUCCESS)                                                                        \
		{                                                                                                  \
			const char * error_string;                                                                 \
			cuda_fn->cuGetErrorString(status, &error_string);                                          \
			U_LOG_E("CUDA Init Error: %s:%d: %s (%d)", __FILE__, __LINE__, error_string, (int)status); \
			throw std::runtime_error(std::string("CUDA init error: ") + error_string);                 \
		}                                                                                                  \
	} while (0)

std::shared_ptr<video_encoder_nvenc_shared_state> video_encoder_nvenc_shared_state::get()
{
	static std::weak_ptr<video_encoder_nvenc_shared_state> instance;
	static std::mutex m;
	std::unique_lock lock(m);
	auto s = instance.lock();
	if (s)
		return s;
	s.reset(new video_encoder_nvenc_shared_state());
	instance = s;
	return s;
}

video_encoder_nvenc_shared_state::video_encoder_nvenc_shared_state()
{
	CudaFunctions * tmp_cuda_fn = nullptr;
	if (cuda_load_functions(&tmp_cuda_fn, nullptr))
	{
		throw std::runtime_error("Failed to load CUDA functions");
	}
	cuda_fn.reset(tmp_cuda_fn);

	NvencFunctions * tmp_nvenc_fn = nullptr;
	if (nvenc_load_functions(&tmp_nvenc_fn, nullptr))
	{
		throw std::runtime_error("Failed to load nvenc functions");
	}
	nvenc_fn.reset(tmp_nvenc_fn);

	CU_CHECK(cuda_fn->cuInit(0));
	CU_CHECK(cuda_fn->cuCtxCreate(&cuda, 0, 0));

	fn.version = NV_ENCODE_API_FUNCTION_LIST_VER;
	NVENC_CHECK_NOENCODER(nvenc_fn->NvEncodeAPICreateInstance(&fn));
}

video_encoder_nvenc_shared_state::~video_encoder_nvenc_shared_state()
{
	if (cuda)
	{
		cuda_fn->cuCtxDestroy(cuda);
	}
}
