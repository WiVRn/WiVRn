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
#include <poll.h>

using namespace wivrn;

class wivrn_session
{
public:
	using control_socket_t = typed_socket<TCP, to_headset::packets, from_headset::packets>;
	using stream_socket_t = typed_socket<UDP, to_headset::packets, from_headset::packets>;

private:
	control_socket_t control;
	stream_socket_t stream;

	template <typename T>
	void handshake(T address, bool tcp_only);

public:
	std::variant<in_addr, in6_addr> address;

	wivrn_session(in6_addr address, int port, bool tcp_only);
	wivrn_session(in_addr address, int port, bool tcp_only);
	wivrn_session(const wivrn_session &) = delete;
	wivrn_session & operator=(const wivrn_session &) = delete;

	template <typename T>
	void send_control(T && packet)
	{
		control.send(std::forward<T>(packet));
	}

	template <typename T>
	void serialize_control(serialization_packet & p, const T & data)
	{
		control.serialize<T>(p, data);
	}

	template <typename T>
	void send_stream(T && packet)
	{
		if (stream)
			stream.send(std::forward<T>(packet));
		else
			control.send(std::forward<T>(packet));
	}

	template <typename T>
	void serialize_stream(serialization_packet & p, const T & data)
	{
		stream.serialize<T>(p, data);
	}

	template <typename T>
	int poll(T && visitor, std::chrono::milliseconds timeout)
	{
		pollfd fds[2] = {};
		fds[0].events = POLLIN;
		fds[0].fd = stream.get_fd();
		fds[1].events = POLLIN;
		fds[1].fd = control.get_fd();

		while (auto packet = stream.receive_pending())
			std::visit(std::forward<T>(visitor), std::move(*packet));
		while (auto packet = control.receive_pending())
			std::visit(std::forward<T>(visitor), std::move(*packet));

		int r = ::poll(fds, std::size(fds), timeout.count());
		if (r < 0)
			throw std::system_error(errno, std::system_category());

		if (fds[0].revents & (POLLHUP | POLLERR))
			throw std::runtime_error("Error on stream socket");

		if (fds[1].revents & (POLLHUP | POLLERR))
			throw std::runtime_error("Error on control socket");

		if (fds[0].revents & POLLIN)
		{
			auto packet = stream.receive();
			if (packet)
				std::visit(std::forward<T>(visitor), std::move(*packet));
		}

		if (fds[1].revents & POLLIN)
		{
			auto packet = control.receive();
			if (packet)
				std::visit(std::forward<T>(visitor), std::move(*packet));
		}

		return r;
	}

	uint64_t bytes_received() const
	{
		return control.bytes_received() + stream.bytes_received();
	}

	uint64_t bytes_sent() const
	{
		return control.bytes_sent() + stream.bytes_sent();
	}
};
