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

#include "session.h"

#include "details/enumerate.h"
#include "xr.h"
#include <vulkan/vulkan.h>
#include <openxr/openxr_platform.h>

xr::session::session(xr::instance & inst, xr::system & sys, VkInstance vk_inst, VkPhysicalDevice pdev, VkDevice dev, int queue_family_index) :
        inst(&inst)
{
	XrGraphicsBindingVulkan2KHR vulkan_binding{};
	vulkan_binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;

	vulkan_binding.instance = vk_inst;
	vulkan_binding.physicalDevice = pdev;
	vulkan_binding.device = dev;
	vulkan_binding.queueFamilyIndex = queue_family_index;
	vulkan_binding.queueIndex = 0;

	XrSessionCreateInfo session_info{};
	session_info.type = XR_TYPE_SESSION_CREATE_INFO;
	session_info.next = &vulkan_binding;
	session_info.systemId = sys;
	CHECK_XR(xrCreateSession(inst, &session_info, &id));
}

std::vector<XrReferenceSpaceType> xr::session::get_reference_spaces() const
{
	return details::enumerate<XrReferenceSpaceType>(xrEnumerateReferenceSpaces, id);
}

xr::space xr::session::create_reference_space(XrReferenceSpaceType ref, const XrPosef & pose)
{
	XrReferenceSpaceCreateInfo create_info{};
	create_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	create_info.referenceSpaceType = ref;
	create_info.poseInReferenceSpace = pose;

	XrSpace s;
	CHECK_XR(xrCreateReferenceSpace(id, &create_info, &s));

	return s;
}

xr::space xr::session::create_action_space(XrAction action, const XrPosef & pose)
{
	XrActionSpaceCreateInfo create_info{};
	create_info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
	create_info.action = action;
	create_info.poseInActionSpace = pose;
	create_info.subactionPath = XR_NULL_PATH;

	XrSpace s;
	CHECK_XR(xrCreateActionSpace(id, &create_info, &s));

	return s;
}

std::vector<VkFormat> xr::session::get_swapchain_formats() const
{
	std::vector<VkFormat> formats;
	for (auto i: details::enumerate<int64_t>(xrEnumerateSwapchainFormats, id))
		formats.push_back(static_cast<VkFormat>(i));
	return formats;
}

XrFrameState xr::session::wait_frame()
{
	XrFrameWaitInfo wait_info{};
	wait_info.type = XR_TYPE_FRAME_WAIT_INFO;

	XrFrameState state{};
	state.type = XR_TYPE_FRAME_STATE;

	CHECK_XR(xrWaitFrame(id, &wait_info, &state));

	return state;
}

void xr::session::begin_frame()
{
	XrFrameBeginInfo begin_info{};
	begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;

	CHECK_XR(xrBeginFrame(id, &begin_info));
}

void xr::session::end_frame(XrTime display_time, const std::vector<XrCompositionLayerBaseHeader *> & layers, XrEnvironmentBlendMode blend_mode)
{
	XrFrameEndInfo end_info{};
	end_info.type = XR_TYPE_FRAME_END_INFO;
	end_info.displayTime = display_time;
	end_info.environmentBlendMode = blend_mode;
	end_info.layerCount = layers.size();
	end_info.layers = layers.data();

	CHECK_XR(xrEndFrame(id, &end_info));
}

void xr::session::begin_session(XrViewConfigurationType view_config)
{
	XrSessionBeginInfo begin_info{};
	begin_info.type = XR_TYPE_SESSION_BEGIN_INFO;
	begin_info.primaryViewConfigurationType = view_config;

	CHECK_XR(xrBeginSession(id, &begin_info));
}

void xr::session::end_session()
{
	CHECK_XR(xrEndSession(id));
}

std::pair<XrViewStateFlags, std::vector<XrView>> xr::session::locate_views(XrViewConfigurationType view_config_type,
                                                                           XrTime display_time,
                                                                           XrSpace space)
{
	XrViewLocateInfo view_locate_info{};
	view_locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
	view_locate_info.viewConfigurationType = view_config_type;
	view_locate_info.displayTime = display_time;
	view_locate_info.space = space;

	XrViewState view_state{};
	view_state.type = XR_TYPE_VIEW_STATE;

	auto views = details::enumerate<XrView>(xrLocateViews, id, &view_locate_info, &view_state);

	return {view_state.viewStateFlags, views};
}

