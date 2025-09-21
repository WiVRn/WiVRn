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

#include <string>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

struct shader
{
	struct input
	{
		int location;
		std::string name;
		vk::Format format;
		int array_size;
	};

	struct specialization_constant
	{
		int id;
		std::string name;
	};

	vk::raii::ShaderModule shader_module;
	std::vector<input> inputs;
	std::vector<specialization_constant> specialization_constants;

	vk::ShaderModule operator*() const
	{
		return *shader_module;
	}

	operator vk::ShaderModule() const
	{
		return *shader_module;
	}
};

struct shader_loader
{
	vk::raii::Device & device;

	shader_loader(vk::raii::Device & device);

	std::shared_ptr<shader> operator()(std::span<const uint32_t> spirv);
	std::shared_ptr<shader> operator()(const std::string & name);
};

std::shared_ptr<shader> load_shader(vk::raii::Device & device, const std::string & name);
