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

#include "wivrn_client_pico.h"
#include "protocol_version.h"
#include "secrets.h"
#include "smp.h"
#include "wivrn_packets.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IPTOS_DSCP_EF
#define IPTOS_DSCP_EF 0xb8
#endif

using namespace std::chrono_literals;

const char * handshake_error::what() const noexcept
{
	return message.c_str();
}

handshake_error::handshake_error(std::string_view message) :
        message(message) {}

namespace
{
template <typename T>
void init_stream(T & stream)
{
	stream.set_receive_buffer_size(1024 * 1024 * 5);
}
} // namespace

template <typename T>
void wivrn_session_pico::handshake(T address, bool tcp_only, crypto::key & headset_keypair,
                                   const std::string & model_name,
                                   std::function<std::string(int fd)> pin_enter)
{
 try
 {
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = control.get_fd();

	auto receive = [&](std::optional<std::chrono::seconds> timeout = std::nullopt) {
		std::chrono::steady_clock::time_point timeout_abs{};
		if (timeout)
			timeout_abs = std::chrono::steady_clock::now() + *timeout;

		while (true)
		{
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_abs - std::chrono::steady_clock::now());
			spdlog::warn("handshake: receive: polling, timeout_ms={}", std::max<int>(ms.count(), 100));
			int r = ::poll(&fds, 1, std::max<int>(ms.count(), 100));
			if (r < 0)
			{
				spdlog::warn("handshake: receive: poll failed: {}", strerror(errno));
				throw std::system_error(errno, std::system_category());
			}

			if (r > 0 && (fds.revents & POLLIN))
			{
				spdlog::warn("handshake: receive: data available, calling control.receive()");
				auto packet = control.receive();
				spdlog::warn("handshake: receive: receive returned, has_packet={}", packet.has_value());
				if (not packet)
					continue;

				return std::move(*packet);
			}

			if (fds.revents & (POLLHUP | POLLERR))
			{
				spdlog::warn("handshake: receive: socket error (POLLHUP/POLLERR)");
				throw std::runtime_error("Socket error during handshake");
			}

			if (std::chrono::steady_clock::now() >= timeout_abs)
				throw std::runtime_error("Timeout");
		}
	};

	spdlog::warn("handshake: sending crypto_handshake");
	send_control(from_headset::crypto_handshake{
	        .protocol_version = wivrn::protocol_version,
	        .public_key = headset_keypair.public_key(),
	        .name = model_name,
	});

	spdlog::warn("handshake: waiting for server response");
	to_headset::crypto_handshake crypto_handshake = std::get<to_headset::crypto_handshake>(receive(10s));
	spdlog::warn("handshake: received server response, state={}", (int)crypto_handshake.state);

	std::string pin = "000000";
	switch (crypto_handshake.state)
	{
		case to_headset::crypto_handshake::crypto_state::encryption_disabled: {
			spdlog::warn("handshake: encryption disabled on server");
			send_control(from_headset::crypto_handshake{});
			spdlog::warn("handshake: sent confirmation, waiting for handshake packet");
			to_headset::handshake h{std::get<to_headset::handshake>(receive(10s))};
			spdlog::warn("handshake: received handshake packet, stream_port={}", h.stream_port);
			if (h.stream_port > 0 && !tcp_only)
			{
				spdlog::warn("handshake: creating UDP stream socket");
				stream = decltype(stream)();
				stream.connect(address, h.stream_port);
				init_stream(stream);
				spdlog::warn("handshake: UDP stream socket connected");
			}
			else
			{
				spdlog::warn("handshake: skipping UDP stream (tcp_only={} stream_port={})", tcp_only, h.stream_port);
			}
			break;
		}

		case to_headset::crypto_handshake::crypto_state::pin_needed:
			pin = pin_enter(control.get_fd());

			try
			{
				crypto::smp pin_check;

				auto msg1 = pin_check.step1(pin);
				send_control(from_headset::pin_check_1{msg1});

				auto msg2 = std::get<to_headset::pin_check_2>(receive(10s)).message;

				auto msg3 = pin_check.step3(msg2);
				send_control(from_headset::pin_check_3{msg3});

				auto msg4 = std::get<to_headset::pin_check_4>(receive(10s)).message;
				bool pin_match = pin_check.step5(msg4);

				if (not pin_match)
					throw std::runtime_error("Incorrect PIN");
			}
			catch (crypto::smp_cheated &)
			{
				throw std::runtime_error("Unable to check PIN");
			}

			[[fallthrough]];

		case to_headset::crypto_handshake::crypto_state::client_already_paired: {
			spdlog::info("Using pin \"{}\"", pin);

			crypto::key server_key = crypto::key::from_public_key(crypto_handshake.public_key);
			secrets s{headset_keypair, server_key, pin};
			control.set_aes_key_and_ivs(s.control_key, s.control_iv_to_headset, s.control_iv_from_headset);

			send_control(from_headset::crypto_handshake{});

			to_headset::handshake h{std::get<to_headset::handshake>(receive(10s))};
			if (h.stream_port > 0 && !tcp_only)
			{
				stream = decltype(stream)();

				stream.set_aes_key_and_ivs(s.stream_key, s.stream_iv_header_to_headset, s.stream_iv_header_from_headset);
				stream.connect(address, h.stream_port);
				init_stream(stream);
			}
			break;
		}

		case to_headset::crypto_handshake::crypto_state::pairing_disabled:
			spdlog::info("Pairing is disabled on server");
			return;

		case to_headset::crypto_handshake::crypto_state::incompatible_version:
			spdlog::error("Incompatible protocol versions");
			return;
	}

	spdlog::warn("handshake: switch done, sending stream handshake");
	send_stream(from_headset::handshake{});
	spdlog::warn("handshake: sent stream handshake, waiting for confirmation");

	auto timeout = std::chrono::steady_clock::now() + 10s;
	while (true)
	{
		if (poll([](const auto && packet) { return std::is_same_v<std::remove_cvref_t<decltype(packet)>, to_headset::handshake>; }, 100ms))
		{
			handshake_ok = true;
			return;
		}

		if (std::chrono::steady_clock::now() >= timeout)
			return;

		if (stream)
		{
			stream.send(from_headset::handshake{});
		}
	}
 }
 catch (...)
 {
	spdlog::warn("handshake: exception caught, handshake failed");
 }
}

wivrn_session_pico::wivrn_session_pico(in6_addr address, int port, bool tcp_only,
                                       crypto::key & headset_keypair,
                                       const std::string & model_name,
                                       std::function<std::string(int fd)> pin_enter) :
        control(address, port), stream(-1), address(address)
{
	handshake(address, tcp_only, headset_keypair, model_name, pin_enter);
}

wivrn_session_pico::wivrn_session_pico(in_addr address, int port, bool tcp_only,
                                       crypto::key & headset_keypair,
                                       const std::string & model_name,
                                       std::function<std::string(int fd)> pin_enter) :
        control(address, port), stream(-1), address(address)
{
	handshake(address, tcp_only, headset_keypair, model_name, pin_enter);
}
