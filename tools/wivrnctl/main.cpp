/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <CLI/CLI.hpp>
#include <systemd/sd-bus.h>

struct deleter
{
	void operator()(sd_bus * p)
	{
		sd_bus_unref(p);
	}
	void operator()(sd_bus_message * p)
	{
		sd_bus_message_unref(p);
	}
};

using sd_bus_ptr = std::unique_ptr<sd_bus, deleter>;
using sd_bus_message_ptr = std::unique_ptr<sd_bus_message, deleter>;

const char * destination = "io.github.wivrn.Server";
const char * path = "/io/github/wivrn/Server";
const char * interface = "io.github.wivrn.Server";

sd_bus_ptr get_user_bus()
{
	sd_bus * bus = nullptr;
	int ret = sd_bus_open_user(&bus);
	if (ret < 0)
		throw std::runtime_error(std::string("failed to connect to dbus: ") + strerror(-ret));

	return sd_bus_ptr(bus);
}

template <typename... Args>
sd_bus_message_ptr call_method(const sd_bus_ptr & bus, const char * member, const char * signature, Args &&... args)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message * msg = nullptr;
	int ret = sd_bus_call_method(bus.get(), destination, path, interface, member, &error, &msg, signature, std::forward<Args>(args)...);
	if (ret < 0)
	{
		std::runtime_error e(std::string("call to ") + member + " failed: " + error.message);
		sd_bus_error_free(&error);
		throw e;
	}
	return sd_bus_message_ptr(msg);
}

void stop_server()
{
	call_method(get_user_bus(),
	            "Quit",
	            "");
}

void enroll(int duration)
{
	if (duration == 0)
	{
		call_method(get_user_bus(),
		            "DisableEnrollHeadset",
		            "");
	}
	else
	{
		auto pin_msg = call_method(get_user_bus(),
		                           "EnrollHeadset",
		                           "i",
		                           duration * 60);

		const char * pin;
		int ret = sd_bus_message_read(pin_msg.get(), "s", &pin);
		if (ret < 0)
			throw std::system_error(-ret, std::system_category(), "Failed to read PIN");

		std::cout << "PIN: " << pin << std::endl;
	}
}

int main(int argc, char ** argv)
{
	CLI::App app;

	app.require_subcommand(1);

	int enroll_duration;
	auto enroll_command = app.add_subcommand("enroll", "Allow a new headset to connect")
	                              ->callback([&]() { return enroll(enroll_duration); });
	auto duration = enroll_command
	                        ->add_option("--duration,-d", enroll_duration, "Duration in minutes to allow new connections")
	                        ->transform(CLI::Validator([](std::string & input) {
		                        if (input.starts_with("-"))
			                        throw CLI::ValidationError("duration must be positive");
		                        if (input == "unlimited")
			                        input = "-1";
		                        return "";
	                        },
	                                                   "",
	                                                   ""))
	                        ->default_val(2)
	                        ->option_text("INT|unlimited");

	app.add_subcommand("stop-server", "Stop wivrn-server process")
	        ->callback(stop_server);

	try
	{
		CLI11_PARSE(app, argc, argv);
	}
	catch (std::exception & e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
}
