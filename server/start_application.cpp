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

struct raii_sd_bus_error
{
	sd_bus_error data = SD_BUS_ERROR_NULL;

	auto operator&()
	{
		return &data;
	}

	~raii_sd_bus_error()
	{
		sd_bus_error_free(&data);
	}
};

using sd_bus_ptr = std::unique_ptr<sd_bus, deleter>;
using sd_msg_ptr = std::unique_ptr<sd_bus_message, deleter>;

struct container
{
	container(sd_bus_message * msg, char type, const char * contents) :
	        msg(msg)
	{
		int ret = sd_bus_message_open_container(msg, type, contents);
		if (ret < 0)
			throw std::system_error(-ret, std::system_category(), "sd_bus_message_open_container failed");
	}
	~container()
	{
		int ret = sd_bus_message_close_container(msg);
		if (ret < 0)
			std::cerr << "sd_bus_message_close_container failed" << std::endl;
	}
	sd_bus_message * msg;
};

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

std::string start_service(sd_bus * bus, const std::string & service_name, const std::vector<std::string> & args)
{
	assert(not args.empty());
	raii_sd_bus_error error;
	sd_bus_message * msg = nullptr;
	const char * destination = "org.freedesktop.systemd1";
	const char * path = "/org/freedesktop/systemd1";
	const char * interface = "org.freedesktop.systemd1.Manager";
	const char * member = "StartTransientUnit";
	const char * mode = "replace";

	int ret = sd_bus_message_new_method_call(bus, &msg, destination, path, interface, member);
	if (ret < 0)
		throw std::system_error(-ret, std::system_category(), "sd_bus_message_new_method_call failed");
	sd_msg_ptr raii_msg(msg);

	// Name, mode
	if (ret = sd_bus_message_append(msg, "ss", service_name.c_str(), mode); ret < 0)
		throw std::system_error(-ret, std::system_category(), "sd_bus_message_append failed");

	// Properties
	{
		container a(msg, 'a', "(sv)");
		if (ret = sd_bus_message_append(msg, "(sv)", "Description", "s", "Application spawned by WiVRn"); ret < 0)
			throw std::system_error(-ret, std::system_category(), "failed to set description");

		container r(msg, 'r', "sv");
		if (ret = sd_bus_message_append(msg, "s", "ExecStart"); ret < 0)
			throw std::system_error(-ret, std::system_category(), "sd_bus_message_append failed");
		container v(msg, 'v', "a(sasb)");
		container a1(msg, 'a', "(sasb)");
		container r1(msg, 'r', "sasb");
		if (ret = sd_bus_message_append(msg, "s", args[0].c_str()); ret < 0)
			throw std::system_error(-ret, std::system_category(), "sd_bus_message_append failed");

		{
			container a(msg, 'a', "s");
			for (const auto & arg: args)
			{
				if (ret = sd_bus_message_append(msg, "s", arg.c_str()); ret < 0)
					throw std::system_error(-ret, std::system_category(), "sd_bus_message_append failed");
			}
		}

		if (ret = sd_bus_message_append(msg, "b", false); ret < 0)
			throw std::system_error(-ret, std::system_category(), "sd_bus_message_append failed");
	}

	// aux (empty)
	if (ret = sd_bus_message_append(msg, "a(sa(sv))", 0); ret < 0)
		throw std::system_error(-ret, std::system_category(), "sd_bus_message_append failed");

	sd_bus_message * reply = nullptr;
	if (ret = sd_bus_call(bus, msg, 0, &error, &reply); ret < 0)
		throw std::system_error(-ret, std::system_category(), std::format("sd_bus_call failed: {}", error.data.message));
	sd_msg_ptr raii_reply(reply);

	char * object;
	if (ret = sd_bus_message_read(reply, "o", &object); ret < 0)
		throw std::system_error(-ret, std::system_category(), "sd_bus_message_read failed");

	return object;
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

pid_t wivrn::start_unit_file(const std::vector<std::string> & args)
{
	if (args.empty())
		return 0;

	std::string service_name = std::format("wivrn-application-{}.service", std::chrono::steady_clock::now().time_since_epoch().count());
	pid_t pid;
	auto bus = get_user_bus();
	int ret = get_service_pid(bus.get(), service_name, pid);

	if (ret < 0 || pid == 0)
	{
		auto obj = start_service(bus.get(), service_name, args);
		std::cout << "started service: " << obj << std::endl;

		// Wait until the service is active
		while (!is_service_active(bus.get(), service_name))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		// Try to get the PID again
		ret = get_service_pid(bus.get(), service_name, pid);
		if (ret < 0)
			throw std::system_error(-ret, std::system_category(), "failed to get application PID");
	}

	return pid;
}

#endif

static int exec_application(std::vector<std::string> args)
{
	if (args.empty())
		return 0;

	std::vector<std::string> tmp;
	std::vector<char *> argv;
	argv.reserve(args.size() + 3);

	if (wivrn::flatpak_key(wivrn::flatpak::section::session_bus_policy, "org.freedesktop.Flatpak") == "talk")
	{
		tmp.push_back("flatpak-spawn");
		tmp.push_back("--host");
		for (auto & arg: tmp)
			argv.push_back(arg.data());
	}

	for (auto & arg: args)
		argv.push_back(arg.data());
	argv.push_back(nullptr);

	std::string executable = argv.front();

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

pid_t wivrn::fork_application(const std::vector<std::string> & args)
{
	if (args.empty())
		return 0;

	pid_t application_pid = fork();
	if (application_pid < 0)
		throw std::system_error(errno, std::system_category(), "fork");

	if (application_pid == 0)
	{
		// Start a new process group so that all processes started by the
		// application can be signaled
		setpgrp();

		return exec_application(args);
	}

	return application_pid;
}
