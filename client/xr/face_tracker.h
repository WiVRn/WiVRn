/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "fb_face_tracker2.h"
#include "htc_face_tracker.h"
#include "pico_face_tracker.h"
#include "xr/system.h"

#include <variant>

namespace xr
{

class instance;
class session;

using face_tracker = std::variant<std::monostate, xr::fb_face_tracker2, xr::htc_face_tracker, xr::pico_face_tracker>;

face_tracker make_face_tracker(xr::instance &, xr::system &, xr::session &);

face_tracker_type face_tracker_supported(xr::instance &, xr::system &);

} // namespace xr
