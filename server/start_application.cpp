/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "start_application.h"

#include "utils/flatpak.h"

#include <cassert>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace wivrn
{

children_manager::~children_manager() {}

static std::vector<std::string> unescape_string(const std::string & app_string)
{
	std::vector<std::string> app;
	app.emplace_back();

	bool seen_backslash = false;
	bool seen_single_quote = false;
	bool seen_double_quote = false;
	for (auto c: app_string)
	{
		if (seen_backslash)
		{
			app.back() += c;
			seen_backslash = false;
		}
		else if (seen_single_quote)
		{
			if (c == '\'')
				seen_single_quote = false;
			else if (c == '\\')
				seen_backslash = true;
			else
				app.back() += c;
		}
		else if (seen_double_quote)
		{
			if (c == '"')
				seen_double_quote = false;
			else if (c == '\\')
				seen_backslash = true;
			else
				app.back() += c;
		}
		else
		{
			switch (c)
			{
				case '\\':
					seen_backslash = true;
					break;
				case '\'':
					seen_single_quote = true;
					break;
				case '"':
					seen_double_quote = true;
					break;
				case ' ':
					if (app.back() != "")
						app.emplace_back();
					break;
				default:
					app.back() += c;
			}
		}
	}

	if (app.back() == "")
		app.pop_back();

	return app;
}

void children_manager::start_application(const std::string & exec, const std::optional<std::string> & path)
{
	start_application(unescape_string(exec), path);
}

forked_children::forked_children(std::function<void()> state_changed_cb) :
        state_changed_cb(state_changed_cb) {}

void forked_children::start_application(const std::vector<std::string> & args, const std::optional<std::string> & path)
{
	if (args.empty())
		return;

	pid_t application_pid = fork();

	if (application_pid < 0)
		throw std::system_error(errno, std::system_category(), "fork");

	if (application_pid > 0)
	{
		guint app_watch = g_child_watch_add(
		        application_pid,
		        [](pid_t pid, int status, void * self_) {
			        auto self = (forked_children *)self_;
			        bool empty = self->children.empty();
			        auto node = self->children.extract(pid);
			        if (not node)
			        {
				        std::cerr << "Failed to update child application information" << std::endl;
			        }
			        display_child_status(status, "Application");
			        const auto & watches = node.mapped();
			        g_source_remove(watches.child);
			        if (watches.kill)
				        g_source_remove(watches.kill);

			        if (self->state_changed_cb and self->children.empty() and not empty)
				        self->state_changed_cb();
		        },
		        this);
		children.emplace(application_pid, watches{.child = app_watch});
		return;
	}

	// Start a new process group so that all processes started by the
	// application can be signaled
	setpgrp();

	std::vector<std::string> tmp;
	std::vector<char *> argv;
	argv.reserve(args.size() + 3);

	if (wivrn::flatpak_key(wivrn::flatpak::section::session_bus_policy, "org.freedesktop.Flatpak") == "talk")
	{
		tmp.push_back("flatpak-spawn");
		tmp.push_back("--host");
		if (path)
			tmp.push_back("--directory=" + *path);
	}
	else
	{
		try
		{
			std::filesystem::current_path(*path);
		}
		catch (std::exception & e)
		{
			std::cerr << "Failed to set path to " << *path << ": " << e.what() << std::endl;
		}
	}

	tmp.insert(tmp.end(), args.begin(), args.end());
	for (auto & arg: tmp)
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
	perror("Failed to start application");
	exit(EXIT_FAILURE);
}

forked_children::~forked_children()
{
	for (auto & [pid, watches]: children)
	{
		g_source_remove(watches.child);
		if (watches.kill)
			g_source_remove(watches.kill);
	}
}

bool forked_children::running() const
{
	return not children.empty();
}

void forked_children::stop()
{
	for (auto & [pid, watches]: children)
	{
		kill(-pid, SIGTERM);
		intptr_t pid_ptr = pid;

		// Send SIGKILL after 1s if it is still running
		static_assert(sizeof(pid_t) < sizeof(intptr_t));
		watches.kill = g_timeout_add(
		        1000,
		        [](void * pid_) {
			        auto pid = (intptr_t)pid_;
			        assert(pid > 0);
			        kill(-pid, SIGKILL);
			        return G_SOURCE_REMOVE;
		        },
		        (void *)pid_ptr);
	}
}

void display_child_status(int wstatus, const std::string & name)
{
	std::cerr << name << " exited, exit status " << WEXITSTATUS(wstatus);
	if (WIFSIGNALED(wstatus))
	{
		std::cerr << ", received signal " << sigabbrev_np(WTERMSIG(wstatus)) << " ("
		          << strsignal(WTERMSIG(wstatus)) << ")"
		          << (WCOREDUMP(wstatus) ? ", core dumped" : "") << std::endl;
	}
	else
	{
		std::cerr << std::endl;
	}
}

} // namespace wivrn
