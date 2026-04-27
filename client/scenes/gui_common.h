/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2026  Patrick Nicolas <patricknicolas@laposte.net>
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

class configuration;
class imgui_context;

namespace xr
{
class instance;
class session;
} // namespace xr

namespace wivrn
{

namespace gui
{

bool refresh_rate(
        xr::instance & instance,
        xr::session & session,
        imgui_context & imgui_ctx,
        configuration & config);

bool post_processing(
        imgui_context & imgui_ctx,
        configuration & config);

} // namespace gui
} // namespace wivrn
