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

#include "ipc_server_cb.h"
#include "driver/wivrn_session.h"
#include "target_instance_wivrn.h"
#include "utils/method.h"

namespace wivrn
{
void ipc_server_cb::init_failed(xrt_result res)
{
}

void ipc_server_cb::mainloop_entering(ipc_server * server, xrt_instance * xrt_inst)
{
	auto inst = static_cast<wivrn::instance *>(xrt_inst);
	inst->set_ipc_server(server);
}

void ipc_server_cb::mainloop_leaving(ipc_server * server, xrt_instance * xrt_inst)
{
	auto inst = static_cast<wivrn::instance *>(xrt_inst);
	inst->set_ipc_server(nullptr);
}

ipc_server_cb::ipc_server_cb() :
        ipc_server_callbacks{
                .init_failed = method_pointer2<&ipc_server_cb::init_failed>,
                .mainloop_entering = method_pointer2<&ipc_server_cb::mainloop_entering>,
                .mainloop_leaving = method_pointer2<&ipc_server_cb::mainloop_leaving>,
        }
{}
} // namespace wivrn
