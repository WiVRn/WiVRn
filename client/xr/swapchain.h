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
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>

namespace xr
{
class session;

class swapchain : public utils::handle<XrSwapchain, xrDestroySwapchain>
{
public:
	struct image
	{
		vk::Image image{};
	};

private:
	int32_t width_;
	int32_t height_;
	int sample_count_;
	vk::Format format_;

	std::vector<image> images_;

public:
	swapchain() = default;
	swapchain(session &, vk::raii::Device & device, vk::Format format, int32_t width, int32_t height, int sample_count = 1, uint32_t array_size = 1);

	int32_t width() const
	{
		return width_;
	}
	int32_t height() const
	{
		return height_;
	}
	XrExtent2Di extent() const
	{
		return {width_, height_};
	}
	int sample_count() const
	{
		return sample_count_;
	}
	const std::vector<image> & images() const
	{
		return images_;
	}
	std::vector<image> & images()
	{
		return images_;
	}
	vk::Format format() const
	{
		return format_;
	}

	int acquire();
	bool wait(XrDuration timeout = XR_INFINITE_DURATION);
	void release();
};
} // namespace xr
