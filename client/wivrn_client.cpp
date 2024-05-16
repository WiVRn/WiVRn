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
#include "wivrn_packets.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/ipv6.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IPTOS_DSCP_EF
// constant is not defined in Android ip.h
#define IPTOS_DSCP_EF 0xb8
#endif

using namespace std::chrono_literals;

namespace
{
template <typename T>
void init_stream(T & stream)
{
	stream.set_receive_buffer_size(1024 * 1024 * 5);
	try
	{
		stream.set_tos(IPTOS_DSCP_EF);
	}
	catch (std::exception & e)
	{
		spdlog::warn("Failed to set IP ToS to Expedited Forwarding: {}", e.what());
	}
}
} // namespace

template <typename T>
void wivrn_session::handshake(T address)
{
	// Wait for handshake on control socket,
	// then send ours on stream or control socket,
	// finally wait for second server handshake
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = control.get_fd();

	auto timeout = std::chrono::steady_clock::now() + 5s;

	// Loop because TCP socket may return partial data
	while (true)
	{
		int r = ::poll(&fds, 1, 5000);
		if (r < 0)
			throw std::system_error(errno, std::system_category());

		if (r > 0 && (fds.revents & POLLIN))
		{
			auto packet = control.receive();
			if (not packet)
				continue;
			try
			{
				auto h = std::get<to_headset::handshake>(*packet);
				if (h.stream_port > 0)
				{
					stream = decltype(stream)();
					stream.connect(address, h.stream_port);
					init_stream(stream);
				}
				break;
			}
			catch (std::exception & e)
			{
				spdlog::error("Error when expecting handshake: {}", e.what());
				throw std::runtime_error("Invalid handshake received");
			}
		}

		if (std::chrono::steady_clock::now() >= timeout)
			throw std::runtime_error("No handshake received");
	}

	// may be on control socket if forced TCP
	send_stream(from_headset::handshake{});

	// Wait for second handshake
	while (true)
	{
		if (poll(
		            [](const auto && packet) { return std::is_same_v<std::remove_cvref_t<decltype(packet)>, to_headset::handshake>; },
		            std::chrono::milliseconds(100)))
			return;
		if (std::chrono::steady_clock::now() >= timeout)
			throw std::runtime_error("Failed to establish connection");

		// If using stream socket, the handshake might be lost
		if (stream)
			stream.send(from_headset::handshake{});
	}
}

wivrn_session::wivrn_session(in6_addr address, int port) :
        control(address, port), stream(-1), address(address)
{
	char buffer[100];
	spdlog::info("Connection to {}:{}", inet_ntop(AF_INET6, &address, buffer, sizeof(buffer)), port);
	handshake(address);
}

wivrn_session::wivrn_session(in_addr address, int port) :
        control(address, port), stream(-1), address(address)
{
	char buffer[100];
	spdlog::info("Connection to {}:{}", inet_ntop(AF_INET, &address, buffer, sizeof(buffer)), port);
	handshake(address);
}
