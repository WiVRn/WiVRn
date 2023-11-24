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
#include <poll.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono_literals;

void wivrn_session::handshake()
{
	//Wait for handshake on control socket, then send ours on stream socket
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = control.get_fd();

	auto timeout = std::chrono::steady_clock::now() + 5s;

	// Loop because TCP socket may return partial data
	while (std::chrono::steady_clock::now() < timeout)
	{
		int r = ::poll(&fds, 1, 5000);
		if (r < 0)
			throw std::system_error(errno, std::system_category());

		if (r > 0 && (fds.revents & POLLIN))
		{
			auto packet = control.receive();
			if (not packet)
				continue;
			if (std::holds_alternative<to_headset::handshake>(*packet))
			{
				// If this packet is lost, a tracking packet will do the job to establish the connection
				send_stream(from_headset::handshake{});
				return;
			}

			throw std::runtime_error("Invalid handshake received");
		}
	}

	throw std::runtime_error("No handshake received");
}

wivrn_session::wivrn_session(in6_addr address, int port) :
        control(address, port), stream(), address(address)
{
	stream.connect(address, port);
	stream.set_receive_buffer_size(1024 * 1024 * 5);

	char buffer[100];
	spdlog::info("Connection to {}:{}", inet_ntop(AF_INET6, &address, buffer, sizeof(buffer)), port);
	handshake();
}

wivrn_session::wivrn_session(in_addr address, int port) :
        control(address, port), stream(), address(address)
{
	stream.connect(address, port);
	stream.set_receive_buffer_size(1024 * 1024 * 5);

	char buffer[100];
	spdlog::info("Connection to {}:{}", inet_ntop(AF_INET, &address, buffer, sizeof(buffer)), port);
	handshake();
}
