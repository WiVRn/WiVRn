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

#include "backtrace.h"

#include <cassert>
#include <cxxabi.h>
#include <dlfcn.h>
#include <span>
#include <spdlog/fmt/fmt.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <unwind.h>
#include <vector>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <link.h>

struct bt_state
{
	struct lib_info
	{
		std::string name;
		uintptr_t address;
		std::span<const Elf64_Phdr> headers;
	};

	size_t max_size;
	std::vector<void *> trace;
	std::unordered_map<std::string, lib_info> libs;
};

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context * context, void * arg)
{
	bt_state & state = *static_cast<bt_state *>(arg);
	uintptr_t pc = _Unwind_GetIP(context);
	if (pc)
	{
		if (state.trace.size() >= state.max_size)
		{
			return _URC_END_OF_STACK;
		}
		else
		{
			state.trace.push_back(reinterpret_cast<void *>(pc));
		}
	}
	return _URC_NO_REASON;
}

static int iterate_phdr_callback(dl_phdr_info * info, size_t size, void * arg)
{
	bt_state & state = *static_cast<bt_state *>(arg);

	std::string library = info->dlpi_name;
	auto slash = library.find_last_of('/');
	if (slash != std::string::npos)
		library = library.substr(slash + 1);

	state.libs.emplace(library, bt_state::lib_info{library, info->dlpi_addr, {info->dlpi_phdr, info->dlpi_phdr + info->dlpi_phnum}});

	return 0;
}

static std::string unmangle(const char * function)
{
	if (!function)
		return "(null)";

	int status;
	char * demangled = abi::__cxa_demangle(function, nullptr, nullptr, &status);

	if (status != 0)
		return function;

	std::string s = demangled;
	free(demangled);
	return s;
}

std::vector<utils::backtrace_entry> utils::backtrace(size_t max)
{
	bt_state state;
	state.max_size = max;

	dl_iterate_phdr(iterate_phdr_callback, &state);

	_Unwind_Backtrace(unwind_callback, &state);

	std::vector<backtrace_entry> trace;
	trace.reserve(state.trace.size());
	for (void * pc: state.trace)
	{
		Dl_info info;
		if (dladdr(pc, &info))
		{
			// size_t offset = (size_t)pc - (size_t)info.dli_saddr;
			// std::string function = unmangle(info.dli_sname);
			std::string library = info.dli_fname;

			auto slash = library.find_last_of('/');
			if (slash != std::string::npos)
				library = library.substr(slash + 1);

			auto it = state.libs.find(library);
			uintptr_t library_base = 0;
			if (it != state.libs.end())
				library_base = it->second.address;

			trace.push_back(backtrace_entry{
			        .library = library,
			        .library_base = library_base,
			        .pc = (uintptr_t)pc,
			});
			// fmt::format("{:#016x}: {}, {}+{:#x}", (uintptr_t)pc, library, function, offset));
		}
	}

	return trace;
}
