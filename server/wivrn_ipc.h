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
#include "wivrn_sockets.h"

#include <memory>
#include <optional>
#include <stdint.h>
#include <variant>

namespace wivrn
{
class wivrn_connection;
}

extern std::unique_ptr<wivrn::wivrn_connection> connection;

namespace from_monado
{
struct headset_connected
{};

struct headset_disconnected
{};

struct bitrate_changed
{
	uint32_t bitrate_bps;
};

struct server_error
{
	std::string where;
	std::string message;
};

using packets = std::variant<
        wivrn::from_headset::headset_info_packet,
        wivrn::from_headset::start_app,
        headset_connected,
        headset_disconnected,
        bitrate_changed,
        server_error>;
} // namespace from_monado

namespace to_monado
{
struct stop
{};

struct disconnect
{};

struct set_bitrate
{
	uint32_t bitrate_bps;
};

using packets = std::variant<stop, disconnect, set_bitrate>;
} // namespace to_monado

extern std::optional<wivrn::typed_socket<wivrn::UnixDatagram, to_monado::packets, from_monado::packets>> wivrn_ipc_socket_monado;

std::optional<to_monado::packets> receive_from_main();
template <typename T>
void send_to_main(T packet)
{
	wivrn_ipc_socket_monado->send(std::move(packet));
}

void init_cleanup_functions();
void add_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata);
void remove_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata);
void run_cleanup_functions();
