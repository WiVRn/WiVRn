/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "utils/singleton.h"
#include "vk_mem_alloc.h"

class vk_allocator : public singleton<vk_allocator>
{
	VmaAllocator handle = nullptr;

public:
	const bool has_debug_utils;
	vk_allocator(const VmaAllocatorCreateInfo &, bool has_debug_utils);
	~vk_allocator();

	operator VmaAllocator()
	{
		return handle;
	}
};
