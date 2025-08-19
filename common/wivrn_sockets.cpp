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

#include "wivrn_sockets.h"

#include "crypto.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <memory>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <system_error>
#include <unistd.h>

thread_local crypto::encrypt_context wivrn::UDP::encrypter{EVP_aes_128_ctr()};
std::atomic<uint64_t> wivrn::UDP::iv_counter;

const char * wivrn::invalid_packet::what() const noexcept
{
	return "Invalid packet";
}

const char * wivrn::socket_shutdown::what() const noexcept
{
	return "Socket shutdown";
}

wivrn::fd_base::fd_base(wivrn::fd_base && other) :
        fd(other.fd)
{
	other.fd = -1;
}

wivrn::fd_base & wivrn::fd_base::operator=(wivrn::fd_base && other)
{
	std::swap(fd, other.fd);
	return *this;
}

wivrn::fd_base::~fd_base()
{
	if (fd >= 0)
		::close(fd);
}

wivrn::UDP::UDP()
{
	fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd < 0)
		throw std::system_error{errno, std::generic_category()};
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

wivrn::UDP::UDP(int fd)
{
	this->fd = fd;
}

void wivrn::UDP::bind(sockaddr_in6 address)
{
	if (::bind(fd, (sockaddr *)&address, sizeof(address)) < 0)
		throw std::system_error{errno, std::generic_category()};
}

void wivrn::UDP::connect(in6_addr address, int port)
{
	sockaddr_in6 sa;
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = address;
	sa.sin6_port = htons(port);

	if (::connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0)
		throw std::system_error{errno, std::generic_category()};
}

void wivrn::UDP::connect(in_addr address, int port)
{
	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr = address;
	sa.sin_port = htons(port);

	if (::connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0)
		throw std::system_error{errno, std::generic_category()};
}

void wivrn::UDP::subscribe_multicast(in6_addr address)
{
	assert(IN6_IS_ADDR_MULTICAST(&address));

	ipv6_mreq subscribe{};
	subscribe.ipv6mr_multiaddr = address;

	if (setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &subscribe, sizeof(subscribe)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}
}

