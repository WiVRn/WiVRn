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

#include "target_instance_wivrn.h"

#include "utils/method.h"

#include "xrt/xrt_instance.h"
#include "xrt/xrt_system.h"

#include "util/u_system.h"
#include "util/u_trace_marker.h"

#include <assert.h>

#include "driver/wivrn_session.h"
#include "wivrn_ipc.h"

namespace wivrn
{
xrt_result_t instance::create_system(
        struct xrt_system ** out_xsys,
        struct xrt_system_devices ** out_xsysd,
        struct xrt_space_overseer ** out_xspovrs,
        struct xrt_system_compositor ** out_xsysc)
{
	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);
	assert(out_xsysc == NULL || *out_xsysc == NULL);
	auto u_sys = u_system_create();
	*out_xsys = &u_sys->base;

	struct xrt_system_compositor * xsysc = NULL;
	auto res = wivrn::wivrn_session::create_session(
	        std::move(connection),
	        *u_sys,
	        out_xsysd,
	        out_xspovrs,
	        out_xsysc);
	if (res != XRT_SUCCESS)
		return res;
	session = (wivrn_session *)*out_xsysd;
	u_system_set_system_compositor(u_sys, *out_xsysc);
	return res;
}

xrt_result_t instance::get_prober(struct xrt_prober ** out_xp)
{
	*out_xp = nullptr;
	return XRT_ERROR_PROBER_NOT_SUPPORTED;
}

instance::instance() :
        xrt_instance{
                .create_system = method_pointer<&instance::create_system>,
                .get_prober = method_pointer<&instance::get_prober>,
                .destroy = [](xrt_instance * ptr) { delete (instance *)ptr; },
                .startup_timestamp = os_monotonic_get_ns(),
        }
{
}

void instance::set_ipc_server(ipc_server * server)
{
	assert(session);
	if (server)
		session->start(server);
	else
		session->stop();
}

} // namespace wivrn
xrt_result_t
xrt_instance_create(struct xrt_instance_info * ii, struct xrt_instance ** out_xinst)
{
	u_trace_marker_init();

	*out_xinst = new wivrn::instance();

	return XRT_SUCCESS;
}
