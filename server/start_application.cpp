// Copyright 2024, Gavin John et al.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief   Main file for WiVRn Monado service.
 * @author  Gavin John
 */

#include "start_application.h"

#include "driver/configuration.h"
#include "utils/flatpak.h"

#include <iomanip>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

#if WIVRN_USE_SYSTEMD
#include <chrono>
#include <string>
#include <systemd/sd-bus.h>
#include <thread>

namespace
{

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

sd_bus_ptr get_user_bus()
{
	sd_bus * bus = nullptr;
	int ret;
	// reimplement sd_bus_open_user to honor the env variable
	if (const char * bus_address = getenv("DBUS_SESSION_BUS_ADDRESS"))
	{
		if (ret = sd_bus_new(&bus); ret < 0)
			throw std::system_error(-ret, std::system_category(), "failed to create dbus object");
		sd_bus_ptr res(bus);

		if (ret = sd_bus_set_address(bus, bus_address); ret < 0)
			throw std::system_error(-ret, std::system_category(), std::string("failed to connect to dbus at address ") + bus_address);

		if (ret = sd_bus_set_bus_client(bus, 1); ret < 0)
			throw std::system_error(-ret, std::system_category(), std::string("failed to configure dbus at address ") + bus_address);
		if (ret = sd_bus_set_trusted(bus, 1); ret < 0)
			throw std::system_error(-ret, std::system_category(), std::string("failed to trust dbus at address ") + bus_address);

		if (ret = sd_bus_start(bus); ret < 0)
			throw std::system_error(-ret, std::system_category(), std::string("failed to start dbus connection ") + bus_address);

		return res;
	}
	if (ret = sd_bus_open_user(&bus); ret < 0)
		throw std::system_error(-ret, std::system_category(), "failed to connect to dbus");

	return sd_bus_ptr(bus);
}
} // namespace

static int get_service_pid(sd_bus * bus, const std::string & service_name, pid_t & pid)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message * msg = nullptr;
	const char * destination = "org.freedesktop.systemd1";
	const char * path = "/org/freedesktop/systemd1";
	const char * interface = "org.freedesktop.systemd1.Manager";
	const char * member = "GetUnit";

	int ret = 0;

	// Call the method
	ret = sd_bus_call_method(bus, destination, path, interface, member, &error, &msg, "s", service_name.c_str());
	if (ret < 0)
	{
		std::cerr << "Failed to issue method call: " << error.message << std::endl;
		goto finish;
	}

	// Parse the response message
	const char * object_path;
	ret = sd_bus_message_read(msg, "o", &object_path);
	if (ret < 0)
	{
		std::cerr << "Failed to parse response message: " << strerror(-ret) << std::endl;
		goto finish;
	}

	// Get the service's main PID
	ret = sd_bus_get_property_trivial(bus, destination, object_path, "org.freedesktop.systemd1.Service", "MainPID", &error, 'u', &pid);
	if (ret < 0)
	{
		std::cerr << "Failed to get PID: " << error.message << std::endl;
		goto finish;
	}

finish:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret;
}

int start_service(sd_bus * bus, const std::string & service_name)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message * msg = nullptr;
	const char * destination = "org.freedesktop.systemd1";
	const char * path = "/org/freedesktop/systemd1";
	const char * interface = "org.freedesktop.systemd1.Manager";
	const char * member = "StartUnit";
	const char * mode = "replace";

	// Call the method to start the service
	int ret = sd_bus_call_method(bus, destination, path, interface, member, &error, &msg, "ss", service_name.c_str(), mode);
	if (ret < 0)
	{
		std::cerr << "Failed to start service: " << error.message << std::endl;
		goto finish;
	}

finish:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret;
}

bool is_service_active(sd_bus * bus, const std::string & service_name)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message * msg = nullptr;
	const char * destination = "org.freedesktop.systemd1";
	const char * path = "/org/freedesktop/systemd1";
	const char * interface = "org.freedesktop.systemd1.Manager";
	const char * member = "GetUnit";

	bool is_active = false;
	const char * object_path;
	// Call the method
	int ret = sd_bus_call_method(bus, destination, path, interface, member, &error, &msg, "s", service_name.c_str());
	if (ret < 0)
	{
		std::cerr << "Failed to issue method call: " << error.message << std::endl;
		goto finish;
	}

	// Parse the response message
	ret = sd_bus_message_read(msg, "o", &object_path);
	if (ret < 0)
	{
		std::cerr << "Failed to parse response message: " << strerror(-ret) << std::endl;
		goto finish;
	}

	// Check the service's ActiveState property
	char * active_state;
	ret = sd_bus_get_property_string(bus, destination, object_path, "org.freedesktop.systemd1.Unit", "ActiveState", &error, &active_state);
	if (ret < 0)
	{
		std::cerr << "Failed to get ActiveState: " << error.message << std::endl;
		goto finish;
	}

	is_active = (std::string_view(active_state) == "active");

finish:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return is_active;
}

pid_t wivrn::start_unit_file()
{
	std::string service_name = "wivrn-application.service";
	pid_t pid;
	auto bus = get_user_bus();
	int ret = get_service_pid(bus.get(), service_name, pid);

	if (ret < 0 || pid == 0)
	{
		ret = start_service(bus.get(), service_name);
		if (ret < 0)
		{
			std::cerr << "Failed to start service " << service_name << std::endl;
			return ret;
		}

		// Wait until the service is active
		while (!is_service_active(bus.get(), service_name))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		// Try to get the PID again
		ret = get_service_pid(bus.get(), service_name, pid);
		if (ret < 0)
		{
			std::cerr << "Failed to get PID for service " << service_name << std::endl;
			return ret;
		}
	}

	return pid;
}

#endif

pid_t wivrn::fork_application()
{
	auto config = configuration::read_user_configuration();

	if (config.application.empty())
		return 0;

	pid_t application_pid = fork();
	if (application_pid < 0)
	{
		throw std::system_error(errno, std::system_category(), "fork");
	}

	if (application_pid == 0)
	{
		// Start a new process group so that all processes started by the
		// application can be signaled
		setpgrp();

		return exec_application(config);
	}

	return application_pid;
}

int wivrn::exec_application(configuration config)
{
	if (config.application.empty())
		return 0;

	std::string executable;
	std::vector<std::string> args;

	if (flatpak_key(flatpak::section::session_bus_policy, "org.freedesktop.Flatpak") == "talk")
	{
		executable = "flatpak-spawn";
		args.push_back("flatpak-spawn");
		args.push_back("--host");
	}
	else
	{
		executable = config.application.front();
	}

	for (auto & arg: config.application)
		args.push_back(arg.data());

	std::vector<char *> argv;
	argv.reserve(args.size());

	for (auto & i: args)
		argv.push_back(i.data());
	argv.push_back(nullptr);

	std::cerr << "Launching " << executable << std::endl;
	std::cerr << "With args:" << std::endl;
	for (auto & i: argv)
	{
		if (i)
			std::cerr << "    " << std::quoted(i) << std::endl;
	}

	execvp(executable.c_str(), argv.data());

	perror("Cannot start application");
	exit(EXIT_FAILURE);
}
