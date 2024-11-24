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

sd_bus_message_ptr get_property(const sd_bus_ptr & bus, const char * member, const char * signature)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message * msg = nullptr;
	int ret = sd_bus_get_property(bus.get(), destination, path, interface, member, &error, &msg, signature);
	if (ret < 0)
	{
		std::runtime_error e(std::string("read property ") + member + " failed: " + error.message);
		sd_bus_error_free(&error);
		throw e;
	}
	return sd_bus_message_ptr(msg);
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

void list_enrolled(bool show_keys)
{
	auto msg = get_property(get_user_bus(),
	                        "KnownKeys",
	                        "a(ss)");

	int ret = sd_bus_message_enter_container(msg.get(), 'a', "(ss)");
	if (ret < 0)
		throw std::system_error(-ret, std::system_category(), "Failed to get enrolled headsets");

	std::vector<std::pair<std::string, std::string>> values;
	while (true)
	{
		char * name;
		char * key;
		int ret = sd_bus_message_read(msg.get(), "(ss)", &name, &key);
		if (ret == 0)
			break;
		if (ret < 0)
			throw std::system_error(-ret, std::system_category(), "Failed to get enrolled headset details");

		values.emplace_back(name, key);
	}
	size_t width = 0;
	if (show_keys)
	{
		for (const auto & [name, key]: values)
			width = std::max(width, name.length());
		width += 1;
	}
	std::cout << "Enrolled headsets:" << std::endl;
	for (const auto & [name, key]: values)
	{
		if (width > 0)
			std::cout << std::left << std::setw(width);
		std::cout << name;
		if (show_keys)
			std::cout << key;
		std::cout << std::endl;
	}
}

void stop_server()
{
	call_method(get_user_bus(),
	            "Quit",
	            "");
}

void disconnect()
{
	call_method(get_user_bus(),
	            "Disconnect",
	            "");
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

	bool show_keys = false;
	auto list_command = app.add_subcommand("list-enrolled", "List headsets allowed to connect")
	                            ->callback([&]() { list_enrolled(show_keys); });

	list_command->add_flag("--keys, -k", show_keys, "Show public keys");

	app.add_subcommand("stop-server", "Stop wivrn-server process")
	        ->callback(stop_server);

	app.add_subcommand("disconnect", "Disconnect headset")
	        ->callback(disconnect);

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
