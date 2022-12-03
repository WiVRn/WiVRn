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

#include "wivrn_packets.h"

#ifdef XR_USE_PLATFORM_ANDROID
#include "android/audio.h"
using audio = ::wivrn::android::audio;
#else

class audio
{
public:

	audio(const audio&) = delete;
	audio& operator=(const audio&) = delete;
	audio(const xrt::drivers::wivrn::to_headset::audio_stream_description&, const in_addr& ) {}
	audio(const xrt::drivers::wivrn::to_headset::audio_stream_description&, const in6_addr& ) {}
	~audio() = default;

	static void get_audio_description(xrt::drivers::wivrn::from_headset::headset_info_packet& info) {}
};

#endif
