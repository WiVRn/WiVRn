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
#include "details/enumerate.h"
#include "session.h"

static XrSwapchainCreateInfo make_info(vk::Format format, int32_t width, int32_t height, int sample_count, uint32_t array_size)
{
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

	return {
	        .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
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
}

xr::swapchain::swapchain(xr::session & s, vk::raii::Device & device, vk::Format format, int32_t width, int32_t height, int sample_count, uint32_t array_size) :
        xr::swapchain(s, device, make_info(format, width, height, sample_count, array_size))
{
}

xr::swapchain::swapchain(xr::session & s,
                         vk::raii::Device & device,
                         XrSwapchainCreateInfo create_info) :
        width_(create_info.width),
        height_(create_info.height),
        sample_count_(create_info.sampleCount),
        format_(vk::Format(create_info.format))
{
	assert(sample_count_ == 1);
	CHECK_XR(xrCreateSwapchain(s, &create_info, &id));

	std::vector<XrSwapchainImageVulkanKHR> array =
	        details::enumerate<XrSwapchainImageVulkanKHR>(xrEnumerateSwapchainImages, id);

	images_.resize(array.size());
	for (uint32_t i = 0; i < array.size(); i++)
	{
		images_[i].image = array[i].image;

		vk::ImageViewCreateInfo iv_create_info{
		        .image = array[i].image,
		        .viewType = vk::ImageViewType::e2D,
		        .format = format_,
		        .components = {},
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .baseMipLevel = 0,
		                .levelCount = 1,
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        }};

		images_[i].view = vk::raii::ImageView(device, iv_create_info);
	}
}

int xr::swapchain::acquire()
{
	uint32_t index;

	XrSwapchainImageAcquireInfo acquire_info{
	        .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
	};

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

	CHECK_XR(xrReleaseSwapchainImage(id, &release_info));
}
