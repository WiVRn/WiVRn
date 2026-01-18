/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "audio_setup.h"

namespace wivrn
{

std::unique_ptr<audio_device> create_pulse_handle(
        const std::string & source_name,
        const std::string & source_description,
        const std::string & sink_name,
        const std::string & sink_description,
        const wivrn::from_headset::headset_info_packet & info,
        wivrn::wivrn_session & session);
}