std::string xr::session::get_current_interaction_profile(const std::string & path)
{
	XrInteractionProfileState state{};
	state.type = XR_TYPE_INTERACTION_PROFILE_STATE;
	CHECK_XR(xrGetCurrentInteractionProfile(id, inst->string_to_path(path), &state));

	return inst->path_to_string(state.interactionProfile);
}

void xr::session::attach_actionsets(const std::vector<XrActionSet> & actionsets)
{
	XrSessionActionSetsAttachInfo attach_info{};
	attach_info.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	attach_info.countActionSets = actionsets.size();
	attach_info.actionSets = actionsets.data();

	CHECK_XR(xrAttachSessionActionSets(id, &attach_info));
}

std::vector<std::string> xr::session::sources_for_action(XrAction action)
{
	XrBoundSourcesForActionEnumerateInfo action_info{
	        .type = XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO,
	        .action = action,
	};

	auto sources = xr::details::enumerate<XrPath>(xrEnumerateBoundSourcesForAction, id, &action_info);

	std::vector<std::string> sources_name;
	sources_name.reserve(sources.size());
	for (XrPath path: sources)
	{
		sources_name.push_back(inst->path_to_string(path));
	}

	return sources_name;
}

std::vector<std::string> xr::session::localized_sources_for_action(XrAction action, XrInputSourceLocalizedNameFlags components)
{
	XrBoundSourcesForActionEnumerateInfo action_info{
	        .type = XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO,
	        .action = action,
	};

	auto sources = xr::details::enumerate<XrPath>(xrEnumerateBoundSourcesForAction, id, &action_info);

	std::vector<std::string> sources_name;
	sources_name.reserve(sources.size());
	for (XrPath path: details::enumerate<XrPath>(xrEnumerateBoundSourcesForAction, id, &action_info))
	{
		XrInputSourceLocalizedNameGetInfo name_info{
		        .type = XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO,
		        .sourcePath = path,
		        .whichComponents = components,
		};

		sources_name.push_back(details::enumerate<char>(xrGetInputSourceLocalizedName, id, &name_info));
	}

	return sources_name;
}

float xr::session::get_current_refresh_rate()
{
	auto xrGetDisplayRefreshRateFB = inst->get_proc<PFN_xrGetDisplayRefreshRateFB>("xrGetDisplayRefreshRateFB");

	float refresh_rate = 0;
	if (xrGetDisplayRefreshRateFB)
		CHECK_XR(xrGetDisplayRefreshRateFB(id, &refresh_rate));

	return refresh_rate;
}

std::vector<float> xr::session::get_refresh_rates()
{
	auto xrEnumerateDisplayRefreshRatesFB = inst->get_proc<PFN_xrEnumerateDisplayRefreshRatesFB>("xrEnumerateDisplayRefreshRatesFB");

	if (xrEnumerateDisplayRefreshRatesFB)
		return xr::details::enumerate<float>(xrEnumerateDisplayRefreshRatesFB, id);

	return {};
}

void xr::session::sync_actions(XrActionSet action_set, XrPath subaction_path)
{
	XrActiveActionSet active_action_set{
	        .actionSet = action_set,
	        .subactionPath = subaction_path};

	XrActionsSyncInfo sync_info{
	        .type = XR_TYPE_ACTIONS_SYNC_INFO,
	        .countActiveActionSets = 1,
	        .activeActionSets = &active_action_set,
	};

	CHECK_XR(xrSyncActions(id, &sync_info));
}

void xr::session::sync_actions(XrActionSet action_set, const std::string & subaction_path)
{
	sync_actions(action_set, inst->string_to_path(subaction_path));
}

xr::session::~session()
{
	if (id != XR_NULL_HANDLE)
		xrDestroySession(id);
}
