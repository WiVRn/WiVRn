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
#include <memory>
#include <wivrn_sockets.h>

extern std::unique_ptr<xrt::drivers::wivrn::TCP> tcp;

namespace from_monado
{
struct headsdet_connected
{};
struct headsdet_disconnected
{};

using packets = std::variant<xrt::drivers::wivrn::from_headset::headset_info_packet, headsdet_connected, headsdet_disconnected>;
} // namespace from_monado

namespace to_monado
{
struct disconnect
{};

using packets = std::variant<disconnect>;
} // namespace to_monado

extern std::optional<xrt::drivers::wivrn::typed_socket<xrt::drivers::wivrn::UnixDatagram, to_monado::packets, from_monado::packets>> wivrn_ipc_socket_monado;

std::optional<to_monado::packets> receive_from_main();
template <typename T>
void send_to_main(T && packet)
{
	wivrn_ipc_socket_monado->send(std::forward<T>(packet));
}

void init_cleanup_functions();
void add_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata);
void remove_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata);
void run_cleanup_functions();
