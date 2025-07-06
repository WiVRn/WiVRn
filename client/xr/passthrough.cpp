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
#include "openxr/openxr.h"
#include "session.h"
#include "xr/check.h"
#include "xr/instance.h"

static PFN_xrDestroyPassthroughLayerFB xrDestroyPassthroughLayerFB{};
static PFN_xrDestroyPassthroughFB xrDestroyPassthroughFB{};
static PFN_xrDestroyPassthroughHTC xrDestroyPassthroughHTC{};

XrResult xr::destroy_passthrough_layer_fb(XrPassthroughLayerFB id)
{
	return xrDestroyPassthroughLayerFB(id);
}
XrResult xr::destroy_passthrough_fb(XrPassthroughFB id)
{
	return xrDestroyPassthroughFB(id);
}
XrResult xr::destroy_passthrough_htc(XrPassthroughHTC id)
{
	return xrDestroyPassthroughHTC(id);
}

xr::passthrough_layer_fb::passthrough_layer_fb(instance & inst, session & s, const XrPassthroughLayerCreateInfoFB & info)
{
	xrDestroyPassthroughLayerFB = inst.get_proc<PFN_xrDestroyPassthroughLayerFB>("xrDestroyPassthroughLayerFB");

	PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB = inst.get_proc<PFN_xrCreatePassthroughLayerFB>("xrCreatePassthroughLayerFB");
	CHECK_XR(xrCreatePassthroughLayerFB(s, &info, &id));
}

static XrPassthroughFB create_passthrough_fb(xr::instance & inst, xr::session & s)
{
	PFN_xrCreatePassthroughFB xrCreatePassthroughFB = inst.get_proc<PFN_xrCreatePassthroughFB>("xrCreatePassthroughFB");
	xrDestroyPassthroughFB = inst.get_proc<PFN_xrDestroyPassthroughFB>("xrDestroyPassthroughFB");

	XrPassthroughFB id;
	XrPassthroughCreateInfoFB info{
	        .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB,
	        .flags = 0,
	};

	CHECK_XR(xrCreatePassthroughFB(s, &info, &id));
	return id;
}

xr::passthrough_fb::passthrough_fb(instance & inst, session & s) :
        utils::handle<XrPassthroughFB, destroy_passthrough_fb>(create_passthrough_fb(inst, s)),
        passthrough_layer(inst, s, XrPassthroughLayerCreateInfoFB{
                                           .type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
                                           .passthrough = id,
                                           .flags = 0,
                                           .purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB,
                                   })
{
	xrPassthroughStartFB = inst.get_proc<PFN_xrPassthroughStartFB>("xrPassthroughStartFB");
	xrPassthroughPauseFB = inst.get_proc<PFN_xrPassthroughPauseFB>("xrPassthroughPauseFB");
	xrPassthroughLayerPauseFB = inst.get_proc<PFN_xrPassthroughLayerPauseFB>("xrPassthroughLayerPauseFB");
	xrPassthroughLayerResumeFB = inst.get_proc<PFN_xrPassthroughLayerResumeFB>("xrPassthroughLayerResumeFB");

	composition_layer = XrCompositionLayerPassthroughFB{
	        .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
	        .flags = 0,
	        .space = XR_NULL_HANDLE,
	        .layerHandle = passthrough_layer,
	};
	start();
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
	        .form = XR_PASSTHROUGH_FORM_PLANAR_HTC,
	};

	CHECK_XR(xrCreatePassthroughHTC(s, &info, &id));

	composition_layer = XrCompositionLayerPassthroughHTC{
	        .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_HTC,
	        .layerFlags = 0,
	        .space = XR_NULL_HANDLE,
	        .passthrough = id,
	        .color = {
	                .type = XR_TYPE_PASSTHROUGH_COLOR_HTC,
	                .alpha = 1,
	        },
	};
}
