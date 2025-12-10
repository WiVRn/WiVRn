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

#include "wivrn_ipc.h"
#include "wivrn_packets.h"
#include "wivrn_sockets.h"

#include <atomic>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <stop_token>
#include <system_error>

namespace wivrn
{
class incorrect_pin : public std::runtime_error
{
public:
	incorrect_pin();
};

class wivrn_connection
{
public:
	enum class encryption_state
	{
		disabled,
		enabled,
		pairing,
	};

private:
	typed_socket<TCP, from_headset::packets, to_headset::packets> control;
	typed_socket<UDP, from_headset::packets, to_headset::packets> stream;
	std::atomic<bool> active = false;
	std::string pin;
	encryption_state state;

	from_headset::headset_info_packet info_packet;

	void init(std::stop_token stop_token, std::function<void()> tick = []() {});

public:
	wivrn_connection(std::stop_token stop_token, encryption_state state, std::string pin, TCP && tcp);
	wivrn_connection(const wivrn_connection &) = delete;
	wivrn_connection & operator=(const wivrn_connection &) = delete;

	bool has_stream() const
	{
		return stream;
	}

	bool is_active()
	{
		return active;
	}
	void reset(TCP && tcp, std::function<void()> tick = []() {});
	void shutdown();

	template <typename T>
	void send_control(T && packet)
	{
		try
		{
			if (active)
				control.send(std::forward<T>(packet));
		}
		catch (...)
		{
			active = false;
			throw;
		}
	}

	template <typename T>
	void send_stream(T && packet)
	{
		try
		{
			if (active)
			{
				if (stream)
					stream.send(std::forward<T>(packet));
				else
					control.send(std::forward<T>(packet));
			}
		}
		catch (...)
		{
			active = false;
			throw;
		}
	}

	std::optional<from_headset::packets> poll_control(int timeout);

	const from_headset::headset_info_packet & info()
	{
		return info_packet;
	}

	template <typename T>
	int poll(T && visitor, int timeout)
	{
		pollfd fds[3] = {};
		fds[0].events = POLLIN;
		fds[0].fd = stream.get_fd();
		fds[1].events = POLLIN;
		fds[1].fd = control.get_fd();
		fds[2].fd = wivrn_ipc_socket_monado->get_fd();
		fds[2].events = POLLIN;

		while (auto packet = stream.receive_pending())
			std::visit(std::forward<T>(visitor), std::move(*packet));
		while (auto packet = control.receive_pending())
			std::visit(std::forward<T>(visitor), std::move(*packet));

		int r = ::poll(fds, std::size(fds), timeout);
		if (r < 0)
			throw std::system_error(errno, std::system_category());

		if (fds[0].revents & (POLLHUP | POLLERR))
			throw std::runtime_error("Error on stream socket");

		if (fds[1].revents & (POLLHUP | POLLERR))
			throw std::runtime_error("Error on control socket");

		if (fds[2].revents & (POLLHUP | POLLERR))
			throw std::runtime_error("Error on IPC socket");

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

		if (fds[2].revents & POLLIN)
		{
			auto packet = receive_from_main();
			if (packet)
				std::visit(std::forward<T>(visitor), std::move(*packet));
		}
		return r;
	}
};
} // namespace wivrn
