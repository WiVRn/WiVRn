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

#include "swapchain.h"
#include "application.h"
#include "details/enumerate.h"
#include "session.h"

xr::swapchain::swapchain(
        xr::instance & inst,
        xr::session & s,
        vk::raii::Device & device,
        vk::Format format,
        int32_t width,
        int32_t height,
        int sample_count,
        uint32_t array_size,
        XrFoveationProfileFB foveated) :
        width_(width),
        height_(height),
        sample_count_(sample_count),
        format_(format)
{
	assert(sample_count == 1);
	if (foveated)
		update = inst.get_proc<PFN_xrUpdateSwapchainFB>("xrUpdateSwapchainFB");

	XrSwapchainUsageFlags usage_flags;

	switch (format)
	{
		case vk::Format::eD16Unorm:
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eX8D24UnormPack32:
		case vk::Format::eD32Sfloat:
		case vk::Format::eD32SfloatS8Uint:
			usage_flags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			break;
		default:
			usage_flags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			break;
	}

	XrSwapchainCreateInfoFoveationFB foveation_info{
	        .type = XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB,
	        .flags = XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB,
	};

	XrSwapchainCreateInfo create_info{
	        .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
	        .next = foveated ? &foveation_info : nullptr,
	        .createFlags = 0,
	        .usageFlags = usage_flags,
	        .format = static_cast<VkFormat>(format),
	        .sampleCount = (uint32_t)sample_count,
	        .width = (uint32_t)width,
	        .height = (uint32_t)height,
	        .faceCount = 1,
	        .arraySize = array_size,
	        .mipCount = 1,
	};

	CHECK_XR(xrCreateSwapchain(s, &create_info, &id));

	std::tuple<std::vector<XrSwapchainImageVulkanKHR>, std::vector<XrSwapchainImageFoveationVulkanFB>> array;
	auto & images = std::get<0>(array);
	auto & foveation = std::get<1>(array);
	if (foveated)
	{
		update_foveation(foveated);
		details::enumerate2(xrEnumerateSwapchainImages, array, id);
		assert(images.size() == foveation.size());
	}
	else
	{
		details::enumerate(xrEnumerateSwapchainImages, images, id);
	}

	images_.resize(images.size());
	for (uint32_t i = 0; i < images.size(); i++)
	{
		images_[i].image = images[i].image;
		if (foveated)
		{
			images_[i].foveation = foveation[i].image;
		}
	}
}

int xr::swapchain::acquire()
{
	uint32_t index;

	XrSwapchainImageAcquireInfo acquire_info{
	        .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
	};

	auto lock = application::get_queue().lock();
	CHECK_XR(xrAcquireSwapchainImage(id, &acquire_info, &index));

	return index;
}

bool xr::swapchain::wait(XrDuration timeout)
{
	XrSwapchainImageWaitInfo wait_info{
	        .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
	        .timeout = timeout,
	};

	XrResult result = xrWaitSwapchainImage(id, &wait_info);
	CHECK_XR(result, "xrWaitSwapchainImage");
	return result == XR_SUCCESS;
}

void xr::swapchain::release()
{
	XrSwapchainImageReleaseInfo release_info{
	        .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
	};

	auto lock = application::get_queue().lock();
	CHECK_XR(xrReleaseSwapchainImage(id, &release_info));
}

void xr::swapchain::update_foveation(XrFoveationProfileFB foveation)
{
	XrSwapchainStateFoveationFB update_info{
	        .type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
	        .profile = foveation,
	};
	CHECK_XR(update(id, (XrSwapchainStateBaseHeaderFB *)&update_info));
}
