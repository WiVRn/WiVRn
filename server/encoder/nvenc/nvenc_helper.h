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

#include "util/u_logging.h"
#include "video_encoder_nvenc_shared_state.h"
#include "wivrn_packets.h"
#include <ffnvcodec/nvEncodeAPI.h>
#include <memory>

#define NVENC_CHECK_NOENCODER(x)                                          \
	do                                                                \
	{                                                                 \
		NVENCSTATUS status = x;                                   \
		if (status != NV_ENC_SUCCESS)                             \
		{                                                         \
			U_LOG_E("%s:%d: %d", __FILE__, __LINE__, status); \
			throw std::runtime_error("nvenc error");          \
		}                                                         \
	} while (0)

#define NVENC_CHECK(x)                                                                                                                  \
	do                                                                                                                              \
	{                                                                                                                               \
		NVENCSTATUS status = x;                                                                                                 \
		if (status != NV_ENC_SUCCESS)                                                                                           \
		{                                                                                                                       \
			U_LOG_E("%s:%d: %d, %s", __FILE__, __LINE__, status, shared_state->fn.nvEncGetLastErrorString(session_handle)); \
			throw std::runtime_error("nvenc error");                                                                        \
		}                                                                                                                       \
	} while (0)

#define CU_CHECK(x)                                                                               \
	do                                                                                        \
	{                                                                                         \
		CUresult status = x;                                                              \
		if (status != CUDA_SUCCESS)                                                       \
		{                                                                                 \
			const char * error_string;                                                \
			shared_state->cuda_fn->cuGetErrorString(status, &error_string);           \
			U_LOG_E("%s:%d: %s (%d)", __FILE__, __LINE__, error_string, (int)status); \
			throw std::runtime_error(std::string("CUDA error: ") + error_string);     \
		}                                                                                 \
	} while (0)

bool operator==(const GUID & l, const GUID & r);

namespace wivrn
{
GUID encode_guid(wivrn::video_codec codec);

int get_caps(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encode_guid, NV_ENC_CAPS caps);
void check_encode_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encode_guid);
void check_preset_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encode_guid, GUID preset_guid);
void check_profile_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encodeGUID, GUID profileGUID, std::string err_msg = "GPU doesn't support selected encoding profile.");
} // namespace wivrn
