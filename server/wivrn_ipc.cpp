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

#include "wivrn_ipc.h"
#include "driver/wivrn_connection.h"

#include <algorithm>
#include <sys/mman.h>
#include <sys/wait.h>

std::unique_ptr<wivrn::wivrn_connection> connection;

struct cleanup_function
{
	void (*callback)(uintptr_t);
	uintptr_t userdata;
};

std::array<cleanup_function, 1024> * cleanup_functions; // In shared memory

std::optional<to_monado::packets> receive_from_main()
{
	return wivrn_ipc_socket_monado->receive();
}

void init_cleanup_functions()
{
	if (cleanup_functions)
		munmap(cleanup_functions, sizeof(*cleanup_functions));
	cleanup_functions = (decltype(cleanup_functions))mmap(nullptr, sizeof(*cleanup_functions), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);

	std::fill(cleanup_functions->begin(), cleanup_functions->end(), cleanup_function{});
}

void add_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata)
{
	for (auto & i: *cleanup_functions)
	{
		if (i.callback == nullptr)
		{
			i = {callback, userdata};
			return;
		}
	}
}

void remove_cleanup_function(void (*callback)(uintptr_t), uintptr_t userdata)
{
	for (auto & i: *cleanup_functions)
	{
		if (i.callback == callback && i.userdata == userdata)
		{
			i = {};
			return;
		}
	}
}

void run_cleanup_functions()
{
	// Don't fork if cleanup_functions is empty
	if (std::ranges::all_of(*cleanup_functions, [](auto & i) { return i.callback == nullptr; }))
		return;

	// Fork because pulseaudio doesn't like being initialized in the parent and the child
	// of a fork, and ipc_server_main() is called in a child process
	pid_t child = fork();

	if (child < 0)
	{
		perror("fork");
	}
	else if (child == 0)
	{
		for (auto & i: *cleanup_functions)
		{
			if (i.callback)
			{
				i.callback(i.userdata);
			}
			i = {};
		}
		exit(0);
	}
	else if (child > 0)
	{
		int wstatus = 0;
		waitpid(child, &wstatus, 0);
	}
}
