/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "accept_connection.h"

#include "wivrn_packets.h"
#include "wivrn_sockets.h"

#include <sys/poll.h>

extern int control_pipe_fd;

static void report_status(int status)
{
	uint8_t status_byte = status;
	write(control_pipe_fd, &status_byte, sizeof(status_byte));
}

std::unique_ptr<xrt::drivers::wivrn::TCP> accept_connection(int watch_fd, std::function<bool()> quit)
{
	report_status(0);

	xrt::drivers::wivrn::TCPListener listener(xrt::drivers::wivrn::default_port);

	pollfd fds[2]{
	        {.fd = watch_fd, .events = POLLIN},
	        {.fd = listener.get_fd(), .events = POLLIN},
	};

	while (not(quit and quit()))
	{
		if (poll(fds, std::size(fds), 100) < 0)
		{
			perror("poll");
			return {};
		}

		if (fds[0].revents & POLLIN)
			return {};

		if (fds[1].revents & POLLIN)
		{
			report_status(1);
			return std::make_unique<xrt::drivers::wivrn::TCP>(listener.accept().first);
		}
	}

	return {};
}
