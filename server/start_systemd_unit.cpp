/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "start_systemd_unit.h"
#include "systemd_manager.h"
#include "systemd_unit.h"
#include "utils/strings.h"
#include <filesystem>
#include <gio/gio.h>
#include <glib-object.h>
#include <iostream>

#include <chrono>
#include <format>

namespace wivrn
{

void systemd_units_manager::deleter::operator()(systemdManager * obj)
{
	g_object_unref(obj);
}
void systemd_units_manager::deleter::operator()(unitUnit * obj)
{
	g_object_unref(obj);
}

systemd_units_manager::systemd_units_manager(GDBusConnection * connection, std::function<void()> state_changed_cb) :
        connection(connection), state_changed_cb(state_changed_cb)
{
	GError * error = nullptr;
	proxy.reset(systemd_manager_proxy_new_sync(
	        connection,
	        G_DBUS_PROXY_FLAGS_NONE,
	        "org.freedesktop.systemd1",
	        "/org/freedesktop/systemd1",
	        nullptr,
	        &error));

	if (error)
	{
		std::runtime_error e(std::string("Failed to connect to systemd user session: ") + error->message);
		g_error_free(error);
		throw e;
	}

	systemd_manager_call_subscribe_sync(
	        proxy.get(),
	        nullptr,
	        &error);

	if (error)
	{
		std::runtime_error e(std::string("Failed to subscribe to systemd messages: ") + error->message);
		g_error_free(error);
		throw e;
	}

	g_signal_connect(proxy.get(),
	                 "job-removed",
	                 G_CALLBACK(&on_job_removed),
	                 this);
}

bool systemd_units_manager::running() const
{
	return not(jobs.empty() and units.empty());
}

void systemd_units_manager::stop()
{
	for (auto & unit: units)
	{
		unit_unit_call_stop(unit.get(),
		                    "replace",
		                    nullptr,
		                    nullptr,
		                    nullptr);
	}
}

void systemd_units_manager::on_job_removed(
        systemdManagerProxy * connection,
        guint32 id,
        gchar * job,
        gchar * unit,
        gchar * result,
        void * self_)
{
	auto self = (systemd_units_manager *)(self_);

	auto node = self->jobs.extract(job);
	if (node.empty())
		return;

	if (strcmp(result, "done"))
	{
		std::cerr << "Failed to start application " << node.mapped() << ": " << result << std::endl;
		return;
	}

	std::string object = "/org/freedesktop/systemd1/unit/";
	for (char * c = unit; *c != '\0'; ++c)
	{
		if (std::isalnum(*c))
			object += *c;
		else
			object += std::format("_{:02x}", *c);
	}

	unit_unit_proxy_new(
	        self->connection,
	        G_DBUS_PROXY_FLAGS_NONE,
	        "org.freedesktop.systemd1",
	        object.c_str(),
	        nullptr,
	        [](GObject * source_object, GAsyncResult * res, void * self_) {
		        auto self = (systemd_units_manager *)(self_);

		        GError * error = nullptr;

		        std::unique_ptr<unitUnit, deleter> unit(unit_unit_proxy_new_finish(res, &error));
		        if (error)
		        {
			        std::cerr << "failed to create unit proxy: " << error->message << std::endl;
			        g_error_free(error);
			        return;
		        }

		        g_signal_connect(unit.get(),
		                         "notify::active-state",
		                         G_CALLBACK(on_unit_result),
		                         self);

		        bool changed = self->units.empty();

		        self->units.emplace(std::move(unit));
		        if (changed and self->state_changed_cb)
			        self->state_changed_cb();
	        },
	        self);
}

void systemd_units_manager::on_unit_result(
        unitUnit * unit,
        const GParamSpec * pspec,
        void * self_)
{
	auto self = (systemd_units_manager *)(self_);
	std::string_view state = unit_unit_get_active_state(unit);
	bool empty = self->units.empty();
	if (state == "inactive" or state == "failed")
		std::erase_if(self->units, [&](auto & o) { return o.get() == unit; });
	if (self->state_changed_cb and self->units.empty() and not empty)
		self->state_changed_cb();
}

namespace
{
struct variant_builder
{
	GVariantBuilder builder;

	variant_builder(const GVariantType * type)
	{
		g_variant_builder_init(&builder, type);
	}
	~variant_builder()
	{
		g_variant_builder_clear(&builder);
	}
	auto operator&()
	{
		return &builder;
	}

	GVariant * end()
	{
		return g_variant_builder_end(&builder);
	}
};

// Almost the same as canonical, but don't try to follow symlinks
std::filesystem::path normalize(std::filesystem::path path)
{
	if (path.is_relative())
		path = std::filesystem::current_path() / path;

	std::vector<std::filesystem::path> items;
	for (auto && item: path)
	{
		if (item.empty() or item == ".")
			continue;
		if (item == "..")
		{
			if (items.size() < 2)
				throw std::runtime_error("too many ..");
			items.pop_back();
		}
		else
			items.push_back(std::move(item));
	}
	path.clear();
	for (const auto & item: items)
		path /= item;
	return path;
}

}; // namespace

void systemd_units_manager::start_application(const std::vector<std::string> & args, const std::optional<std::string> & path)
{
	if (args.empty())
		return;

	std::cerr << "Launching";
	for (const auto & i: args)
		std::cerr << " " << std::quoted(i);
	std::cerr << std::endl;

	std::string service_name = std::format("wivrn-application-{}.service", std::chrono::steady_clock::now().time_since_epoch().count());

	variant_builder b(G_VARIANT_TYPE("a(sv)"));
	g_variant_builder_add(&b,
	                      "(sv)",
	                      "Description",
	                      g_variant_new("s", "Application spawned by WiVRn"));

	variant_builder argv(G_VARIANT_TYPE("as"));
	for (const auto & arg: args)
		g_variant_builder_add(&argv, "s", arg.c_str());
	auto exec_start = g_variant_new("(s@asb)",
	                                args.front().c_str(),
	                                g_variant_new("as", &argv),
	                                false);
	g_variant_builder_add(&b,
	                      "(sv)",
	                      "ExecStart",
	                      g_variant_new_array(
	                              nullptr,
	                              &exec_start,
	                              1));

	if (path)
		g_variant_builder_add(&b,
		                      "(sv)",
		                      "WorkingDirectory",
		                      g_variant_new("s", path->c_str()));

	if (auto path = std::getenv("PATH"))
	{
		variant_builder pathv(G_VARIANT_TYPE("as"));
		for (auto item: utils::split(path, ":"))
		{
			if (item.empty())
				continue;
			try
			{
				item = normalize(item);
				g_variant_builder_add(&pathv, "s", item.c_str());
			}
			catch (std::exception & e)
			{
				std::cerr << "Failed to normalize element " << std::quoted(item) << " from $PATH: " << e.what() << std::endl;
			}
		}
		g_variant_builder_add(&b,
		                      "(sv)",
		                      "ExecSearchPath",
		                      g_variant_new("as", &pathv));
	}

	auto properties = b.end();
	auto aux = g_variant_new_array(G_VARIANT_TYPE("(sa(sv))"), nullptr, 0);

	gchar * job = nullptr;
	GError * error = nullptr;
	systemd_manager_call_start_transient_unit_sync(
	        proxy.get(), service_name.c_str(), "replace", properties, aux, &job, nullptr, &error);

	if (job)
	{
		jobs.emplace(job, std::move(service_name));
		g_free(job);
	}

	if (error)
	{
		std::runtime_error e(std::format("start transient unit failed: {}", error->message));
		g_error_free(error);
		throw e;
	}
}
} // namespace wivrn
