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

#include "server/ipc_server_interface.h"

namespace wivrn
{
class ipc_server_cb : public ipc_server_callbacks
{
	void init_failed(xrt_result);
	void mainloop_entering(ipc_server *, xrt_instance *);
	void mainloop_leaving(ipc_server *, xrt_instance *);
	void client_connected(ipc_server *, uint32_t);
	void client_disconnected(ipc_server *, uint32_t);

public:
	using base = void;
	ipc_server_cb();
};
} // namespace wivrn
