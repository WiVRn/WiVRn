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

#include "wivrn_connection.h"
#include "util/u_logging.h"
#include <arpa/inet.h>
#include <poll.h>

using namespace std::chrono_literals;

wivrn_connection::wivrn_connection(TCP && tcp) :
        control(std::move(tcp))
{
	sockaddr_in6 server_address;
	socklen_t len = sizeof(server_address);
	if (getsockname(control.get_fd(), (sockaddr*)&server_address, &len) < 0)
	{
		throw std::system_error(errno, std::system_category(), "Cannot get socket port");
	}
	int port = ntohs(((struct sockaddr_in6*)&server_address)->sin6_port);

	sockaddr_in6 client_address;
	len = sizeof(client_address);
	if (getpeername(control.get_fd(), (sockaddr *)&client_address, &len) < 0)
	{
		throw std::system_error(errno, std::system_category(), "Cannot get client address");
	}

	// Wait for client to send handshake on UDP
	auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(10);

	stream.bind(port);

	control.send(to_headset::handshake{});

	while (true)
	{
		pollfd fds[2] = {};
		fds[0].events = POLLIN;
		fds[0].fd = stream.get_fd();
		fds[1].events = 0; // only check for errors on control socket
		fds[1].fd = control.get_fd();

		int r = ::poll(fds, std::size(fds), 1000);
		if (r < 0)
			throw std::system_error(errno, std::system_category());

		if (fds[0].revents & (POLLHUP | POLLERR))
			throw std::runtime_error("Error on stream socket");

		if (fds[1].revents & (POLLHUP | POLLERR))
			throw std::runtime_error("Error on control socket");

		if (fds[0].revents & POLLIN)
		{
			auto [packet, peer_addr] = stream.receive_from_raw();
			if (memcmp(&peer_addr.sin6_addr, &client_address.sin6_addr, sizeof(peer_addr.sin6_addr)) == 0)
			{
				int client_port = htons(peer_addr.sin6_port);
				stream.connect(peer_addr.sin6_addr, client_port);
				U_LOG_D("Stream socket connected, client port %d", client_port);
				stream.set_send_buffer_size(1024 * 1024 * 5);
				break;
			}

		}

		if (std::chrono::steady_clock::now() > timeout)
		{
			throw std::runtime_error("No handshake received on stream socket");
		}
	}

	try
	{
		// Set Expedited forwarding https://datatracker.ietf.org/doc/html/rfc3246
		stream.set_tos(0b101110);
	}
	catch (std::exception& e)
	{
		U_LOG_I("Failed to set IP ToS to Expedited Forwarding: %s", e.what());
	}
}

std::optional<from_headset::packets> wivrn_connection::poll_control(int timeout)
{
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = control.get_fd();

	int r = ::poll(&fds, 1, timeout);
	if (r < 0)
		throw std::system_error(errno, std::system_category());

	if (r > 0 && (fds.revents & POLLIN))
	{
		return control.receive();
	}

	return {};
}
