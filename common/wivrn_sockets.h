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

#include "wivrn_serialization.h"

#include <cassert>
#include <memory>
#include <mutex>
#include <netinet/ip.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrt::drivers::wivrn
{
class socket_shutdown : public std::exception
{
public:
	const char * what() const noexcept override;
};

class invalid_packet : public std::exception
{
public:
	const char * what() const noexcept override;
};

template <typename T, typename... Ts>
struct index_of_type
{};

template <typename T, typename... Ts>
struct index_of_type<T, T, Ts...> : std::integral_constant<size_t, 0>
{};

template <typename T, typename T2, typename... Ts>
struct index_of_type<T, T2, Ts...> : std::integral_constant<size_t, 1 + index_of_type<T, Ts...>::value>
{};

static_assert(index_of_type<int, int, float>::value == 0);
static_assert(index_of_type<float, int, float>::value == 1);

class fd_base
{
protected:
	int fd = -1;

	fd_base(const fd_base &) = delete;
public:
	fd_base() = default;
	fd_base(int fd): fd{fd} {}
	fd_base(fd_base &&);
	fd_base& operator=(fd_base &&);
	~fd_base();

	int get_fd() const
	{
		return fd;
	}

	operator bool() const
	{
		return fd != -1;
	}
};

class UDP : public fd_base
{
public:
	UDP();

	deserialization_packet receive_raw();
	void send_raw(const std::vector<uint8_t> & data);

	void connect(in6_addr address, int port);
	void connect(in_addr address, int port);
	void bind(int port);
	void subscribe_multicast(in6_addr address);
	void unsubscribe_multicast(in6_addr address);
	void set_receive_buffer_size(int size);
};

class TCP : public fd_base
{
	std::vector<uint8_t> buffer;
	std::unique_ptr<std::mutex> mutex;

	void init();

public:
	TCP(in6_addr address, int port);
	TCP(in_addr address, int port);
	explicit TCP(int fd);

	deserialization_packet receive_raw();
	void send_raw(const std::vector<uint8_t> & data);
};

class TCPListener : public fd_base
{
public:
	TCPListener();
	TCPListener(int port);

	template <typename T = TCP>
	std::pair<T, sockaddr_in6> accept()
	{
		assert(fd != -1);
		sockaddr_in6 addr{};
		socklen_t addrlen = sizeof(addr);

		int fd2 = ::accept(fd, (sockaddr *)&addr, &addrlen);
		if (fd2 < 0)
			throw std::system_error{errno, std::generic_category()};

		return {T{fd2}, addr};
	}
};

template <typename Socket, typename ReceivedType, typename SentType>
class typed_socket : public Socket
{
public:
	template <typename... Args>
	typed_socket(Args &&... args) :
	        Socket(std::forward<Args>(args)...)
	{}

	std::optional<ReceivedType> receive()
	{
		deserialization_packet packet = this->receive_raw();
		if (packet.empty())
			return {};

		return packet.deserialize<ReceivedType>();
	}

	template <typename T = SentType, typename = std::enable_if_t<std::is_same_v<T, SentType>>>
	void send(const T & data)
	{
		serialization_packet p;
		p.serialize(data);
		this->send_raw(std::move(p));
	}
};

} // namespace xrt::drivers::wivrn
