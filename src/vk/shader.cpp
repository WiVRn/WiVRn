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

#include "shader.h"

#include "vk.h"

vk::shader::shader(VkDevice device, const std::vector<uint32_t> & spirv) :
        device(device)
{
	VkShaderModuleCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.pCode = data(spirv);
	create_info.codeSize = spirv.size() * sizeof(uint32_t);

	CHECK_VK(vkCreateShaderModule(device, &create_info, nullptr, &id));
}

vk::shader::shader(VkDevice device, const std::string & name) :
        shader(device, shaders.at(name)) {}

vk::shader::~shader()
{
	if (device)
		vkDestroyShaderModule(device, id, nullptr);
}
