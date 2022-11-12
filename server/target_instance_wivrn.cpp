// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared default implementation of the instance with compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_gfx_native.h"
#include "xrt/xrt_system.h"
#include "xrt/xrt_config_build.h"

#include "os/os_time.h"

#include "util/u_debug.h"
#include "util/u_trace_marker.h"
#include "util/u_system_helpers.h"

#include <assert.h>

#include "driver/wivrn_session.h"

/*
 *
 * Internal functions.
 *
 */

extern std::unique_ptr<TCP> tcp;

static xrt_result_t
wivrn_instance_create_system(struct xrt_instance *xinst,
                         struct xrt_system_devices **out_xsysd,
                         struct xrt_system_compositor **out_xsysc)
{
	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);
	assert(out_xsysc == NULL || *out_xsysc == NULL);

	struct xrt_system_compositor *xsysc = NULL;
	struct xrt_system_devices *xsysd = xrt::drivers::wivrn::wivrn_session::create_session(std::move(*tcp));
	tcp.reset();

	if (!xsysd)
		return XRT_ERROR_DEVICE_CREATION_FAILED;

	xrt_result_t xret = XRT_SUCCESS;


	struct xrt_device *head = xsysd->roles.head;

	if (xret == XRT_SUCCESS && xsysc == NULL) {
		xret = xrt_gfx_provider_create_system(head, &xsysc);
	}

	if (xret != XRT_SUCCESS) {
		xrt_system_devices_destroy(&xsysd);
		return xret;
	}

	*out_xsysd = xsysd;
	*out_xsysc = xsysc;

	return xret;
}

static void
wivrn_instance_destroy(struct xrt_instance *xinst)
{
	delete xinst;
}

static xrt_result_t
wivrn_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
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
xrt_instance_create(struct xrt_instance_info *ii, struct xrt_instance **out_xinst)
{
	u_trace_marker_init();

	struct xrt_instance *xinst = U_TYPED_CALLOC(struct xrt_instance);
	xinst->create_system = wivrn_instance_create_system;
	xinst->get_prober = wivrn_instance_get_prober;
	xinst->destroy = wivrn_instance_destroy;

	xinst->startup_timestamp = os_monotonic_get_ns();

	*out_xinst = xinst;

	return XRT_SUCCESS;
}
