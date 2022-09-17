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

#include "utils/handle.h"
#include <vector>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace xr
{
class session;

class swapchain : public utils::handle<XrSwapchain>
{
public:
	struct image
	{
		VkImage image{};
		VkImageView view{};
	};

private:
	VkDevice device;
	uint32_t width_;
	uint32_t height_;
	int sample_count_;
	VkFormat format_;

	std::vector<image> images_;

public:
	swapchain() = default;
	swapchain(session &, VkDevice device, VkFormat format, uint32_t width, uint32_t height, int sample_count = 1);
	swapchain(swapchain &&) = default;
	swapchain(const swapchain &) = delete;
	swapchain & operator=(swapchain &&) = default;
	swapchain & operator=(const swapchain &) = delete;
	~swapchain();

	uint32_t width() const
	{
		return width_;
	}
	uint32_t height() const
	{
		return height_;
	}
	int sample_count() const
	{
		return sample_count_;
	}
	const std::vector<image> & images() const
	{
		return images_;
	};
	std::vector<image> & images()
	{
		return images_;
	}
	VkFormat format() const
	{
		return format_;
	}

	int acquire();
	bool wait(XrDuration timeout = XR_INFINITE_DURATION);
	void release();
};
} // namespace xr
