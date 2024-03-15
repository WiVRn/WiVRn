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

#include "passthrough.h"
#include "session.h"
#include "openxr/openxr.h"

xr::passthrough_fb::passthrough_fb(instance & inst, session & s)
{
	PFN_xrCreatePassthroughFB xrCreatePassthroughFB = inst.get_proc<PFN_xrCreatePassthroughFB>("xrCreatePassthroughFB");
	PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB = inst.get_proc<PFN_xrCreatePassthroughLayerFB>("xrCreatePassthroughLayerFB");

	xrDestroyPassthroughFB = inst.get_proc<PFN_xrDestroyPassthroughFB>("xrDestroyPassthroughFB");
	xrPassthroughStartFB = inst.get_proc<PFN_xrPassthroughStartFB>("xrPassthroughStartFB");
	xrPassthroughPauseFB = inst.get_proc<PFN_xrPassthroughPauseFB>("xrPassthroughPauseFB");
	xrPassthroughLayerPauseFB = inst.get_proc<PFN_xrPassthroughLayerPauseFB>("xrPassthroughLayerPauseFB");
	xrPassthroughLayerResumeFB = inst.get_proc<PFN_xrPassthroughLayerResumeFB>("xrPassthroughLayerResumeFB");

	XrPassthroughCreateInfoFB info{
		.type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB,
		.flags = 0
	};

	CHECK_XR(xrCreatePassthroughFB(s, &info, &id));

	XrPassthroughLayerCreateInfoFB layer_info{
		.type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
		.passthrough = id,
		.flags = 0,
		.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB
	};

	CHECK_XR(xrCreatePassthroughLayerFB(s, &layer_info, &passthrough_layer));

	composition_layer = XrCompositionLayerPassthroughFB{
		.type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
		.flags = 0,
		.space = XR_NULL_HANDLE,
		.layerHandle = passthrough_layer
	};
}

xr::passthrough_fb::~passthrough_fb()
{
	if (passthrough_layer && xrDestroyPassthroughLayerFB)
		xrDestroyPassthroughLayerFB(passthrough_layer);

	if (id != XR_NULL_HANDLE && xrDestroyPassthroughFB)
		xrDestroyPassthroughFB(id);
}

void xr::passthrough_fb::start()
{
	CHECK_XR(xrPassthroughStartFB(id));
	CHECK_XR(xrPassthroughLayerResumeFB(passthrough_layer));
}

void xr::passthrough_fb::pause()
{
	CHECK_XR(xrPassthroughLayerPauseFB(passthrough_layer));
	CHECK_XR(xrPassthroughPauseFB(id));
}

xr::passthrough_htc::passthrough_htc(instance & inst, session & s)
{
	PFN_xrCreatePassthroughHTC xrCreatePassthroughHTC = inst.get_proc<PFN_xrCreatePassthroughHTC>("xrCreatePassthroughHTC");
	xrDestroyPassthroughHTC = inst.get_proc<PFN_xrDestroyPassthroughHTC>("xrDestroyPassthroughHTC");

	XrPassthroughCreateInfoHTC info{
		.type = XR_TYPE_PASSTHROUGH_CREATE_INFO_HTC,
		.form = XR_PASSTHROUGH_FORM_PLANAR_HTC
	};

	CHECK_XR(xrCreatePassthroughHTC(s, &info, &id));

	composition_layer = XrCompositionLayerPassthroughHTC{
		.type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_HTC,
		.layerFlags = 0,
		.space = XR_NULL_HANDLE,
		.passthrough = id,
		.color = {
			.type = XR_TYPE_PASSTHROUGH_COLOR_HTC,
			.alpha = 1
		}
	};
}

xr::passthrough_htc::~passthrough_htc()
{
	if (id != XR_NULL_HANDLE && xrDestroyPassthroughHTC)
		xrDestroyPassthroughHTC(id);
}

