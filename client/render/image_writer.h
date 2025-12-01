/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "utils/thread_safe.h"
#include "vk/allocation.h"
#include <filesystem>
#include <vulkan/vulkan.hpp>

void write_image(
        vk::raii::Device & device,
        thread_safe<vk::raii::Queue> & queue,
        uint32_t queue_family_index,
        const std::filesystem::path & path,
        vk::Image image,
        const vk::ImageCreateInfo & info);

inline void write_image(
        vk::raii::Device & device,
        thread_safe<vk::raii::Queue> & queue,
        uint32_t queue_family_index,
        const std::filesystem::path & path,
        const image_allocation & image)
{
	write_image(device, queue, queue_family_index, path, (vk::Image)image, image.info());
}
