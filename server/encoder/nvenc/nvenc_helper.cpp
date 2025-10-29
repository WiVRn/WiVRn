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

#include "nvenc_helper.h"

bool operator==(const GUID & l, const GUID & r)
{
	return l.Data1 == r.Data1 and
	       l.Data2 == r.Data2 and
	       l.Data3 == r.Data3 and
	       std::ranges::equal(l.Data4, r.Data4);
}

namespace wivrn
{
GUID encode_guid(wivrn::video_codec codec)
{
	switch (codec)
	{
		case wivrn::h264:
			return NV_ENC_CODEC_H264_GUID;
		case wivrn::h265:
			return NV_ENC_CODEC_HEVC_GUID;
		case wivrn::av1:
			return NV_ENC_CODEC_AV1_GUID;
		case wivrn::raw:
			break;
	}
	throw std::out_of_range("Invalid codec " + std::to_string(codec));
}

void check_encode_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encode_guid)
{
	uint32_t count;
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeGUIDCount(session_handle, &count));

	std::vector<GUID> encodeGUIDs(count);
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeGUIDs(session_handle, encodeGUIDs.data(), count, &count));

	if (!std::ranges::contains(encodeGUIDs, encode_guid))
	{
		throw std::runtime_error("nvenc: GPU doesn't support selected codec.");
	}
}

void check_preset_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encode_guid, GUID preset_guid)
{
	uint32_t count;
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetCount(session_handle, encode_guid, &count));

	std::vector<GUID> presetGUIDs(count);
	NVENC_CHECK(shared_state->fn.nvEncGetEncodePresetGUIDs(session_handle, encode_guid, presetGUIDs.data(), count, &count));

	if (!std::ranges::contains(presetGUIDs, preset_guid))
	{
		throw std::runtime_error("nvenc: Internal error. GPU doesn't support selected encoder preset.");
	}
}

void check_profile_guid_supported(std::shared_ptr<video_encoder_nvenc_shared_state> shared_state, void * session_handle, GUID encodeGUID, GUID profileGUID, std::string err_msg)
{
	uint32_t count;
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeProfileGUIDCount(session_handle, encodeGUID, &count));

	std::vector<GUID> profileGUIDs(count);
	NVENC_CHECK(shared_state->fn.nvEncGetEncodeProfileGUIDs(session_handle, encodeGUID, profileGUIDs.data(), count, &count));

	if (!std::ranges::contains(profileGUIDs, profileGUID))
	{
		throw std::runtime_error("nvenc: " + err_msg);
	}
}
} // namespace wivrn
