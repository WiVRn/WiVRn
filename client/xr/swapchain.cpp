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
#include "error.h"
#include "session.h"
#include "xr.h"

xr::swapchain::swapchain(xr::session & s, vk::raii::Device& device, vk::Format format, uint32_t width, uint32_t height, int sample_count)
{
	assert(sample_count == 1);

	XrSwapchainCreateInfo create_info{};
	create_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	create_info.createFlags = 0;
	create_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	create_info.format = static_cast<VkFormat>(format);
	create_info.sampleCount = sample_count;
	create_info.width = width;
	create_info.height = height;
	create_info.faceCount = 1;
	create_info.arraySize = 1;
	create_info.mipCount = 1;

	width_ = width;
	height_ = height;
	sample_count_ = sample_count;
	format_ = format;

	CHECK_XR(xrCreateSwapchain(s, &create_info, &id));

	std::vector<XrSwapchainImageVulkanKHR> array =
	        details::enumerate<XrSwapchainImageVulkanKHR>(xrEnumerateSwapchainImages, id);

	images_.resize(array.size());
	for (uint32_t i = 0; i < array.size(); i++)
	{
		images_[i].image = array[i].image;

		vk::ImageViewCreateInfo iv_create_info;
		iv_create_info.image = array[i].image;
		iv_create_info.viewType = vk::ImageViewType::e2D;
		iv_create_info.format = format;
		iv_create_info.components.r = vk::ComponentSwizzle::eIdentity;
		iv_create_info.components.g = vk::ComponentSwizzle::eIdentity;
		iv_create_info.components.b = vk::ComponentSwizzle::eIdentity;
		iv_create_info.components.a = vk::ComponentSwizzle::eIdentity;
		iv_create_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		iv_create_info.subresourceRange.baseMipLevel = 0;
		iv_create_info.subresourceRange.levelCount = 1;
		iv_create_info.subresourceRange.baseArrayLayer = 0;
		iv_create_info.subresourceRange.layerCount = 1;

		images_[i].view = vk::raii::ImageView(device, iv_create_info);
	}
}

xr::swapchain::~swapchain()
{
	if (id != XR_NULL_HANDLE)
		xrDestroySwapchain(id);
}

int xr::swapchain::acquire()
{
	uint32_t index;

	XrSwapchainImageAcquireInfo acquire_info{};
	acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

	CHECK_XR(xrAcquireSwapchainImage(id, &acquire_info, &index));

	return index;
}

bool xr::swapchain::wait(XrDuration timeout)
{
	XrSwapchainImageWaitInfo wait_info{};
	wait_info.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	wait_info.timeout = timeout;

	XrResult result = xrWaitSwapchainImage(id, &wait_info);
	CHECK_XR(result, "xrWaitSwapchainImage");
	return result == XR_SUCCESS;
}

void xr::swapchain::release()
{
	XrSwapchainImageReleaseInfo release_info{};
	release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;

	CHECK_XR(xrReleaseSwapchainImage(id, &release_info));
}
