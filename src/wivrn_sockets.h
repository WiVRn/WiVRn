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

class socket_base
{
protected:
	int fd = -1;

	socket_base() = default;
	socket_base(const socket_base &) = delete;
	socket_base(socket_base &&);
	~socket_base();

public:
	int get_fd() const
	{
		return fd;
	}
};

class UDP : public socket_base
{
public:
	UDP();
	UDP(const UDP &) = delete;
	UDP(UDP &&) = default;

	deserialization_packet receive_raw();
	std::pair<deserialization_packet, sockaddr_in6> receive_from_raw();
	void send_raw(const std::vector<uint8_t> & data);

	void connect(in6_addr address, int port);
	void bind(int port);
	void subscribe_multicast(in6_addr address);
	void unsubscribe_multicast(in6_addr address);
	void set_receive_buffer_size(int size);
};

class TCP : public socket_base
{
	std::vector<uint8_t> buffer;
	std::unique_ptr<std::mutex> mutex;

public:
	TCP(in6_addr address, int port);
	explicit TCP(int fd);
	TCP(const TCP &) = delete;
	TCP(TCP &&) = default;

	deserialization_packet receive_raw();
	void send_raw(const std::vector<uint8_t> & data);
};

class TCPListener : public socket_base
{
public:
	TCPListener(int port);
	TCPListener(const TCPListener &) = delete;
	TCPListener(TCPListener &&) = default;

	std::pair<TCP, sockaddr_in6> accept();
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

	std::optional<std::pair<ReceivedType, sockaddr_in6>> receive_from()
	{
		auto [buffer, addr] = this->receive_from_raw();
		if (buffer.empty())
			return {};

		return {deserialization_packet(std::move(buffer)).deserialize<ReceivedType>(), addr};
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
