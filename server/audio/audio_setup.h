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

#include <memory>
#include <string>

#include "wivrn_packets.h"

namespace wivrn
{
class wivrn_session;

struct audio_device
{
	// unpublishes the device
	virtual ~audio_device() = default;

	virtual wivrn::to_headset::audio_stream_description description() const = 0;

	virtual void process_mic_data(wivrn::audio_data &&) = 0;

	static std::unique_ptr<audio_device> create(
	        const std::string & source_name,
	        const std::string & source_description,
	        const std::string & sink_name,
	        const std::string & sink_description,
	        const wivrn::from_headset::headset_info_packet & info,
	        wivrn::wivrn_session & session);
};
} // namespace wivrn
