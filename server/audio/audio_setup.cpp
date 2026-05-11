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

#include "audio_setup.h"

#include "util/u_logging.h"
#include "wivrn_config.h"

#if WIVRN_USE_PULSEAUDIO
#include "audio_pulse.h"
#endif

#if WIVRN_USE_PIPEWIRE
#include "audio_pipewire.h"
#endif

std::unique_ptr<wivrn::audio_device> wivrn::audio_device::create(
        const std::string & source_name,
        const std::string & source_description,
        const std::string & sink_name,
        const std::string & sink_description,
        const wivrn::from_headset::headset_info_packet & info,
        wivrn::wivrn_session & session)
{
#if WIVRN_USE_PIPEWIRE
	if (auto res = create_pipewire_handle(source_name, source_description, sink_name, sink_description, info, session))
		return res;
#endif

#if WIVRN_USE_PULSEAUDIO
	if (auto res = create_pulse_handle(source_name, source_description, sink_name, sink_description, info, session))
		return res;
#endif
	U_LOG_W("No audio backend available");
	return nullptr;
}
