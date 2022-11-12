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

#include "wivrn_client.h"
#include "spdlog/common.h"
#include "version.h"
#include "wivrn_packets.h"
#include "wivrn_serialization.h"
#include <algorithm>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/ipv6.h>
#include <net/if.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono_literals;

wivrn_session::wivrn_session(in6_addr address, int port) : control(address, port), stream()
{
	// TODO: negociate UDP port
	stream.bind(stream_port);
	stream.connect(address, stream_port);
	stream.set_receive_buffer_size(1024 * 1024 * 5);

	char buffer[100];
	spdlog::info("Connection to {}:{}", inet_ntop(AF_INET6, &address, buffer, sizeof(buffer)), port);
}

wivrn_session::wivrn_session(in_addr address, int port) : control(address, port), stream()
{
	// TODO: negociate UDP port
	stream.bind(stream_port);
	stream.connect(address, stream_port);
	stream.set_receive_buffer_size(1024 * 1024 * 5);

	char buffer[100];
	spdlog::info("Connection to {}:{}", inet_ntop(AF_INET, &address, buffer, sizeof(buffer)), port);
}

// std::unique_ptr<wivrn_session> wivrn_client::poll()
// {
// 	auto now = std::chrono::steady_clock::now();
//
// 	auto time_since_last_broadcast = now - last_broadcast;
//
// 	if (time_since_last_broadcast >= 1s)
// 	{
// 		last_broadcast = now;
//
// 		details::hash_context h;
// 		serialization_traits<from_headset::control_packets>::type_hash(h);
// 		serialization_traits<to_headset::control_packets>::type_hash(h);
// 		serialization_traits<from_headset::stream_packets>::type_hash(h);
// 		serialization_traits<to_headset::stream_packets>::type_hash(h);
//
// 		from_headset::client_announce_packet packet{
// 		        .magic = from_headset::client_announce_packet::magic_value,
// 		        .client_version = "WiVRn " GIT_VERSION,
// 		        .protocol_hash = h.hash,
// 		};
//
// 		for (auto & i: broadcasters)
// 			i.send(packet);
// 	}
//
// 	pollfd fds{};
// 	fds.events = POLLIN;
// 	fds.fd = listener.get_fd();
//
// 	int r = ::poll(&fds, 1, 0);
// 	if (r < 0)
// 		throw std::system_error(errno, std::system_category());
//
// 	if (r > 0 && (fds.revents & POLLIN))
// 	{
// 		auto [tcp, addr] = listener.accept();
// 		char buffer[100];
// 		spdlog::info("Connection from {}", inet_ntop(AF_INET6, &addr.sin6_addr, buffer, sizeof(buffer)));
// 		return std::make_unique<wivrn_session>(std::move(tcp), addr.sin6_addr);
// 	}
//
// 	return {};
// }

void wivrn_session::send_control(const from_headset::control_packets & packet)
{
	control.send(packet);
}

void wivrn_session::send_stream(const from_headset::stream_packets & packet)
{
	stream.send(packet);
}
