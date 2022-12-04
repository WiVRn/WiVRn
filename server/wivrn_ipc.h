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

#include <array>
#include <memory>
#include <wivrn_sockets.h>

extern std::unique_ptr<xrt::drivers::wivrn::TCP> tcp;

void init_cleanup_functions();
void add_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata);
void remove_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata);
void run_cleanup_functions();
