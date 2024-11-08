/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "sleep_inhibitor.h"
#include <gio/gio.h>
#include <glib.h>
#include <iostream>
#include <unistd.h>

sleep_inhibitor::sleep_inhibitor()
{
	GError * error = nullptr;
	GDBusProxy * proxy = g_dbus_proxy_new_for_bus_sync(
	        G_BUS_TYPE_SYSTEM,
	        G_DBUS_PROXY_FLAGS_NONE,
	        nullptr,
	        "org.freedesktop.login1",
	        "/org/freedesktop/login1",
	        "org.freedesktop.login1.Manager",
	        nullptr,
	        &error);

	if (error)
	{
		std::cerr << "Cannot create DBus proxy for org.freedesktop.login1: " << error->message << std::endl;
		g_error_free(error);
		return;
	}

	GUnixFDList * fd_list;

	GVariant * output = g_dbus_proxy_call_with_unix_fd_list_sync(
	        proxy,
	        "Inhibit",
	        g_variant_new(
	                "(ssss)",
	                "sleep:idle",                // What
	                "WiVRn",                     // Who
	                "A WiVRn session is active", // Why
	                "block"),                    // Mode
	        G_DBUS_CALL_FLAGS_NONE,
	        -1,       // timeout_msec
	        nullptr,  // fd_list
	        &fd_list, // out_fd_list
	        nullptr,  // cancellable
	        &error);

	if (error)
	{
		std::cerr << "Cannot inhibit sleep: " << error->message << std::endl;
		g_error_free(error);
		return;
	}

	int fd_index;
	g_variant_get(output, "(h)", &fd_index);

	int fd_count;
	int * fds = g_unix_fd_list_steal_fds(fd_list, &fd_count);

	if (fd_index < fd_count)
		fd = fds[fd_index];

	g_free(fds);

	g_variant_unref(output);
	g_object_unref(fd_list);
}

sleep_inhibitor::~sleep_inhibitor()
{
	if (fd >= 0)
		close(fd);
}
