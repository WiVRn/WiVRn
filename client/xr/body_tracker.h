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

#include "fb_body_tracker.h"
#include "htc_body_tracker.h"
#include "pico_body_tracker.h"
#include "xr/system.h"

#include <utility>
#include <variant>
#include <vector>
#include <openxr/openxr.h>

namespace xr
{

class instance;
class session;

using body_tracker = std::variant<std::monostate, xr::fb_body_tracker, xr::htc_body_tracker, xr::pico_body_tracker>;

body_tracker make_body_tracker(xr::instance &, xr::system &, xr::session &, std::vector<std::pair<XrPath, xr::space>> & generic_trackers, bool full_body, bool hips);

body_tracker_type body_tracker_supported(xr::instance &, xr::system &);

} // namespace xr
