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

#ifdef XR_USE_PLATFORM_ANDROID
#include <android_native_app_glue.h>
#endif

#include "error.h"
#include "utils/check.h"
#include "utils/handle.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace xr
{
union event
{
	XrEventDataBuffer header;
	XrEventDataInstanceLossPending loss_pending;
	XrEventDataInteractionProfileChanged interaction_profile_changed;
	XrEventDataReferenceSpaceChangePending space_changed_pending;
	XrEventDataSessionStateChanged state_changed;
	XrEventDataDisplayRefreshRateChangedFB refresh_rate_changed;
};
class instance : public utils::handle<XrInstance>
{
	std::string runtime_version;
	std::string runtime_name;
	std::unordered_set<std::string> loaded_extensions;

public:
#if defined(XR_USE_PLATFORM_ANDROID)
	explicit instance(std::string_view application_name, void * applicationVM, void * applicationActivity, std::vector<const char *> extensions = {});
#else
	explicit instance(std::string_view application_name, std::vector<const char *> extensions = {});
#endif

	instance() = default;
	instance(instance &&) = default;
	instance & operator=(instance &&) = default;
	~instance();

	const std::string & get_runtime_version() const
	{
		return runtime_version;
	}
	const std::string & get_runtime_name() const
	{
		return runtime_name;
	}

	template <typename F>
	F get_proc(const char * name)
	{
		F f;
		XrResult result = xrGetInstanceProcAddr(id, name, (PFN_xrVoidFunction *)&f);
		if (!XR_SUCCEEDED(result))
		{
			throw std::system_error(result, vk::error_category(), std::string("xrGetInstanceProcAddr(") + name + ")");
		}
		return f;
	}

	bool poll_event(event &);

	XrPath string_to_path(const std::string & path);
	std::string path_to_string(XrPath path);

	void suggest_bindings(const std::string & interaction_profile,
	                      std::vector<XrActionSuggestedBinding> & bindings);

	XrTime now();

	static std::vector<XrExtensionProperties> extensions(const char * layer_name = nullptr);

	bool has_extension(const std::string & extension_name)
	{
		return loaded_extensions.find(extension_name) != loaded_extensions.end();
	}
};
} // namespace xr
