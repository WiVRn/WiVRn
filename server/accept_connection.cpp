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

#include "avahi_publisher.h"
#include "driver/configuration.h"
#include "hostname.h"
#include "version.h"
#include "wivrn_packets.h"
#include "wivrn_sockets.h"

#include <map>
#include <string>

static void avahi_set_bool_callback(AvahiWatch * w, int fd, AvahiWatchEvent event, void * userdata)
{
	bool * flag = (bool *)userdata;
	*flag = true;
}

std::unique_ptr<xrt::drivers::wivrn::TCP> accept_connection(int watch_fd, std::function<bool()> quit)
{
	char protocol_string[17];
	sprintf(protocol_string, "%016lx", xrt::drivers::wivrn::protocol_version);

	std::map<std::string, std::string> TXT = {
	        {"protocol", protocol_string},
	        {"version", xrt::drivers::wivrn::git_version},
	        {"cookie", server_cookie()},
	};

	avahi_publisher publisher(hostname().c_str(), "_wivrn._tcp", xrt::drivers::wivrn::default_port, TXT);

	xrt::drivers::wivrn::TCPListener listener(xrt::drivers::wivrn::default_port);
	bool client_connected = false;
	bool fd_triggered = false;

	AvahiWatch * watch_listener = publisher.watch_new(listener.get_fd(), AVAHI_WATCH_IN, &avahi_set_bool_callback, &client_connected);
	AvahiWatch * watch_user = publisher.watch_new(watch_fd, AVAHI_WATCH_IN, &avahi_set_bool_callback, &fd_triggered);

	while (not(client_connected or fd_triggered or (quit and quit())))
	{
		if (not publisher.iterate(quit ? 100 : -1))
			break;
	}

	publisher.watch_free(watch_listener);
	publisher.watch_free(watch_user);

	if (client_connected)
		return std::make_unique<xrt::drivers::wivrn::TCP>(listener.accept().first);

	return nullptr;
}
