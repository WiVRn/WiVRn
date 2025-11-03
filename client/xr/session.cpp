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
#include "openxr/openxr.h"
#include "utils/contains.h"
#include "xr/instance.h"
#include "xr/system.h"
#include "xr/to_string.h"
#include <ranges>
#include <vulkan/vulkan.h>
#include <openxr/openxr_platform.h>

xr::session::session(xr::instance & inst, xr::system & sys, vk::raii::Instance & vk_inst, vk::raii::PhysicalDevice & pdev, vk::raii::Device & dev, thread_safe<vk::raii::Queue> & queue, int queue_family_index) :
        inst(&inst),
        queue(&queue)
{
	XrGraphicsBindingVulkan2KHR vulkan_binding{
	        .type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR,

	        .instance = *vk_inst,
	        .physicalDevice = *pdev,
	        .device = *dev,
	        .queueFamilyIndex = (uint32_t)queue_family_index,
	        .queueIndex = 0,
	};

	XrSessionCreateInfo session_info{
	        .type = XR_TYPE_SESSION_CREATE_INFO,
	        .next = &vulkan_binding,
	        .systemId = sys,
	};

	CHECK_XR(xrCreateSession(inst, &session_info, &id));

	if (inst.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		xrGetDisplayRefreshRateFB = inst.get_proc<PFN_xrGetDisplayRefreshRateFB>("xrGetDisplayRefreshRateFB");
		xrEnumerateDisplayRefreshRatesFB = inst.get_proc<PFN_xrEnumerateDisplayRefreshRatesFB>("xrEnumerateDisplayRefreshRatesFB");
		xrRequestDisplayRefreshRateFB = inst.get_proc<PFN_xrRequestDisplayRefreshRateFB>("xrRequestDisplayRefreshRateFB");
	}

	if (inst.has_extension(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))
		xrPerfSettingsSetPerformanceLevelEXT = inst.get_proc<PFN_xrPerfSettingsSetPerformanceLevelEXT>("xrPerfSettingsSetPerformanceLevelEXT");
}

std::vector<XrReferenceSpaceType> xr::session::get_reference_spaces() const
{
	return details::enumerate<XrReferenceSpaceType>(xrEnumerateReferenceSpaces, id);
}

xr::space xr::session::create_reference_space(XrReferenceSpaceType ref, const XrPosef & pose)
{
	XrReferenceSpaceCreateInfo create_info{
	        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
	        .referenceSpaceType = ref,
	        .poseInReferenceSpace = pose,
	};

	XrSpace s;
	CHECK_XR(xrCreateReferenceSpace(id, &create_info, &s));

	return s;
}

xr::space xr::session::create_action_space(XrAction action, const XrPosef & pose)
{
	XrActionSpaceCreateInfo create_info{
	        .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
	        .action = action,
	        .subactionPath = XR_NULL_PATH,
	        .poseInActionSpace = pose,
	};

	XrSpace s;
	CHECK_XR(xrCreateActionSpace(id, &create_info, &s));

	return s;
}

xr::hand_tracker xr::session::create_hand_tracker(XrHandEXT hand, XrHandJointSetEXT hand_joint_set)
{
	return {
	        *inst,
	        *this,
	        XrHandTrackerCreateInfoEXT{
	                .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
	                .next = nullptr,
	                .hand = hand,
	                .handJointSet = hand_joint_set,
	        }};
}

std::vector<vk::Format> xr::session::get_swapchain_formats() const
{
	std::vector<vk::Format> formats;
	for (auto i: details::enumerate<int64_t>(xrEnumerateSwapchainFormats, id))
		formats.push_back(static_cast<vk::Format>(i));
	return formats;
}

XrFrameState xr::session::wait_frame()
{
	XrFrameWaitInfo wait_info{
	        .type = XR_TYPE_FRAME_WAIT_INFO,
	};

	XrFrameState state{
	        .type = XR_TYPE_FRAME_STATE,
	};

	CHECK_XR(xrWaitFrame(id, &wait_info, &state));

	return state;
}

void xr::session::begin_frame()
{
	XrFrameBeginInfo begin_info{
	        .type = XR_TYPE_FRAME_BEGIN_INFO,
	};

	auto lock = queue->lock();
	CHECK_XR(xrBeginFrame(id, &begin_info));
}

void xr::session::end_frame(XrTime display_time, const std::vector<XrCompositionLayerBaseHeader *> & layers, XrEnvironmentBlendMode blend_mode)
{
	XrFrameEndInfo end_info{
	        .type = XR_TYPE_FRAME_END_INFO,
	        .displayTime = display_time,
	        .environmentBlendMode = blend_mode,
	        .layerCount = (uint32_t)layers.size(),
	        .layers = layers.data(),
	};

	auto lock = queue->lock();
	CHECK_XR(xrEndFrame(id, &end_info));
}

