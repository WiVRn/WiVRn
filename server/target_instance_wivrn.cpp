// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared default implementation of the instance with compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_instance.h"
#include "xrt/xrt_system.h"

#include "os/os_time.h"

#include "util/u_system.h"
#include "util/u_trace_marker.h"

#include <assert.h>

#include "driver/wivrn_session.h"

/*
 *
 * Internal functions.
 *
 */

extern std::unique_ptr<TCP> tcp;

static xrt_result_t
wivrn_instance_create_system(struct xrt_instance * xinst,
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
	        std::move(*tcp),
	        *u_sys,
	        out_xsysd,
	        out_xspovrs,
	        out_xsysc);
	u_system_set_system_compositor(u_sys, *out_xsysc);
	tcp.reset();
	return res;
}

static void
wivrn_instance_destroy(struct xrt_instance * xinst)
{
	delete xinst;
}

static xrt_result_t
wivrn_instance_get_prober(struct xrt_instance * xinst, struct xrt_prober ** out_xp)
{
	*out_xp = nullptr;
	return XRT_ERROR_PROBER_NOT_SUPPORTED;
}

/*
 *
 * Exported function(s).
 *
 */

xrt_result_t
xrt_instance_create(struct xrt_instance_info * ii, struct xrt_instance ** out_xinst)
{
	u_trace_marker_init();

	*out_xinst = new xrt_instance{
	        .create_system = wivrn_instance_create_system,
	        .get_prober = wivrn_instance_get_prober,
	        .destroy = wivrn_instance_destroy,
	        .startup_timestamp = os_monotonic_get_ns(),
	};

	return XRT_SUCCESS;
}
