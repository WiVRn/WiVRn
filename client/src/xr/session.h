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

#pragma once

#include "utils/handle.h"
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

#include "space.h"

namespace xr
{
class instance;
class system;

class session : public utils::handle<XrSession>
{
	instance * inst = nullptr;

public:
	session() = default;
	session(instance &, system &, VkInstance, VkPhysicalDevice, VkDevice, int queue_family_index);
	session(session &&) = default;
	session & operator=(session &&) = default;

	~session();

	std::vector<XrReferenceSpaceType> get_reference_spaces() const;
	space create_reference_space(XrReferenceSpaceType ref, const XrPosef & pose = {{0, 0, 0, 1}, {0, 0, 0}});
	space create_action_space(XrAction action, const XrPosef & pose = {{0, 0, 0, 1}, {0, 0, 0}});

	std::vector<VkFormat> get_swapchain_formats() const;

	XrFrameState wait_frame();
	void begin_frame();
	void end_frame(XrTime display_time, const std::vector<XrCompositionLayerBaseHeader *> & layers, XrEnvironmentBlendMode blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE);

	void begin_session(XrViewConfigurationType view_config);
	void end_session();

	std::pair<XrViewStateFlags, std::vector<XrView>> locate_views(XrViewConfigurationType view_config_type,
	                                                              XrTime display_time,
	                                                              XrSpace space);

	std::string get_current_interaction_profile(const std::string & path);
	void attach_actionsets(const std::vector<XrActionSet> & actionsets);
	std::vector<std::string> sources_for_action(XrAction a);
	std::vector<std::string> localized_sources_for_action(XrAction action, XrInputSourceLocalizedNameFlags components = XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT | XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT | XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT);

	float get_current_refresh_rate();
	std::vector<float> get_refresh_rates();

	void sync_actions(XrActionSet action_set, XrPath subaction_path = XR_NULL_PATH);
	void sync_actions(XrActionSet action_set, const std::string & subaction_path);
};
} // namespace xr
