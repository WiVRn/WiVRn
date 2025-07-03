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

#include "wivrn_connection.h"
#include "configuration.h"
#include "protocol_version.h"
#include "secrets.h"
#include "smp.h"
#include "wivrn_ipc.h"
#include "wivrn_packets.h"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <poll.h>
#include <regex>
#include <sys/socket.h>
#include <variant>

using namespace std::chrono_literals;

static void handle_event_from_main_loop(to_monado::disconnect)
{
	// Ignore disconnect request when no headset is connected
}

static void handle_event_from_main_loop(to_monado::set_bitrate)
{
	// Ignore bitrate request when no headset is connected
}

static std::string clean_key(std::string key)
{
	static const std::regex header{"^-+BEGIN .*-+$", std::regex_constants::multiline};
	static const std::regex footer{"^-+END .*-+$", std::regex_constants::multiline};
	static const std::regex whitespace{"[[:space:]]"};

	key = std::regex_replace(key, header, "");
	key = std::regex_replace(key, footer, "");
	key = std::regex_replace(key, whitespace, "");

	return key;
}

wivrn::incorrect_pin::incorrect_pin() :
        std::runtime_error("Incorrect PIN") {}

wivrn::wivrn_connection::wivrn_connection(std::stop_token stop_token, encryption_state state, std::string pin, TCP && tcp) :
        control(std::move(tcp)),
        stream(-1),
        pin(pin),
        state(state)
{
	init(stop_token);
}

