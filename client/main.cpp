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

#include "application.h"
#include "scenes/lobby.h"
#include "scenes/stream.h"
#include "spdlog/spdlog.h"

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#ifdef __ANDROID__
#include "spdlog/sinks/android_sink.h"
#include <android/native_window.h>
#include <android_native_app_glue.h>
#else
#include "spdlog/sinks/stdout_color_sinks.h"
#endif

#ifdef __ANDROID__
void real_main(android_app * native_app)
#else
void real_main()
#endif
{
	try
	{
		application_info info;
#ifdef __ANDROID__
		info.native_app = native_app;
#endif
		info.name = "WiVRn";
		info.version = VK_MAKE_VERSION(1, 0, 0);
		application app(info);

		std::string server_address = app.get_server_address();
		if (server_address.empty())
			app.push_scene<scenes::lobby>();
		else
		{
			std::unique_ptr<wivrn_session> session;

			auto colon = server_address.rfind(":");
			int port;
			if (colon == std::string::npos)
			{
				port = xrt::drivers::wivrn::default_port;
			} else {
				port = std::stoi(server_address.substr(colon + 1));
				server_address = server_address.substr(0, colon);
			}
			struct addrinfo hint{
				.ai_flags = AI_ADDRCONFIG,
				.ai_family = AF_UNSPEC,
				.ai_socktype = SOCK_STREAM,
			};
			struct addrinfo * addresses;
			if (int err = getaddrinfo(server_address.c_str(), nullptr, &hint, &addresses))
			{
				throw std::runtime_error("Unable to resolve address for " + server_address + ":" + gai_strerror(err));
			}
			for (addrinfo *addr = addresses ; addr and not session; addr = addr->ai_next)
			{
				try {
					char buf[100];
					switch (addr->ai_family)
					{
						case AF_INET:
							inet_ntop(addr->ai_family, &((sockaddr_in*)addr->ai_addr)->sin_addr, buf, sizeof(buf));
							spdlog::info("Trying to connect to {} port {}", buf, port);
							session = std::make_unique<wivrn_session>(((sockaddr_in*)addr->ai_addr)->sin_addr, port);
							break;
						case AF_INET6:
							inet_ntop(addr->ai_family, &((sockaddr_in6*)addr->ai_addr)->sin6_addr, buf, sizeof(buf));
							spdlog::info("Trying to connect to {} port {}", buf, port);
							session = std::make_unique<wivrn_session>(((sockaddr_in6*)addr->ai_addr)->sin6_addr, port);
							break;
					}
				}
				catch(std::exception& e)
				{
					spdlog::warn("Cannot connect to {}: {}", server_address, e.what());
				}
			}
			freeaddrinfo(addresses);
			if (not session)
				throw std::runtime_error("Unable to connect to " + server_address + ":" + std::to_string(port));
			app.push_scene(scenes::stream::create(std::move(session), false));
		}

		app.run();
	}
	catch (std::exception & e)
	{
		spdlog::error("Caught exception: \"{}\"", e.what());
	}
	catch (...)
	{
		spdlog::error("Caught unknown exception");
	}

#ifdef __ANDROID__
	ANativeActivity_finish(native_app->activity);

	// Read all pending events.
	while (!native_app->destroyRequested)
	{
		int events;
		struct android_poll_source * source;

		while (ALooper_pollAll(-1, nullptr, &events, (void **)&source) >= 0)
		{
			// Process this event.
			if (source != nullptr)
				source->process(native_app, source);
		}
	}
#endif
}

#ifdef __ANDROID__
void android_main(android_app * native_app) __attribute__((visibility("default")));
void android_main(android_app * native_app)
{
	static auto logger = spdlog::android_logger_mt("WiVRn", "WiVRn");

	spdlog::set_default_logger(logger);

	real_main(native_app);
}
#else
int main(int argc, char * argv[])
{
	spdlog::set_default_logger(spdlog::stdout_color_mt("WiVRn"));

	char * loglevel = getenv("WIVRN_LOGLEVEL");
	if (loglevel)
	{
		if (!strcasecmp(loglevel, "trace"))
			spdlog::set_level(spdlog::level::trace);
		else if (!strcasecmp(loglevel, "debug"))
			spdlog::set_level(spdlog::level::debug);
		else if (!strcasecmp(loglevel, "info"))
			spdlog::set_level(spdlog::level::info);
		else if (!strcasecmp(loglevel, "warning"))
			spdlog::set_level(spdlog::level::warn);
		else if (!strcasecmp(loglevel, "error"))
			spdlog::set_level(spdlog::level::err);
		else if (!strcasecmp(loglevel, "critical"))
			spdlog::set_level(spdlog::level::critical);
		else if (!strcasecmp(loglevel, "off"))
			spdlog::set_level(spdlog::level::off);
		else
			spdlog::warn("Invalid value for WIVRN_LOGLEVEL environment variable");
	}

	real_main();
}
#endif
