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
#include <vector>
#include <vulkan/vulkan.h>

namespace vk
{
class renderpass : public utils::handle<VkRenderPass>
{
	VkDevice device{};

public:
	struct info
	{
		std::vector<VkAttachmentDescription> attachments;
		std::vector<VkSubpassDescription> subpasses;
		std::vector<VkSubpassDependency> dependencies;
	};

	renderpass() = default;
	renderpass(VkDevice device, info & create_info);
	renderpass(const renderpass &) = delete;
	renderpass(renderpass &&) = default;
	renderpass & operator=(const renderpass &) = delete;
	renderpass & operator=(renderpass &&) = default;
	~renderpass();
};
} // namespace vk
