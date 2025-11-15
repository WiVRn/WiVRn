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

#include "server/ipc_server.h"
#include "xrt/xrt_instance.h"

namespace wivrn
{
class wivrn_session;
class instance : public xrt_instance
{
	xrt_result_t is_system_available(bool *);
	xrt_result_t create_system(
	        xrt_system **,
	        xrt_system_devices **,
	        xrt_space_overseer **,
	        xrt_system_compositor **);

	xrt_result_t get_prober(xrt_prober **);

	wivrn_session * session = nullptr;
	ipc_server * server = nullptr;

public:
	using base = xrt_instance;
	instance();
	void set_ipc_server(ipc_server *);
};
} // namespace wivrn
