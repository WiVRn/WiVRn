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
#include "version.h"
#include "wivrn_packets.h"
#include "wivrn_serialization.h"
#include <algorithm>
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

static std::vector<std::pair<int, std::string>> get_network_interfaces()
{
	ifaddrs * addresses;
	if (getifaddrs(&addresses) < 0)
	{
		spdlog::warn("Cannot get network interfaces: {}", strerror(errno));
		return {};
	}

	int fd = socket(AF_INET6, SOCK_DGRAM, 0);

	const int required_flags = IFF_UP | IFF_MULTICAST;
	const int forbidden_flags = IFF_LOOPBACK;

	std::vector<std::pair<int, std::string>> interfaces;
	for (ifaddrs * i = addresses; i; i = i->ifa_next)
	{
		if (i->ifa_addr == nullptr)
			continue;

		if (i->ifa_addr->sa_family != AF_INET6)
			continue;

		if ((i->ifa_flags & required_flags) != required_flags)
			continue;

		if ((i->ifa_flags & forbidden_flags) != 0)
			continue;

		if (std::any_of(interfaces.begin(), interfaces.end(), [&](const auto & j) { return j.second == i->ifa_name; }))
			continue;

		ifreq interface {};
		strncpy(interface.ifr_name, i->ifa_name, sizeof(interface.ifr_name) - 1);
		ioctl(fd, SIOCGIFINDEX, &interface);

		interfaces.emplace_back(interface.ifr_ifindex, i->ifa_name);
	}

	close(fd);
	freeifaddrs(addresses);

	return interfaces;
}

wivrn_client::wivrn_client() :
        listener(control_port)
{
	auto interfaces = get_network_interfaces();

	if (interfaces.empty())
	{
		spdlog::critical("No suitable network interface found");
		throw std::runtime_error("No suitable network interface found"); // TODO class exception
	}

	for (auto [id, name]: interfaces)
	{
		auto & socket = broadcasters.emplace_back();
		if (setsockopt(socket.get_fd(), IPPROTO_IPV6, IPV6_MULTICAST_IF, &id, sizeof(id)) < 0)
		{
			spdlog::error("setsockopt: {}", strerror(errno));
		}

		spdlog::info("Starting multicaster on {}", name);
		socket.connect(announce_address, announce_port);
	}
}

wivrn_session::wivrn_session(xrt::drivers::wivrn::TCP && tcp, in6_addr address) :
        control(std::move(tcp)), stream()
{
	stream.bind(stream_port);
	stream.connect(address, stream_port);
	stream.set_receive_buffer_size(1024 * 1024 * 5);
}

std::unique_ptr<wivrn_session> wivrn_client::poll()
{
	auto now = std::chrono::steady_clock::now();

	auto time_since_last_broadcast = now - last_broadcast;

	if (time_since_last_broadcast >= 1s)
	{
		last_broadcast = now;

		details::hash_context h;
		serialization_traits<from_headset::control_packets>::type_hash(h);
		serialization_traits<to_headset::control_packets>::type_hash(h);
		serialization_traits<from_headset::stream_packets>::type_hash(h);
		serialization_traits<to_headset::stream_packets>::type_hash(h);

		from_headset::client_announce_packet packet{
		        .magic = from_headset::client_announce_packet::magic_value,
		        .client_version = "WiVRn " GIT_VERSION,
		        .protocol_hash = h.hash,
		};

		for (auto & i: broadcasters)
			i.send(packet);
	}

	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = listener.get_fd();

	int r = ::poll(&fds, 1, 0);
	if (r < 0)
		throw std::system_error(errno, std::system_category());

	if (r > 0 && (fds.revents & POLLIN))
	{
		auto [tcp, addr] = listener.accept();
		char buffer[100];
		spdlog::info("Connection from {}", inet_ntop(AF_INET6, &addr.sin6_addr, buffer, sizeof(buffer)));
		return std::make_unique<wivrn_session>(std::move(tcp), addr.sin6_addr);
	}

	return {};
}

void wivrn_session::send_control(const from_headset::control_packets & packet)
{
	control.send(packet);
}

void wivrn_session::send_stream(const from_headset::stream_packets & packet)
{
	stream.send(packet);
}
