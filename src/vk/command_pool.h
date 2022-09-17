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
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>

namespace vk
{
class command_pool : public utils::handle<VkCommandPool>
{
	VkDevice device{};
	std::unique_ptr<std::mutex> lock;

public:
	command_pool() = default;
	command_pool(VkDevice device, uint32_t queueFamilyIndex);

	command_pool(const command_pool &) = delete;
	command_pool(command_pool &&) = default;
	command_pool & operator=(const command_pool &) = delete;
	command_pool & operator=(command_pool &&) = default;
	~command_pool();

	std::vector<VkCommandBuffer> allocate_command_buffers(uint32_t count, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	VkCommandBuffer allocate_command_buffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	void free_command_buffers(const std::vector<VkCommandBuffer> & command_buffers);
	void free_command_buffer(VkCommandBuffer command_buffer);
};
} // namespace vk
