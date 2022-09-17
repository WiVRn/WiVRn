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

#include "command_pool.h"
#include "vk.h"

vk::command_pool::command_pool(VkDevice device, uint32_t queueFamilyIndex) :
        device(device), lock(std::make_unique<std::mutex>())
{
	VkCommandPoolCreateInfo cmdpool_create_info{};
	cmdpool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdpool_create_info.queueFamilyIndex = queueFamilyIndex;
	cmdpool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	CHECK_VK(vkCreateCommandPool(device, &cmdpool_create_info, nullptr, &id));
}

vk::command_pool::~command_pool()
{
	if (device)
		vkDestroyCommandPool(device, id, nullptr);
}

std::vector<VkCommandBuffer> vk::command_pool::allocate_command_buffers(uint32_t count, VkCommandBufferLevel level)
{
	std::unique_lock _{*lock};

	VkCommandBufferAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = id;
	alloc_info.level = level;
	alloc_info.commandBufferCount = count;

	std::vector<VkCommandBuffer> command_buffers{count};
	CHECK_VK(vkAllocateCommandBuffers(device, &alloc_info, command_buffers.data()));

	return command_buffers;
}

VkCommandBuffer vk::command_pool::allocate_command_buffer(VkCommandBufferLevel level)
{
	std::unique_lock _{*lock};

	VkCommandBufferAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = id;
	alloc_info.level = level;
	alloc_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer;
	CHECK_VK(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer));

	return command_buffer;
}

void vk::command_pool::free_command_buffers(const std::vector<VkCommandBuffer> & command_buffers)
{
	std::unique_lock _{*lock};
	vkFreeCommandBuffers(device, id, command_buffers.size(), command_buffers.data());
}

void vk::command_pool::free_command_buffer(VkCommandBuffer command_buffer)
{
	if (command_buffer)
	{
		std::unique_lock _{*lock};
		vkFreeCommandBuffers(device, id, 1, &command_buffer);
	}
}
