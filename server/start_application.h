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

#include <functional>
#include <glib.h>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

namespace wivrn
{

class children_manager
{
public:
	virtual ~children_manager();

	virtual void start_application(const std::vector<std::string> &) = 0;
	void start_application(const std::string &);

	virtual bool running() const = 0;

	virtual void stop() = 0;
};

// Start applications using fork+exec
class forked_children : public children_manager
{
	struct watches
	{
		guint child = 0;
		guint kill = 0;
	};
	std::unordered_map<pid_t, watches> children;
	std::function<void()> state_changed_cb;

public:
	forked_children(std::function<void()> state_changed_cb);
	virtual ~forked_children();

	void start_application(const std::vector<std::string> &) override;

	// true if any unit started by this object is still running
	bool running() const override;

	void stop() override;
};

void display_child_status(int wstatus, const std::string & name);
} // namespace wivrn