void xr::session::begin_session(XrViewConfigurationType view_config)
{
	XrSessionBeginInfo begin_info{
	        .type = XR_TYPE_SESSION_BEGIN_INFO,
	        .primaryViewConfigurationType = view_config,
	};

	CHECK_XR(xrBeginSession(id, &begin_info));
}

void xr::session::end_session()
{
	CHECK_XR(xrEndSession(id));
}

XrViewStateFlags xr::session::locate_views(XrViewConfigurationType view_config_type,
                                           XrTime display_time,
                                           XrSpace space,
                                           std::vector<XrView> & views)
{
	XrViewLocateInfo view_locate_info{
	        .type = XR_TYPE_VIEW_LOCATE_INFO,
	        .viewConfigurationType = view_config_type,
	        .displayTime = display_time,
	        .space = space,
	};

	XrViewState view_state{
	        .type = XR_TYPE_VIEW_STATE,
	};

	details::enumerate(xrLocateViews, views, id, &view_locate_info, &view_state);

	return view_state.viewStateFlags;
}

std::pair<XrViewStateFlags, std::vector<XrView>> xr::session::locate_views(XrViewConfigurationType view_config_type,
                                                                           XrTime display_time,
                                                                           XrSpace space)
{
	std::vector<XrView> views(2, XrView{.type = XR_TYPE_VIEW});
	return {locate_views(view_config_type, display_time, space, views), std::move(views)};
}

std::string xr::session::get_current_interaction_profile(const std::string & path)
{
	XrInteractionProfileState state{
	        .type = XR_TYPE_INTERACTION_PROFILE_STATE,
	};

	CHECK_XR(xrGetCurrentInteractionProfile(id, inst->string_to_path(path), &state));

	return inst->path_to_string(state.interactionProfile);
}

void xr::session::attach_actionsets(const std::vector<XrActionSet> & actionsets)
{
	XrSessionActionSetsAttachInfo attach_info{
	        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
	        .countActionSets = (uint32_t)actionsets.size(),
	        .actionSets = actionsets.data(),
	};

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
	for (XrPath path: sources)
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
	assert(inst->has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME));

	float refresh_rate = 0;
	if (xrGetDisplayRefreshRateFB)
		CHECK_XR(xrGetDisplayRefreshRateFB(id, &refresh_rate));

	return refresh_rate;
}

std::vector<float> xr::session::get_refresh_rates()
{
	if (xrEnumerateDisplayRefreshRatesFB)
	{
		try
		{
			return xr::details::enumerate<float>(xrEnumerateDisplayRefreshRatesFB, id);
		}
		catch (...)
		{
			// Return no available refresh rate in case of error
		}
	}

	return {};
}

void xr::session::set_refresh_rate(float refresh_rate)
{
	if (xrRequestDisplayRefreshRateFB)
	{
		if (auto res = xrRequestDisplayRefreshRateFB(id, refresh_rate); res != XR_SUCCESS)
			spdlog::warn("Refresh rate change failed: {}", xr::to_string(res));
	}
}

void xr::session::sync_actions(std::span<XrActionSet> action_sets)
{
	std::vector<XrActiveActionSet> active_action_sets(action_sets.size());

	for (auto && [i, j]: std::views::zip(active_action_sets, action_sets))
	{
		i.actionSet = j;
		i.subactionPath = XR_NULL_PATH;
	}

	XrActionsSyncInfo sync_info{
	        .type = XR_TYPE_ACTIONS_SYNC_INFO,
	        .countActiveActionSets = (uint32_t)active_action_sets.size(),
	        .activeActionSets = active_action_sets.data(),
	};

	CHECK_XR(xrSyncActions(id, &sync_info));
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

void xr::session::enable_passthrough(xr::system & system)
{
	if (not std::holds_alternative<std::monostate>(passthrough))
		return;

	if (system.passthrough_supported() == xr::passthrough_type::none)
		return;

	if (inst->has_extension(XR_FB_PASSTHROUGH_EXTENSION_NAME))
	{
		passthrough.emplace<xr::passthrough_fb>(*inst, *this);
	}
	else if (inst->has_extension(XR_HTC_PASSTHROUGH_EXTENSION_NAME))
	{
		passthrough.emplace<xr::passthrough_htc>(*inst, *this);
	}
	else if (utils::contains(system.environment_blend_modes(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO), XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND))
	{
		passthrough.emplace<xr::passthrough_alpha_blend>();
	}
}

void xr::session::disable_passthrough()
{
	if (std::holds_alternative<std::monostate>(passthrough))
		return;
	passthrough.emplace<std::monostate>();
}

void xr::session::set_performance_level(XrPerfSettingsDomainEXT domain, XrPerfSettingsLevelEXT level)
{
	if (xrPerfSettingsSetPerformanceLevelEXT)
		xrPerfSettingsSetPerformanceLevelEXT(*this, domain, level);
}
