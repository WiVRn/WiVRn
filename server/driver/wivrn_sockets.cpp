// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "wivrn_sockets.h"

#include <arpa/inet.h>
#include <cassert>
#include <netdb.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <netinet/tcp.h>

const char *
xrt::drivers::wivrn::invalid_packet::what() const noexcept
{
	return "Invalid packet";
}

const char *
xrt::drivers::wivrn::socket_shutdown::what() const noexcept
{
	return "Socket shutdown";
}

xrt::drivers::wivrn::socket_base::socket_base(xrt::drivers::wivrn::socket_base &&other) : fd(other.fd)
{
	other.fd = -1;
}

xrt::drivers::wivrn::socket_base::~socket_base()
{
	if (fd >= 0) {
		::close(fd);
	}
}

xrt::drivers::wivrn::UDP::UDP()
{
	fd = socket(AF_INET6, SOCK_DGRAM, 0);

	if (fd < 0) {
		throw std::system_error{errno, std::generic_category()};
	}
}

void
xrt::drivers::wivrn::UDP::bind(int port)
{
	sockaddr_in6 bind_addr{};
	bind_addr.sin6_family = AF_INET6;
	bind_addr.sin6_addr = IN6ADDR_ANY_INIT;
	bind_addr.sin6_port = htons(port);

	if (::bind(fd, (sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		throw std::system_error{errno, std::generic_category()};
	}
}

void
xrt::drivers::wivrn::UDP::connect(in6_addr address, int port)
{
	sockaddr_in6 sa;
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = address;
	sa.sin6_port = htons(port);

	if (::connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0) {
		throw std::system_error{errno, std::generic_category()};
	}
}

void
xrt::drivers::wivrn::UDP::connect(in_addr address, int port)
{
	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr = address;
	sa.sin_port = htons(port);

	if (::connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0)
		throw std::system_error{errno, std::generic_category()};
}

void
xrt::drivers::wivrn::UDP::subscribe_multicast(in6_addr address)
{
	assert(IN6_IS_ADDR_MULTICAST(&address));

	ipv6_mreq subscribe{};
	subscribe.ipv6mr_multiaddr = address;

	if (setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &subscribe, sizeof(subscribe)) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}
}

void
xrt::drivers::wivrn::UDP::unsubscribe_multicast(in6_addr address)
{
	assert(IN6_IS_ADDR_MULTICAST(&address));

	ipv6_mreq subscribe{};
	subscribe.ipv6mr_multiaddr = address;

	if (setsockopt(fd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &subscribe, sizeof(subscribe)) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}
}

void
xrt::drivers::wivrn::UDP::set_receive_buffer_size(int size)
{
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
}

void
xrt::drivers::wivrn::TCP::init()
{
	int nodelay = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	mutex = std::make_unique<std::mutex>();
}

xrt::drivers::wivrn::TCP::TCP(int fd)
{
	this->fd = fd;

	init();
}

xrt::drivers::wivrn::TCP::TCP(in6_addr address, int port)
{
	fd = socket(AF_INET6, SOCK_STREAM, 0);

	if (fd < 0)
		throw std::system_error{errno, std::generic_category()};

	sockaddr_in6 sa;
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = address;
	sa.sin6_port = htons(port);

	if (connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	init();
}

xrt::drivers::wivrn::TCP::TCP(in_addr address, int port)
{
	fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		throw std::system_error{errno, std::generic_category()};

	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr = address;
	sa.sin_port = htons(port);

	if (connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	init();
}

xrt::drivers::wivrn::TCPListener::TCPListener(int port)
{
	fd = socket(AF_INET6, SOCK_STREAM, 0);

	if (fd < 0) {
		throw std::system_error{errno, std::generic_category()};
	}

	int reuse_addr = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	sockaddr_in6 addr{};
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	addr.sin6_addr = in6addr_any;

	if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	int backlog = 1;
	if (listen(fd, backlog) < 0) {
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}
}

std::pair<xrt::drivers::wivrn::TCP, sockaddr_in6>
xrt::drivers::wivrn::TCPListener::accept()
{
	sockaddr_in6 addr{};
	socklen_t addrlen = sizeof(addr);

	int fd2 = ::accept(fd, (sockaddr *)&addr, &addrlen);
	if (fd2 < 0)
		throw std::system_error{errno, std::generic_category()};

	return {TCP{fd2}, addr};
}

xrt::drivers::wivrn::deserialization_packet
xrt::drivers::wivrn::UDP::receive_raw()
{
	std::vector<uint8_t> buffer(2000);

	ssize_t received = recv(fd, buffer.data(), buffer.size(), 0);
	if (received < 0)
		throw std::system_error{errno, std::generic_category()};

	buffer.resize(received);

	return deserialization_packet{buffer};
}

void
xrt::drivers::wivrn::UDP::send_raw(const std::vector<uint8_t> &data)
{
	if (::send(fd, data.data(), data.size(), 0) < 0) {
		throw std::system_error{errno, std::generic_category()};
	}
}

xrt::drivers::wivrn::deserialization_packet
xrt::drivers::wivrn::TCP::receive_raw()
{
	size_t expected_size;
	size_t already_received = buffer.size();

	if (already_received < 4) {
		expected_size = 4 - buffer.size();
	} else {
		uint32_t payload_size = *reinterpret_cast<uint32_t *>(buffer.data());
		expected_size = payload_size + 4 - buffer.size();
	}

	buffer.resize(already_received + expected_size);
	ssize_t received = recv(fd, buffer.data() + already_received, expected_size, MSG_DONTWAIT);

	if (received < 0)
		throw std::system_error{errno, std::generic_category()};

	if (received == 0)
		throw socket_shutdown{};

	buffer.resize(already_received + received);

	if (buffer.size() < 4)
		return {};

	uint32_t payload_size = *reinterpret_cast<uint32_t *>(buffer.data());
	if (buffer.size() < 4 + payload_size)
		return {};

	assert(buffer.size() == 4 + payload_size);

	std::vector<uint8_t> new_buffer;
	std::swap(buffer, new_buffer);

	return deserialization_packet{std::move(new_buffer), 4};
}

void
xrt::drivers::wivrn::TCP::send_raw(const std::vector<uint8_t> &data)
{
	std::lock_guard lock(*mutex);

	uint32_t size = data.size();
	ssize_t sent = ::send(fd, &size, sizeof(size), MSG_NOSIGNAL);

	if (sent == 0) {
		throw socket_shutdown{};
	}
	if (sent < 0) {
		throw std::system_error{errno, std::generic_category()};
	}
	assert(sent == sizeof(size));

	size_t index = 0;
	while (index < data.size()) {
		sent = ::send(fd, data.data() + index, data.size() - index, MSG_NOSIGNAL);

		if (sent == 0) {
			throw socket_shutdown{};
		} else if (sent < 0) {
			throw std::system_error{errno, std::generic_category()};
		}

		index += sent;
	}
}