void wivrn::wivrn_connection::init(std::stop_token stop_token, std::function<void()> tick)
{
	active = false;

	sockaddr_in6 server_address;
	socklen_t len = sizeof(server_address);
	if (getsockname(control.get_fd(), (sockaddr *)&server_address, &len) < 0)
	{
		throw std::system_error(errno, std::system_category(), "Cannot get socket port");
	}
	int port = ntohs(((struct sockaddr_in6 *)&server_address)->sin6_port);

	sockaddr_in6 client_address;
	len = sizeof(client_address);
	if (getpeername(control.get_fd(), (sockaddr *)&client_address, &len) < 0)
	{
		throw std::system_error(errno, std::system_category(), "Cannot get client address");
	}

	if (configuration().tcp_only)
	{
		port = -1;
	}
	else
	{
		stream = decltype(stream)();
		stream.bind(server_address);
	}

	auto receive = [&](std::optional<std::chrono::seconds> timeout = std::nullopt, bool allow_stream_socket = false) {
		// Returns the packet and the port (on the stream socket) or -1 (on the control socket)

		std::chrono::steady_clock::time_point timeout_abs{};
		if (timeout)
			timeout_abs = std::chrono::steady_clock::now() + *timeout;

		while (true)
		{
			if (stop_token.stop_requested())
				throw std::runtime_error("Connection cancelled");

			tick();

			pollfd fds[3] = {};
			if (allow_stream_socket)
			{
				fds[0].events = POLLIN;
				fds[0].fd = stream.get_fd();
			}
			fds[1].events = POLLIN;
			fds[1].fd = control.get_fd();
			fds[2].events = POLLIN;
			fds[2].fd = wivrn_ipc_socket_monado->get_fd();

			// Make sure tick() is called at least every 100ms
			int r = ::poll(fds, std::size(fds), 100);
			if (r < 0)
				throw std::system_error(errno, std::system_category());

			if (allow_stream_socket and (fds[0].revents & (POLLHUP | POLLERR)))
				throw std::runtime_error("Error on stream socket");

			if (fds[1].revents & (POLLHUP | POLLERR))
				throw std::runtime_error("Error on control socket");

			if (fds[2].revents & (POLLHUP | POLLERR))
				throw std::runtime_error("Error on IPC socket");

			if (allow_stream_socket and (fds[0].revents & POLLIN))
			{
				auto [raw_packet, peer_addr] = stream.receive_from_raw();

				// Ignore packets sent from the wrong address
				if (memcmp(&peer_addr.sin6_addr, &client_address.sin6_addr, sizeof(peer_addr.sin6_addr)) == 0)
					return std::make_pair(raw_packet.deserialize<from_headset::packets>(), (int)htons(peer_addr.sin6_port));
			}

			if (fds[1].revents & POLLIN)
			{
				std::optional<from_headset::packets> packet = control.receive();
				if (packet)
					return std::make_pair(std::move(*packet), -1);
			}

			if (fds[2].revents & POLLIN)
			{
				auto packet = receive_from_main();
				if (packet)
					std::visit([](auto && x) { handle_event_from_main_loop(x); }, *packet);
			}

			if (timeout and std::chrono::steady_clock::now() > timeout_abs)
			{
				throw std::runtime_error("No handshake received from client");
			}
		}
	};

	// Wait for client to send handshake packet
	auto crypto_handshake = std::get<from_headset::crypto_handshake>(receive(10s).first);

	if (crypto_handshake.protocol_version != wivrn::protocol_version)
	{
		control.send(to_headset::crypto_handshake{
		        .state = to_headset::crypto_handshake::crypto_state::incompatible_version,
		});
		throw std::runtime_error("Incompatible protocol version");
	}

	crypto::key headset_key = crypto::key::from_public_key(crypto_handshake.public_key);
	bool is_public_key_known = std::ranges::any_of(
	        wivrn::known_keys(),
	        [key = clean_key(crypto_handshake.public_key)](const wivrn::headset_key & k) {
		        return k.public_key == key;
	        });

	switch (state)
	{
		case encryption_state::disabled:
			// Encryption and authentication are disabled
			control.send(to_headset::crypto_handshake{
			        .state = to_headset::crypto_handshake::crypto_state::encryption_disabled,
			});
			break;

		case encryption_state::enabled:
			if (not is_public_key_known)
			{
				control.send(to_headset::crypto_handshake{
				        .state = to_headset::crypto_handshake::crypto_state::pairing_disabled,
				});
				throw std::runtime_error("Client not known and pairing is disabled");
			}

			[[fallthrough]];

		case encryption_state::pairing:
			// Generate an ephemeral key pair just for exchanging the AES key
			crypto::key server_key = crypto::key::generate_x448_keypair();

			control.send(to_headset::crypto_handshake{
			        .public_key = server_key.public_key(),
			        .state = is_public_key_known
			                         ? to_headset::crypto_handshake::crypto_state::client_already_paired
			                         : to_headset::crypto_handshake::crypto_state::pin_needed,
			});

			if (not is_public_key_known)
			{
				try
				{
					// Check the PIN
					crypto::smp pin_check;

					auto msg1 = std::get<from_headset::pin_check_1>(receive(2min).first).message;

					auto msg2 = pin_check.step2(msg1, pin);
					control.send(to_headset::pin_check_2{msg2});

					auto msg3 = std::get<from_headset::pin_check_3>(receive(10s).first).message;

					auto [msg4, pin_match] = pin_check.step4(msg3);
					control.send(to_headset::pin_check_4{msg4});

					if (not pin_match)
						throw incorrect_pin{};
				}
				catch (crypto::smp_cheated &)
				{
					throw std::runtime_error("Unable to check PIN");
				}
			}

			secrets s{server_key, headset_key, is_public_key_known ? "000000" : pin};
			control.set_aes_key_and_ivs(s.control_key, s.control_iv_from_headset, s.control_iv_to_headset);
			stream.set_aes_key_and_ivs(s.stream_key, s.stream_iv_header_from_headset, s.stream_iv_header_to_headset);
			break;
	}

	// Wait for confirmation that the client has set up encryption
	if (not std::holds_alternative<from_headset::crypto_handshake>(receive().first))
		throw std::runtime_error("No handshake received from client");

	control.send(to_headset::handshake{.stream_port = port});

	auto [stream_handshake, client_port] = receive(10s, true);

	// Check the packet type
	if (not std::holds_alternative<from_headset::handshake>(stream_handshake))
	{
		throw std::runtime_error("No handshake received from client");
	}

	if (client_port >= 0)
	{
		stream.connect(client_address.sin6_addr, client_port);
		stream.set_send_buffer_size(1024 * 1024 * 5);
	}
	else
	{
		// No stream socket
		stream = decltype(stream)(-1);
	}

	control.send(to_headset::handshake{.stream_port = port});

	info_packet = std::get<from_headset::headset_info_packet>(receive(10s).first);

	active = true;

	if (state == encryption_state::pairing and not is_public_key_known)
		wivrn::add_known_key({
		        .public_key = clean_key(headset_key.public_key()),
		        .name = crypto_handshake.name,
		});
	else if (state != encryption_state::disabled)
		wivrn::update_last_connection_timestamp(clean_key(headset_key.public_key()));
}

void wivrn::wivrn_connection::reset(TCP && tcp, std::function<void()> tick)
{
	if (stream)
		stream = decltype(stream)();

	control = std::move(tcp);
	init({}, tick);
}

void wivrn::wivrn_connection::shutdown()
{
	if (stream)
		::shutdown(stream.get_fd(), SHUT_RDWR);
	if (control)
		::shutdown(control.get_fd(), SHUT_RDWR);
}

std::optional<wivrn::from_headset::packets> wivrn::wivrn_connection::poll_control(int timeout)
{
	pollfd fds{};
	fds.events = POLLIN;
	fds.fd = control.get_fd();

	int r = ::poll(&fds, 1, timeout);
	if (r < 0)
		throw std::system_error(errno, std::system_category());

	if (r > 0 && (fds.revents & POLLIN))
	{
		return control.receive();
	}

	return {};
}
