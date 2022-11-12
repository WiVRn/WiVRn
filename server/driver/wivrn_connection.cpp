// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "wivrn_connection.h"
#include <poll.h>

using namespace std::chrono_literals;

wivrn_connection::wivrn_connection(TCP&& tcp, in6_addr address) : control(std::move(tcp))
{
	stream.bind(stream_port);
	stream.connect(address, stream_port);
}

void
wivrn_connection::send_control(const to_headset::control_packets &packet)
{
	control.send(packet);
}

void
wivrn_connection::send_stream(const to_headset::stream_packets &packet)
{
	stream.send(packet);
}

std::optional<from_headset::stream_packets>
wivrn_connection::poll_stream(int timeout)
{
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = stream.get_fd();

	int r = ::poll(&fds, 1, timeout);
	if (r < 0)
		throw std::system_error(errno, std::system_category());

	if (r > 0 && (fds.revents & POLLIN)) {
		return stream.receive();
	}

	return {};
}

std::optional<from_headset::control_packets>
wivrn_connection::poll_control(int timeout)
{
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = control.get_fd();

	int r = ::poll(&fds, 1, timeout);
	if (r < 0)
		throw std::system_error(errno, std::system_category());

	if (r > 0 && (fds.revents & POLLIN)) {
		return control.receive();
	}

	return {};
}
