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

#pragma once

#include "start_application.h"

#include <gio/gio.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "systemd_manager.h"
#include "systemd_unit.h"

namespace wivrn
{

// Start applications in transient units
class systemd_units_manager : public children_manager
{
	struct deleter
	{
		void operator()(systemdManager *);
		void operator()(unitUnit *);
	};
	GDBusConnection * connection;
	std::unique_ptr<systemdManager, deleter> proxy;
	std::unordered_map<std::string, std::string> jobs;
	std::unordered_set<std::unique_ptr<unitUnit, deleter>> units;

	std::function<void()> state_changed_cb;

public:
	systemd_units_manager(GDBusConnection * connection, std::function<void()> state_changed_cb);

	// create and start the transient unit
	void start_application(const std::vector<std::string> &) override;

	// true if any unit started by this object is still running
	bool running() const override;

	void stop() override;

private:
	static void on_job_removed(
	        systemdManagerProxy * proxy,
	        guint32 id,
	        gchar * job,
	        gchar * unit,
	        gchar * result,
	        void * self);

	static void on_unit_result(
	        unitUnit * proxy,
	        const GParamSpec * pspec,
	        void * self);
};
} // namespace wivrn
