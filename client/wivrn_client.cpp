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
#include "hardware.h"
#include "protocol_version.h"
#include "secrets.h"
#include "smp.h"
#include "spdlog/common.h"
#include "utils/i18n.h"
#include "wivrn_packets.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/ipv6.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IPTOS_DSCP_EF
// constant is not defined in Android ip.h
#define IPTOS_DSCP_EF 0xb8
#endif

using namespace std::chrono_literals;

const size_t num_streams = 5;

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
void wivrn_session::handshake(T address, bool tcp_only, crypto::key & headset_keypair, std::function<std::string(int fd)> pin_enter)
{
	// FIXME this comment
	// Wait for handshake on control socket,
	// then send ours on stream or control socket,
	// finally wait for second server handshake

	auto receive = [](auto & socket, std::chrono::milliseconds timeout) {
		auto timeout_abs = std::chrono::steady_clock::now() + timeout;

		pollfd fds{
		        .fd = socket.get_fd(),
		        .events = POLLIN,
		};

		while (true)
		{
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_abs - std::chrono::steady_clock::now());
			int r = ::poll(&fds, 1, std::max<int>(ms.count(), 100));
			if (r < 0)
				throw std::system_error(errno, std::system_category());

			if (r > 0 && (fds.revents & POLLIN))
			{
				auto packet = socket.receive();
				if (not packet)
					continue;

				return std::move(*packet);
			}

			if (std::chrono::steady_clock::now() >= timeout_abs)
				throw std::runtime_error(_("Timeout"));
		}
	};

	send_control(from_headset::crypto_handshake{
	        .protocol_version = wivrn::protocol_version,
	        .public_key = headset_keypair.public_key(),
	        .name = model_name(),
	});

	to_headset::crypto_handshake crypto_handshake = std::get<to_headset::crypto_handshake>(receive(control, 10s));

	std::string pin = "000000";
	switch (crypto_handshake.state)
	{
		case to_headset::crypto_handshake::crypto_state::encryption_disabled: {
			spdlog::info("Encryption is disabled on server");

			send_control(from_headset::crypto_handshake{});

			to_headset::handshake h{std::get<to_headset::handshake>(receive(control, 10s))};
			if (h.stream_port > 0 && !tcp_only)
			{
				for (size_t i = 0; i < num_streams; ++i)
				{
					auto & s = streams.emplace_back();
					s.connect(address, h.stream_port);
					init_stream(s);
				}
			}
			break;
		}

		case to_headset::crypto_handshake::crypto_state::pin_needed:
			pin = pin_enter(control.get_fd());

			// Check the PIN
			try
			{
				crypto::smp pin_check;

				auto msg1 = pin_check.step1(pin);
				send_control(from_headset::pin_check_1{msg1});

				auto msg2 = std::get<to_headset::pin_check_2>(receive(control, 10s)).message;

				auto msg3 = pin_check.step3(msg2);
				send_control(from_headset::pin_check_3{msg3});

				auto msg4 = std::get<to_headset::pin_check_4>(receive(control, 10s)).message;
				bool pin_match = pin_check.step5(msg4);

				if (not pin_match)
					throw std::runtime_error(_("Incorrect PIN"));
			}
			catch (crypto::smp_cheated &)
			{
				throw std::runtime_error(_("Unable to check PIN"));
			}

			[[fallthrough]];

		case to_headset::crypto_handshake::crypto_state::client_already_paired: {
			spdlog::info("Using pin \"{}\"", pin);

			crypto::key server_key = crypto::key::from_public_key(crypto_handshake.public_key);
			secrets s{headset_keypair, server_key, pin};
			control.set_aes_key_and_ivs(s.control_key, s.control_iv_to_headset, s.control_iv_from_headset);

			// Confirm that encryption is set up
			send_control(from_headset::crypto_handshake{});

			to_headset::handshake h{std::get<to_headset::handshake>(receive(control, 10s))};
			if (h.stream_port > 0 && !tcp_only)
			{
				for (size_t i = 0; i < num_streams; ++i)
				{
					auto & st = streams.emplace_back();
					st.set_aes_key_and_ivs(s.stream_key, s.stream_iv_header_to_headset, s.stream_iv_header_from_headset);
					st.connect(address, h.stream_port);
					init_stream(st);
				}
			}
			break;
		}

		case to_headset::crypto_handshake::crypto_state::pairing_disabled:
			spdlog::info("Pairing is disabled on server");
			throw std::runtime_error(_("Pairing is disabled on server"));

		case to_headset::crypto_handshake::crypto_state::incompatible_version:
			spdlog::error("Incompatible protocol versions");
			throw std::runtime_error(_("Incompatible server version"));
	}

	// Send the desired number of UDP streams
	control.send(from_headset::handshake{.num_udp_streams = uint8_t(streams.size())});
	std::get<to_headset::handshake>(receive(control, 2s));

	// Initialize UDP streams
	auto timeout = std::chrono::steady_clock::now() + 8s;
	for (auto & s: streams)
	{
		while (true)
		{
			try
			{
				s.send(from_headset::handshake{});
				auto p = receive(s, 100ms);
				break;
			}
			catch (std::runtime_error & e)
			{
				// timeout, send a new packet and continue as packets may be lost
			}

			if (std::chrono::steady_clock::now() >= timeout)
				throw std::runtime_error(_("Timeout"));
		}
	}
}

wivrn_session::wivrn_session(in6_addr address, int port, bool tcp_only, crypto::key & headset_keypair, std::function<std::string(int fd)> pin_enter) :
        control(address, port), streams(-1), address(address)
{
	try
	{
		handshake(address, tcp_only, headset_keypair, pin_enter);
	}
	catch (std::exception & e)
	{
		throw handshake_error{e.what()};
	}
}

wivrn_session::wivrn_session(in_addr address, int port, bool tcp_only, crypto::key & headset_keypair, std::function<std::string(int fd)> pin_enter) :
        control(address, port), address(address)
{
	try
	{
		handshake(address, tcp_only, headset_keypair, pin_enter);
	}
	catch (std::exception & e)
	{
		throw handshake_error{e.what()};
	}
}
