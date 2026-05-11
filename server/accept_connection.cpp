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

#include "util/u_logging.h"
#include "utils/overloaded.h"

#include "driver/configuration.h"
#include "driver/wivrn_session.h"
#include "wivrn_ipc.h"
#include "wivrn_sockets.h"

#include <sys/poll.h>

std::unique_ptr<wivrn::TCP> wivrn::accept_connection(wivrn_session & cnx, std::stop_token stop, std::function<void(wivrn_session &)> tick)
{
	wivrn_ipc_socket_monado->send(from_monado::headset_disconnected{});

	wivrn::TCPListener listener(configuration().port);

	pollfd fds[2]{
	        {.fd = listener.get_fd(), .events = POLLIN},
	        {.fd = wivrn_ipc_socket_monado->get_fd(), .events = POLLIN},
	};

	while (not stop.stop_requested())
	{
		if (poll(fds, std::size(fds), 100) < 0)
		{
			perror("poll");
			return {};
		}

		if (fds[0].revents & POLLIN)
		{
			wivrn_ipc_socket_monado->send(from_monado::headset_connected{});
			return std::make_unique<wivrn::TCP>(listener.accept().first);
		}

		if (fds[1].revents & POLLIN)
		{
			auto packet = receive_from_main();
			if (packet)
				std::visit(utils::overloaded{
				                   [&cnx](to_monado::stop) {
					                   // gets handled in wivrn_session::reconnect since we return nullptr
					                   U_LOG_I("Received stop packet during reconnect, stopping");
					                   cnx.request_stop();
				                   },
				                   [](auto &&) {
					                   // Ignore request when no headset is connected
				                   },
				           },
				           *packet);
		}

		if (tick)
			tick(cnx);
	}

	return {};
}