void wivrn::UDP::unsubscribe_multicast(in6_addr address)
{
	assert(IN6_IS_ADDR_MULTICAST(&address));

	ipv6_mreq subscribe{};
	subscribe.ipv6mr_multiaddr = address;

	if (setsockopt(fd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &subscribe, sizeof(subscribe)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}
}

void wivrn::UDP::set_receive_buffer_size(int size)
{
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
}

void wivrn::UDP::set_send_buffer_size(int size)
{
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
}

void wivrn::UDP::set_tos(int tos)
{
	int err = setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	if (err == -1)
	{
		throw std::system_error{errno, std::generic_category()};
	}
}

void wivrn::TCP::init()
{
	int nodelay = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	mutex = std::make_unique<std::mutex>();
}

wivrn::TCP::TCP(int fd)
{
	this->fd = fd;

	init();
}

wivrn::TCP::TCP(in6_addr address, int port)
{
	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0)
		throw std::system_error{errno, std::generic_category()};
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	sockaddr_in6 sa;
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = address;
	sa.sin6_port = htons(port);

	if (connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	init();
}

wivrn::TCP::TCP(in_addr address, int port)
{
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		throw std::system_error{errno, std::generic_category()};
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr = address;
	sa.sin_port = htons(port);

	if (connect(fd, (sockaddr *)&sa, sizeof(sa)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	init();
}

wivrn::TCPListener::TCPListener(int port)
{
	fd = socket(AF_INET6, SOCK_STREAM, 0);

	if (fd < 0)
	{
		throw std::system_error{errno, std::generic_category()};
	}

	int reuse_addr = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	sockaddr_in6 addr{};
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	addr.sin6_addr = in6addr_any;

	if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	int backlog = 1;
	if (listen(fd, backlog) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}
}

std::pair<wivrn::deserialization_packet, sockaddr_in6> wivrn::UDP::receive_from_raw()
{
	sockaddr_in6 addr;
	socklen_t addrlen = sizeof(addr);

	size_t size = recvfrom(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC, (sockaddr *)&addr, &addrlen);

#if defined(__cpp_lib_smart_ptr_for_overwrite) && __cpp_lib_smart_ptr_for_overwrite >= 202002L
	auto buffer = std::make_shared_for_overwrite<uint8_t[]>(size);
#else
	std::shared_ptr<uint8_t[]> buffer(new uint8_t[size]);
#endif
	ssize_t received = recvfrom(fd, buffer.get(), size, 0, (sockaddr *)&addr, &addrlen);
	if (received < 0)
		throw std::system_error{errno, std::generic_category()};

	bytes_received_ += received;

	std::span message{buffer.get(), (size_t)received};

	if (encrypted)
	{
		// Not big enough for the IV: drop the packet
		if (received < sizeof(uint64_t))
			return {};

		std::array<uint8_t, 16> full_iv;
		memcpy(full_iv.data(), buffer.get(), sizeof(uint64_t)); // TODO: endianness?
		memcpy(full_iv.data() + sizeof(uint64_t), recv_iv_header.data(), recv_iv_header.size());

		message = message.subspan(sizeof(uint64_t));

		decrypter.set_iv(full_iv);
		decrypter.decrypt_in_place(message);
	}

	return {deserialization_packet{std::move(buffer), message}, addr};
}

wivrn::deserialization_packet wivrn::UDP::receive_pending()
{
	if (messages.empty())
		return {};

	auto span = messages.back();
	messages.pop_back();
	return deserialization_packet{buffer, span};
}

wivrn::deserialization_packet wivrn::UDP::receive_raw()
{
	if (not messages.empty())
	{
		auto span = messages.back();
		messages.pop_back();
		return deserialization_packet{buffer, span};
	}

	static const size_t message_size = 2048;
	static const size_t num_messages = 20;
#if defined(__cpp_lib_smart_ptr_for_overwrite) && __cpp_lib_smart_ptr_for_overwrite >= 202002L
	buffer = std::make_shared_for_overwrite<uint8_t[]>(message_size * num_messages);
#else
	buffer.reset(new uint8_t[message_size * num_messages]);
#endif
	std::vector<iovec> iovecs;
	std::vector<mmsghdr> mmsgs;
	iovecs.reserve(num_messages);
	mmsgs.reserve(num_messages);
	for (size_t i = 0; i < num_messages; ++i)
	{
		iovecs.push_back({
		        .iov_base = buffer.get() + message_size * i,
		        .iov_len = message_size,
		});

		mmsgs.push_back(
		        {
		                .msg_hdr = {
		                        .msg_iov = &iovecs.back(),
		                        .msg_iovlen = 1,
		                },
		        });
	}

	int received = recvmmsg(fd, mmsgs.data(), num_messages, MSG_DONTWAIT, nullptr);

	if (received < 0)
		throw std::system_error{errno, std::generic_category()};
	if (received == 0)
		throw socket_shutdown();

	messages.reserve(received);

	for (int i = received - 1; i >= 0; --i)
	{
		bytes_received_ += mmsgs[i].msg_len;

		std::span<uint8_t> message{(uint8_t *)iovecs[i].iov_base, mmsgs[i].msg_len};
		assert(message.data() != nullptr);

		if (encrypted)
		{
			// Not big enough for the IV: drop the packet
			if (message.size() < sizeof(uint64_t))
				throw std::runtime_error("Packet too small: " + std::to_string(message.size()));

			std::array<uint8_t, 16> full_iv;
			memcpy(full_iv.data(), message.data(), sizeof(uint64_t)); // TODO: endianness?
			memcpy(full_iv.data() + sizeof(uint64_t), recv_iv_header.data(), recv_iv_header.size());

			message = message.subspan(sizeof(uint64_t));

			decrypter.set_iv(full_iv);
			decrypter.decrypt_in_place(message);
		}

		if (i == 0)
			return deserialization_packet{buffer, message};

		messages.push_back(message);
	}

	__builtin_unreachable();
}

void wivrn::UDP::send_raw(serialization_packet && packet)
{
	thread_local std::vector<iovec> iovecs;
	iovecs.clear();

	std::vector<std::span<uint8_t>> & data = packet;

	uint64_t counter;
	if (encrypted)
	{
		counter = iv_counter.fetch_add(1);

		std::array<uint8_t, 16> full_iv;
		memcpy(full_iv.data(), &counter, sizeof(uint64_t)); // TODO: endianness?
		memcpy(full_iv.data() + sizeof(uint64_t), send_iv_header.data(), send_iv_header.size());

		iovecs.emplace_back(&counter, sizeof(uint64_t));

		encrypter.set_key_and_iv(key, full_iv);
		encrypter.encrypt_in_place(data);
	}

	for (const auto & span: data)
	{
		iovecs.emplace_back(span.data(), span.size());
		bytes_sent_ += span.size();
	}

	if (::writev(fd, iovecs.data(), iovecs.size()) < 0)
		throw std::system_error{errno, std::generic_category()};
}

void wivrn::UDP::send_many_raw(std::span<serialization_packet> packets)
{
	thread_local std::vector<iovec> iovecs;
	thread_local std::vector<mmsghdr> mmsgs;
	thread_local std::vector<uint64_t> iv_counters;

	if (packets.empty())
		return;

	iovecs.clear();
	mmsgs.clear();
	iv_counters.clear();

	iv_counters.reserve(packets.size());

	for (serialization_packet & packet: packets)
	{
		std::vector<std::span<uint8_t>> & data = packet;

		if (encrypted)
		{
			iv_counters.push_back(iv_counter.fetch_add(1));

			std::array<uint8_t, 16> full_iv;
			memcpy(full_iv.data(), &iv_counters.back(), sizeof(uint64_t)); // TODO: endianness?
			memcpy(full_iv.data() + sizeof(uint64_t), send_iv_header.data(), send_iv_header.size());

			iovecs.emplace_back(&iv_counters.back(), sizeof(uint64_t));

			encrypter.set_key_and_iv(key, full_iv);
			encrypter.encrypt_in_place(data);
		}

		for (const auto & span: data)
		{
			iovecs.emplace_back(span.data(), span.size_bytes());
			bytes_sent_ += span.size();
		}

		if (encrypted)
			mmsgs.push_back({.msg_hdr = {.msg_iovlen = data.size() + 1}});
		else
			mmsgs.push_back({.msg_hdr = {.msg_iovlen = data.size()}});
	}

	for (size_t i = 0, j = 0; i < packets.size(); ++i)
	{
		mmsgs[i].msg_hdr.msg_iov = &iovecs[j];
		j += mmsgs[i].msg_hdr.msg_iovlen;
	}

	// sendmmsg may not send all messages, just consider them as lost for UDP
	if (sendmmsg(fd, mmsgs.data(), mmsgs.size(), 0) < 0)
		throw std::system_error{errno, std::generic_category()};
}

wivrn::deserialization_packet wivrn::TCP::receive_raw()
{
	ssize_t expected_size;

	if (data.size_bytes() < sizeof(uint32_t))
	{
		expected_size = sizeof(uint32_t) - data.size_bytes();
	}
	else
	{
		uint32_t payload_size = *reinterpret_cast<uint32_t *>(data.data());
		expected_size = payload_size + sizeof(uint32_t) - data.size_bytes();
	}

	if (expected_size > capacity_left)
	{
		size_t new_size = std::max<size_t>(data.size_bytes() + expected_size,
		                                   4096);
		auto old = std::move(buffer);
#if defined(__cpp_lib_smart_ptr_for_overwrite) && __cpp_lib_smart_ptr_for_overwrite >= 202002L
		buffer = std::make_shared_for_overwrite<uint8_t[]>(new_size);
#else
		buffer.reset(new uint8_t[new_size]);
#endif
		memcpy(buffer.get(), data.data(), data.size_bytes());
		data = std::span(buffer.get(), data.size());
		capacity_left = new_size - data.size_bytes();
	}

	if (capacity_left > 0)
	{
		ssize_t received_size = recv(fd, &*data.end(), capacity_left, MSG_DONTWAIT);

		if (received_size < 0)
			throw std::system_error{errno, std::generic_category()};

		if (received_size == 0)
			throw socket_shutdown{};

		bytes_received_ += received_size;

		if (decrypter)
		{
			std::span<uint8_t> received_data{&*data.end(), (size_t)received_size};
			decrypter.decrypt_in_place(received_data);
		}

		data = std::span(data.data(), data.size() + received_size);
		capacity_left -= received_size;
	}

	if (data.size_bytes() < sizeof(uint32_t))
		return {};

	uint32_t payload_size = *reinterpret_cast<uint32_t *>(data.data());
	if (payload_size == 0)
		throw std::runtime_error("Invalid packet: 0 size");

	if (data.size_bytes() < sizeof(uint32_t) + payload_size)
		return {};

	auto span = data.subspan(sizeof(uint32_t), payload_size);
	data = data.subspan(sizeof(uint32_t) + payload_size);
	return deserialization_packet{buffer, span};
}

wivrn::deserialization_packet wivrn::TCP::receive_pending()
{
	if (data.size_bytes() < sizeof(uint32_t))
		return {};

	uint32_t payload_size = *reinterpret_cast<uint32_t *>(data.data());
	if (payload_size == 0)
		throw std::runtime_error("Invalid packet: 0 size");

	if (data.size_bytes() < sizeof(uint32_t) + payload_size)
		return {};

	auto span = data.subspan(sizeof(uint32_t), payload_size);
	data = data.subspan(sizeof(uint32_t) + payload_size);
	return deserialization_packet{buffer, span};
}

void wivrn::TCP::send_raw(serialization_packet && packet)
{
	thread_local std::vector<iovec> iovecs;
	iovecs.clear();

	std::vector<std::span<uint8_t>> & data = packet;

	uint32_t size = 0;
	iovecs.emplace_back(&size, sizeof(size));
	for (const auto & span: data)
	{
		size += span.size_bytes();
		iovecs.emplace_back(span.data(), span.size_bytes());
	}

	msghdr hdr{
	        .msg_name = nullptr,
	        .msg_namelen = 0,
	        .msg_iov = iovecs.data(),
	        .msg_iovlen = iovecs.size(),
	        .msg_control = nullptr,
	        .msg_controllen = 0,
	        .msg_flags = 0,
	};

	std::lock_guard lock(*mutex);
	if (encrypter)
	{
		data.insert(data.begin(), {(uint8_t *)&size, sizeof(size)});
		encrypter.encrypt_in_place(data);
	}

	while (true)
	{
		ssize_t sent = ::sendmsg(fd, &hdr, MSG_NOSIGNAL);

		if (sent == 0)
			throw socket_shutdown{};

		if (sent < 0)
			throw std::system_error{errno, std::generic_category()};

		bytes_sent_ += sent;

		// iov fully consumed
		while (hdr.msg_iovlen > 0 and sent >= hdr.msg_iov[0].iov_len)
		{
			sent -= hdr.msg_iov[0].iov_len;
			++hdr.msg_iov;
			--hdr.msg_iovlen;
		}
		if (hdr.msg_iovlen == 0)
			return;
		hdr.msg_iov[0].iov_base = (void *)((uintptr_t)hdr.msg_iov[0].iov_base + sent);
		hdr.msg_iov[0].iov_len -= sent;
	}
}

void wivrn::TCP::send_many_raw(std::span<serialization_packet> packets)
{
	thread_local std::vector<iovec> iovecs;
	thread_local std::vector<uint32_t> sizes;
	thread_local std::vector<std::span<uint8_t>> spans;

	if (packets.empty())
		return;

	iovecs.clear();
	sizes.clear();
	spans.clear();

	sizes.reserve(packets.size());

	for (serialization_packet & packet: packets)
	{
		std::vector<std::span<uint8_t>> & data = packet;

		auto & size = sizes.emplace_back(0);
		iovecs.emplace_back(&size, sizeof(size));
		spans.emplace_back((uint8_t *)&size, sizeof(size));

		for (const auto & span: data)
		{
			size += span.size_bytes();
			iovecs.emplace_back(span.data(), span.size_bytes());
			spans.emplace_back(span.data(), span.size_bytes());
		}
	}

	msghdr hdr{
	        .msg_name = nullptr,
	        .msg_namelen = 0,
	        .msg_iov = iovecs.data(),
	        .msg_iovlen = iovecs.size(),
	        .msg_control = nullptr,
	        .msg_controllen = 0,
	        .msg_flags = 0,
	};

	std::lock_guard lock(*mutex);
	if (encrypter)
	{
		encrypter.encrypt_in_place(spans);
	}

	while (true)
	{
		ssize_t sent = ::sendmsg(fd, &hdr, MSG_NOSIGNAL);

		if (sent == 0)
			throw socket_shutdown{};

		if (sent < 0)
			throw std::system_error{errno, std::generic_category()};

		bytes_sent_ += sent;

		// iov fully consumed
		while (hdr.msg_iovlen > 0 and sent >= hdr.msg_iov[0].iov_len)
		{
			sent -= hdr.msg_iov[0].iov_len;
			++hdr.msg_iov;
			--hdr.msg_iovlen;
		}
		if (hdr.msg_iovlen == 0)
			return;
		hdr.msg_iov[0].iov_base = (void *)((uintptr_t)hdr.msg_iov[0].iov_base + sent);
		hdr.msg_iov[0].iov_len -= sent;
	}
}

void wivrn::UDP::set_aes_key_and_ivs(std::span<std::uint8_t, 16> key_, std::span<std::uint8_t, 8> recv_iv_header_, std::span<std::uint8_t, 8> send_iv_header_)
{
	decrypter = crypto::decrypt_context{EVP_aes_128_ctr()};
	decrypter.set_key(key_);

	std::ranges::copy(key_, key.begin());
	std::ranges::copy(recv_iv_header_, recv_iv_header.begin());
	std::ranges::copy(send_iv_header_, send_iv_header.begin());
	encrypted = true;
}

void wivrn::TCP::set_aes_key_and_ivs(std::span<std::uint8_t, 16> key, std::span<std::uint8_t, 16> recv_iv, std::span<std::uint8_t, 16> send_iv)
{
	encrypter = crypto::encrypt_context{EVP_aes_128_ctr()};
	encrypter.set_key(key);
	encrypter.set_iv(send_iv);

	decrypter = crypto::decrypt_context{EVP_aes_128_ctr()};
	decrypter.set_key(key);
	decrypter.set_iv(recv_iv);
}
