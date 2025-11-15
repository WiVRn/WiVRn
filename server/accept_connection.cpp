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

#include "server/ipc_server_interface.h"

#include "utils/overloaded.h"

#include "wivrn_config.h"
#include "wivrn_ipc.h"
#include "wivrn_sockets.h"

#include <sys/poll.h>

std::unique_ptr<wivrn::TCP> wivrn::accept_connection(ipc_server * server, int watch_fd, std::function<bool()> quit)
{
	wivrn_ipc_socket_monado->send(from_monado::headset_disconnected{});

	wivrn::TCPListener listener(wivrn::default_port);

	pollfd fds[3]{
	        {.fd = watch_fd, .events = POLLIN},
	        {.fd = listener.get_fd(), .events = POLLIN},
	        {.fd = wivrn_ipc_socket_monado->get_fd(), .events = POLLIN},
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
			wivrn_ipc_socket_monado->send(from_monado::headset_connected{});
			return std::make_unique<wivrn::TCP>(listener.accept().first);
		}

		if (fds[2].revents & POLLIN)
		{
			auto packet = receive_from_main();
			if (packet)
				std::visit(utils::overloaded{
				                   [server](to_monado::stop) {
					                   ipc_server_stop(server);
				                   },
				                   [](auto &&) {
					                   // Ignore request when no headset is connected
				                   },
				           },
				           *packet);
		}
	}

	return {};
}
